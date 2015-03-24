/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/processor.h"

#include "xenia/cpu/cpu-private.h"
#include "xenia/cpu/runtime.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/export_resolver.h"

namespace xe {
namespace cpu {

// TODO(benvanik): remove when enums converted.
using namespace xe::cpu;
using namespace xe::cpu::backend;

using PPCContext = xe::cpu::frontend::PPCContext;

void InitializeIfNeeded();
void CleanupOnShutdown();

void InitializeIfNeeded() {
  static bool has_initialized = false;
  if (has_initialized) {
    return;
  }
  has_initialized = true;

  // ppc::RegisterDisasmCategoryAltivec();
  // ppc::RegisterDisasmCategoryALU();
  // ppc::RegisterDisasmCategoryControl();
  // ppc::RegisterDisasmCategoryFPU();
  // ppc::RegisterDisasmCategoryMemory();

  atexit(CleanupOnShutdown);
}

void CleanupOnShutdown() {}

Processor::Processor(xe::Memory* memory, ExportResolver* export_resolver)
    : export_resolver_(export_resolver),
      runtime_(0),
      memory_(memory),
      interrupt_thread_state_(NULL),
      interrupt_thread_block_(0) {
  InitializeIfNeeded();
}

Processor::~Processor() {
  if (interrupt_thread_block_) {
    memory_->HeapFree(interrupt_thread_block_, 2048);
    delete interrupt_thread_state_;
  }

  delete runtime_;
}

int Processor::Setup() {
  assert_null(runtime_);

  uint32_t debug_info_flags = DEBUG_INFO_DEFAULT;
  uint32_t trace_flags = 0;
  if (FLAGS_trace_function_generation) {
    trace_flags |= TRACE_FUNCTION_GENERATION;
  }
  if (FLAGS_trace_kernel_calls) {
    trace_flags |= TRACE_EXTERN_CALLS;
  }
  if (FLAGS_trace_user_calls) {
    trace_flags |= TRACE_USER_CALLS;
  }
  if (FLAGS_trace_instructions) {
    trace_flags |= TRACE_SOURCE;
  }
  if (FLAGS_trace_registers) {
    trace_flags |= TRACE_SOURCE_VALUES;
  }

  runtime_ =
      new Runtime(memory_, export_resolver_, debug_info_flags, trace_flags);
  if (!runtime_) {
    return 1;
  }

  std::unique_ptr<Backend> backend;
  // backend.reset(new xe::cpu::backend::x64::X64Backend(runtime));
  int result = runtime_->Initialize(std::move(backend));
  if (result) {
    return result;
  }

  interrupt_thread_state_ = new ThreadState(runtime_, 0, 0, 16 * 1024, 0);
  interrupt_thread_state_->set_name("Interrupt");
  interrupt_thread_block_ = memory_->HeapAlloc(0, 2048, MEMORY_FLAG_ZERO);
  interrupt_thread_state_->context()->r[13] = interrupt_thread_block_;

  return 0;
}

int Processor::Execute(ThreadState* thread_state, uint64_t address) {
  SCOPE_profile_cpu_f("cpu");

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
  fn->Call(thread_state, lr);
  return 0;
}

uint64_t Processor::Execute(ThreadState* thread_state, uint64_t address,
                            uint64_t args[], size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  PPCContext* context = thread_state->context();
  assert_true(arg_count <= 5);
  for (size_t i = 0; i < arg_count; ++i) {
    context->r[3 + i] = args[i];
  }
  if (Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

Irql Processor::RaiseIrql(Irql new_value) {
  return static_cast<Irql>(
      poly::atomic_exchange(static_cast<uint32_t>(new_value),
                            reinterpret_cast<volatile uint32_t*>(&irql_)));
}

void Processor::LowerIrql(Irql old_value) {
  poly::atomic_exchange(static_cast<uint32_t>(old_value),
                        reinterpret_cast<volatile uint32_t*>(&irql_));
}

uint64_t Processor::ExecuteInterrupt(uint32_t cpu, uint64_t address,
                                     uint64_t args[], size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  // Acquire lock on interrupt thread (we can only dispatch one at a time).
  std::lock_guard<std::mutex> lock(interrupt_thread_lock_);

  // Set 0x10C(r13) to the current CPU ID.
  uint8_t* p = memory_->membase();
  poly::store_and_swap<uint8_t>(p + interrupt_thread_block_ + 0x10C, cpu);

  // Execute interrupt.
  uint64_t result = Execute(interrupt_thread_state_, address, args, arg_count);

  return result;
}

}  // namespace cpu
}  // namespace xe
