/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/xenon_thread_state.h>

#include <xdb/protocol.h>
#include <xenia/cpu/xenon_runtime.h>

using namespace alloy;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;
using namespace xe::cpu;

XenonThreadState::XenonThreadState(XenonRuntime* runtime, uint32_t thread_id,
                                   size_t stack_size,
                                   uint64_t thread_state_address)
    : ThreadState(runtime, thread_id),
      stack_size_(stack_size),
      thread_state_address_(thread_state_address) {
  stack_address_ = memory_->HeapAlloc(0, stack_size, MEMORY_FLAG_ZERO);
  assert_not_zero(stack_address_);

  // Allocate with 64b alignment.
  context_ = (PPCContext*)xe_malloc_aligned(sizeof(PPCContext));
  assert_true(((uint64_t)context_ & 0xF) == 0);
  xe_zero_struct(context_, sizeof(PPCContext));

  // Stash pointers to common structures that callbacks may need.
  context_->reserve_address = memory_->reserve_address();
  context_->membase = memory_->membase();
  context_->runtime = runtime;
  context_->thread_state = this;
  context_->thread_id = thread_id_;

  // Set initial registers.
  context_->r[1] = stack_address_ + stack_size;
  context_->r[13] = thread_state_address_;

  // Pad out stack a bit, as some games seem to overwrite the caller by about
  // 16 to 32b.
  context_->r[1] -= 64;

  raw_context_ = context_;

  runtime_->debugger()->OnThreadCreated(this);
}

XenonThreadState::~XenonThreadState() {
  runtime_->debugger()->OnThreadDestroyed(this);

  xe_free_aligned(context_);
  memory_->HeapFree(stack_address_, stack_size_);
}

void XenonThreadState::WriteRegisters(xdb::protocol::Registers* registers) {
  registers->lr = context_->lr;
  registers->ctr = context_->ctr;
  registers->xer = 0xFEFEFEFE;
  registers->cr[0] = context_->cr0.value;
  registers->cr[1] = context_->cr1.value;
  registers->cr[2] = context_->cr2.value;
  registers->cr[3] = context_->cr3.value;
  registers->cr[4] = context_->cr4.value;
  registers->cr[5] = context_->cr5.value;
  registers->cr[6] = context_->cr6.value;
  registers->cr[7] = context_->cr7.value;
  registers->fpscr = context_->fpscr.value;
  registers->vscr = context_->vscr_sat;
  static_assert(sizeof(registers->gpr) == sizeof(context_->r),
                "structs must match");
  static_assert(sizeof(registers->fpr) == sizeof(context_->f),
                "structs must match");
  static_assert(sizeof(registers->vr) == sizeof(context_->v),
                "structs must match");
  memcpy(registers->gpr, context_->r, sizeof(context_->r));
  memcpy(registers->fpr, context_->f, sizeof(context_->f));
  memcpy(registers->vr, context_->v, sizeof(context_->v));
}
