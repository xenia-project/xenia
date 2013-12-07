/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/objects/xthread.h>

#include <xenia/cpu/cpu.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_threading.h>
#include <xenia/kernel/xboxkrnl/objects/xevent.h>
#include <xenia/kernel/xboxkrnl/objects/xmodule.h>


using namespace alloy;
using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {
  static uint32_t next_xthread_id = 0;
  static uint32_t current_thread_tls = xeKeTlsAlloc();
  static xe_mutex_t* critical_region_ = xe_mutex_alloc(10000);
  static XThread* shared_kernel_thread_ = 0;
}


XThread::XThread(KernelState* kernel_state,
                 uint32_t stack_size,
                 uint32_t xapi_thread_startup,
                 uint32_t start_address, uint32_t start_context,
                 uint32_t creation_flags) :
    XObject(kernel_state, kTypeThread),
    thread_id_(++next_xthread_id),
    thread_handle_(0),
    thread_state_address_(0),
    thread_state_(0),
    event_(NULL),
    irql_(0) {
  creation_params_.stack_size           = stack_size;
  creation_params_.xapi_thread_startup  = xapi_thread_startup;
  creation_params_.start_address        = start_address;
  creation_params_.start_context        = start_context;
  creation_params_.creation_flags       = creation_flags;

  // Adjust stack size - min of 16k.
  if (creation_params_.stack_size < 16 * 1024 * 1024) {
    creation_params_.stack_size = 16 * 1024 * 1024;
  }

  event_ = new XEvent(kernel_state);
  event_->Initialize(true, false);
}

XThread::~XThread() {
  event_->Release();

  PlatformDestroy();

  if (thread_state_) {
    delete thread_state_;
  }
  if (tls_address_) {
    kernel_state()->memory()->HeapFree(tls_address_, 0);
  }
  if (thread_state_address_) {
    kernel_state()->memory()->HeapFree(thread_state_address_, 0);
  }

  if (thread_handle_) {
    // TODO(benvanik): platform kill
    XELOGE("Thread disposed without exiting");
  }
}

XThread* XThread::GetCurrentThread() {
  XThread* thread = (XThread*)xeKeTlsGetValue(current_thread_tls);
  if (!thread) {
    // Assume this is some shared interrupt thread/etc.
    XThread::EnterCriticalRegion();
    thread = shared_kernel_thread_;
    if (!thread) {
      thread = new XThread(
          KernelState::shared(), 32 * 1024, 0, 0, 0, 0);
      shared_kernel_thread_ = thread;
      xeKeTlsSetValue(current_thread_tls, (uint64_t)thread);
    }
    XThread::LeaveCriticalRegion();
  }
  return thread;
}

uint32_t XThread::GetCurrentThreadHandle() {
  XThread* thread = XThread::GetCurrentThread();
  return thread->handle();
}

uint32_t XThread::GetCurrentThreadId(const uint8_t* thread_state_block) {
  return XEGETUINT32BE(thread_state_block + 0x14C);
}

uint32_t XThread::thread_state() {
  return thread_state_address_;
}

uint32_t XThread::thread_id() {
  return thread_id_;
}

uint32_t XThread::last_error() {
  uint8_t *p = memory()->Translate(thread_state_address_);
  return XEGETUINT32BE(p + 0x160);
}

void XThread::set_last_error(uint32_t error_code) {
  uint8_t *p = memory()->Translate(thread_state_address_);
  XESETUINT32BE(p + 0x160, error_code);
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
  thread_state_address_ = (uint32_t)memory()->HeapAlloc(
      0, 2048, MEMORY_FLAG_ZERO);
  if (!thread_state_address_) {
    XELOGW("Unable to allocate thread state block");
    return X_STATUS_NO_MEMORY;
  }

  // Set native info.
  SetNativePointer(thread_state_address_);

  XModule* module = kernel_state()->GetExecutableModule();

  // Allocate TLS block.
  const xe_xex2_header_t* header = module->xex_header();
  uint32_t tls_size = header->tls_info.slot_count * header->tls_info.data_size;
  tls_address_ = (uint32_t)memory()->HeapAlloc(
      0, tls_size, MEMORY_FLAG_ZERO);
  if (!tls_address_) {
    XELOGW("Unable to allocate thread local storage block");
    module->Release();
    return X_STATUS_NO_MEMORY;
  }

  // Copy in default TLS info.
  // TODO(benvanik): is this correct?
  memory()->Copy(
      tls_address_, header->tls_info.raw_data_address, tls_size);

  // Setup the thread state block (last error/etc).
  uint8_t *p = memory()->Translate(thread_state_address_);
  XESETUINT32BE(p + 0x000, tls_address_);
  XESETUINT32BE(p + 0x100, thread_state_address_);
  XESETUINT32BE(p + 0x14C, thread_id_);
  XESETUINT32BE(p + 0x150, 0); // ?
  XESETUINT32BE(p + 0x160, 0); // last error

  // Allocate processor thread state.
  // This is thread safe.
  thread_state_ = new XenonThreadState(
      kernel_state()->processor()->runtime(),
      thread_id_, creation_params_.stack_size, thread_state_address_);

  X_STATUS return_code = PlatformCreate();
  if (XFAILED(return_code)) {
    XELOGW("Unable to create platform thread (%.8X)", return_code);
    module->Release();
    return return_code;
  }

  module->Release();
  return X_STATUS_SUCCESS;
}

