/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_MAIN_WINDOW_H_
#define XENIA_DEBUG_UI_MAIN_WINDOW_H_

#include <memory>

#include "xenia/ui/gl/wgl_control.h"
#include "xenia/ui/platform.h"
#include "xenia/ui/window.h"

namespace xe {
namespace debug {
namespace ui {

class MainWindow : public xe::ui::PlatformWindow {
 public:
  ~MainWindow() override;

  static std::unique_ptr<MainWindow> Create();

 private:
  explicit MainWindow();

  bool Initialize();

  void OnClose() override;
  void OnCommand(int id) override;

  xe::ui::PlatformLoop loop_;
  xe::ui::PlatformMenu main_menu_;
  std::unique_ptr<xe::ui::gl::WGLControl> control_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MAIN_WINDOW_H_
