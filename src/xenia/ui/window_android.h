/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_ANDROID_H_
#define XENIA_UI_WINDOW_ANDROID_H_

#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

class AndroidWindow : public Window {
 public:
  // Many functions are left unimplemented because the layout is configured from
  // XML and Java.

  AndroidWindow(WindowedAppContext& app_context, const std::string& title)
      : Window(app_context, title) {}
  ~AndroidWindow();

  NativePlatformHandle native_platform_handle() const override {
    return nullptr;
  }
  // TODO(Triang3l): ANativeWindow for Vulkan surface creation.
  NativeWindowHandle native_handle() const override { return nullptr; }

  void EnableMainMenu() override {}
  void DisableMainMenu() override {}

  bool SetIcon(const void* buffer, size_t size) override { return false; }

  bool CaptureMouse() override { return false; }
  bool ReleaseMouse() override { return false; }

  int get_medium_dpi() const override { return 160; }

  // TODO(Triang3l): Call the close event, which may finish the activity.
  void Close() override {}
};

// Dummy for the menu item - menus are controlled by the layout.
// TODO(Triang3l): Make something like MenuItem work as the basic common action
// interface for Java buttons.
class AndroidMenuItem final : public MenuItem {
 public:
  AndroidMenuItem(Type type, const std::string& text, const std::string& hotkey,
                  std::function<void()> callback)
      : MenuItem(type, text, hotkey, callback) {}

  void EnableMenuItem(Window& window) override {}
  void DisableMenuItem(Window& window) override {}
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_ANDROID_H_
