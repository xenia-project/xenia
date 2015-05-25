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

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/cpu.h"
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

using namespace xe::cpu;

uint32_t next_xthread_id = 0;
thread_local XThread* current_thread_tls;
std::mutex critical_region_;
XThread* shared_kernel_thread_ = 0;

XThread::XThread(KernelState* kernel_state, uint32_t stack_size,
                 uint32_t xapi_thread_startup, uint32_t start_address,
                 uint32_t start_context, uint32_t creation_flags)
    : XObject(kernel_state, kTypeThread),
      thread_id_(++next_xthread_id),
      thread_handle_(0),
      pcr_address_(0),
      thread_state_address_(0),
      thread_state_(0),
      irql_(0) {
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

  event_ = object_ref<XEvent>(new XEvent(kernel_state));
  event_->Initialize(true, false);

  // The kernel does not take a reference. We must unregister in the dtor.
  kernel_state_->RegisterThread(this);
}

XThread::~XThread() {
  // Unregister first to prevent lookups while deleting.
  kernel_state_->UnregisterThread(this);

  delete apc_list_;

  event_.reset();

  PlatformDestroy();

  if (thread_state_) {
    delete thread_state_;
  }
  kernel_state()->memory()->SystemHeapFree(scratch_address_);
  kernel_state()->memory()->SystemHeapFree(tls_address_);
  kernel_state()->memory()->SystemHeapFree(pcr_address_);

  if (thread_handle_) {
    // TODO(benvanik): platform kill
    XELOGE("Thread disposed without exiting");
  }
}

XThread* XThread::GetCurrentThread() {
  XThread* thread = current_thread_tls;
  if (!thread) {
    // Assume this is some shared interrupt thread/etc.
    XThread::EnterCriticalRegion();
    thread = shared_kernel_thread_;
    if (!thread) {
      thread = new XThread(KernelState::shared(), 256 * 1024, 0, 0, 0, 0);
      shared_kernel_thread_ = thread;
      current_thread_tls = thread;
    }
    XThread::LeaveCriticalRegion();
  }
  return thread;
}

uint32_t XThread::GetCurrentThreadHandle() {
  XThread* thread = XThread::GetCurrentThread();
  return thread->handle();
}

uint32_t XThread::GetCurrentThreadId(const uint8_t* pcr) {
  return xe::load_and_swap<uint32_t>(pcr + 0x2D8 + 0x14C);
}

uint32_t XThread::last_error() {
  uint8_t* p = memory()->TranslateVirtual(thread_state_address_);
  return xe::load_and_swap<uint32_t>(p + 0x160);
}

void XThread::set_last_error(uint32_t error_code) {
  uint8_t* p = memory()->TranslateVirtual(thread_state_address_);
  xe::store_and_swap<uint32_t>(p + 0x160, error_code);
}

void XThread::set_name(const std::string& name) {
  name_ = name;
  xe::threading::set_name(thread_handle_, name);
}

