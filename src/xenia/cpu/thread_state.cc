/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/thread_state.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/debug/debugger.h"

#include "xenia/xbox.h"

namespace xe {
namespace cpu {

using namespace xe::cpu;

using PPCContext = xe::cpu::frontend::PPCContext;

thread_local ThreadState* thread_state_ = nullptr;

ThreadState::ThreadState(Processor* processor, uint32_t thread_id,
                         uint32_t stack_address, uint32_t stack_size,
                         uint32_t pcr_address)
    : processor_(processor),
      memory_(processor->memory()),
      thread_id_(thread_id),
      name_(""),
      backend_data_(0),
      stack_size_(stack_size),
      pcr_address_(pcr_address) {
  if (thread_id_ == UINT_MAX) {
    // System thread. Assign the system thread ID with a high bit
    // set so people know what's up.
    uint32_t system_thread_handle = xe::threading::current_thread_id();
    thread_id_ = 0x80000000 | system_thread_handle;
  }
  backend_data_ = processor->backend()->AllocThreadData();

  uint32_t stack_position;
  if (!stack_address) {
    stack_size = (stack_size + 0xFFF) & 0xFFFFF000;
    uint32_t stack_alignment = (stack_size & 0xF000) ? 0x1000 : 0x10000;
    uint32_t stack_padding = stack_alignment * 1;
    uint32_t actual_stack_size = stack_padding + stack_size;
    memory()
        ->LookupHeapByType(false, 0x10000)
        ->Alloc(actual_stack_size, stack_alignment,
                kMemoryAllocationReserve | kMemoryAllocationCommit,
                kMemoryProtectRead | kMemoryProtectWrite, true,
                &stack_address_);
    assert_true(!(stack_address_ & 0xFFF));  // just to be safe
    stack_position = stack_address_ + actual_stack_size;
    stack_allocated_ = true;
    memset(memory()->TranslateVirtual(stack_address_), 0xBE, actual_stack_size);
    memory()
        ->LookupHeap(stack_address_)
        ->Protect(stack_address_, stack_padding, kMemoryProtectNoAccess);
  } else {
    stack_address_ = stack_address;
    stack_position = stack_address_ + stack_size;
    stack_allocated_ = false;
  }
  assert_not_zero(stack_address_);

  // Allocate with 64b alignment.
  context_ =
      reinterpret_cast<PPCContext*>(_aligned_malloc(sizeof(PPCContext), 64));
  assert_true(((uint64_t)context_ & 0x3F) == 0);
  std::memset(context_, 0, sizeof(PPCContext));

  // Stash pointers to common structures that callbacks may need.
  context_->reserve_address = memory_->reserve_address();
  context_->reserve_value = memory_->reserve_value();
  context_->virtual_membase = memory_->virtual_membase();
  context_->physical_membase = memory_->physical_membase();
  context_->processor = processor_;
  context_->thread_state = this;
  context_->thread_id = thread_id_;

  // Set initial registers.
  context_->r[1] = stack_position;
  context_->r[13] = pcr_address_;

  // Pad out stack a bit, as some games seem to overwrite the caller by about
  // 16 to 32b.
  context_->r[1] -= 64;

  processor_->debugger()->OnThreadCreated(this);
}

ThreadState::~ThreadState() {
  processor_->debugger()->OnThreadDestroyed(this);

  if (backend_data_) {
    processor_->backend()->FreeThreadData(backend_data_);
  }
  if (thread_state_ == this) {
    thread_state_ = nullptr;
  }

  _aligned_free(context_);
  if (stack_allocated_) {
    memory()->LookupHeap(stack_address_)->Decommit(stack_address_, stack_size_);
  }
}

void ThreadState::Bind(ThreadState* thread_state) {
  thread_state_ = thread_state;
}

ThreadState* ThreadState::Get() { return thread_state_; }

uint32_t ThreadState::GetThreadID() { return thread_state_->thread_id_; }

}  // namespace cpu
}  // namespace xe
