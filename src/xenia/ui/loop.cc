/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/loop.h"

#include "el/message_handler.h"
#include "el/util/metrics.h"
#include "el/util/timer.h"
#include "xenia/base/assert.h"
#include "xenia/base/threading.h"

namespace xe {
namespace ui {

// The loop designated as being the main loop elemental-forms will use for all
// activity.
// TODO(benvanik): refactor elemental-forms to now need a global.
Loop* elemental_loop_ = nullptr;

Loop* Loop::elemental_loop() { return elemental_loop_; }

void Loop::set_elemental_loop(Loop* loop) {
  assert_null(elemental_loop_);
  elemental_loop_ = loop;
}

Loop::Loop() = default;

Loop::~Loop() {
  if (this == elemental_loop_) {
    elemental_loop_ = nullptr;
  }
}

void Loop::PostSynchronous(std::function<void()> fn) {
  if (is_on_loop_thread()) {
    // Prevent deadlock if we are executing on ourselves.
    fn();
    return;
  }
  xe::threading::Fence fence;
  Post([&fn, &fence]() {
    fn();
    fence.Signal();
  });
  fence.Wait();
}

}  // namespace ui
}  // namespace xe

void el::util::RescheduleTimer(uint64_t fire_time) {
  assert_not_null(xe::ui::elemental_loop_);
  if (fire_time == el::MessageHandler::kNotSoon) {
    return;
  }

  uint64_t now = el::util::GetTimeMS();
  uint64_t delay_millis = fire_time >= now ? fire_time - now : 0;
  xe::ui::elemental_loop_->PostDelayed(
      []() {
        uint64_t next_fire_time = el::MessageHandler::GetNextMessageFireTime();
        uint64_t now = el::util::GetTimeMS();
        if (now < next_fire_time) {
          // We timed out *before* we were supposed to (the OS is not playing
          // nice).
          // Calling ProcessMessages now won't achieve a thing so force a
          // reschedule
          // of the platform timer again with the same time.
          // ReschedulePlatformTimer(next_fire_time, true);
          return;
        }

        el::MessageHandler::ProcessMessages();

        // If we still have things to do (because we didn't process all
        // messages,
        // or because there are new messages), we need to rescedule, so call
        // RescheduleTimer.
        el::util::RescheduleTimer(el::MessageHandler::GetNextMessageFireTime());
      },
      delay_millis);
}