X_STATUS XThread::Create() {
  // Allocate thread state block from heap.
  // This is set as r13 for user code and some special inlined Win32 calls
  // (like GetLastError/etc) will poke it directly.
  // We try to use it as our primary store of data just to keep things all
  // consistent.
  // 0x000: pointer to tls data
  // 0x100: pointer to self?
  // 0x14C: thread id
  // 0x150: if >0 then error states don't get set
  // 0x160: last error
  // So, at offset 0x100 we have a 4b pointer to offset 200, then have the
  // structure.
  pcr_address_ = memory()->SystemHeapAlloc(0x2D8 + 0xAB0);
  thread_state_address_ = pcr_address_ + 0x2D8;
  if (!thread_state_address_) {
    XELOGW("Unable to allocate thread state block");
    return X_STATUS_NO_MEMORY;
  }

  // Set native info.
  SetNativePointer(thread_state_address_, true);

  auto module = kernel_state()->GetExecutableModule();

  // Allocate thread scratch.
  // This is used by interrupts/APCs/etc so we can round-trip pointers through.
  scratch_size_ = 4 * 16;
  scratch_address_ = memory()->SystemHeapAlloc(scratch_size_);

  // Allocate TLS block.
  uint32_t tls_size = 32;  // Default 32 (is this OK?)
  if (module && module->xex_header()) {
    const xe_xex2_header_t* header = module->xex_header();
    tls_size = header->tls_info.slot_count * header->tls_info.data_size;
  }

  tls_address_ = memory()->SystemHeapAlloc(tls_size);
  if (!tls_address_) {
    XELOGW("Unable to allocate thread local storage block");
    return X_STATUS_NO_MEMORY;
  }

  // Copy in default TLS info (or zero it out)
  if (module && module->xex_header()) {
    const xe_xex2_header_t* header = module->xex_header();

    // Copy in default TLS info.
    // TODO(benvanik): is this correct?
    memory()->Copy(tls_address_, header->tls_info.raw_data_address, tls_size);
  } else {
    memory()->Fill(tls_address_, tls_size, 0);
  }

  // Allocate processor thread state.
  // This is thread safe.
  thread_state_ = new ThreadState(kernel_state()->processor(), thread_id_,
                                  ThreadStackType::kUserStack, 0,
                                  creation_params_.stack_size, pcr_address_);
  XELOGI("XThread%04X (%X) Stack: %.8X-%.8X", handle(),
         thread_state_->thread_id(), thread_state_->stack_limit(),
         thread_state_->stack_base());

  uint8_t* pcr = memory()->TranslateVirtual(pcr_address_);
  std::memset(pcr, 0x0, 0x2D8 + 0xAB0);  // Zero the PCR
  xe::store_and_swap<uint32_t>(pcr + 0x000, tls_address_);
  xe::store_and_swap<uint32_t>(pcr + 0x030, pcr_address_);
  xe::store_and_swap<uint32_t>(pcr + 0x070, thread_state_->stack_address() +
                                                thread_state_->stack_size());
  xe::store_and_swap<uint32_t>(pcr + 0x074, thread_state_->stack_address());
  xe::store_and_swap<uint32_t>(pcr + 0x100, thread_state_address_);
  xe::store_and_swap<uint8_t>(pcr + 0x10C, 1);   // Current CPU(?)
  xe::store_and_swap<uint32_t>(pcr + 0x150, 0);  // DPC active bool?

  // Setup the thread state block (last error/etc).
  uint8_t* p = memory()->TranslateVirtual(thread_state_address_);
  xe::store_and_swap<uint32_t>(p + 0x000, 6);
  xe::store_and_swap<uint32_t>(p + 0x008, thread_state_address_ + 0x008);
  xe::store_and_swap<uint32_t>(p + 0x00C, thread_state_address_ + 0x008);
  xe::store_and_swap<uint32_t>(p + 0x010, thread_state_address_ + 0x010);
  xe::store_and_swap<uint32_t>(p + 0x014, thread_state_address_ + 0x010);
  xe::store_and_swap<uint32_t>(p + 0x040, thread_state_address_ + 0x018 + 8);
  xe::store_and_swap<uint32_t>(p + 0x044, thread_state_address_ + 0x018 + 8);
  xe::store_and_swap<uint32_t>(p + 0x048, thread_state_address_);
  xe::store_and_swap<uint32_t>(p + 0x04C, thread_state_address_ + 0x018);
  xe::store_and_swap<uint16_t>(p + 0x054, 0x102);
  xe::store_and_swap<uint16_t>(p + 0x056, 1);
  xe::store_and_swap<uint32_t>(
      p + 0x05C, thread_state_->stack_address() + thread_state_->stack_size());
  xe::store_and_swap<uint32_t>(p + 0x060, thread_state_->stack_address());
  xe::store_and_swap<uint32_t>(p + 0x068, tls_address_);
  xe::store_and_swap<uint8_t>(p + 0x06C, 0);
  xe::store_and_swap<uint32_t>(p + 0x074, thread_state_address_ + 0x074);
  xe::store_and_swap<uint32_t>(p + 0x078, thread_state_address_ + 0x074);
  xe::store_and_swap<uint32_t>(p + 0x07C, thread_state_address_ + 0x07C);
  xe::store_and_swap<uint32_t>(p + 0x080, thread_state_address_ + 0x07C);
  xe::store_and_swap<uint8_t>(p + 0x08B, 1);
  // D4 = APC
  // FC = semaphore (ptr, 0, 2)
  // A88 = APC
  // 18 = timer
  xe::store_and_swap<uint32_t>(p + 0x09C, 0xFDFFD7FF);
  xe::store_and_swap<uint32_t>(
      p + 0x0D0, thread_state_->stack_address() + thread_state_->stack_size());
  FILETIME t;
  GetSystemTimeAsFileTime(&t);
  xe::store_and_swap<uint64_t>(
      p + 0x130, ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime);
  xe::store_and_swap<uint32_t>(p + 0x144, thread_state_address_ + 0x144);
  xe::store_and_swap<uint32_t>(p + 0x148, thread_state_address_ + 0x144);
  xe::store_and_swap<uint32_t>(p + 0x14C, thread_id_);
  xe::store_and_swap<uint32_t>(p + 0x150, creation_params_.start_address);
  xe::store_and_swap<uint32_t>(p + 0x154, thread_state_address_ + 0x154);
  xe::store_and_swap<uint32_t>(p + 0x158, thread_state_address_ + 0x154);
  xe::store_and_swap<uint32_t>(p + 0x160, 0);  // last error
  xe::store_and_swap<uint32_t>(p + 0x16C, creation_params_.creation_flags);
  xe::store_and_swap<uint32_t>(p + 0x17C, 1);

  SetNativePointer(thread_state_address_);

  X_STATUS return_code = PlatformCreate();
  if (XFAILED(return_code)) {
    XELOGW("Unable to create platform thread (%.8X)", return_code);
    return return_code;
  }

  char thread_name[32];
  snprintf(thread_name, xe::countof(thread_name), "XThread%04X", handle());
  set_name(thread_name);

  uint32_t proc_mask = creation_params_.creation_flags >> 24;
  if (proc_mask) {
    SetAffinity(proc_mask);
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XThread::Exit(int exit_code) {
  // TODO(benvanik): set exit code in thread state block

  // TODO(benvanik); dispatch events? waiters? etc?
  event_->Set(0, false);
  RundownAPCs();

  // NOTE: unless PlatformExit fails, expect it to never return!
  X_STATUS return_code = PlatformExit(exit_code);
  if (XFAILED(return_code)) {
    return return_code;
  }
  return X_STATUS_SUCCESS;
}

#if XE_PLATFORM_WIN32

static uint32_t __stdcall XThreadStartCallbackWin32(void* param) {
  auto thread = object_ref<XThread>(reinterpret_cast<XThread*>(param));
  thread->set_name(thread->name());
  xe::Profiler::ThreadEnter(thread->name().c_str());
  current_thread_tls = thread.get();
  thread->Execute();
  current_thread_tls = nullptr;
  xe::Profiler::ThreadExit();
  return 0;
}

X_STATUS XThread::PlatformCreate() {
  bool suspended = creation_params_.creation_flags & 0x1;
  thread_handle_ =
      CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)XThreadStartCallbackWin32,
                   this, suspended ? CREATE_SUSPENDED : 0, NULL);
  if (!thread_handle_) {
    uint32_t last_error = GetLastError();
    // TODO(benvanik): translate?
    XELOGE("CreateThread failed with %d", last_error);
    return last_error;
  }

  if (creation_params_.creation_flags & 0x60) {
    SetThreadPriority(thread_handle_,
                      creation_params_.creation_flags & 0x20 ? 1 : 0);
  }

  return X_STATUS_SUCCESS;
}

