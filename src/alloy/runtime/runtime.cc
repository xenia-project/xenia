/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/runtime.h>

#include <gflags/gflags.h>

#include <alloy/runtime/module.h>
#include <poly/poly.h>

// TODO(benvanik): based on compiler support
#include <alloy/backend/ivm/ivm_backend.h>
#include <alloy/backend/x64/x64_backend.h>

DEFINE_string(runtime_backend, "any", "Runtime backend [any, ivm, x64].");

namespace alloy {
namespace runtime {

using alloy::backend::Backend;
using alloy::frontend::Frontend;

Runtime::Runtime(Memory* memory) : memory_(memory) {}

Runtime::~Runtime() {
  {
    std::lock_guard<std::mutex> guard(modules_lock_);
    modules_.clear();
  }

  debugger_.reset();
  frontend_.reset();
  backend_.reset();
}

int Runtime::Initialize(std::unique_ptr<Frontend> frontend,
                        std::unique_ptr<Backend> backend) {
  // Must be initialized by subclass before calling into this.
  assert_not_null(memory_);

  // Create debugger first. Other types hook up to it.
  debugger_.reset(new Debugger(this));

  if (frontend_ || backend_) {
    return 1;
  }

  if (!backend) {
#if defined(ALLOY_HAS_X64_BACKEND) && ALLOY_HAS_X64_BACKEND
    if (FLAGS_runtime_backend == "x64") {
      backend.reset(new alloy::backend::x64::X64Backend(this));
    }
#endif  // ALLOY_HAS_X64_BACKEND
#if defined(ALLOY_HAS_IVM_BACKEND) && ALLOY_HAS_IVM_BACKEND
    if (FLAGS_runtime_backend == "ivm") {
      backend.reset(new alloy::backend::ivm::IVMBackend(this));
    }
#endif  // ALLOY_HAS_IVM_BACKEND
    if (FLAGS_runtime_backend == "any") {
#if defined(ALLOY_HAS_X64_BACKEND) && ALLOY_HAS_X64_BACKEND
      if (!backend) {
        backend.reset(new alloy::backend::x64::X64Backend(this));
      }
#endif  // ALLOY_HAS_X64_BACKEND
#if defined(ALLOY_HAS_IVM_BACKEND) && ALLOY_HAS_IVM_BACKEND
      if (!backend) {
        backend.reset(new alloy::backend::ivm::IVMBackend(this));
      }
#endif  // ALLOY_HAS_IVM_BACKEND
    }
  }

  if (!backend) {
    return 1;
  }

  int result = backend->Initialize();
  if (result) {
    return result;
  }

  result = frontend->Initialize();
  if (result) {
    return result;
  }

  backend_ = std::move(backend);
  frontend_ = std::move(frontend);

  return 0;
}

int Runtime::AddModule(std::unique_ptr<Module> module) {
  std::lock_guard<std::mutex> guard(modules_lock_);
  modules_.push_back(std::move(module));
  return 0;
}

Module* Runtime::GetModule(const char* name) {
  std::lock_guard<std::mutex> guard(modules_lock_);
  for (const auto& module : modules_) {
    if (module->name() == name) {
      return module.get();
    }
  }
  return nullptr;
}

std::vector<Module*> Runtime::GetModules() {
  std::lock_guard<std::mutex> guard(modules_lock_);
  std::vector<Module*> clone(modules_.size());
  for (const auto& module : modules_) {
    clone.push_back(module.get());
  }
  return clone;
}

std::vector<Function*> Runtime::FindFunctionsWithAddress(uint64_t address) {
  return entry_table_.FindWithAddress(address);
}

int Runtime::ResolveFunction(uint64_t address, Function** out_function) {
  SCOPE_profile_cpu_f("alloy");

  *out_function = nullptr;
  Entry* entry;
  Entry::Status status = entry_table_.GetOrCreate(address, &entry);
  if (status == Entry::STATUS_NEW) {
    // Needs to be generated. We have the 'lock' on it and must do so now.

    // Grab symbol declaration.
    FunctionInfo* symbol_info;
    int result = LookupFunctionInfo(address, &symbol_info);
    if (result) {
      return result;
    }

    result = DemandFunction(symbol_info, &entry->function);
    if (result) {
      entry->status = Entry::STATUS_FAILED;
      return result;
    }
    entry->end_address = symbol_info->end_address();
    status = entry->status = Entry::STATUS_READY;
  }
  if (status == Entry::STATUS_READY) {
    // Ready to use.
    *out_function = entry->function;
    return 0;
  } else {
    // Failed or bad state.
    return 1;
  }
}

int Runtime::LookupFunctionInfo(uint64_t address,
                                FunctionInfo** out_symbol_info) {
  SCOPE_profile_cpu_f("alloy");

  *out_symbol_info = nullptr;

  // TODO(benvanik): fast reject invalid addresses/log errors.

  // Find the module that contains the address.
  Module* code_module = nullptr;
  {
    std::lock_guard<std::mutex> guard(modules_lock_);
    // TODO(benvanik): sort by code address (if contiguous) so can bsearch.
    // TODO(benvanik): cache last module low/high, as likely to be in there.
    for (const auto& module : modules_) {
      if (module->ContainsAddress(address)) {
        code_module = module.get();
        break;
      }
    }
  }
  if (!code_module) {
    // No module found that could contain the address.
    return 1;
  }

  return LookupFunctionInfo(code_module, address, out_symbol_info);
}

int Runtime::LookupFunctionInfo(Module* module, uint64_t address,
                                FunctionInfo** out_symbol_info) {
  SCOPE_profile_cpu_f("alloy");

  // Atomic create/lookup symbol in module.
  // If we get back the NEW flag we must declare it now.
  FunctionInfo* symbol_info = nullptr;
  SymbolInfo::Status symbol_status =
      module->DeclareFunction(address, &symbol_info);
  if (symbol_status == SymbolInfo::STATUS_NEW) {
    // Symbol is undeclared, so declare now.
    int result = frontend_->DeclareFunction(symbol_info);
    if (result) {
      symbol_info->set_status(SymbolInfo::STATUS_FAILED);
      return 1;
    }
    symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
  }

  *out_symbol_info = symbol_info;
  return 0;
}

int Runtime::DemandFunction(FunctionInfo* symbol_info,
                            Function** out_function) {
  SCOPE_profile_cpu_f("alloy");

  *out_function = nullptr;

  // Lock function for generation. If it's already being generated
  // by another thread this will block and return DECLARED.
  Module* module = symbol_info->module();
  SymbolInfo::Status symbol_status = module->DefineFunction(symbol_info);
  if (symbol_status == SymbolInfo::STATUS_NEW) {
    // Symbol is undefined, so define now.
    Function* function = nullptr;
    int result =
        frontend_->DefineFunction(symbol_info, DEBUG_INFO_DEFAULT, &function);
    if (result) {
      symbol_info->set_status(SymbolInfo::STATUS_FAILED);
      return result;
    }
    symbol_info->set_function(function);

    // Before we give the symbol back to the rest, let the debugger know.
    debugger_->OnFunctionDefined(symbol_info, function);

    symbol_info->set_status(SymbolInfo::STATUS_DEFINED);
    symbol_status = symbol_info->status();
  }

  if (symbol_status == SymbolInfo::STATUS_FAILED) {
    // Symbol likely failed.
    return 1;
  }

  *out_function = symbol_info->function();

  return 0;
}

}  // namespace runtime
}  // namespace alloy
