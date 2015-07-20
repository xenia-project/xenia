/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/objects/xtimer.h"

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace kernel {

XTimer::XTimer(KernelState* kernel_state) : XObject(kernel_state, kTypeTimer) {}

XTimer::~XTimer() = default;

void XTimer::Initialize(uint32_t timer_type) {
  assert_false(timer_);
  switch (timer_type) {
    case 0:  // NotificationTimer
      timer_ = xe::threading::Timer::CreateManualResetTimer();
      break;
    case 1:  // SynchronizationTimer
      timer_ = xe::threading::Timer::CreateSynchronizationTimer();
      break;
    default:
      assert_always();
      break;
  }
}

X_STATUS XTimer::SetTimer(int64_t due_time, uint32_t period_ms,
                          uint32_t routine, uint32_t routine_arg, bool resume) {
  // Caller is checking for STATUS_TIMER_RESUME_IGNORED.
  if (resume) {
    return X_STATUS_TIMER_RESUME_IGNORED;
  }

  // Stash routine for callback.
  current_routine_ = routine;
  current_routine_arg_ = routine_arg;
  if (current_routine_) {
    // Queue APC to call back routine with (arg, low, high).
    // TODO(benvanik): APC dispatch.
    XELOGE("Timer needs APC!");
    assert_zero(current_routine_);
  }

  due_time = Clock::ScaleGuestDurationFileTime(due_time);
  period_ms = Clock::ScaleGuestDurationMillis(period_ms);

  bool result;
  if (!period_ms) {
    result = timer_->SetOnce(std::chrono::nanoseconds(due_time * 100));
  } else {
    result = timer_->SetRepeating(std::chrono::nanoseconds(due_time * 100),
                                  std::chrono::milliseconds(period_ms));
  }

  return result ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
}

X_STATUS XTimer::Cancel() {
  return timer_->Cancel() ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
}

}  // namespace kernel
}  // namespace xe
