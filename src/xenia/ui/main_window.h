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

#include "xenia/ui/window.h"
#include "xenia/xbox.h"

// TODO(benvanik): only on windows.
#include "xenia/ui/win32/win32_file_picker.h"
#include "xenia/ui/win32/win32_loop.h"
#include "xenia/ui/win32/win32_menu_item.h"
#include "xenia/ui/win32/win32_window.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace ui {

using PlatformLoop = xe::ui::win32::Win32Loop;
using PlatformWindow = xe::ui::win32::Win32Window;
using PlatformMenu = xe::ui::win32::Win32MenuItem;
using PlatformFilePicker = xe::ui::win32::Win32FilePicker;

class MainWindow : public PlatformWindow {
 public:
  explicit MainWindow(Emulator* emulator);
  ~MainWindow() override;

  Emulator* emulator() const { return emulator_; }
  PlatformLoop* loop() { return &loop_; }

  void Start();

 private:
  bool Initialize();

  void UpdateTitle();
  void ToggleFullscreen();

  void OnClose() override;
  void OnCommand(int id) override;

  Emulator* emulator_;
  PlatformLoop loop_;
  PlatformMenu main_menu_;
  bool fullscreen_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MAIN_WINDOW_H_
