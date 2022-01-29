/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window_android.h"

#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/ui/surface_android.h"
#include "xenia/ui/windowed_app_context_android.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<AndroidWindow>(
      app_context, title, desired_logical_width, desired_logical_height);
}

AndroidWindow::~AndroidWindow() {
  EnterDestructor();
  AndroidWindowedAppContext& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  if (android_app_context.GetActivityWindow() == this) {
    android_app_context.SetActivityWindow(nullptr);
  }
}

bool AndroidWindow::OpenImpl() {
  // The window is a proxy between the main activity and Xenia, so there can be
  // only one for an activity.
  AndroidWindowedAppContext& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  AndroidWindow* previous_activity_window =
      android_app_context.GetActivityWindow();
  assert_null(previous_activity_window);
  if (previous_activity_window) {
    // Don't detach the old window as it's assuming it's still attached while
    // it's in an open phase.
    return false;
  }
  android_app_context.SetActivityWindow(this);
  return true;
}

void AndroidWindow::RequestCloseImpl() {
  // Finishing the Activity would cause the entire WindowedAppContext to quit,
  // which is not the same behavior as on other platforms - the
  // WindowedAppContext quit is done explicitly by the listeners during
  // OnClosing if needed (the main Window is potentially being changed to a
  // different one, for instance). Therefore, only detach the window from the
  // app context.

  WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
  OnAfterClose();

  AndroidWindowedAppContext& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  if (android_app_context.GetActivityWindow() == this) {
    android_app_context.SetActivityWindow(nullptr);
  }
}

std::unique_ptr<Surface> AndroidWindow::CreateSurfaceImpl(
    Surface::TypeFlags allowed_types) {
  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow) {
    // TODO(Triang3l): AndroidNativeWindowSurface for the ANativeWindow.
  }
  return nullptr;
}

void AndroidWindow::RequestPaintImpl() {
  // TODO(Triang3l): postInvalidate.
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::string& text,
                                               const std::string& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<AndroidMenuItem>(type, text, hotkey, callback);
}

}  // namespace ui
}  // namespace xe
