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

#include "poly/ui/window.h"
#include "xenia/xbox.h"

// TODO(benvanik): only on windows.
#include "poly/ui/win32/win32_loop.h"
#include "poly/ui/win32/win32_window.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace ui {

using PlatformLoop = poly::ui::win32::Win32Loop;
using PlatformWindow = poly::ui::win32::Win32Window;

class MainWindow : public PlatformWindow {
 public:
  explicit MainWindow(Emulator* emulator);
  ~MainWindow() override;

  Emulator* emulator() const { return emulator_; }
  PlatformLoop* loop() { return &loop_; }

  void Start();

  X_STATUS LaunchPath(std::wstring path);

 private:
  bool Initialize();

  void OnClose() override;

  Emulator* emulator_;
  PlatformLoop loop_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MAIN_WINDOW_H_
