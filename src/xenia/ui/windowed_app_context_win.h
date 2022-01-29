/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_WIN_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_WIN_H_

#include <memory>

#include "xenia/base/platform_win.h"
#include "xenia/ui/windowed_app_context.h"

// For per-monitor DPI awareness v1.
#include <ShellScalingApi.h>

namespace xe {
namespace ui {

class Win32WindowedAppContext final : public WindowedAppContext {
 public:
  // clang-format off
  struct PerMonitorDpiV1Api {
    HRESULT (STDAPICALLTYPE* get_dpi_for_monitor)(
        HMONITOR hmonitor, MONITOR_DPI_TYPE dpi_type, UINT* dpi_x, UINT* dpi_y);
  };
  struct PerMonitorDpiV2Api {
    // Important note: Even though per-monitor DPI awareness version 2
    // inherently has automatic non-client area DPI scaling, PMv2 was added in
    // Windows 10 1703. However, these functions were added earlier, in 1607.
    // While we haven't tested the actual behavior on 1607 and this is just a
    // guess, still, before using AdjustWindowRectExForDpi for the window DPI,
    // make sure that EnableNonClientDpiScaling is called for the window in its
    // WM_NCCREATE handler to enable automatic non-client DPI scaling on PMv1 on
    // 1607, so AdjustWindowRectExForDpi (which doesn't have a window argument
    // so it can't know whether automatic scaling is enabled for it) will likely
    // return values that reflect the actual size of the non-client area of the
    // window (otherwise, for instance, "un-adjusting" the actual window
    // rectangle using AdjustWindowRectExForDpi may result in a negative client
    // area size if the DPI passed to it is larger than the DPI actually used
    // for the non-client area).
    BOOL (WINAPI* adjust_window_rect_ex_for_dpi)(
        LPRECT rect, DWORD style, BOOL menu, DWORD ex_style, UINT dpi);
    BOOL (WINAPI* enable_non_client_dpi_scaling)(HWND hwnd);
    UINT (WINAPI* get_dpi_for_system)();
    UINT (WINAPI* get_dpi_for_window)(HWND hwnd);
  };
  // clang-format on

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

  // Per-monitor DPI awareness version 2 is expected to be enabled via the
  // manifest, as that's the recommended way, which also doesn't require calling
  // SetProcessDpiAwareness before doing anything that may depend on DPI
  // awareness (so it's safe to use any Windows APIs before what would be
  // supposed to initialize it).
  // Windows 8.1 per-monitor DPI awareness version 1.
  const PerMonitorDpiV1Api* per_monitor_dpi_v1_api() const {
    return per_monitor_dpi_v1_api_available_ ? &per_monitor_dpi_v1_api_
                                             : nullptr;
  }
  // Windows 10 1607 per-monitor DPI awareness API, also heavily used for
  // per-monitor DPI awareness version 2 functionality added in Windows 10 1703.
  const PerMonitorDpiV2Api* per_monitor_dpi_v2_api() const {
    return per_monitor_dpi_v2_api_available_ ? &per_monitor_dpi_v2_api_
                                             : nullptr;
  }

 private:
  enum : UINT {
    kPendingFunctionsWindowClassMessageExecute = WM_USER,
  };

  static LRESULT CALLBACK PendingFunctionsWndProc(HWND hwnd, UINT message,
                                                  WPARAM wparam, LPARAM lparam);

  HINSTANCE hinstance_;
  int show_cmd_;

  HMODULE shcore_module_ = nullptr;
  PerMonitorDpiV1Api per_monitor_dpi_v1_api_ = {};
  PerMonitorDpiV2Api per_monitor_dpi_v2_api_ = {};
  bool per_monitor_dpi_v1_api_available_ = false;
  bool per_monitor_dpi_v2_api_available_ = false;

  static bool pending_functions_window_class_registered_;
  HWND pending_functions_hwnd_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_WIN_H_
