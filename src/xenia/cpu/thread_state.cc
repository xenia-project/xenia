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

#include "xenia/xbox.h"

namespace xe {
namespace cpu {

thread_local ThreadState* thread_state_ = nullptr;

ThreadState::ThreadState(Processor* processor, uint32_t thread_id,
                         uint32_t stack_base, uint32_t pcr_address)
    : processor_(processor),
      memory_(processor->memory()),
      thread_id_(thread_id) {
  if (thread_id_ == UINT_MAX) {
    // System thread. Assign the system thread ID with a high bit
    // set so people know what's up.
    uint32_t system_thread_handle = xe::threading::current_thread_system_id();
    thread_id_ = 0x80000000 | system_thread_handle;
  }
  backend_data_ = processor->backend()->AllocThreadData();

  // Allocate with 64b alignment.
  context_ = memory::AlignedAlloc<ppc::PPCContext>(64);
  assert_true(((uint64_t)context_ & 0x3F) == 0);
  std::memset(context_, 0, sizeof(ppc::PPCContext));

  // Stash pointers to common structures that callbacks may need.
  context_->global_mutex = &xe::global_critical_region::mutex();
  context_->virtual_membase = memory_->virtual_membase();
  context_->physical_membase = memory_->physical_membase();
  context_->processor = processor_;
  context_->thread_state = this;
  context_->thread_id = thread_id_;

  // Set initial registers.
  context_->r[1] = stack_base;
  context_->r[13] = pcr_address;
}

ThreadState::~ThreadState() {
  if (backend_data_) {
    processor_->backend()->FreeThreadData(backend_data_);
  }
  if (thread_state_ == this) {
    thread_state_ = nullptr;
  }

  memory::AlignedFree(context_);
}

void ThreadState::Bind(ThreadState* thread_state) {
  thread_state_ = thread_state;
}

ThreadState* ThreadState::Get() { return thread_state_; }

uint32_t ThreadState::GetThreadID() {
  return thread_state_ ? thread_state_->thread_id_ : 0xFFFFFFFF;
}

}  // namespace cpu
}  // namespace xe
