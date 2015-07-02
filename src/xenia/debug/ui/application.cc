/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/application.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/debug/ui/main_window.h"
#include "xenia/profiling.h"

#include "third_party/turbobadger/src/tb/tb_msg.h"
#include "third_party/turbobadger/src/tb/tb_system.h"

namespace xe {
namespace debug {
namespace ui {

Application* current_application_ = nullptr;

Application* Application::current() {
  assert_not_null(current_application_);
  return current_application_;
}

Application::Application() { current_application_ = this; }

Application::~Application() {
  assert_true(current_application_ == this);
  current_application_ = nullptr;
}

std::unique_ptr<Application> Application::Create() {
  std::unique_ptr<Application> app(new Application());

  xe::threading::Fence fence;
  app->loop()->Post([&app, &fence]() {
    xe::threading::set_name("Win32 Loop");
    xe::Profiler::ThreadEnter("Win32 Loop");

    if (!app->Initialize()) {
      XEFATAL("Failed to initialize application");
      exit(1);
    }

    fence.Signal();
  });
  fence.Wait();

  return app;
}

bool Application::Initialize() {
  main_window_ = std::make_unique<MainWindow>(this);
  if (!main_window_->Initialize()) {
    XELOGE("Unable to initialize main window");
    return false;
  }

  return true;
}

void Application::Quit() {
  loop_.Quit();

  // TODO(benvanik): proper exit.
  XELOGI("User-initiated death!");
  exit(1);
}

}  // namespace ui
}  // namespace debug
}  // namespace xe

// This doesn't really belong here (it belongs in tb_system_[linux/windows].cpp.
// This is here since the proper implementations has not yet been done.
void tb::TBSystem::RescheduleTimer(uint64_t fire_time) {
  if (fire_time == tb::kNotSoon) {
    return;
  }

  uint64_t now = tb::TBSystem::GetTimeMS();
  uint64_t delay_millis = fire_time >= now ? fire_time - now : 0;
  xe::debug::ui::Application::current()->loop()->PostDelayed([]() {
    uint64_t next_fire_time = tb::MessageHandler::GetNextMessageFireTime();
    uint64_t now = tb::TBSystem::GetTimeMS();
    if (now < next_fire_time) {
      // We timed out *before* we were supposed to (the OS is not playing nice).
      // Calling ProcessMessages now won't achieve a thing so force a reschedule
      // of the platform timer again with the same time.
      // ReschedulePlatformTimer(next_fire_time, true);
      return;
    }

    tb::MessageHandler::ProcessMessages();

    // If we still have things to do (because we didn't process all messages,
    // or because there are new messages), we need to rescedule, so call
    // RescheduleTimer.
    tb::TBSystem::RescheduleTimer(tb::MessageHandler::GetNextMessageFireTime());
  }, delay_millis);
}
