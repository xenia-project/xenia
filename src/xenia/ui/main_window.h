/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_MAIN_WINDOW_H_
#define XENIA_UI_MAIN_WINDOW_H_

#include "xenia/ui/platform.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace ui {

class MainWindow : public PlatformWindow {
 public:
  ~MainWindow() override;

  static std::unique_ptr<PlatformWindow> Create(Emulator* emulator);

  Emulator* emulator() const { return emulator_; }

 private:
  explicit MainWindow(Emulator* emulator);

  bool Initialize();

  void UpdateTitle();

  void OnClose() override;
  void OnCommand(int id) override;

  Emulator* emulator_;
  PlatformLoop loop_;
  PlatformMenu main_menu_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MAIN_WINDOW_H_
