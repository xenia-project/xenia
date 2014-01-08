/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xtimer.h>


using namespace xe;
using namespace xe::kernel;


XTimer::XTimer(KernelState* kernel_state) :
    XObject(kernel_state, kTypeTimer),
    handle_(NULL) {
}

XTimer::~XTimer() {
  if (handle_) {
    CloseHandle(handle_);
  }
}

void XTimer::Initialize(uint32_t timer_type) {
  XEASSERTNULL(handle_);

  bool manual_reset = false;
  switch (timer_type) {
  case 0: // NotificationTimer
    manual_reset = true;
    break;
  case 1: // SynchronizationTimer
    manual_reset = false;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }

  handle_ = CreateWaitableTimer(NULL, manual_reset, NULL);
}

X_STATUS XTimer::SetTimer(int64_t due_time, uint32_t period_ms) {
  LARGE_INTEGER due_time_li;
  due_time_li.QuadPart = due_time;
  if (SetWaitableTimer(handle_, &due_time_li, period_ms, NULL, NULL, TRUE)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

X_STATUS XTimer::Cancel() {
  return CancelWaitableTimer(handle_) == 0 ?
      X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
}
