/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/processor.h>

#include <xenia/emulator.h>
#include <xenia/cpu/xenon_memory.h>
#include <xenia/cpu/xenon_runtime.h>


using namespace alloy;
using namespace alloy::backend;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;
using namespace xe;
using namespace xe::cpu;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    //ppc::RegisterDisasmCategoryAltivec();
    //ppc::RegisterDisasmCategoryALU();
    //ppc::RegisterDisasmCategoryControl();
    //ppc::RegisterDisasmCategoryFPU();
    //ppc::RegisterDisasmCategoryMemory();

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
  }
}


Processor::Processor(Emulator* emulator) :
    emulator_(emulator), export_resolver_(emulator->export_resolver()),
    runtime_(0), memory_(emulator->memory()),
    interrupt_thread_lock_(NULL), interrupt_thread_state_(NULL),
    interrupt_thread_block_(0) {
  InitializeIfNeeded();
}

Processor::~Processor() {
  if (interrupt_thread_block_) {
    memory_->HeapFree(interrupt_thread_block_, 2048);
    delete interrupt_thread_state_;
    xe_mutex_free(interrupt_thread_lock_);
  }

  delete runtime_;
}

int Processor::Setup() {
  XEASSERTNULL(runtime_);

  runtime_ = new XenonRuntime(memory_, export_resolver_);
  if (!runtime_) {
    return 1;
  }

  Backend* backend = 0;
  // Backend* backend = new alloy::backend::ivm::IVMBackend(
  //     runtime);
  // Backend* backend = new alloy::backend::x64::X64Backend(
  //     runtime);
  int result = runtime_->Initialize(backend);
  if (result) {
    return result;
  }

  interrupt_thread_lock_ = xe_mutex_alloc(10000);
  interrupt_thread_state_ = new XenonThreadState(
      runtime_, 0, 16 * 1024, 0);
  interrupt_thread_block_ = memory_->HeapAlloc(
      0, 2048, MEMORY_FLAG_ZERO);
  interrupt_thread_state_->context()->r[13] = interrupt_thread_block_;

  return 0;
}

void Processor::AddRegisterAccessCallbacks(
    xe::cpu::RegisterAccessCallbacks callbacks) {
  runtime_->AddRegisterAccessCallbacks(callbacks);
}

int Processor::Execute(XenonThreadState* thread_state, uint64_t address) {
  // Attempt to get the function.
  Function* fn;
  if (runtime_->ResolveFunction(address, &fn)) {
    // Symbol not found in any module.
    XELOGCPU("Execute(%.8X): failed to find function", address);
    return 1;
  }

  PPCContext* context = thread_state->context();

  // This could be set to anything to give us a unique identifier to track
  // re-entrancy/etc.
  uint32_t lr = 0xBEBEBEBE;

  // Setup registers.
  context->lr = lr;

  // Execute the function.
  fn->Call(thread_state);
  return 0;
}

uint64_t Processor::Execute(
    XenonThreadState* thread_state, uint64_t address, uint64_t arg0) {
  PPCContext* context = thread_state->context();
  context->r[3] = arg0;
  if (Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

uint64_t Processor::Execute(
    XenonThreadState* thread_state, uint64_t address, uint64_t arg0,
    uint64_t arg1) {
  PPCContext* context = thread_state->context();
  context->r[3] = arg0;
  context->r[4] = arg1;
  if (Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

uint64_t Processor::ExecuteInterrupt(
    uint32_t cpu, uint64_t address, uint64_t arg0, uint64_t arg1) {
  // Acquire lock on interrupt thread (we can only dispatch one at a time).
  xe_mutex_lock(interrupt_thread_lock_);

  // Set 0x10C(r13) to the current CPU ID.
  uint8_t* p = memory_->membase();
  XESETUINT8BE(p + interrupt_thread_block_ + 0x10C, cpu);

  // Execute interrupt.
  uint64_t result = Execute(interrupt_thread_state_, address, arg0, arg1);

  xe_mutex_unlock(interrupt_thread_lock_);
  return result;
}
