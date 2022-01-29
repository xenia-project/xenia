/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_win.h"

#include <cstdlib>

#include "xenia/base/platform_win.h"

namespace xe {
namespace ui {

bool Win32WindowedAppContext::pending_functions_window_class_registered_;

Win32WindowedAppContext::~Win32WindowedAppContext() {
  if (pending_functions_hwnd_) {
    DestroyWindow(pending_functions_hwnd_);
  }
  if (shcore_module_) {
    FreeLibrary(shcore_module_);
  }
}

bool Win32WindowedAppContext::Initialize() {
  // Logging possibly not initialized in this function yet.

  // Obtain function pointers that may be used for windows if available.
  shcore_module_ = LoadLibraryW(L"SHCore.dll");
  if (shcore_module_) {
    per_monitor_dpi_v1_api_available_ = true;
    per_monitor_dpi_v1_api_available_ &=
        (*reinterpret_cast<void**>(
             &per_monitor_dpi_v1_api_.get_dpi_for_monitor) =
             GetProcAddress(shcore_module_, "GetDpiForMonitor")) != nullptr;
    per_monitor_dpi_v2_api_available_ = true;
    per_monitor_dpi_v2_api_available_ &=
        (*reinterpret_cast<void**>(
             &per_monitor_dpi_v2_api_.adjust_window_rect_ex_for_dpi) =
             GetProcAddress(shcore_module_, "AdjustWindowRectExForDpi")) !=
        nullptr;
    per_monitor_dpi_v2_api_available_ &=
        (*reinterpret_cast<void**>(
             &per_monitor_dpi_v2_api_.enable_non_client_dpi_scaling) =
             GetProcAddress(shcore_module_, "EnableNonClientDpiScaling")) !=
        nullptr;
    per_monitor_dpi_v2_api_available_ &=
        (*reinterpret_cast<void**>(
             &per_monitor_dpi_v2_api_.get_dpi_for_system) =
             GetProcAddress(shcore_module_, "GetDpiForSystem")) != nullptr;
    per_monitor_dpi_v2_api_available_ &=
        (*reinterpret_cast<void**>(
             &per_monitor_dpi_v2_api_.get_dpi_for_window) =
             GetProcAddress(shcore_module_, "GetDpiForWindow")) != nullptr;
  }

  // Create the message-only window for executing pending functions - using a
  // window instead of executing them between iterations so non-main message
  // loops, such as Windows modals, can execute pending functions too.
  static const WCHAR kPendingFunctionsWindowClassName[] =
      L"XeniaPendingFunctionsWindowClass";
  if (!pending_functions_window_class_registered_) {
    WNDCLASSEXW pending_functions_window_class = {};
    pending_functions_window_class.cbSize =
        sizeof(pending_functions_window_class);
    pending_functions_window_class.lpfnWndProc = PendingFunctionsWndProc;
    pending_functions_window_class.hInstance = hinstance_;
    pending_functions_window_class.lpszClassName =
        kPendingFunctionsWindowClassName;
    if (!RegisterClassExW(&pending_functions_window_class)) {
      return false;
    }
    pending_functions_window_class_registered_ = true;
  }
  pending_functions_hwnd_ = CreateWindowExW(
      0, kPendingFunctionsWindowClassName, L"Xenia Pending Functions",
      WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      HWND_MESSAGE, nullptr, hinstance_, this);
  if (!pending_functions_hwnd_) {
    return false;
  }

  return true;
}

void Win32WindowedAppContext::NotifyUILoopOfPendingFunctions() {
  while (!PostMessageW(pending_functions_hwnd_,
                       kPendingFunctionsWindowClassMessageExecute, 0, 0)) {
    Sleep(1);
  }
}

void Win32WindowedAppContext::PlatformQuitFromUIThread() {
  // Send WM_QUIT to whichever loop happens to process it - may be the loop of a
  // built-in modal window, which is unaware of HasQuitFromUIThread, don't let
  // it delay quitting indefinitely.
  PostQuitMessage(EXIT_SUCCESS);
}

int Win32WindowedAppContext::RunMainMessageLoop() {
  int result = EXIT_SUCCESS;
  MSG message;
  // The HasQuitFromUIThread check is not absolutely required, but for
  // additional safety in case WM_QUIT is not received for any reason.
  while (!HasQuitFromUIThread()) {
    BOOL message_result = GetMessageW(&message, nullptr, 0, 0);
    if (message_result == 0 || message_result == -1) {
      // WM_QUIT (0 - this is the primary message loop, no need to resend, also
      // contains the result from PostQuitMessage in wParam) or an error
      // (-1). Quitting the context will run the pending functions. Getting
      // WM_QUIT doesn't imply that QuitFromUIThread has been called already, it
      // may originate in some place other than PlatformQuitFromUIThread as
      // well - call it to finish everything including the pending functions.
      QuitFromUIThread();
      result = message_result ? EXIT_FAILURE : int(message.wParam);
      break;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return result;
}

LRESULT CALLBACK Win32WindowedAppContext::PendingFunctionsWndProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_CLOSE) {
    // Need the window for the entire context's lifetime, don't allow anything
    // to close it.
    return 0;
  }
  if (message == WM_NCCREATE) {
    SetWindowLongPtrW(
        hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(
            reinterpret_cast<const CREATESTRUCTW*>(lparam)->lpCreateParams));
  } else {
    auto app_context = reinterpret_cast<Win32WindowedAppContext*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app_context) {
      switch (message) {
        case WM_DESTROY:
          // The message-only window owned by the context is being destroyed,
          // thus the context won't be able to execute pending functions
          // anymore - can't continue functioning normally.
          app_context->QuitFromUIThread();
          break;
        case kPendingFunctionsWindowClassMessageExecute:
          app_context->ExecutePendingFunctionsFromUIThread();
          return 0;
        default:
          break;
      }
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace ui
}  // namespace xe