X_STATUS XThread::Exit(int exit_code) {
  // TODO(benvanik): set exit code in thread state block

  // TODO(benvanik); dispatch events? waiters? etc?
  event_->Set(0, false);

  // NOTE: unless PlatformExit fails, expect it to never return!
  X_STATUS return_code = PlatformExit(exit_code);
  if (XFAILED(return_code)) {
    return return_code;
  }
  return X_STATUS_SUCCESS;
}

#if XE_PLATFORM(WIN32)

static uint32_t __stdcall XThreadStartCallbackWin32(void* param) {
  XThread* thread = reinterpret_cast<XThread*>(param);
  xeKeTlsSetValue(current_thread_tls, (uint64_t)thread);
  thread->Execute();
  xeKeTlsSetValue(current_thread_tls, NULL);
  thread->Release();
  return 0;
}

X_STATUS XThread::PlatformCreate() {
  thread_handle_ = CreateThread(
      NULL,
      creation_params_.stack_size,
      (LPTHREAD_START_ROUTINE)XThreadStartCallbackWin32,
      this,
      creation_params_.creation_flags,
      NULL);
  if (!thread_handle_) {
    uint32_t last_error = GetLastError();
    // TODO(benvanik): translate?
    XELOGE("CreateThread failed with %d", last_error);
    return last_error;
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
  XThread* thread = reinterpret_cast<XThread*>(param);
  xeKeTlsSetValue(current_thread_tls, (uint64_t)thread);
  thread->Execute();
  xeKeTlsSetValue(current_thread_tls, NULL);
  thread->Release();
  return 0;
}

X_STATUS XThread::PlatformCreate() {
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, creation_params_.stack_size);

  int result_code;
  if (creation_params_.creation_flags & X_CREATE_SUSPENDED) {
#if XE_PLATFORM(OSX)
    result_code = pthread_create_suspended_np(
        reinterpret_cast<pthread_t*>(&thread_handle_),
        &attr,
        &XThreadStartCallbackPthreads,
        this);
#else
    // TODO(benvanik): pthread_create_suspended_np on linux
    XEASSERTALWAYS();
#endif  // OSX
  } else {
    result_code = pthread_create(
        reinterpret_cast<pthread_t*>(&thread_handle_),
        &attr,
        &XThreadStartCallbackPthreads,
        this);
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
  pthread_exit((void*)exit_code);
  return X_STATUS_SUCCESS;
}

#endif  // WIN32

void XThread::Execute() {
  // If a XapiThreadStartup value is present, we use that as a trampoline.
  // Otherwise, we are a raw thread.
  if (creation_params_.xapi_thread_startup) {
    kernel_state()->processor()->Execute(
        thread_state_,
        creation_params_.xapi_thread_startup,
        creation_params_.start_address, creation_params_.start_context);
  } else {
    // Run user code.
    int exit_code = (int)kernel_state()->processor()->Execute(
        thread_state_,
        creation_params_.start_address, creation_params_.start_context);
    // If we got here it means the execute completed without an exit being called.
    // Treat the return code as an implicit exit code.
    Exit(exit_code);
  }
}

X_STATUS XThread::Wait(uint32_t wait_reason, uint32_t processor_mode,
                       uint32_t alertable, uint64_t* opt_timeout) {
  return event_->Wait(wait_reason, processor_mode, alertable, opt_timeout);
}

void XThread::EnterCriticalRegion() {
  // Global critical region. This isn't right, but is easy.
  xe_mutex_lock(critical_region_);
}

void XThread::LeaveCriticalRegion() {
  xe_mutex_unlock(critical_region_);
}

uint32_t XThread::RaiseIrql(uint32_t new_irql) {
  return xe_atomic_exchange_32(new_irql, &irql_);
}

void XThread::LowerIrql(uint32_t new_irql) {
  irql_ = new_irql;
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

X_STATUS XThread::Delay(
  uint32_t processor_mode, uint32_t alertable, uint64_t interval) {
  int64_t timeout_ticks = interval;
  DWORD timeout_ms;
  if (timeout_ticks > 0) {
    // Absolute time, based on January 1, 1601.
    // TODO(benvanik): convert time to relative time.
    XEASSERTALWAYS();
    timeout_ms = 0;
  } else if (timeout_ticks < 0) {
    // Relative time.
    timeout_ms = (DWORD)(-timeout_ticks / 10000); // Ticks -> MS
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
