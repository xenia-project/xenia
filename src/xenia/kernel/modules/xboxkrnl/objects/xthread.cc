/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/objects/xthread.h>

#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {
  static uint32_t next_xthread_id = 0;
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
    thread_state_(0) {
  creation_params_.stack_size           = stack_size;
  creation_params_.xapi_thread_startup  = xapi_thread_startup;
  creation_params_.start_address        = start_address;
  creation_params_.start_context        = start_context;
  creation_params_.creation_flags       = creation_flags;

  // Adjust stack size - min of 16k.
  if (creation_params_.stack_size < 16 * 1024 * 1024) {
    creation_params_.stack_size = 16 * 1024 * 1024;
  }
}

XThread::~XThread() {
  PlatformDestroy();

  if (thread_state_) {
    kernel_state()->processor()->DeallocThread(thread_state_);
  }
  if (tls_address_) {
    xe_memory_heap_free(kernel_state()->memory(), tls_address_, 0);
  }
  if (thread_state_address_) {
    xe_memory_heap_free(kernel_state()->memory(), thread_state_address_, 0);
  }

  if (thread_handle_) {
    // TODO(benvanik): platform kill
    XELOGE("Thread disposed without exiting");
  }
}

uint32_t XThread::GetCurrentThreadId(const uint8_t* thread_state_block) {
  return XEGETUINT32BE(thread_state_block + 0x14C);
}

uint32_t XThread::thread_id() {
  return thread_id_;
}

uint32_t XThread::last_error() {
  uint8_t *p = xe_memory_addr(memory(), thread_state_address_);
  return XEGETUINT32BE(p + 0x160);
}

void XThread::set_last_error(uint32_t error_code) {
  uint8_t *p = xe_memory_addr(memory(), thread_state_address_);
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
  thread_state_address_ = xe_memory_heap_alloc(memory(), 0, 2048, 0);
  if (!thread_state_address_) {
    XELOGW("Unable to allocate thread state block");
    return X_STATUS_NO_MEMORY;
  }

  // Allocate TLS block.
  XModule* module = kernel_state()->GetExecutableModule();
  const xe_xex2_header_t* header = module->xex_header();
  uint32_t tls_size = header->tls_info.slot_count * header->tls_info.data_size;
  tls_address_ = xe_memory_heap_alloc(memory(), 0, tls_size, 0);
  if (!tls_address_) {
    XELOGW("Unable to allocate thread local storage block");
    return X_STATUS_NO_MEMORY;
  }

  // Copy in default TLS info.
  // TODO(benvanik): is this correct?
  xe_memory_copy(memory(),
      tls_address_, header->tls_info.raw_data_address, tls_size);

  // Setup the thread state block (last error/etc).
  uint8_t *p = xe_memory_addr(memory(), thread_state_address_);
  XESETUINT32BE(p + 0x000, tls_address_);
  XESETUINT32BE(p + 0x100, thread_state_address_);
  XESETUINT32BE(p + 0x14C, thread_id_);
  XESETUINT32BE(p + 0x150, 0); // ?
  XESETUINT32BE(p + 0x160, 0); // last error

  // Allocate processor thread state.
  // This is thread safe.
  thread_state_ = kernel_state()->processor()->AllocThread(
      creation_params_.stack_size, thread_state_address_, thread_id_);
  if (!thread_state_) {
    XELOGW("Unable to allocate processor thread state");
    return X_STATUS_NO_MEMORY;
  }

  X_STATUS return_code = PlatformCreate();
  if (XFAILED(return_code)) {
    XELOGW("Unable to create platform thread (%.8X)", return_code);
    return return_code;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XThread::Exit(int exit_code) {
  // TODO(benvanik): set exit code in thread state block
  // TODO(benvanik); dispatch events? waiters? etc?

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
  thread->Execute();
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
  thread->Execute();
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
  // Run XapiThreadStartup first, if present.
  if (creation_params_.xapi_thread_startup) {
    XELOGE("xapi_thread_startup not implemented");
  }

  // Run user code.
  int exit_code = (int)kernel_state()->processor()->Execute(
      thread_state_,
      creation_params_.start_address, creation_params_.start_context);

  // If we got here it means the execute completed without an exit being called.
  // Treat the return code as an implicit exit code.
  Exit(exit_code);
}