void XThread::PlatformDestroy() {
  CloseHandle(reinterpret_cast<HANDLE>(thread_handle_));
  thread_handle_ = NULL;
}

X_STATUS XThread::PlatformExit(int exit_code) {
  // NOTE: does not return.
  ExitThread(exit_code);
  return X_STATUS_SUCCESS;
}

#else

static void* XThreadStartCallbackPthreads(void* param) {
  auto thread = object_ref<XThread>(reinterpret_cast<XThread*>(param));
  xe::Profiler::ThreadEnter(thread->name().c_str());
  current_thread_tls = thread.get();
  thread->Execute();
  current_thread_tls = nullptr;
  xe::Profiler::ThreadExit();
  return 0;
}

X_STATUS XThread::PlatformCreate() {
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  // TODO(benvanik): this shouldn't be necessary
  // pthread_attr_setstacksize(&attr, creation_params_.stack_size);

  int result_code;
  if (creation_params_.creation_flags & 0x1) {
#if XE_PLATFORM_MAC
    result_code = pthread_create_suspended_np(
        reinterpret_cast<pthread_t*>(&thread_handle_), &attr,
        &XThreadStartCallbackPthreads, this);
#else
    // TODO(benvanik): pthread_create_suspended_np on linux
    assert_always();
#endif  // OSX
  } else {
    result_code = pthread_create(reinterpret_cast<pthread_t*>(&thread_handle_),
                                 &attr, &XThreadStartCallbackPthreads, this);
  }

  pthread_attr_destroy(&attr);

  switch (result_code) {
    case 0:
      // Succeeded!
      return X_STATUS_SUCCESS;
    default:
    case EAGAIN:
      return X_STATUS_NO_MEMORY;
    case EINVAL:
    case EPERM:
      return X_STATUS_INVALID_PARAMETER;
  }
}

void XThread::PlatformDestroy() {
  // No-op?
}

X_STATUS XThread::PlatformExit(int exit_code) {
  // NOTE: does not return.
  pthread_exit(reinterpret_cast<void*>(exit_code));
  return X_STATUS_SUCCESS;
}

