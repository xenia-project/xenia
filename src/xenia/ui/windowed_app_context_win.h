/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_WIN_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_WIN_H_

#include <memory>

#include "xenia/base/platform_win.h"
#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class Win32WindowedAppContext final : public WindowedAppContext {
 public:
  // Must call Initialize and check its result after creating to be able to
  // perform pending function calls.
  explicit Win32WindowedAppContext(HINSTANCE hinstance, int show_cmd)
      : hinstance_(hinstance), show_cmd_(show_cmd) {}
  ~Win32WindowedAppContext();

  bool Initialize();

  HINSTANCE hinstance() const { return hinstance_; }
  int show_cmd() const { return show_cmd_; }

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  int RunMainMessageLoop();

 private:
  enum : UINT {
    kPendingFunctionsWindowClassMessageExecute = WM_USER,
  };

  static LRESULT CALLBACK PendingFunctionsWndProc(HWND hwnd, UINT message,
                                                  WPARAM wparam, LPARAM lparam);

  HINSTANCE hinstance_;
  int show_cmd_;
  HWND pending_functions_hwnd_ = nullptr;

  static bool pending_functions_window_class_registered_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_WIN_H_
