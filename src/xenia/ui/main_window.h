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

#include <poly/ui/window.h>

// TODO(benvanik): only on windows.
#include <poly/ui/win32/win32_loop.h>
#include <poly/ui/win32/win32_window.h>

namespace xe {
namespace ui {

using PlatformLoop = poly::ui::win32::Win32Loop;
using PlatformWindow = poly::ui::win32::Win32Window;

class MainWindow : public PlatformWindow {
 public:
  MainWindow();
  ~MainWindow();

  PlatformLoop* loop() { return &loop_; }

  void Start();

 private:
  bool Initialize();

  PlatformLoop loop_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MAIN_WINDOW_H_
