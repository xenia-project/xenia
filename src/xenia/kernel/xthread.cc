/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xthread.h"

#include <gflags/gflags.h>

#include <cstring>

#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/frontend/ppc_instr.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xevent.h"

DEFINE_bool(ignore_thread_priorities, true,
            "Ignores game-specified thread priorities.");
DEFINE_bool(ignore_thread_affinities, true,
            "Ignores game-specified thread affinities.");

namespace xe {
namespace kernel {

uint32_t next_xthread_id_ = 0;
thread_local XThread* current_thread_tls_ = nullptr;

XThread::XThread(KernelState* kernel_state)
    : XObject(kernel_state, kTypeThread), guest_thread_(true) {}

XThread::XThread(KernelState* kernel_state, uint32_t stack_size,
                 uint32_t xapi_thread_startup, uint32_t start_address,
                 uint32_t start_context, uint32_t creation_flags,
                 bool guest_thread, bool main_thread)
    : XObject(kernel_state, kTypeThread),
      thread_id_(++next_xthread_id_),
      guest_thread_(guest_thread),
      main_thread_(main_thread),
      apc_list_(kernel_state->memory()) {
  creation_params_.stack_size = stack_size;
  creation_params_.xapi_thread_startup = xapi_thread_startup;
  creation_params_.start_address = start_address;
  creation_params_.start_context = start_context;

  // top 8 bits = processor ID (or 0 for default)
  // bit 0 = 1 to create suspended
  creation_params_.creation_flags = creation_flags;

  // Adjust stack size - min of 16k.
  if (creation_params_.stack_size < 16 * 1024) {
    creation_params_.stack_size = 16 * 1024;
  }

  if (!guest_thread_) {
    host_object_ = true;
  }

  // The kernel does not take a reference. We must unregister in the dtor.
  kernel_state_->RegisterThread(this);
}

XThread::~XThread() {
  // Unregister first to prevent lookups while deleting.
  kernel_state_->UnregisterThread(this);

  if (emulator()->debugger()) {
    emulator()->debugger()->OnThreadDestroyed(this);
  }

  thread_.reset();

  if (thread_state_) {
    delete thread_state_;
  }
  kernel_state()->memory()->SystemHeapFree(scratch_address_);
  kernel_state()->memory()->SystemHeapFree(tls_address_);
  kernel_state()->memory()->SystemHeapFree(pcr_address_);

  if (thread_) {
    // TODO(benvanik): platform kill
    XELOGE("Thread disposed without exiting");
  }
}

bool XThread::IsInThread(XThread* other) {
  return current_thread_tls_ == other;
}
bool XThread::IsInThread() { return current_thread_tls_ != nullptr; }

XThread* XThread::GetCurrentThread() {
  XThread* thread = current_thread_tls_;
  if (!thread) {
    assert_always("Attempting to use kernel stuff from a non-kernel thread");
  }
  return thread;
}

uint32_t XThread::GetCurrentThreadHandle() {
  XThread* thread = XThread::GetCurrentThread();
  return thread->handle();
}

uint32_t XThread::GetCurrentThreadId() {
  XThread* thread = XThread::GetCurrentThread();
  return thread->guest_object<X_KTHREAD>()->thread_id;
}

uint32_t XThread::GetLastError() {
  XThread* thread = XThread::GetCurrentThread();
  return thread->last_error();
}

void XThread::SetLastError(uint32_t error_code) {
  XThread* thread = XThread::GetCurrentThread();
  thread->set_last_error(error_code);
}

uint32_t XThread::last_error() { return guest_object<X_KTHREAD>()->last_error; }

void XThread::set_last_error(uint32_t error_code) {
  guest_object<X_KTHREAD>()->last_error = error_code;
}

void XThread::set_name(const std::string& name) {
  name_ = xe::format_string("%s (%.8X)", name.c_str(), handle());

  if (thread_) {
    // May be getting set before the thread is created.
    // One the thread is ready it will handle it.
    thread_->set_name(name_);
  }
}

uint8_t next_cpu = 0;
uint8_t GetFakeCpuNumber(uint8_t proc_mask) {
  if (!proc_mask) {
    next_cpu++;
    if (next_cpu > 6) {
      next_cpu = 0;
    }

    return next_cpu;  // is this reasonable?
  }
  assert_false(proc_mask & 0xC0);

  uint8_t cpu_number = 7 - xe::lzcnt(proc_mask);
  assert_true(1 << cpu_number == proc_mask);
  return cpu_number;
}

void XThread::InitializeGuestObject() {
  auto guest_thread = guest_object<X_KTHREAD>();

  // Setup the thread state block (last error/etc).
  uint8_t* p = memory()->TranslateVirtual(guest_object());
  guest_thread->header.type = 6;
  guest_thread->suspend_count =
      (creation_params_.creation_flags & X_CREATE_SUSPENDED) ? 1 : 0;

  xe::store_and_swap<uint32_t>(p + 0x010, guest_object() + 0x010);
  xe::store_and_swap<uint32_t>(p + 0x014, guest_object() + 0x010);

  xe::store_and_swap<uint32_t>(p + 0x040, guest_object() + 0x018 + 8);
  xe::store_and_swap<uint32_t>(p + 0x044, guest_object() + 0x018 + 8);
  xe::store_and_swap<uint32_t>(p + 0x048, guest_object());
  xe::store_and_swap<uint32_t>(p + 0x04C, guest_object() + 0x018);

  xe::store_and_swap<uint16_t>(p + 0x054, 0x102);
  xe::store_and_swap<uint16_t>(p + 0x056, 1);
  xe::store_and_swap<uint32_t>(p + 0x05C, stack_base_);
  xe::store_and_swap<uint32_t>(p + 0x060, stack_limit_);
  xe::store_and_swap<uint32_t>(p + 0x068, tls_address_);
  xe::store_and_swap<uint8_t>(p + 0x06C, 0);
  xe::store_and_swap<uint32_t>(p + 0x074, guest_object() + 0x074);
  xe::store_and_swap<uint32_t>(p + 0x078, guest_object() + 0x074);
  xe::store_and_swap<uint32_t>(p + 0x07C, guest_object() + 0x07C);
  xe::store_and_swap<uint32_t>(p + 0x080, guest_object() + 0x07C);
  xe::store_and_swap<uint32_t>(p + 0x084,
                               kernel_state_->process_info_block_address());
  xe::store_and_swap<uint8_t>(p + 0x08B, 1);
  // 0xD4 = APC
  // 0xFC = semaphore (ptr, 0, 2)
  // 0xA88 = APC
  // 0x18 = timer
  xe::store_and_swap<uint32_t>(p + 0x09C, 0xFDFFD7FF);
  xe::store_and_swap<uint32_t>(p + 0x0D0, stack_base_);
  xe::store_and_swap<uint64_t>(p + 0x130, Clock::QueryGuestSystemTime());
  xe::store_and_swap<uint32_t>(p + 0x144, guest_object() + 0x144);
  xe::store_and_swap<uint32_t>(p + 0x148, guest_object() + 0x144);
  xe::store_and_swap<uint32_t>(p + 0x14C, thread_id_);
  xe::store_and_swap<uint32_t>(p + 0x150, creation_params_.start_address);
  xe::store_and_swap<uint32_t>(p + 0x154, guest_object() + 0x154);
  xe::store_and_swap<uint32_t>(p + 0x158, guest_object() + 0x154);
  xe::store_and_swap<uint32_t>(p + 0x160, 0);  // last error
  xe::store_and_swap<uint32_t>(p + 0x16C, creation_params_.creation_flags);
  xe::store_and_swap<uint32_t>(p + 0x17C, 1);
}

bool XThread::AllocateStack(uint32_t size) {
  auto heap = memory()->LookupHeap(0x40000000);

  auto alignment = heap->page_size();
  auto padding = heap->page_size() * 2;  // Guard pages
  size = xe::round_up(size, alignment);
  auto actual_size = size + padding;

  uint32_t address = 0;
  if (!heap->AllocRange(0x40000000, 0x7F000000, actual_size, alignment,
                        kMemoryAllocationReserve | kMemoryAllocationCommit,
                        kMemoryProtectRead | kMemoryProtectWrite, false,
                        &address)) {
    return false;
  }

  stack_alloc_base_ = address;
  stack_alloc_size_ = actual_size;
  stack_limit_ = address + (padding / 2);
  stack_base_ = stack_limit_ + size;

  // Initialize the stack with junk
  memory()->Fill(stack_alloc_base_, actual_size, 0xBE);

  // Setup the guard pages
  heap->Protect(stack_alloc_base_, padding / 2, kMemoryProtectNoAccess);
  heap->Protect(stack_base_, padding / 2, kMemoryProtectNoAccess);

  return true;
}

X_STATUS XThread::Create() {
  // Thread kernel object.
  if (!CreateNative<X_KTHREAD>()) {
    XELOGW("Unable to allocate thread object");
    return X_STATUS_NO_MEMORY;
  }

  // Allocate a stack.
  if (!AllocateStack(creation_params_.stack_size)) {
    return X_STATUS_NO_MEMORY;
  }

  // Allocate thread scratch.
  // This is used by interrupts/APCs/etc so we can round-trip pointers through.
  scratch_size_ = 4 * 16;
  scratch_address_ = memory()->SystemHeapAlloc(scratch_size_);

  // Allocate TLS block.
  // Games will specify a certain number of 4b slots that each thread will get.
  xex2_opt_tls_info* tls_header = nullptr;
  auto module = kernel_state()->GetExecutableModule();
  if (module) {
    module->GetOptHeader(XEX_HEADER_TLS_INFO, &tls_header);
  }

  const uint32_t kDefaultTlsSlotCount = 32;
  uint32_t tls_slots = kDefaultTlsSlotCount;
  uint32_t tls_extended_size = 0;
  if (tls_header && tls_header->slot_count) {
    tls_slots = tls_header->slot_count;
    tls_extended_size = tls_header->data_size;
  }

  // Allocate both the slots and the extended data.
  // HACK: we're currently not using the extra memory allocated for TLS slots
  // and instead relying on native TLS slots, so don't allocate anything for
  // the slots.
  uint32_t tls_slot_size = 0;  // tls_slots * 4;
  uint32_t tls_total_size = tls_slot_size + tls_extended_size;
  tls_address_ = memory()->SystemHeapAlloc(tls_total_size);
  if (!tls_address_) {
    XELOGW("Unable to allocate thread local storage block");
    return X_STATUS_NO_MEMORY;
  }

  // Zero all of TLS.
  memory()->Fill(tls_address_, tls_total_size, 0);
  if (tls_extended_size) {
    // If game has extended data, copy in the default values.
    assert_not_zero(tls_header->raw_data_address);
    memory()->Copy(tls_address_, tls_header->raw_data_address,
                   tls_header->raw_data_size);
  }

  // Allocate thread state block from heap.
  // http://www.microsoft.com/msj/archive/s2ce.aspx
  // This is set as r13 for user code and some special inlined Win32 calls
  // (like GetLastError/etc) will poke it directly.
  // We try to use it as our primary store of data just to keep things all
  // consistent.
  // 0x000: pointer to tls data
  // 0x100: pointer to TEB(?)
  // 0x10C: Current CPU(?)
  // 0x150: if >0 then error states don't get set (DPC active bool?)
  // TEB:
  // 0x14C: thread id
  // 0x160: last error
  // So, at offset 0x100 we have a 4b pointer to offset 200, then have the
  // structure.
  pcr_address_ = memory()->SystemHeapAlloc(0x2D8);
  if (!pcr_address_) {
    XELOGW("Unable to allocate thread state block");
    return X_STATUS_NO_MEMORY;
  }

  // Allocate processor thread state.
  // This is thread safe.
  thread_state_ = new cpu::ThreadState(kernel_state()->processor(), thread_id_,
                                       stack_base_, pcr_address_);
  XELOGI("XThread%08X (%X) Stack: %.8X-%.8X", handle(), thread_id_,
         stack_limit_, stack_base_);

  // Exports use this to get the kernel.
  thread_state_->context()->kernel_state = kernel_state_;

  X_KPCR* pcr = memory()->TranslateVirtual<X_KPCR*>(pcr_address_);

  pcr->tls_ptr = tls_address_;
  pcr->pcr_ptr = pcr_address_;
  pcr->current_thread = guest_object();

  pcr->stack_base_ptr = stack_base_;
  pcr->stack_end_ptr = stack_limit_;

  uint8_t proc_mask =
      static_cast<uint8_t>(creation_params_.creation_flags >> 24);

  pcr->current_cpu = GetFakeCpuNumber(proc_mask);  // Current CPU(?)
  pcr->dpc_active = 0;                             // DPC active bool?

  // Initialize the KTHREAD object.
  InitializeGuestObject();

  // Always retain when starting - the thread owns itself until exited.
  Retain();
  RetainHandle();

  xe::threading::Thread::CreationParameters params;
  params.stack_size = 16 * 1024 * 1024;  // Allocate a big host stack.
  params.create_suspended = true;
  thread_ = xe::threading::Thread::Create(params, [this]() {
    // Set thread ID override. This is used by logging.
    xe::threading::set_current_thread_id(handle());

    // Set name immediately, if we have one.
    thread_->set_name(name());

    // Profiler needs to know about the thread.
    xe::Profiler::ThreadEnter(name().c_str());

    // Execute user code.
    current_thread_tls_ = this;
    Execute();
    current_thread_tls_ = nullptr;

    xe::Profiler::ThreadExit();

    // Release the self-reference to the thread.
    Release();
  });

  if (!thread_) {
    // TODO(benvanik): translate error?
    XELOGE("CreateThread failed");
    return X_STATUS_NO_MEMORY;
  }
  thread_->set_affinity_mask(proc_mask);

  // Set the thread name based on host ID (for easier debugging).
  if (name_.empty()) {
    char thread_name[32];
    snprintf(thread_name, xe::countof(thread_name), "XThread%.04X",
             thread_->system_id());
    set_name(thread_name);
  }

  if (creation_params_.creation_flags & 0x60) {
    thread_->set_priority(creation_params_.creation_flags & 0x20 ? 1 : 0);
  }

  if (emulator()->debugger()) {
    emulator()->debugger()->OnThreadCreated(this);
  }

  if ((creation_params_.creation_flags & X_CREATE_SUSPENDED) == 0) {
    // Start the thread now that we're all setup.
    thread_->Resume();
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XThread::Exit(int exit_code) {
  // This may only be called on the thread itself.
  assert_true(XThread::GetCurrentThread() == this);

  // TODO(benvanik): dispatch events? waiters? etc?
  RundownAPCs();

  // Set exit code.
  X_KTHREAD* thread = guest_object<X_KTHREAD>();
  thread->header.signal_state = 1;
  thread->exit_status = exit_code;

  kernel_state()->OnThreadExit(this);

  if (emulator()->debugger()) {
    emulator()->debugger()->OnThreadExit(this);
  }

  // NOTE: unless PlatformExit fails, expect it to never return!
  current_thread_tls_ = nullptr;
  xe::Profiler::ThreadExit();

  running_ = false;
  Release();
  ReleaseHandle();

  // NOTE: this does not return!
  xe::threading::Thread::Exit(exit_code);
  return X_STATUS_SUCCESS;
}

X_STATUS XThread::Terminate(int exit_code) {
  // TODO(benvanik): inform the profiler that this thread is exiting.

  // Set exit code.
  X_KTHREAD* thread = guest_object<X_KTHREAD>();
  thread->header.signal_state = 1;
  thread->exit_status = exit_code;

  if (emulator()->debugger()) {
    emulator()->debugger()->OnThreadExit(this);
  }

  running_ = false;
  Release();
  ReleaseHandle();

  thread_->Terminate(exit_code);
  return X_STATUS_SUCCESS;
}

void XThread::Execute() {
  XELOGKERNEL("XThread::Execute thid %d (handle=%.8X, '%s', native=%.8X)",
              thread_id_, handle(), name_.c_str(), thread_->system_id());
  running_ = true;

  // Let the kernel know we are starting.
  kernel_state()->OnThreadExecute(this);

  // All threads get a mandatory sleep. This is to deal with some buggy
  // games that are assuming the 360 is so slow to create threads that they
  // have time to initialize shared structures AFTER CreateThread (RR).
  xe::threading::Sleep(std::chrono::milliseconds(100));

  int exit_code = 0;

  // Dispatch any APCs that were queued before the thread was created first.
  DeliverAPCs();

  // If a XapiThreadStartup value is present, we use that as a trampoline.
  // Otherwise, we are a raw thread.
  if (creation_params_.xapi_thread_startup) {
    uint64_t args[] = {creation_params_.start_address,
                       creation_params_.start_context};
    kernel_state()->processor()->Execute(thread_state_,
                                         creation_params_.xapi_thread_startup,
                                         args, xe::countof(args));
  } else {
    // Run user code.
    uint64_t args[] = {creation_params_.start_context};
    exit_code = static_cast<int>(kernel_state()->processor()->Execute(
        thread_state_, creation_params_.start_address, args,
        xe::countof(args)));
    // If we got here it means the execute completed without an exit being
    // called.
    // Treat the return code as an implicit exit code.
  }

  running_ = false;
  Exit(exit_code);
}

void XThread::EnterCriticalRegion() {
  xe::global_critical_region::mutex().lock();
}

void XThread::LeaveCriticalRegion() {
  xe::global_critical_region::mutex().unlock();
}

uint32_t XThread::RaiseIrql(uint32_t new_irql) {
  return irql_.exchange(new_irql);
}

void XThread::LowerIrql(uint32_t new_irql) { irql_ = new_irql; }

void XThread::CheckApcs() { DeliverAPCs(); }

void XThread::LockApc() { EnterCriticalRegion(); }

void XThread::UnlockApc(bool queue_delivery) {
  bool needs_apc = apc_list_.HasPending();
  LeaveCriticalRegion();
  if (needs_apc && queue_delivery) {
    thread_->QueueUserCallback([this]() { DeliverAPCs(); });
  }
}

void XThread::EnqueueApc(uint32_t normal_routine, uint32_t normal_context,
                         uint32_t arg1, uint32_t arg2) {
  LockApc();

  // Allocate APC.
  // We'll tag it as special and free it when dispatched.
  uint32_t apc_ptr = memory()->SystemHeapAlloc(XAPC::kSize);
  auto apc = reinterpret_cast<XAPC*>(memory()->TranslateVirtual(apc_ptr));

  apc->Initialize();
  apc->kernel_routine = XAPC::kDummyKernelRoutine;
  apc->rundown_routine = XAPC::kDummyRundownRoutine;
  apc->normal_routine = normal_routine;
  apc->normal_context = normal_context;
  apc->arg1 = arg1;
  apc->arg2 = arg2;
  apc->enqueued = 1;

  uint32_t list_entry_ptr = apc_ptr + 8;
  apc_list_.Insert(list_entry_ptr);

  UnlockApc(true);
}

void XThread::DeliverAPCs() {
  // http://www.drdobbs.com/inside-nts-asynchronous-procedure-call/184416590?pgno=1
  // http://www.drdobbs.com/inside-nts-asynchronous-procedure-call/184416590?pgno=7
  auto processor = kernel_state()->processor();
  LockApc();
  while (apc_list_.HasPending()) {
    // Get APC entry (offset for LIST_ENTRY offset) and cache what we need.
    // Calling the routine may delete the memory/overwrite it.
    uint32_t apc_ptr = apc_list_.Shift() - 8;
    auto apc = reinterpret_cast<XAPC*>(memory()->TranslateVirtual(apc_ptr));
    bool needs_freeing = apc->kernel_routine == XAPC::kDummyKernelRoutine;

    XELOGD("Delivering APC to %.8X", uint32_t(apc->normal_routine));

    // Mark as uninserted so that it can be reinserted again by the routine.
    apc->enqueued = 0;

    // Call kernel routine.
    // The routine can modify all of its arguments before passing it on.
    // Since we need to give guest accessible pointers over, we copy things
    // into and out of scratch.
    uint8_t* scratch_ptr = memory()->TranslateVirtual(scratch_address_);
    xe::store_and_swap<uint32_t>(scratch_ptr + 0, apc->normal_routine);
    xe::store_and_swap<uint32_t>(scratch_ptr + 4, apc->normal_context);
    xe::store_and_swap<uint32_t>(scratch_ptr + 8, apc->arg1);
    xe::store_and_swap<uint32_t>(scratch_ptr + 12, apc->arg2);
    if (apc->kernel_routine != XAPC::kDummyKernelRoutine) {
      // kernel_routine(apc_address, &normal_routine, &normal_context,
      // &system_arg1, &system_arg2)
      uint64_t kernel_args[] = {
          apc_ptr,
          scratch_address_ + 0,
          scratch_address_ + 4,
          scratch_address_ + 8,
          scratch_address_ + 12,
      };
      processor->Execute(thread_state_, apc->kernel_routine, kernel_args,
                         xe::countof(kernel_args));
    }
    uint32_t normal_routine = xe::load_and_swap<uint32_t>(scratch_ptr + 0);
    uint32_t normal_context = xe::load_and_swap<uint32_t>(scratch_ptr + 4);
    uint32_t arg1 = xe::load_and_swap<uint32_t>(scratch_ptr + 8);
    uint32_t arg2 = xe::load_and_swap<uint32_t>(scratch_ptr + 12);

    // Call the normal routine. Note that it may have been killed by the kernel
    // routine.
    if (normal_routine) {
      UnlockApc(false);
      // normal_routine(normal_context, system_arg1, system_arg2)
      uint64_t normal_args[] = {normal_context, arg1, arg2};
      processor->Execute(thread_state_, normal_routine, normal_args,
                         xe::countof(normal_args));
      LockApc();
    }

    XELOGD("Completed delivery of APC to %.8X (%.8X, %.8X, %.8X)",
           normal_routine, normal_context, arg1, arg2);

    // If special, free it.
    if (needs_freeing) {
      memory()->SystemHeapFree(apc_ptr);
    }
  }
  UnlockApc(true);
}

void XThread::RundownAPCs() {
  assert_true(XThread::GetCurrentThread() == this);
  LockApc();
  while (apc_list_.HasPending()) {
    // Get APC entry (offset for LIST_ENTRY offset) and cache what we need.
    // Calling the routine may delete the memory/overwrite it.
    uint32_t apc_ptr = apc_list_.Shift() - 8;
    auto apc = reinterpret_cast<XAPC*>(memory()->TranslateVirtual(apc_ptr));
    bool needs_freeing = apc->kernel_routine == XAPC::kDummyKernelRoutine;

    // Mark as uninserted so that it can be reinserted again by the routine.
    apc->enqueued = 0;

    // Call the rundown routine.
    if (apc->rundown_routine == XAPC::kDummyRundownRoutine) {
      // No-op.
    } else if (apc->rundown_routine) {
      // rundown_routine(apc)
      uint64_t args[] = {apc_ptr};
      kernel_state()->processor()->Execute(thread_state(), apc->rundown_routine,
                                           args, xe::countof(args));
    }

    // If special, free it.
    if (needs_freeing) {
      memory()->SystemHeapFree(apc_ptr);
    }
  }
  UnlockApc(true);
}

int32_t XThread::QueryPriority() { return thread_->priority(); }

void XThread::SetPriority(int32_t increment) {
  priority_ = increment;
  int32_t target_priority = 0;
  if (increment > 0x22) {
    target_priority = xe::threading::ThreadPriority::kHighest;
  } else if (increment > 0x11) {
    target_priority = xe::threading::ThreadPriority::kAboveNormal;
  } else if (increment < -0x22) {
    target_priority = xe::threading::ThreadPriority::kLowest;
  } else if (increment < -0x11) {
    target_priority = xe::threading::ThreadPriority::kBelowNormal;
  } else {
    target_priority = xe::threading::ThreadPriority::kNormal;
  }
  if (!FLAGS_ignore_thread_priorities) {
    thread_->set_priority(target_priority);
  }
}

void XThread::SetAffinity(uint32_t affinity) {
  // Affinity mask, as in SetThreadAffinityMask.
  // Xbox thread IDs:
  // 0 - core 0, thread 0 - user
  // 1 - core 0, thread 1 - user
  // 2 - core 1, thread 0 - sometimes xcontent
  // 3 - core 1, thread 1 - user
  // 4 - core 2, thread 0 - xaudio
  // 5 - core 2, thread 1 - user
  // TODO(benvanik): implement better thread distribution.
  // NOTE: these are logical processors, not physical processors or cores.
  if (xe::threading::logical_processor_count() < 6) {
    XELOGW("Too few processors - scheduling will be wonky");
  }
  SetActiveCpu(GetFakeCpuNumber(affinity));
  affinity_ = affinity;
  if (!FLAGS_ignore_thread_affinities) {
    thread_->set_affinity_mask(affinity);
  }
}

uint32_t XThread::active_cpu() const {
  uint8_t* pcr = memory()->TranslateVirtual(pcr_address_);
  return xe::load_and_swap<uint8_t>(pcr + 0x10C);
}

void XThread::SetActiveCpu(uint32_t cpu_index) {
  uint8_t* pcr = memory()->TranslateVirtual(pcr_address_);
  xe::store_and_swap<uint8_t>(pcr + 0x10C, cpu_index);
}

uint32_t XThread::suspend_count() {
  return guest_object<X_KTHREAD>()->suspend_count;
}

X_STATUS XThread::Resume(uint32_t* out_suspend_count) {
  --guest_object<X_KTHREAD>()->suspend_count;

  if (thread_->Resume(out_suspend_count)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

X_STATUS XThread::Suspend(uint32_t* out_suspend_count) {
  auto global_lock = global_critical_region_.Acquire();

  ++guest_object<X_KTHREAD>()->suspend_count;

  // If we are suspending ourselves, we can't hold the lock.
  if (XThread::IsInThread() && XThread::GetCurrentThread() == this) {
    global_lock.unlock();
  }

  if (thread_->Suspend(out_suspend_count)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

X_STATUS XThread::Delay(uint32_t processor_mode, uint32_t alertable,
                        uint64_t interval) {
  int64_t timeout_ticks = interval;
  uint32_t timeout_ms;
  if (timeout_ticks > 0) {
    // Absolute time, based on January 1, 1601.
    // TODO(benvanik): convert time to relative time.
    assert_always();
    timeout_ms = 0;
  } else if (timeout_ticks < 0) {
    // Relative time.
    timeout_ms = uint32_t(-timeout_ticks / 10000);  // Ticks -> MS
  } else {
    timeout_ms = 0;
  }
  timeout_ms = Clock::ScaleGuestDurationMillis(timeout_ms);
  if (alertable) {
    auto result =
        xe::threading::AlertableSleep(std::chrono::milliseconds(timeout_ms));
    switch (result) {
      default:
      case xe::threading::SleepResult::kSuccess:
        return X_STATUS_SUCCESS;
      case xe::threading::SleepResult::kAlerted:
        return X_STATUS_USER_APC;
    }
  } else {
    xe::threading::Sleep(std::chrono::milliseconds(timeout_ms));
    return X_STATUS_SUCCESS;
  }
}

bool XThread::StepToAddress(uint32_t pc) {
  auto functions = emulator()->processor()->FindFunctionsWithAddress(pc);
  if (functions.empty()) {
    // Function hasn't been generated yet. Generate it.
    if (!emulator()->processor()->ResolveFunction(pc)) {
      XELOGE("XThread::StepToAddress(%.8X) - Function could not be resolved",
             pc);
      return false;
    }
  }

  // Instruct the thread to step forwards.
  threading::Fence fence;
  cpu::Breakpoint bp(kernel_state()->processor(), pc,
                     [&fence](uint32_t guest_address, uint64_t host_address) {
                       fence.Signal();
                     });
  if (bp.Install()) {
    // HACK
    uint32_t suspend_count = 1;
    while (suspend_count) {
      thread_->Resume(&suspend_count);
    }

    fence.Wait();
    bp.Uninstall();
  } else {
    assert_always();
    XELOGE("XThread: Could not install breakpoint to step forward!");
    return false;
  }

  return true;
}

uint32_t XThread::StepIntoBranch(uint32_t pc) {
  cpu::frontend::InstrData i;
  i.address = pc;
  i.code = xe::load_and_swap<uint32_t>(memory()->TranslateVirtual(i.address));
  i.type = cpu::frontend::GetInstrType(i.code);

  auto context = thread_state_->context();

  if (i.type->type & (1 << 4) /* BranchAlways */
      || i.code == 0x4E800020 /* blr */
      || i.code == 0x4E800420 /* bctr */) {
    if (i.code == 0x4E800020) {
      // blr
      uint32_t nia = uint32_t(context->lr);
      StepToAddress(nia);
    } else if (i.code == 0x4E800420) {
      // bctr
      uint32_t nia = uint32_t(context->ctr);
      StepToAddress(nia);
    } else if (i.type->opcode == 0x48000000) {
      // bx
      uint32_t nia = 0;
      if (i.I.AA) {
        nia = (uint32_t)cpu::frontend::XEEXTS26(i.I.LI << 2);
      } else {
        nia = i.address + (uint32_t)cpu::frontend::XEEXTS26(i.I.LI << 2);
      }

      StepToAddress(nia);
    } else {
      // Unknown opcode.
      assert_always();
    }
  } else if (i.type->type & (1 << 3) /* BranchCond */) {
    threading::Fence fence;
    auto callback = [&fence, &pc](uint32_t guest_pc, uint64_t) {
      pc = guest_pc;
      fence.Signal();
    };

    cpu::Breakpoint bpt(kernel_state()->processor(), callback);
    cpu::Breakpoint bpf(kernel_state()->processor(), pc + 4, callback);
    if (!bpf.Install()) {
      XELOGE("XThread: Could not install breakpoint to step forward!");
      assert_always();
    }

    uint32_t nia = 0;
    if (i.type->opcode == 0x40000000) {
      // bcx
      if (i.B.AA) {
        nia = (uint32_t)cpu::frontend::XEEXTS16(i.B.BD << 2);
      } else {
        nia = (uint32_t)(i.address + cpu::frontend::XEEXTS16(i.B.BD << 2));
      }
    } else if (i.type->opcode == 0x4C000420) {
      // bcctrx
      nia = uint32_t(context->ctr);
    } else if (i.type->opcode == 0x4C000020) {
      // bclrx
      nia = uint32_t(context->lr);
    } else {
      // Unknown opcode.
      assert_always();
    }

    bpt.set_address(nia);
    if (!bpt.Install()) {
      assert_always();
      return 0;
    }

    // HACK
    uint32_t suspend_count = 1;
    while (suspend_count) {
      thread_->Resume(&suspend_count);
    }

    fence.Wait();
    bpt.Uninstall();
    bpf.Uninstall();
  }

  return pc;
}

uint32_t XThread::StepToSafePoint(bool ignore_host) {
  // This cannot be done if we're the calling thread!
  if (IsInThread() && GetCurrentThread() == this) {
    assert_always(
        "XThread::StepToSafePoint(): target thread is the calling thread!");
    return 0;
  }

  // Now the fun part begins: Registers are only guaranteed to be synchronized
  // with the PPC context at a basic block boundary. Unfortunately, we most
  // likely stopped the thread at some point other than a boundary. We need to
  // step forward until we reach a boundary, and then perform the save.
  auto stack_walker = kernel_state()->processor()->stack_walker();

  uint64_t frame_host_pcs[64];
  cpu::StackFrame cpu_frames[64];
  size_t count = stack_walker->CaptureStackTrace(
      thread_->native_handle(), frame_host_pcs, 0, xe::countof(frame_host_pcs),
      nullptr, nullptr);
  stack_walker->ResolveStack(frame_host_pcs, cpu_frames, count);
  if (count == 0) {
    return 0;
  }

  auto& first_frame = cpu_frames[0];
  if (ignore_host) {
    for (size_t i = 0; i < count; i++) {
      if (cpu_frames[i].type == cpu::StackFrame::Type::kGuest &&
          cpu_frames[i].guest_pc) {
        first_frame = cpu_frames[i];
        break;
      }
    }
  }

  // Check if we're in guest code or host code.
  uint32_t pc = 0;
  if (first_frame.type == cpu::StackFrame::Type::kGuest) {
    auto& frame = first_frame;
    if (!frame.guest_pc) {
      // Lame. The guest->host thunk is a "guest" function.
      frame = cpu_frames[1];
    }

    pc = frame.guest_pc;

    // We're in guest code.
    // First: Find a synchronizing instruction and go to it.
    cpu::frontend::InstrData i;
    i.address = cpu_frames[0].guest_pc - 4;
    do {
      i.address += 4;
      i.code =
          xe::load_and_swap<uint32_t>(memory()->TranslateVirtual(i.address));
      i.type = cpu::frontend::GetInstrType(i.code);
    } while ((i.type->type & (cpu::frontend::kXEPPCInstrTypeSynchronizeContext |
                              cpu::frontend::kXEPPCInstrTypeBranch)) == 0);

    if (i.address != pc) {
      StepToAddress(i.address);
      pc = i.address;
    }

    // Okay. Now we're on a synchronizing instruction but we need to step
    // past it in order to get a synchronized context.
    // If we're on a branching instruction, it's guaranteed only going to have
    // two possible targets. For non-branching instructions, we can just step
    // over them.
    if (i.type->type & cpu::frontend::kXEPPCInstrTypeBranch) {
      pc = StepIntoBranch(i.address);
    }
  } else {
    // We're in host code. Search backwards til we can get an idea of where
    // we are.
    cpu::GuestFunction* thunk_func = nullptr;
    cpu::Export* export_data = nullptr;
    uint32_t first_pc = 0;
    for (int i = 0; i < count; i++) {
      auto& frame = cpu_frames[i];
      if (frame.type == cpu::StackFrame::Type::kGuest && frame.guest_pc) {
        auto func = frame.guest_symbol.function;
        assert_true(func->is_guest());

        if (!first_pc) {
          first_pc = frame.guest_pc;
        }

        thunk_func = reinterpret_cast<cpu::GuestFunction*>(func);
        export_data = thunk_func->export_data();
        if (export_data) {
          break;
        }
      }
    }

    // If the export is blocking, we wrap up and save inside the export thunk.
    // When we're restored, we'll call the blocking export again.
    // Otherwise, we return from the thunk and save.
    if (export_data && export_data->tags & cpu::ExportTag::kBlocking) {
      pc = thunk_func->address();
    } else if (export_data) {
      // Non-blocking. Run until we return from the thunk.
      pc = uint32_t(thread_state_->context()->lr);
      StepToAddress(pc);
    } else if (first_pc) {
      // We're in the MMIO handler/mfmsr/something calling out of the guest
      // that doesn't use an export. If the current instruction is
      // synchronizing, we can just save here. Otherwise, step forward
      // (and call ourselves again so we run the correct logic).
      cpu::frontend::InstrData i;
      i.address = first_pc;
      i.code =
          xe::load_and_swap<uint32_t>(memory()->TranslateVirtual(first_pc));
      i.type = cpu::frontend::GetInstrType(i.code);
      if (i.type->type & cpu::frontend::kXEPPCInstrTypeSynchronizeContext) {
        // Good to go.
        pc = first_pc;
      } else {
        // Step forward and run this logic again.
        StepToAddress(first_pc + 4);
        return StepToSafePoint(true);
      }
    } else {
      // We've managed to catch a thread before it called into the guest.
      // Set a breakpoint on its startup procedure and capture it there.
      pc = creation_params_.xapi_thread_startup
               ? creation_params_.xapi_thread_startup
               : creation_params_.start_address;
      StepToAddress(pc);
    }
  }

  return pc;
}

struct ThreadSavedState {
  uint32_t thread_id;
  bool is_main_thread;  // Is this the main thread?
  bool is_running;

  // Clock settings (invalid if not running)
  uint64_t tick_count_;
  uint64_t system_time_;

  uint32_t apc_head;
  uint32_t tls_address;
  uint32_t pcr_address;
  uint32_t stack_base;        // High address
  uint32_t stack_limit;       // Low address
  uint32_t stack_alloc_base;  // Allocation address
  uint32_t stack_alloc_size;  // Allocation size

  // Context (invalid if not running)
  struct {
    uint64_t lr;
    uint64_t ctr;
    uint64_t r[32];
    double f[32];
    vec128_t v[128];
    uint32_t cr[8];
    uint32_t fpscr;
    uint8_t xer_ca;
    uint8_t xer_ov;
    uint8_t xer_so;
    uint8_t vscr_sat;
    uint32_t pc;
  } context;
};

bool XThread::Save(ByteStream* stream) {
  if (!guest_thread_) {
    // Host XThreads are expected to be recreated on their own.
    return false;
  }

  XELOGD("XThread %.8X serializing...", handle());

  uint32_t pc = 0;
  if (running_) {
    pc = StepToSafePoint();
    if (!pc) {
      XELOGE("XThread %.8X failed to save: could not step to a safe point!",
             handle());
      assert_always();
      return false;
    }
  }

  if (!SaveObject(stream)) {
    return false;
  }

  stream->Write('THRD');
  stream->Write(name_);

  ThreadSavedState state;
  state.thread_id = thread_id_;
  state.is_main_thread = main_thread_;
  state.is_running = running_;
  state.apc_head = apc_list_.head();
  state.tls_address = tls_address_;
  state.pcr_address = pcr_address_;
  state.stack_base = stack_base_;
  state.stack_limit = stack_limit_;
  state.stack_alloc_base = stack_alloc_base_;
  state.stack_alloc_size = stack_alloc_size_;

  if (running_) {
    state.tick_count_ = Clock::QueryGuestTickCount();
    state.system_time_ =
        Clock::QueryGuestSystemTime() - Clock::guest_system_time_base();

    // Context information
    auto context = thread_state_->context();
    state.context.lr = context->lr;
    state.context.ctr = context->ctr;
    std::memcpy(state.context.r, context->r, 32 * 8);
    std::memcpy(state.context.f, context->f, 32 * 8);
    std::memcpy(state.context.v, context->v, 128 * 16);
    state.context.cr[0] = context->cr0.value;
    state.context.cr[1] = context->cr1.value;
    state.context.cr[2] = context->cr2.value;
    state.context.cr[3] = context->cr3.value;
    state.context.cr[4] = context->cr4.value;
    state.context.cr[5] = context->cr5.value;
    state.context.cr[6] = context->cr6.value;
    state.context.cr[7] = context->cr7.value;
    state.context.fpscr = context->fpscr.value;
    state.context.xer_ca = context->xer_ca;
    state.context.xer_ov = context->xer_ov;
    state.context.xer_so = context->xer_so;
    state.context.vscr_sat = context->vscr_sat;
    state.context.pc = pc;
  }

  stream->Write(&state, sizeof(ThreadSavedState));
  return true;
}

object_ref<XThread> XThread::Restore(KernelState* kernel_state,
                                     ByteStream* stream) {
  // Kind-of a hack, but we need to set the kernel state outside of the object
  // constructor so it doesn't register a handle with the object table.
  auto thread = new XThread(nullptr);
  thread->kernel_state_ = kernel_state;

  if (!thread->RestoreObject(stream)) {
    return false;
  }

  if (stream->Read<uint32_t>() != 'THRD') {
    XELOGE("Could not restore XThread - invalid magic!");
    return false;
  }

  XELOGD("XThread %.8X", thread->handle());

  thread->name_ = stream->Read<std::string>();

  ThreadSavedState state;
  stream->Read(&state, sizeof(ThreadSavedState));
  thread->thread_id_ = state.thread_id;
  thread->main_thread_ = state.is_main_thread;
  thread->apc_list_.set_head(state.apc_head);
  thread->tls_address_ = state.tls_address;
  thread->pcr_address_ = state.pcr_address;
  thread->stack_base_ = state.stack_base;
  thread->stack_limit_ = state.stack_limit;
  thread->stack_alloc_base_ = state.stack_alloc_base;
  thread->stack_alloc_size_ = state.stack_alloc_size;

  thread->apc_list_.set_memory(kernel_state->memory());

  // Register now that we know our thread ID.
  kernel_state->RegisterThread(thread);

  thread->thread_state_ =
      new cpu::ThreadState(kernel_state->processor(), thread->thread_id_,
                           thread->stack_base_, thread->pcr_address_);

  if (state.is_running) {
    auto context = thread->thread_state_->context();
    context->kernel_state = kernel_state;
    context->lr = state.context.lr;
    context->ctr = state.context.ctr;
    std::memcpy(context->r, state.context.r, 32 * 8);
    std::memcpy(context->f, state.context.f, 32 * 8);
    std::memcpy(context->v, state.context.v, 128 * 16);
    context->cr0.value = state.context.cr[0];
    context->cr1.value = state.context.cr[1];
    context->cr2.value = state.context.cr[2];
    context->cr3.value = state.context.cr[3];
    context->cr4.value = state.context.cr[4];
    context->cr5.value = state.context.cr[5];
    context->cr6.value = state.context.cr[6];
    context->cr7.value = state.context.cr[7];
    context->fpscr.value = state.context.fpscr;
    context->xer_ca = state.context.xer_ca;
    context->xer_ov = state.context.xer_ov;
    context->xer_so = state.context.xer_so;
    context->vscr_sat = state.context.vscr_sat;

    // Always retain when starting - the thread owns itself until exited.
    thread->Retain();
    thread->RetainHandle();

    xe::threading::Thread::CreationParameters params;
    params.create_suspended = true;  // Not done restoring yet.
    params.stack_size = 16 * 1024 * 1024;
    thread->thread_ = xe::threading::Thread::Create(params, [thread, state]() {
      // Set thread ID override. This is used by logging.
      xe::threading::set_current_thread_id(thread->handle());

      // Set name immediately, if we have one.
      thread->thread_->set_name(thread->name());

      // Profiler needs to know about the thread.
      xe::Profiler::ThreadEnter(thread->name().c_str());

      // Setup the time now that we're in the thread.
      Clock::SetGuestTickCount(state.tick_count_);
      Clock::SetGuestSystemTime(state.system_time_);

      // Execute user code.
      thread->running_ = true;
      current_thread_tls_ = thread;

      uint32_t pc = state.context.pc;
      thread->kernel_state()->processor()->ExecuteRaw(thread->thread_state_,
                                                      pc);

      current_thread_tls_ = nullptr;

      xe::Profiler::ThreadExit();

      // Release the self-reference to the thread.
      thread->Release();
    });

    if (thread->emulator()->debugger()) {
      thread->emulator()->debugger()->OnThreadCreated(thread);
    }
  }

  return object_ref<XThread>(thread);
}

XHostThread::XHostThread(KernelState* kernel_state, uint32_t stack_size,
                         uint32_t creation_flags, std::function<int()> host_fn)
    : XThread(kernel_state, stack_size, 0, 0, 0, creation_flags, false),
      host_fn_(host_fn) {
  // By default host threads are not debugger suspendable. If the thread runs
  // any guest code this must be overridden.
  can_debugger_suspend_ = false;
}

void XHostThread::Execute() {
  XELOGKERNEL(
      "XThread::Execute thid %d (handle=%.8X, '%s', native=%.8X, <host>)",
      thread_id_, handle(), name_.c_str(), thread_->system_id());

  // Let the kernel know we are starting.
  kernel_state()->OnThreadExecute(this);

  int ret = host_fn_();

  // Exit.
  Exit(ret);
}

}  // namespace kernel
}  // namespace xe
