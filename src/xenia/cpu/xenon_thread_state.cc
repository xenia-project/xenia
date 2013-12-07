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
}

XenonThreadState::~XenonThreadState() {
  alloy::tracing::WriteEvent(EventType::ThreadDeinit({
  }));

  xe_free_aligned(context_);
  memory_->HeapFree(stack_address_, stack_size_);
}
