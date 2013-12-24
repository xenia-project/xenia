/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/xenon_thread_state.h>

#include <alloy/runtime/tracing.h>

#include <xenia/cpu/xenon_runtime.h>

using namespace alloy;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;
using namespace xe::cpu;


XenonThreadState::XenonThreadState(
    XenonRuntime* runtime, uint32_t thread_id,
    size_t stack_size, uint64_t thread_state_address) :
    stack_size_(stack_size), thread_state_address_(thread_state_address),
    ThreadState(runtime, thread_id) {
  stack_address_ = memory_->HeapAlloc(
      0, stack_size, MEMORY_FLAG_ZERO);

  debug_break_ = CreateEvent(NULL, FALSE, FALSE, NULL);

  // Allocate with 64b alignment.
  context_ = (PPCContext*)xe_malloc_aligned(sizeof(PPCContext));
  XEASSERT(((uint64_t)context_ & 0xF) == 0);
  xe_zero_struct(context_, sizeof(PPCContext));

  // Stash pointers to common structures that callbacks may need.
  context_->membase       = memory_->membase();
  context_->runtime       = runtime;
  context_->thread_state  = this;

  // Set initial registers.
  context_->r[1]          = stack_address_ + stack_size;
  context_->r[13]         = thread_state_address_;

  raw_context_ = context_;

  alloy::tracing::WriteEvent(EventType::ThreadInit({
  }));

  runtime_->debugger()->OnThreadCreated(this);
}

XenonThreadState::~XenonThreadState() {
  runtime_->debugger()->OnThreadDestroyed(this);

  CloseHandle(debug_break_);

  alloy::tracing::WriteEvent(EventType::ThreadDeinit({
  }));

  xe_free_aligned(context_);
  memory_->HeapFree(stack_address_, stack_size_);
}

volatile int* XenonThreadState::suspend_flag_address() const {
  return &context_->suspend_flag;
}

int XenonThreadState::Suspend(uint32_t timeout_ms) {
  // Set suspend flag.
  // One of the checks should call in to OnSuspend() at some point.
  xe_atomic_inc_32(&context_->suspend_flag);
  return 0;
}

int XenonThreadState::Resume(bool force) {
  if (context_->suspend_flag) {
    if (force) {
      context_->suspend_flag = 0;
      SetEvent(debug_break_);
    } else {
      if (!xe_atomic_dec_32(&context_->suspend_flag)) {
        SetEvent(debug_break_);
      }
    }
  }
  return 0;
}

void XenonThreadState::EnterSuspend() {
  WaitForSingleObject(debug_break_, INFINITE);
}