#endif  // WIN32

void XThread::Execute() {
  XELOGKERNEL("XThread::Execute thid %d (handle=%.8X, '%s', native=%.8X)",
              thread_id_, handle(), name_.c_str(),
              xe::threading::current_thread_id());

  // Let the kernel know we are starting.
  kernel_state()->OnThreadExecute(this);

  // All threads get a mandatory sleep. This is to deal with some buggy
  // games that are assuming the 360 is so slow to create threads that they
  // have time to initialize shared structures AFTER CreateThread (RR).
  xe::threading::Sleep(std::chrono::milliseconds::duration(100));

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
    int exit_code = (int)kernel_state()->processor()->Execute(
        thread_state_, creation_params_.start_address, args, xe::countof(args));
    // If we got here it means the execute completed without an exit being
    // called.
    // Treat the return code as an implicit exit code.
    Exit(exit_code);
  }

  // Let the kernel know we are exiting.
  kernel_state()->OnThreadExit(this);
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

void XThread::LockApc() { apc_lock_.lock(); }

void XThread::UnlockApc() {
  bool needs_apc = apc_list_->HasPending();
  apc_lock_.unlock();
  if (needs_apc) {
    QueueUserAPC(reinterpret_cast<PAPCFUNC>(DeliverAPCs), thread_handle_,
                 reinterpret_cast<ULONG_PTR>(this));
  }
}

void XThread::DeliverAPCs(void* data) {
  // http://www.drdobbs.com/inside-nts-asynchronous-procedure-call/184416590?pgno=1
  // http://www.drdobbs.com/inside-nts-asynchronous-procedure-call/184416590?pgno=7
  XThread* thread = reinterpret_cast<XThread*>(data);
  auto memory = thread->memory();
  auto processor = thread->kernel_state()->processor();
  auto apc_list = thread->apc_list();
  thread->LockApc();
  while (apc_list->HasPending()) {
    // Get APC entry (offset for LIST_ENTRY offset) and cache what we need.
    // Calling the routine may delete the memory/overwrite it.
    uint32_t apc_address = apc_list->Shift() - 8;
    uint8_t* apc_ptr = memory->TranslateVirtual(apc_address);
    uint32_t kernel_routine = xe::load_and_swap<uint32_t>(apc_ptr + 16);
    uint32_t normal_routine = xe::load_and_swap<uint32_t>(apc_ptr + 24);
    uint32_t normal_context = xe::load_and_swap<uint32_t>(apc_ptr + 28);
    uint32_t system_arg1 = xe::load_and_swap<uint32_t>(apc_ptr + 32);
    uint32_t system_arg2 = xe::load_and_swap<uint32_t>(apc_ptr + 36);

    // Mark as uninserted so that it can be reinserted again by the routine.
    uint32_t old_flags = xe::load_and_swap<uint32_t>(apc_ptr + 40);
    xe::store_and_swap<uint32_t>(apc_ptr + 40, old_flags & ~0xFF00);

    // Call kernel routine.
    // The routine can modify all of its arguments before passing it on.
    // Since we need to give guest accessible pointers over, we copy things
    // into and out of scratch.
    uint8_t* scratch_ptr = memory->TranslateVirtual(thread->scratch_address_);
    xe::store_and_swap<uint32_t>(scratch_ptr + 0, normal_routine);
    xe::store_and_swap<uint32_t>(scratch_ptr + 4, normal_context);
    xe::store_and_swap<uint32_t>(scratch_ptr + 8, system_arg1);
    xe::store_and_swap<uint32_t>(scratch_ptr + 12, system_arg2);
    // kernel_routine(apc_address, &normal_routine, &normal_context,
    // &system_arg1, &system_arg2)
    uint64_t kernel_args[] = {
        apc_address, thread->scratch_address_ + 0, thread->scratch_address_ + 4,
        thread->scratch_address_ + 8, thread->scratch_address_ + 12,
    };
    processor->ExecuteInterrupt(0, kernel_routine, kernel_args,
                                xe::countof(kernel_args));
    normal_routine = xe::load_and_swap<uint32_t>(scratch_ptr + 0);
    normal_context = xe::load_and_swap<uint32_t>(scratch_ptr + 4);
    system_arg1 = xe::load_and_swap<uint32_t>(scratch_ptr + 8);
    system_arg2 = xe::load_and_swap<uint32_t>(scratch_ptr + 12);

    // Call the normal routine. Note that it may have been killed by the kernel
    // routine.
    if (normal_routine) {
      thread->UnlockApc();
      // normal_routine(normal_context, system_arg1, system_arg2)
      uint64_t normal_args[] = {normal_context, system_arg1, system_arg2};
      processor->ExecuteInterrupt(0, normal_routine, normal_args,
                                  xe::countof(normal_args));
      thread->LockApc();
    }
  }
  thread->UnlockApc();
}

