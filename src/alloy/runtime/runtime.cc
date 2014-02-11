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
#include <alloy/runtime/tracing.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::frontend;
using namespace alloy::runtime;


DEFINE_string(runtime_backend, "any",
              "Runtime backend [any, ivm, x64].");


Runtime::Runtime(Memory* memory) :
    memory_(memory), debugger_(0), backend_(0), frontend_(0),
    access_callbacks_(0) {
  tracing::Initialize();
  modules_lock_ = AllocMutex(10000);
}

Runtime::~Runtime() {
  LockMutex(modules_lock_);
  for (ModuleList::iterator it = modules_.begin();
       it != modules_.end(); ++it) {
    Module* module = *it;
    delete module;
  }
  UnlockMutex(modules_lock_);
  FreeMutex(modules_lock_);

  RegisterAccessCallbacks* cbs = access_callbacks_;
  while (cbs) {
    RegisterAccessCallbacks* next = cbs->next;
    delete cbs;
    cbs = next;
  }
  access_callbacks_ = NULL;

  delete frontend_;
  delete backend_;
  delete debugger_;

  tracing::Flush();
}

// TODO(benvanik): based on compiler support
#include <alloy/backend/ivm/ivm_backend.h>
#include <alloy/backend/x64/x64_backend.h>

int Runtime::Initialize(Frontend* frontend, Backend* backend) {
  // Must be initialized by subclass before calling into this.
  XEASSERTNOTNULL(memory_);

  int result = memory_->Initialize();
  if (result) {
    return result;
  }

  // Create debugger first. Other types hook up to it.
  debugger_ = new Debugger(this);

  if (frontend_ || backend_) {
    return 1;
  }

  if (!backend) {
#if defined(ALLOY_HAS_X64_BACKEND) && ALLOY_HAS_X64_BACKEND
    if (FLAGS_runtime_backend == "x64") {
      backend = new alloy::backend::x64::X64Backend(
          this);
    }
#endif  // ALLOY_HAS_X64_BACKEND
#if defined(ALLOY_HAS_IVM_BACKEND) && ALLOY_HAS_IVM_BACKEND
    if (FLAGS_runtime_backend == "ivm") {
      backend = new alloy::backend::ivm::IVMBackend(
          this);
    }
#endif  // ALLOY_HAS_IVM_BACKEND
    if (FLAGS_runtime_backend == "any") {
#if defined(ALLOY_HAS_X64_BACKEND) && ALLOY_HAS_X64_BACKEND
      if (!backend) {
        backend = new alloy::backend::x64::X64Backend(
            this);
      }
#endif  // ALLOY_HAS_X64_BACKEND
#if defined(ALLOY_HAS_IVM_BACKEND) && ALLOY_HAS_IVM_BACKEND
      if (!backend) {
        backend = new alloy::backend::ivm::IVMBackend(
            this);
      }
#endif  // ALLOY_HAS_IVM_BACKEND
    }
  }

  if (!backend) {
    return 1;
  }
  backend_ = backend;
  frontend_ = frontend;

  result = backend_->Initialize();
  if (result) {
    return result;
  }

  result = frontend_->Initialize();
  if (result) {
    return result;
  }

  return 0;
}

int Runtime::AddModule(Module* module) {
  LockMutex(modules_lock_);
  modules_.push_back(module);
  UnlockMutex(modules_lock_);
  return 0;
}

Module* Runtime::GetModule(const char* name) {
  Module* result = NULL;
  LockMutex(modules_lock_);
  for (ModuleList::iterator it = modules_.begin();
       it != modules_.end(); ++it) {
    Module* module = *it;
    if (xestrcmpa(module->name(), name) == 0) {
      result = module;
      break;
    }
  }
  UnlockMutex(modules_lock_);
  return result;
}

Runtime::ModuleList Runtime::GetModules() {
  ModuleList clone;
  LockMutex(modules_lock_);
  clone = modules_;
  UnlockMutex(modules_lock_);
  return clone;
}

std::vector<Function*> Runtime::FindFunctionsWithAddress(uint64_t address) {
  return entry_table_.FindWithAddress(address);
}

int Runtime::ResolveFunction(uint64_t address, Function** out_function) {
  *out_function = NULL;
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

int Runtime::LookupFunctionInfo(
    uint64_t address, FunctionInfo** out_symbol_info) {
  *out_symbol_info = NULL;

  // TODO(benvanik): fast reject invalid addresses/log errors.

  // Find the module that contains the address.
  Module* code_module = NULL;
  LockMutex(modules_lock_);
  // TODO(benvanik): sort by code address (if contiguous) so can bsearch.
  // TODO(benvanik): cache last module low/high, as likely to be in there.
  for (ModuleList::const_iterator it = modules_.begin();
       it != modules_.end(); ++it) {
    Module* module = *it;
    if (module->ContainsAddress(address)) {
      code_module = module;
      break;
    }
  }
  UnlockMutex(modules_lock_);
  if (!code_module) {
    // No module found that could contain the address.
    return 1;
  }

  return LookupFunctionInfo(code_module, address, out_symbol_info);
}

int Runtime::LookupFunctionInfo(Module* module, uint64_t address,
                                FunctionInfo** out_symbol_info) {
  // Atomic create/lookup symbol in module.
  // If we get back the NEW flag we must declare it now.
  FunctionInfo* symbol_info = NULL;
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

int Runtime::DemandFunction(
    FunctionInfo* symbol_info, Function** out_function) {
  *out_function = NULL;

  // Lock function for generation. If it's already being generated
  // by another thread this will block and return DECLARED.
  Module* module = symbol_info->module();
  SymbolInfo::Status symbol_status = module->DefineFunction(symbol_info);
  if (symbol_status == SymbolInfo::STATUS_NEW) {
    // Symbol is undefined, so define now.
    Function* function = NULL;
    int result = frontend_->DefineFunction(symbol_info, DEBUG_INFO_DEFAULT, &function);
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

void Runtime::AddRegisterAccessCallbacks(
    const RegisterAccessCallbacks& callbacks) {
  RegisterAccessCallbacks* cbs = new RegisterAccessCallbacks();
  xe_copy_struct(cbs, &callbacks, sizeof(callbacks));
  cbs->next = access_callbacks_;
  access_callbacks_ = cbs;
}
