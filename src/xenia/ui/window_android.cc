/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window_android.h"

#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/ui/windowed_app_context_android.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string& title) {
  // The window is a proxy between the main activity and Xenia, so there can be
  // only one for an activity.
  AndroidWindowedAppContext& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context);
  AndroidWindow* current_activity_window =
      android_app_context.GetActivityWindow();
  assert_null(current_activity_window);
  if (current_activity_window) {
    return nullptr;
  }
  auto window = std::make_unique<AndroidWindow>(app_context, title);
  android_app_context.SetActivityWindow(window.get());
  return std::move(window);
}

AndroidWindow::~AndroidWindow() {
  AndroidWindowedAppContext& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  if (android_app_context.GetActivityWindow() == this) {
    android_app_context.SetActivityWindow(nullptr);
  }
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::string& text,
                                               const std::string& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<AndroidMenuItem>(type, text, hotkey, callback);
}

}  // namespace ui
}  // namespace xe
