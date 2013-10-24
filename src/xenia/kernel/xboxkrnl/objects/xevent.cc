/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/objects/xevent.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XEvent::XEvent(KernelState* kernel_state) :
    XObject(kernel_state, kTypeEvent),
    handle_(NULL) {
}

XEvent::~XEvent() {
}

void XEvent::Initialize(bool manual_reset, bool initial_state) {
  XEASSERTNULL(handle_);

  handle_ = CreateEvent(NULL, manual_reset, initial_state, NULL);
}

void XEvent::InitializeNative(void* native_ptr, DISPATCH_HEADER& header) {
  XEASSERTNULL(handle_);

  bool manual_reset;
  switch (header.type_flags >> 24) {
  case 0x00: // EventNotificationObject (manual reset)
    manual_reset = true;
    break;
  case 0x01: // EventSynchronizationObject (auto reset)
    manual_reset = false;
    break;
  default:
    XEASSERTALWAYS();
    return;
  }

  bool initial_state = header.signal_state ? true : false;

  handle_ = CreateEvent(NULL, manual_reset, initial_state, NULL);
}

int32_t XEvent::Set(uint32_t priority_increment, bool wait) {
  return SetEvent(handle_) ? 1 : 0;
}

int32_t XEvent::Reset() {
  return ResetEvent(handle_) ? 1 : 0;
}

void XEvent::Clear() {
  ResetEvent(handle_);
}

X_STATUS XEvent::Wait(uint32_t wait_reason, uint32_t processor_mode,
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
