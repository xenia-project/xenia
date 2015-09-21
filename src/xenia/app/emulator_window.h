/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_EMULATOR_WINDOW_H_
#define XENIA_APP_EMULATOR_WINDOW_H_

#include <memory>

#include "xenia/ui/loop.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace app {

class EmulatorWindow {
 public:
  virtual ~EmulatorWindow();

  static std::unique_ptr<EmulatorWindow> Create(Emulator* emulator);

  Emulator* emulator() const { return emulator_; }
  ui::Loop* loop() const { return loop_.get(); }
  ui::Window* window() const { return window_.get(); }

 private:
  explicit EmulatorWindow(Emulator* emulator);

  bool Initialize();
  void UpdateTitle();

  void CpuTimeScalarReset();
  void CpuTimeScalarSetHalf();
  void CpuTimeScalarSetDouble();
  void CpuBreakIntoDebugger();
  void GpuTraceFrame();
  void GpuClearCaches();
  void ToggleFullscreen();
  void ShowHelpWebsite();

  Emulator* emulator_;
  std::unique_ptr<ui::Loop> loop_;
  std::unique_ptr<ui::Window> window_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_EMULATOR_WINDOW_H_
