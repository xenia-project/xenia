/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xtimer.h>

#include <xenia/cpu/processor.h>


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
  assert_null(handle_);

  bool manual_reset = false;
  switch (timer_type) {
  case 0: // NotificationTimer
    manual_reset = true;
    break;
  case 1: // SynchronizationTimer
    manual_reset = false;
    break;
  default:
    assert_always();
    break;
  }

  handle_ = CreateWaitableTimer(NULL, manual_reset, NULL);
}

X_STATUS XTimer::SetTimer(
    int64_t due_time, uint32_t period_ms,
    uint32_t routine, uint32_t routine_arg, bool resume) {
  // Stash routine for callback.
  current_routine_ = routine;
  current_routine_arg_ = routine_arg;

  LARGE_INTEGER due_time_li;
  due_time_li.QuadPart = due_time;
  BOOL result = SetWaitableTimer(
      handle_, &due_time_li, period_ms,
      routine ? (PTIMERAPCROUTINE)CompletionRoutine : NULL, this,
      resume ? TRUE : FALSE);

  // Caller is checking for STATUS_TIMER_RESUME_IGNORED.
  // This occurs if result == TRUE but error is set.
  if (!result && GetLastError() == ERROR_NOT_SUPPORTED) {
    return X_STATUS_TIMER_RESUME_IGNORED;
  }

  return result ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
}

void XTimer::CompletionRoutine(
    XTimer* timer, DWORD timer_low, DWORD timer_high) {
  assert_true(timer->current_routine_);

  // Queue APC to call back routine with (arg, low, high).
  // TODO(benvanik): APC dispatch.
  XELOGE("Timer needs APC!");

  DebugBreak();
}

X_STATUS XTimer::Cancel() {
  return CancelWaitableTimer(handle_) == 0 ?
      X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
}
