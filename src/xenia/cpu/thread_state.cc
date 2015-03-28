/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/thread_state.h"

#include "xdb/protocol.h"
#include "xenia/cpu/runtime.h"

namespace xe {
namespace cpu {

using namespace xe::cpu;

using PPCContext = xe::cpu::frontend::PPCContext;

thread_local ThreadState* thread_state_ = nullptr;

ThreadState::ThreadState(Runtime* runtime, uint32_t thread_id,
                         uint32_t stack_address, uint32_t stack_size,
                         uint32_t thread_state_address)
    : runtime_(runtime),
      memory_(runtime->memory()),
      thread_id_(thread_id),
      name_(""),
      backend_data_(0),
      raw_context_(0),
      stack_size_(stack_size),
      thread_state_address_(thread_state_address) {
  if (thread_id_ == UINT_MAX) {
    // System thread. Assign the system thread ID with a high bit
    // set so people know what's up.
    uint32_t system_thread_handle = poly::threading::current_thread_id();
    thread_id_ = 0x80000000 | system_thread_handle;
  }
  backend_data_ = runtime->backend()->AllocThreadData();

  if (!stack_address) {
    stack_address_ = memory()->SystemHeapAlloc(stack_size);
    stack_allocated_ = true;
  } else {
    stack_address_ = stack_address;
    stack_allocated_ = false;
  }
  assert_not_zero(stack_address_);

  // Allocate with 64b alignment.
  context_ = (PPCContext*)calloc(1, sizeof(PPCContext));
  assert_true(((uint64_t)context_ & 0xF) == 0);

  // Stash pointers to common structures that callbacks may need.
  context_->reserve_address = memory_->reserve_address();
  context_->reserve_value = memory_->reserve_value();
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

ThreadState::~ThreadState() {
  runtime_->debugger()->OnThreadDestroyed(this);

  if (backend_data_) {
    runtime_->backend()->FreeThreadData(backend_data_);
  }
  if (thread_state_ == this) {
    thread_state_ = nullptr;
  }

  free(context_);
  if (stack_allocated_) {
    memory()->SystemHeapFree(stack_address_);
  }
}

void ThreadState::Bind(ThreadState* thread_state) {
  thread_state_ = thread_state;
}

ThreadState* ThreadState::Get() { return thread_state_; }

uint32_t ThreadState::GetThreadID() { return thread_state_->thread_id_; }

void ThreadState::WriteRegisters(xdb::protocol::Registers* registers) {
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

}  // namespace cpu
}  // namespace xe
