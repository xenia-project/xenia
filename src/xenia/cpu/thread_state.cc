/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/thread_state.h"

#include <cstdlib>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/debug/debugger.h"

#include "xenia/xbox.h"

namespace xe {
namespace cpu {

using PPCContext = xe::cpu::frontend::PPCContext;

thread_local ThreadState* thread_state_ = nullptr;

ThreadState::ThreadState(Processor* processor, uint32_t thread_id,
                         ThreadStackType stack_type, uint32_t stack_address,
                         uint32_t stack_size, uint32_t pcr_address)
    : processor_(processor),
      memory_(processor->memory()),
      thread_id_(thread_id),
      stack_type_(stack_type),
      name_(""),
      backend_data_(0),
      stack_size_(stack_size),
      pcr_address_(pcr_address) {
  if (thread_id_ == UINT_MAX) {
    // System thread. Assign the system thread ID with a high bit
    // set so people know what's up.
    uint32_t system_thread_handle = xe::threading::current_thread_system_id();
    thread_id_ = 0x80000000 | system_thread_handle;
  }
  backend_data_ = processor->backend()->AllocThreadData();

  if (!stack_address) {
    // We must always allocate 64K as a guard region before stacks, as we can
    // only Protect() on system page granularity.
    auto heap = memory()->LookupHeap(0x40000000);
    stack_size = (stack_size + 0xFFF) & 0xFFFFF000;
    uint32_t stack_alignment = (stack_size & 0xF000) ? 0x1000 : 0x10000;
    uint32_t stack_padding = heap->page_size();
    uint32_t actual_stack_size = stack_padding + stack_size;
    bool top_down = false;
    switch (stack_type) {
      case ThreadStackType::kKernelStack:
        top_down = true;
        break;
      case ThreadStackType::kUserStack:
        top_down = false;
        break;
      default:
        assert_unhandled_case(stack_type);
        break;
    }
    heap->AllocRange(0x40000000, 0x7FFFFFFF, actual_stack_size, stack_alignment,
                     kMemoryAllocationReserve | kMemoryAllocationCommit,
                     kMemoryProtectRead | kMemoryProtectWrite, top_down,
                     &stack_address_);
    assert_true(!(stack_address_ & 0xFFF));  // just to be safe
    stack_allocated_ = true;
    stack_base_ = stack_address_ + actual_stack_size;
    stack_limit_ = stack_address_ + stack_padding;
    memory()->Fill(stack_address_, actual_stack_size, 0xBE);
    heap->Protect(stack_address_, stack_padding, kMemoryProtectNoAccess);
  } else {
    stack_address_ = stack_address;
    stack_allocated_ = false;
    stack_base_ = stack_address_ + stack_size;
    stack_limit_ = stack_address_;
  }
  assert_not_zero(stack_address_);

  // Allocate with 64b alignment.
  context_ = memory::AlignedAlloc<PPCContext>(64);
  assert_true(((uint64_t)context_ & 0x3F) == 0);
  std::memset(context_, 0, sizeof(PPCContext));

  // Stash pointers to common structures that callbacks may need.
  context_->reserve_address = memory_->reserve_address();
  context_->virtual_membase = memory_->virtual_membase();
  context_->physical_membase = memory_->physical_membase();
  context_->processor = processor_;
  context_->thread_state = this;
  context_->thread_id = thread_id_;

  // Set initial registers.
  context_->r[1] = stack_base_;
  context_->r[13] = pcr_address_;
}

ThreadState::~ThreadState() {
  if (backend_data_) {
    processor_->backend()->FreeThreadData(backend_data_);
  }
  if (thread_state_ == this) {
    thread_state_ = nullptr;
  }

  memory::AlignedFree(context_);
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
