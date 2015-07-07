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

#include "xenia/debug/ui/application.h"
#include "xenia/ui/elemental_control.h"
#include "xenia/ui/platform.h"
#include "xenia/ui/window.h"

namespace xe {
namespace debug {
namespace ui {

class MainWindow : public xe::ui::PlatformWindow {
 public:
  MainWindow(Application* app);
  ~MainWindow() override;

  Application* app() const { return app_; }

  bool Initialize();

 private:
  void OnClose() override;
  void OnCommand(int id) override;

  Application* app_ = nullptr;
  xe::ui::PlatformMenu main_menu_;
  std::unique_ptr<xe::ui::ElementalControl> control_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MAIN_WINDOW_H_
