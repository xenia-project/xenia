/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
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
  // Several state-related functions are left unimplemented because the layout
  // is configured from XML and Java.

  AndroidWindow(WindowedAppContext& app_context, const std::string_view title,
                uint32_t desired_logical_width, uint32_t desired_logical_height)
      : Window(app_context, title, desired_logical_width,
               desired_logical_height) {}
  ~AndroidWindow();

  uint32_t GetMediumDpi() const override { return 160; }

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;
};

// Dummy for the menu item - menus are controlled by the layout.
// TODO(Triang3l): Make something like MenuItem work as the basic common action
// interface for Java buttons.
class AndroidMenuItem final : public MenuItem {
 public:
  AndroidMenuItem(Type type, const std::string& text, const std::string& hotkey,
                  std::function<void()> callback)
      : MenuItem(type, text, hotkey, callback) {}
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_ANDROID_H_
