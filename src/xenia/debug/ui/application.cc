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

namespace xe {
namespace debug {
namespace ui {

Application* current_application_ = nullptr;

Application* Application::current() {
  assert_not_null(current_application_);
  return current_application_;
}

Application::Application() {
  current_application_ = this;
  loop_ = xe::ui::Loop::Create();
}

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
      xe::FatalError("Failed to initialize application");
      return;
    }

    fence.Signal();
  });
  fence.Wait();

  return app;
}

bool Application::Initialize() {
  // Bind the object model to the client so it'll maintain state.
  system_ = std::make_unique<model::System>(loop(), &client_);

  // TODO(benvanik): flags and such.
  if (!client_.Connect("localhost", 9002)) {
    return false;
  }

  main_window_ = std::make_unique<MainWindow>(this);
  if (!main_window_->Initialize()) {
    XELOGE("Unable to initialize main window");
    return false;
  }

  return true;
}

void Application::Quit() {
  loop_->Quit();

  // TODO(benvanik): proper exit.
  XELOGI("User-initiated death!");
  exit(1);
}

}  // namespace ui
}  // namespace debug
}  // namespace xe
