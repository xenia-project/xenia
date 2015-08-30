/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/objects/xthread.h"

#include <gflags/gflags.h>

#include <cstring>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/native_list.h"
#include "xenia/kernel/objects/xevent.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/profiling.h"

DEFINE_bool(ignore_thread_priorities, true,
            "Ignores game-specified thread priorities.");
DEFINE_bool(ignore_thread_affinities, true,
            "Ignores game-specified thread affinities.");

namespace xe {
namespace kernel {

uint32_t next_xthread_id_ = 0;
thread_local XThread* current_thread_tls_ = nullptr;
xe::mutex critical_region_;

XThread::XThread(KernelState* kernel_state, uint32_t stack_size,
                 uint32_t xapi_thread_startup, uint32_t start_address,
                 uint32_t start_context, uint32_t creation_flags,
                 bool guest_thread)
    : XObject(kernel_state, kTypeThread),
      thread_id_(++next_xthread_id_),
      guest_thread_(guest_thread) {
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

  apc_list_ = new NativeList(kernel_state->memory());

  // The kernel does not take a reference. We must unregister in the dtor.
  kernel_state_->RegisterThread(this);
}

XThread::~XThread() {
  // Unregister first to prevent lookups while deleting.
  kernel_state_->UnregisterThread(this);

  if (emulator()->debugger()) {
    emulator()->debugger()->OnThreadDestroyed(this);
  }

  delete apc_list_;

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

uint32_t XThread::last_error() { return guest_object<X_KTHREAD>()->last_error; }

void XThread::set_last_error(uint32_t error_code) {
  guest_object<X_KTHREAD>()->last_error = error_code;
}

void XThread::set_name(const std::string& name) {
  name_ = name;
  if (thread_) {
    // May be getting set before the thread is created.
    // One the thread is ready it will handle it.
    thread_->set_name(name);
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

X_STATUS XThread::Create() {
  // Thread kernel object
  // This call will also setup the native pointer for us.
  auto guest_thread = CreateNative<X_KTHREAD>();
  if (!guest_thread) {
    XELOGW("Unable to allocate thread object");
    return X_STATUS_NO_MEMORY;
  }

  auto module = kernel_state()->GetExecutableModule();

  // Allocate thread scratch.
  // This is used by interrupts/APCs/etc so we can round-trip pointers through.
  scratch_size_ = 4 * 16;
  scratch_address_ = memory()->SystemHeapAlloc(scratch_size_);

  // Allocate TLS block.
  // Games will specify a certain number of 4b slots that each thread will get.
  xex2_opt_tls_info* tls_header = nullptr;
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
  thread_state_ = new cpu::ThreadState(
      kernel_state()->processor(), thread_id_, cpu::ThreadStackType::kUserStack,
      0, creation_params_.stack_size, pcr_address_);
  XELOGI("XThread%08X (%X) Stack: %.8X-%.8X", handle(),
         thread_state_->thread_id(), thread_state_->stack_limit(),
         thread_state_->stack_base());

  // Exports use this to get the kernel.
  thread_state_->context()->kernel_state = kernel_state_;

  uint8_t proc_mask =
      static_cast<uint8_t>(creation_params_.creation_flags >> 24);

  X_KPCR* pcr = memory()->TranslateVirtual<X_KPCR*>(pcr_address_);

  pcr->tls_ptr = tls_address_;
  pcr->pcr_ptr = pcr_address_;
  pcr->current_thread = guest_object();

  pcr->stack_base_ptr =
      thread_state_->stack_address() + thread_state_->stack_size();
  pcr->stack_end_ptr = thread_state_->stack_address();

  pcr->current_cpu = GetFakeCpuNumber(proc_mask);  // Current CPU(?)
  pcr->dpc_active = 0;                             // DPC active bool?

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
  xe::store_and_swap<uint32_t>(
      p + 0x05C, thread_state_->stack_address() + thread_state_->stack_size());
  xe::store_and_swap<uint32_t>(p + 0x060, thread_state_->stack_address());
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
  xe::store_and_swap<uint32_t>(
      p + 0x0D0, thread_state_->stack_address() + thread_state_->stack_size());
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

  // Always retain when starting - the thread owns itself until exited.
  Retain();
  RetainHandle();

  xe::threading::Thread::CreationParameters params;
  params.stack_size = 16 * 1024 * 1024;  // Ignore game, always big!
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
    snprintf(thread_name, xe::countof(thread_name), "XThread%04X (%04X)",
             handle(), thread_->system_id());
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
  // Global critical region. This isn't right, but is easy.
  critical_region_.lock();
}

void XThread::LeaveCriticalRegion() { critical_region_.unlock(); }

uint32_t XThread::RaiseIrql(uint32_t new_irql) {
  return irql_.exchange(new_irql);
}

void XThread::LowerIrql(uint32_t new_irql) { irql_ = new_irql; }

void XThread::CheckApcs() { DeliverAPCs(); }

void XThread::LockApc() { apc_lock_.lock(); }

void XThread::UnlockApc(bool queue_delivery) {
  bool needs_apc = apc_list_->HasPending();
  apc_lock_.unlock();
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
  apc_list_->Insert(list_entry_ptr);

  UnlockApc(true);
}

void XThread::DeliverAPCs() {
  // http://www.drdobbs.com/inside-nts-asynchronous-procedure-call/184416590?pgno=1
  // http://www.drdobbs.com/inside-nts-asynchronous-procedure-call/184416590?pgno=7
  auto processor = kernel_state()->processor();
  LockApc();
  while (apc_list_->HasPending()) {
    // Get APC entry (offset for LIST_ENTRY offset) and cache what we need.
    // Calling the routine may delete the memory/overwrite it.
    uint32_t apc_ptr = apc_list_->Shift() - 8;
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
  while (apc_list_->HasPending()) {
    // Get APC entry (offset for LIST_ENTRY offset) and cache what we need.
    // Calling the routine may delete the memory/overwrite it.
    uint32_t apc_ptr = apc_list_->Shift() - 8;
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

X_STATUS XThread::Resume(uint32_t* out_suspend_count) {
  --guest_object<X_KTHREAD>()->suspend_count;

  if (thread_->Resume(out_suspend_count)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

X_STATUS XThread::Suspend(uint32_t* out_suspend_count) {
  ++guest_object<X_KTHREAD>()->suspend_count;

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

XHostThread::XHostThread(KernelState* kernel_state, uint32_t stack_size,
                         uint32_t creation_flags, std::function<int()> host_fn)
    : XThread(kernel_state, stack_size, 0, 0, 0, creation_flags, false),
      host_fn_(host_fn) {}

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
