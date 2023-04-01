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

static void* AllocateContext() {
  size_t granularity = xe::memory::allocation_granularity();
  for (unsigned pos32 = 0x40; pos32 < 8192; ++pos32) {
    /*
        we want our register which points to the context to have 0xE0000000 in
       the low 32 bits, for checking for whether we need the 4k offset, but also
       if we allocate starting from the page before we allow backends to index
       negatively to get to their own backend specific data, which makes full
        use of int8 displacement


        the downside is we waste most of one granula and probably a fair bit of
       the one starting at 0xE0 by using a direct virtual memory allocation
       instead of malloc
    */
    uintptr_t context_pre =
        ((static_cast<uint64_t>(pos32) << 32) | 0xE0000000) - granularity;

    void* p = memory::AllocFixed(
        (void*)context_pre, granularity + sizeof(ppc::PPCContext),
        memory::AllocationType::kReserveCommit, memory::PageAccess::kReadWrite);
    if (p) {
      return reinterpret_cast<char*>(p) +
             granularity;  // now we have a ctx ptr with the e0 constant in low,
                           // and one page allocated before it
    }
  }

  assert_always("giving up on allocating context, likely leaking contexts");
  return nullptr;
}

static void FreeContext(void* ctx) {
  char* true_start_of_ctx = &reinterpret_cast<char*>(
      ctx)[-static_cast<ptrdiff_t>(xe::memory::allocation_granularity())];
  memory::DeallocFixed(true_start_of_ctx, 0,
                       memory::DeallocationType::kRelease);
}

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

  context_ = reinterpret_cast<ppc::PPCContext*>(
	  AllocateContext());
  processor->backend()->InitializeBackendContext(context_);
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
  // fixme: VSCR must be set here!
  context_->msr = 0x9030;  // dumped from a real 360, 0x8000

  //this register can be used for arbitrary data according to the PPC docs
  //but the suggested use is to mark which vector registers are in use, for faster save/restore
  //it seems unlikely anything uses this, especially since we have way more than 32 vrs, but setting it to all ones seems closer to correct than 0
  context_->vrsave = ~0u;
}

ThreadState::~ThreadState() {
  if (backend_data_) {
    processor_->backend()->FreeThreadData(backend_data_);
  }
  if (thread_state_ == this) {
    thread_state_ = nullptr;
  }
  if (context_) {
    processor_->backend()->DeinitializeBackendContext(context_);
    FreeContext(reinterpret_cast<void*>(context_));
  }
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
