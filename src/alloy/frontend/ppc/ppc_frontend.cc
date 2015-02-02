/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "alloy/frontend/ppc/ppc_frontend.h"

#include "alloy/frontend/ppc/ppc_context.h"
#include "alloy/frontend/ppc/ppc_disasm.h"
#include "alloy/frontend/ppc/ppc_emit.h"
#include "alloy/frontend/ppc/ppc_translator.h"
#include "alloy/runtime/runtime.h"

namespace alloy {
namespace frontend {
namespace ppc {

using alloy::runtime::Function;
using alloy::runtime::FunctionInfo;
using alloy::runtime::Runtime;

void InitializeIfNeeded();
void CleanupOnShutdown();

void InitializeIfNeeded() {
  static bool has_initialized = false;
  if (has_initialized) {
    return;
  }
  has_initialized = true;

  RegisterEmitCategoryAltivec();
  RegisterEmitCategoryALU();
  RegisterEmitCategoryControl();
  RegisterEmitCategoryFPU();
  RegisterEmitCategoryMemory();

  atexit(CleanupOnShutdown);
}

void CleanupOnShutdown() {}

PPCFrontend::PPCFrontend(Runtime* runtime) : Frontend(runtime) {
  InitializeIfNeeded();

  std::unique_ptr<ContextInfo> context_info(
      new ContextInfo(sizeof(PPCContext), offsetof(PPCContext, thread_state),
                      offsetof(PPCContext, thread_id)));
  // Add fields/etc.
  context_info_ = std::move(context_info);
}

PPCFrontend::~PPCFrontend() {
  // Force cleanup now before we deinit.
  translator_pool_.Reset();
}

void CheckGlobalLock(PPCContext* ppc_state, void* arg0, void* arg1) {
  ppc_state->scratch = 0x8000;
}
void HandleGlobalLock(PPCContext* ppc_state, void* arg0, void* arg1) {
  std::mutex* global_lock = reinterpret_cast<std::mutex*>(arg0);
  volatile bool* global_lock_taken = reinterpret_cast<bool*>(arg1);
  uint64_t value = ppc_state->scratch;
  if (value == 0x8000) {
    global_lock->unlock();
    *global_lock_taken = false;
  } else if (value == ppc_state->r[13]) {
    global_lock->lock();
    *global_lock_taken = true;
  }
}

int PPCFrontend::Initialize() {
  int result = Frontend::Initialize();
  if (result) {
    return result;
  }

  void* arg0 = reinterpret_cast<void*>(&builtins_.global_lock);
  void* arg1 = reinterpret_cast<void*>(&builtins_.global_lock_taken);
  builtins_.check_global_lock = runtime_->DefineBuiltin(
      "CheckGlobalLock", (FunctionInfo::ExternHandler)CheckGlobalLock, arg0,
      arg1);
  builtins_.handle_global_lock = runtime_->DefineBuiltin(
      "HandleGlobalLock", (FunctionInfo::ExternHandler)HandleGlobalLock, arg0,
      arg1);

  return result;
}

int PPCFrontend::DeclareFunction(FunctionInfo* symbol_info) {
  // Could scan or something here.
  // Could also check to see if it's a well-known function type and classify
  // for later.
  // Could also kick off a precompiler, since we know it's likely the function
  // will be demanded soon.
  return 0;
}

int PPCFrontend::DefineFunction(FunctionInfo* symbol_info,
                                uint32_t debug_info_flags,
                                uint32_t trace_flags,
                                Function** out_function) {
  PPCTranslator* translator = translator_pool_.Allocate(this);
  int result =
      translator->Translate(symbol_info, debug_info_flags, trace_flags, out_function);
  translator_pool_.Release(translator);
  return result;
}

}  // namespace ppc
}  // namespace frontend
}  // namespace alloy