void XThread::RundownAPCs() {
  LockApc();
  while (apc_list_->HasPending()) {
    // Get APC entry (offset for LIST_ENTRY offset) and cache what we need.
    // Calling the routine may delete the memory/overwrite it.
    uint32_t apc_address = apc_list_->Shift() - 8;
    uint8_t* apc_ptr = memory()->TranslateVirtual(apc_address);
    uint32_t rundown_routine = xe::load_and_swap<uint32_t>(apc_ptr + 20);

    // Mark as uninserted so that it can be reinserted again by the routine.
    uint32_t old_flags = xe::load_and_swap<uint32_t>(apc_ptr + 40);
    xe::store_and_swap<uint32_t>(apc_ptr + 40, old_flags & ~0xFF00);

    // Call the rundown routine.
    if (rundown_routine) {
      // rundown_routine(apc)
      uint64_t args[] = {apc_address};
      kernel_state()->processor()->ExecuteInterrupt(0, rundown_routine, args,
                                                    xe::countof(args));
    }
  }
  UnlockApc();
}

int32_t XThread::QueryPriority() { return GetThreadPriority(thread_handle_); }

void XThread::SetPriority(int32_t increment) {
  int target_priority = 0;
  if (increment > 0x22) {
    target_priority = THREAD_PRIORITY_HIGHEST;
  } else if (increment > 0x11) {
    target_priority = THREAD_PRIORITY_ABOVE_NORMAL;
  } else if (increment < -0x22) {
    target_priority = THREAD_PRIORITY_IDLE;
  } else if (increment < -0x11) {
    target_priority = THREAD_PRIORITY_LOWEST;
  } else {
    target_priority = THREAD_PRIORITY_NORMAL;
  }
  if (!FLAGS_ignore_thread_priorities) {
    SetThreadPriority(thread_handle_, target_priority);
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
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  if (system_info.dwNumberOfProcessors < 6) {
    XELOGW("Too few processors - scheduling will be wonky");
  }
  if (!FLAGS_ignore_thread_affinities) {
    SetThreadAffinityMask(reinterpret_cast<HANDLE>(thread_handle_), affinity);
  }
}

X_STATUS XThread::Resume(uint32_t* out_suspend_count) {
  DWORD result = ResumeThread(thread_handle_);
  if (result >= 0) {
    if (out_suspend_count) {
      *out_suspend_count = result;
    }
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

X_STATUS XThread::Suspend(uint32_t* out_suspend_count) {
  DWORD result = SuspendThread(thread_handle_);
  if (result >= 0) {
    if (out_suspend_count) {
      *out_suspend_count = result;
    }
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

X_STATUS XThread::Delay(uint32_t processor_mode, uint32_t alertable,
                        uint64_t interval) {
  int64_t timeout_ticks = interval;
  DWORD timeout_ms;
  if (timeout_ticks > 0) {
    // Absolute time, based on January 1, 1601.
    // TODO(benvanik): convert time to relative time.
    assert_always();
    timeout_ms = 0;
  } else if (timeout_ticks < 0) {
    // Relative time.
    timeout_ms = (DWORD)(-timeout_ticks / 10000);  // Ticks -> MS
  } else {
    timeout_ms = 0;
  }
  DWORD result = SleepEx(timeout_ms, alertable ? TRUE : FALSE);
  switch (result) {
    case 0:
      return X_STATUS_SUCCESS;
    case WAIT_IO_COMPLETION:
      return X_STATUS_USER_APC;
    default:
      return X_STATUS_ALERTED;
  }
}

void* XThread::GetWaitHandle() { return event_->GetWaitHandle(); }

XHostThread::XHostThread(KernelState* kernel_state, uint32_t stack_size,
                         uint32_t creation_flags, std::function<int()> host_fn)
    : XThread(kernel_state, stack_size, 0, 0, 0, creation_flags),
      host_fn_(host_fn) {}

void XHostThread::Execute() {
  XELOGKERNEL(
      "XThread::Execute thid %d (handle=%.8X, '%s', native=%.8X, <host>)",
      thread_id_, handle(), name_.c_str(), xe::threading::current_thread_id());

  // Let the kernel know we are starting.
  kernel_state()->OnThreadExecute(this);

  int ret = host_fn_();

  // Let the kernel know we are exiting.
  kernel_state()->OnThreadExit(this);

  // Exit.
  Exit(ret);
}

}  // namespace kernel
}  // namespace xe
