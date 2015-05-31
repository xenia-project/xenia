/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_frontend.h"

#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/frontend/ppc_disasm.h"
#include "xenia/cpu/frontend/ppc_emit.h"
#include "xenia/cpu/frontend/ppc_translator.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace frontend {

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

PPCFrontend::PPCFrontend(Processor* processor) : processor_(processor) {
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

Memory* PPCFrontend::memory() const { return processor_->memory(); }

void CheckGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1) {
  ppc_context->scratch = 0x8000;
}
void HandleGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1) {
  auto global_lock = reinterpret_cast<xe::mutex*>(arg0);
  volatile bool* global_lock_taken = reinterpret_cast<bool*>(arg1);
  uint64_t value = ppc_context->scratch;
  if (value == 0x8000) {
    global_lock->lock();
    *global_lock_taken = false;
    global_lock->unlock();
  } else if (value == ppc_context->r[13]) {
    global_lock->lock();
    *global_lock_taken = true;
    global_lock->unlock();
  }
}

bool PPCFrontend::Initialize() {
  void* arg0 = reinterpret_cast<void*>(&builtins_.global_lock);
  void* arg1 = reinterpret_cast<void*>(&builtins_.global_lock_taken);
  builtins_.check_global_lock =
      processor_->DefineBuiltin("CheckGlobalLock", CheckGlobalLock, arg0, arg1);
  builtins_.handle_global_lock = processor_->DefineBuiltin(
      "HandleGlobalLock", HandleGlobalLock, arg0, arg1);

  return true;
}

bool PPCFrontend::DeclareFunction(FunctionInfo* symbol_info) {
  // Could scan or something here.
  // Could also check to see if it's a well-known function type and classify
  // for later.
  // Could also kick off a precompiler, since we know it's likely the function
  // will be demanded soon.
  return true;
}

bool PPCFrontend::DefineFunction(FunctionInfo* symbol_info,
                                 uint32_t debug_info_flags,
                                 Function** out_function) {
  PPCTranslator* translator = translator_pool_.Allocate(this);
  bool result =
      translator->Translate(symbol_info, debug_info_flags, out_function);
  translator_pool_.Release(translator);
  return result;
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
