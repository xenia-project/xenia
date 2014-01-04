/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/objects/xsemaphore.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XSemaphore::XSemaphore(KernelState* kernel_state) :
    XObject(kernel_state, kTypeSemaphore),
    handle_(NULL) {
}

XSemaphore::~XSemaphore() {
  if (handle_) {
    CloseHandle(handle_);
  }
}

void XSemaphore::Initialize(int32_t initial_count, int32_t maximum_count) {
  XEASSERTNULL(handle_);

  handle_ = CreateSemaphore(NULL, initial_count, maximum_count, NULL);
}

void XSemaphore::InitializeNative(void* native_ptr, DISPATCH_HEADER& header) {
  XEASSERTNULL(handle_);

  // NOT IMPLEMENTED
  // We expect Initialize to be called shortly.
}

int32_t XSemaphore::ReleaseSemaphore(int32_t release_count) {
  LONG previous_count = 0;
  ::ReleaseSemaphore(handle_, release_count, &previous_count);
  return previous_count;
}

X_STATUS XSemaphore::Wait(
    uint32_t wait_reason, uint32_t processor_mode,
    uint32_t alertable, uint64_t* opt_timeout) {
  DWORD timeout_ms;
  if (opt_timeout) {
    int64_t timeout_ticks = (int64_t)(*opt_timeout);
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
  } else {
    timeout_ms = INFINITE;
  }

  DWORD result = WaitForSingleObjectEx(handle_, timeout_ms, alertable);
  switch (result) {
  case WAIT_OBJECT_0:
    return X_STATUS_SUCCESS;
  case WAIT_IO_COMPLETION:
    // Or X_STATUS_ALERTED?
    return X_STATUS_USER_APC;
  case WAIT_TIMEOUT:
    return X_STATUS_TIMEOUT;
  default:
  case WAIT_FAILED:
  case WAIT_ABANDONED:
    return X_STATUS_ABANDONED_WAIT_0;
  }
}
