/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/runtime.h"

#include <gflags/gflags.h>

#include "poly/assert.h"
#include "xdb/protocol.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/thread_state.h"

// TODO(benvanik): based on compiler support
#include "xenia/cpu/backend/x64/x64_backend.h"

DEFINE_string(runtime_backend, "any", "Runtime backend [any, x64].");

namespace xe {
namespace cpu {

class BuiltinModule : public Module {
 public:
  BuiltinModule(Runtime* runtime) : Module(runtime), name_("builtin") {}
  const std::string& name() const override { return name_; }
  bool ContainsAddress(uint32_t address) override {
    return (address & 0xFFFFFFF0) == 0xFFFFFFF0;
  }

 private:
  std::string name_;
};

Runtime::Runtime(Memory* memory, ExportResolver* export_resolver,
                 uint32_t debug_info_flags, uint32_t trace_flags)
    : memory_(memory),
      debug_info_flags_(debug_info_flags),
      trace_flags_(trace_flags),
      builtin_module_(nullptr),
      next_builtin_address_(0xFFFF0000ul),
      export_resolver_(export_resolver) {}

Runtime::~Runtime() {
  {
    std::lock_guard<std::mutex> guard(modules_lock_);
    modules_.clear();
  }

  debugger_.reset();
  frontend_.reset();
  backend_.reset();
}

int Runtime::Initialize(std::unique_ptr<xe::cpu::backend::Backend> backend) {
  auto frontend = std::make_unique<xe::cpu::frontend::PPCFrontend>(this);
  // TODO(benvanik): set options/etc.

  // Must be initialized by subclass before calling into this.
  assert_not_null(memory_);

  // Create debugger first. Other types hook up to it.
  debugger_.reset(new Debugger(this));

  std::unique_ptr<Module> builtin_module(new BuiltinModule(this));
  builtin_module_ = builtin_module.get();
  modules_.push_back(std::move(builtin_module));

  if (frontend_ || backend_) {
    return 1;
  }

  if (!backend) {
#if defined(XENIA_HAS_X64_BACKEND) && XENIA_HAS_X64_BACKEND
    if (FLAGS_runtime_backend == "x64") {
      backend.reset(new xe::cpu::backend::x64::X64Backend(this));
    }
#endif  // XENIA_HAS_X64_BACKEND
    if (FLAGS_runtime_backend == "any") {
#if defined(XENIA_HAS_X64_BACKEND) && XENIA_HAS_X64_BACKEND
      if (!backend) {
        backend.reset(new xe::cpu::backend::x64::X64Backend(this));
      }
#endif  // XENIA_HAS_X64_BACKEND
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

FunctionInfo* Runtime::DefineBuiltin(const std::string& name,
                                     FunctionInfo::ExternHandler handler,
                                     void* arg0, void* arg1) {
  uint32_t address = next_builtin_address_;
  next_builtin_address_ += 4;

  FunctionInfo* fn_info;
  builtin_module_->DeclareFunction(address, &fn_info);
  fn_info->set_end_address(address + 4);
  fn_info->set_name(name);
  fn_info->SetupExtern(handler, arg0, arg1);
  fn_info->set_status(SymbolInfo::STATUS_DECLARED);

  return fn_info;
}

std::vector<Function*> Runtime::FindFunctionsWithAddress(uint32_t address) {
  return entry_table_.FindWithAddress(address);
}

int Runtime::ResolveFunction(uint32_t address, Function** out_function) {
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

int Runtime::LookupFunctionInfo(uint32_t address,
                                FunctionInfo** out_symbol_info) {
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

int Runtime::LookupFunctionInfo(Module* module, uint32_t address,
                                FunctionInfo** out_symbol_info) {
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
  *out_function = nullptr;

  // Lock function for generation. If it's already being generated
  // by another thread this will block and return DECLARED.
  Module* module = symbol_info->module();
  SymbolInfo::Status symbol_status = module->DefineFunction(symbol_info);
  if (symbol_status == SymbolInfo::STATUS_NEW) {
    // Symbol is undefined, so define now.
    Function* function = nullptr;
    int result = frontend_->DefineFunction(symbol_info, debug_info_flags_,
                                           trace_flags_, &function);
    if (result) {
      symbol_info->set_status(SymbolInfo::STATUS_FAILED);
      return result;
    }
    symbol_info->set_function(function);

    auto trace_base = memory()->trace_base();
    if (trace_base && trace_flags_ & TRACE_FUNCTION_GENERATION) {
      auto ev = xdb::protocol::FunctionCompiledEvent::Append(trace_base);
      ev->type = xdb::protocol::EventType::FUNCTION_COMPILED;
      ev->flags = 0;
      ev->address = static_cast<uint32_t>(symbol_info->address());
      ev->length =
          static_cast<uint32_t>(symbol_info->end_address() - ev->address);
    }

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

}  // namespace cpu
}  // namespace xe
