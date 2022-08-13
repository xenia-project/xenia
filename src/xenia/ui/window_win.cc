/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window_win.h"

#include <algorithm>
#include <memory>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/ui/surface_win.h"

// Must be included before Windows headers for things like NOMINMAX.
#include "xenia/base/platform_win.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/windowed_app_context_win.h"

#include <ShellScalingApi.h>
#include <dwmapi.h>

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<Win32Window>(
      app_context, title, desired_logical_width, desired_logical_height);
}

Win32Window::Win32Window(WindowedAppContext& app_context,
                         const std::string_view title,
                         uint32_t desired_logical_width,
                         uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width, desired_logical_height),
      arrow_cursor_(LoadCursor(nullptr, IDC_ARROW)) {
  dpi_ = GetCurrentSystemDpi();
}

Win32Window::~Win32Window() {
  EnterDestructor();
  if (cursor_auto_hide_timer_) {
    DeleteTimerQueueTimer(nullptr, cursor_auto_hide_timer_, nullptr);
    cursor_auto_hide_timer_ = nullptr;
  }
  if (hwnd_) {
    // Set hwnd_ to null to ignore events from now on since this Win32Window is
    // entering an indeterminate state.
    HWND hwnd = hwnd_;
    hwnd_ = nullptr;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    DestroyWindow(hwnd);
  }
  if (icon_) {
    DestroyIcon(icon_);
  }
}

uint32_t Win32Window::GetMediumDpi() const { return USER_DEFAULT_SCREEN_DPI; }

bool Win32Window::OpenImpl() {
  const Win32WindowedAppContext& win32_app_context =
      static_cast<const Win32WindowedAppContext&>(app_context());
  HINSTANCE hinstance = win32_app_context.hinstance();

  static bool has_registered_class = false;
  if (!has_registered_class) {
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = Win32Window::WndProcThunk;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hinstance;
    wcex.hIcon = LoadIconW(hinstance, L"MAINICON");
    wcex.hIconSm = nullptr;  // LoadIconW(hinstance, L"MAINICON");
    wcex.hCursor = arrow_cursor_;
    // Matches the black background color of the presenter's painting.
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"XeniaWindowClass";
    if (!RegisterClassExW(&wcex)) {
      XELOGE("RegisterClassEx failed");
      return false;
    }
    has_registered_class = true;
  }

  const Win32MenuItem* main_menu =
      static_cast<const Win32MenuItem*>(GetMainMenu());

  // Setup the initial size for the non-fullscreen window. With per-monitor DPI,
  // this is also done to be able to obtain the initial window rectangle (with
  // CW_USEDEFAULT) to get the monitor for the window position, and then to
  // adjust the normal window size to the new DPI.
  // Save the initial desired size since it may be modified by the handler of
  // the WM_SIZE sent during window creation - it's needed for the initial
  // per-monitor DPI scaling.
  uint32_t initial_desired_logical_width = GetDesiredLogicalWidth();
  uint32_t initial_desired_logical_height = GetDesiredLogicalHeight();
  const Win32WindowedAppContext::PerMonitorDpiV2Api* per_monitor_dpi_v2_api =
      win32_app_context.per_monitor_dpi_v2_api();
  const Win32WindowedAppContext::PerMonitorDpiV1Api* per_monitor_dpi_v1_api =
      win32_app_context.per_monitor_dpi_v1_api();
  // Even with per-monitor DPI, take the closest approximation (system DPI) to
  // potentially more accurately determine the initial monitor.
  dpi_ = GetCurrentSystemDpi();
  DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  DWORD window_ex_style = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
  RECT window_size_rect;
  window_size_rect.left = 0;
  window_size_rect.top = 0;
  window_size_rect.right = LONG(ConvertSizeDpi(initial_desired_logical_width,
                                               dpi_, USER_DEFAULT_SCREEN_DPI));
  window_size_rect.bottom = LONG(ConvertSizeDpi(initial_desired_logical_height,
                                                dpi_, USER_DEFAULT_SCREEN_DPI));
  AdjustWindowRectangle(window_size_rect, window_style,
                        BOOL(main_menu != nullptr), window_ex_style, dpi_);
  // Create the window. Though WM_NCCREATE will assign to `hwnd_` too, still do
  // the assignment here to handle the case of a failure after WM_NCCREATE, for
  // instance.
  hwnd_ = CreateWindowExW(
      window_ex_style, L"XeniaWindowClass",
      reinterpret_cast<LPCWSTR>(xe::to_utf16(GetTitle()).c_str()), window_style,
      CW_USEDEFAULT, CW_USEDEFAULT,
      window_size_rect.right - window_size_rect.left,
      window_size_rect.bottom - window_size_rect.top, nullptr, nullptr,
      hinstance, this);
  if (!hwnd_) {
    XELOGE("CreateWindowExW failed");
    return false;
  }
  // For per-monitor DPI, obtain the DPI of the monitor the window was created
  // on, and adjust the initial normal size for it. If as a result of this
  // resizing, the window is moved to a different monitor, the WM_DPICHANGED
  // handler will do the needed correction.
  uint32_t initial_monitor_dpi = dpi_;
  if (per_monitor_dpi_v2_api) {
    initial_monitor_dpi = per_monitor_dpi_v2_api->get_dpi_for_window(hwnd_);
  } else if (per_monitor_dpi_v1_api) {
    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    UINT monitor_dpi_x, monitor_dpi_y;
    if (monitor && SUCCEEDED(per_monitor_dpi_v1_api->get_dpi_for_monitor(
                       monitor, MDT_DEFAULT, &monitor_dpi_x, &monitor_dpi_y))) {
      initial_monitor_dpi = monitor_dpi_x;
    }
  }
  if (dpi_ != initial_monitor_dpi) {
    dpi_ = initial_monitor_dpi;
    WINDOWPLACEMENT initial_dpi_placement;
    // Note that WINDOWPLACEMENT contains workspace coordinates, which are
    // adjusted to exclude toolbars such as the taskbar - the positions and
    // rectangle origins there can't be mixed with origins of rectangles in
    // virtual screen coordinates such as those involved in functions like
    // GetWindowRect.
    initial_dpi_placement.length = sizeof(initial_dpi_placement);
    if (GetWindowPlacement(hwnd_, &initial_dpi_placement)) {
      window_size_rect.left = 0;
      window_size_rect.top = 0;
      window_size_rect.right = LONG(ConvertSizeDpi(
          initial_desired_logical_width, dpi_, USER_DEFAULT_SCREEN_DPI));
      window_size_rect.bottom = LONG(ConvertSizeDpi(
          initial_desired_logical_height, dpi_, USER_DEFAULT_SCREEN_DPI));
      AdjustWindowRectangle(window_size_rect, window_style,
                            BOOL(main_menu != nullptr), window_ex_style, dpi_);
      initial_dpi_placement.rcNormalPosition.right =
          initial_dpi_placement.rcNormalPosition.left +
          (window_size_rect.right - window_size_rect.left);
      initial_dpi_placement.rcNormalPosition.bottom =
          initial_dpi_placement.rcNormalPosition.top +
          (window_size_rect.bottom - window_size_rect.top);
      SetWindowPlacement(hwnd_, &initial_dpi_placement);
    }
  }
  // Disable rounded corners starting with Windows 11 (or silently receive and
  // ignore E_INVALIDARG on Windows versions before 10.0.22000.0), primarily to
  // preserve all pixels of the guest output.
  DWM_WINDOW_CORNER_PREFERENCE window_corner_preference = DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE,
                        &window_corner_preference,
                        sizeof(window_corner_preference));
  // Disable flicks.
  ATOM atom = GlobalAddAtomW(L"MicrosoftTabletPenServiceProperty");
  const DWORD_PTR dwHwndTabletProperty =
      TABLET_DISABLE_PRESSANDHOLD |    // disables press and hold (right-click)
                                       // gesture
      TABLET_DISABLE_PENTAPFEEDBACK |  // disables UI feedback on pen up (waves)
      TABLET_DISABLE_PENBARRELFEEDBACK |  // disables UI feedback on pen button
                                          // down (circle)
      TABLET_DISABLE_FLICKS |  // disables pen flicks (back, forward, drag down,
                               // drag up)
      TABLET_DISABLE_TOUCHSWITCH | TABLET_DISABLE_SMOOTHSCROLLING |
      TABLET_DISABLE_TOUCHUIFORCEON | TABLET_ENABLE_MULTITOUCHDATA;
  SetPropW(hwnd_, L"MicrosoftTabletPenServiceProperty",
           reinterpret_cast<HANDLE>(dwHwndTabletProperty));
  GlobalDeleteAtom(atom);

  // Enable file dragging from external sources
  DragAcceptFiles(hwnd_, true);

  // Apply the initial state from the Window that the window shouldn't be
  // visibly transitioned to.

  if (icon_) {
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(icon_));
  }

  if (IsFullscreen()) {
    // Go fullscreen after setting up everything related to the placement of the
    // non-fullscreen window.
    WindowDestructionReceiver destruction_receiver(this);
    ApplyFullscreenEntry(destruction_receiver);
    if (destruction_receiver.IsWindowDestroyed()) {
      return true;
    }
  } else {
    if (main_menu) {
      SetMenu(hwnd_, main_menu->handle());
    }
  }

  // Finally show the window.
  ShowWindow(hwnd_, SW_SHOWNORMAL);

  // Report the initial actual state after opening, messages for which might
  // have missed if they were processed during CreateWindowExW when the HWND was
  // not yet attached to the Win32Window.
  {
    WindowDestructionReceiver destruction_receiver(this);

    // Report the desired logical size of the client area in the non-maximized
    // state after the initial layout setup in Windows.
    WINDOWPLACEMENT shown_placement;
    shown_placement.length = sizeof(shown_placement);
    if (GetWindowPlacement(hwnd_, &shown_placement)) {
      // Get the size of the non-client area to subtract it from the size of the
      // entire window in its non-maximized state, to get the client area. For
      // safety, in case the window is somehow smaller than its non-client area
      // (AdjustWindowRect is not exact in various cases also, such as when the
      // menu becomes multiline), clamp to 0.
      RECT non_client_area_rect = {};
      AdjustWindowRectangle(non_client_area_rect);
      OnDesiredLogicalSizeUpdate(
          SizeToLogical(uint32_t(std::max(
              (shown_placement.rcNormalPosition.right -
               shown_placement.rcNormalPosition.left) -
                  (non_client_area_rect.right - non_client_area_rect.left),
              LONG(0)))),
          SizeToLogical(uint32_t(std::max(
              (shown_placement.rcNormalPosition.bottom -
               shown_placement.rcNormalPosition.top) -
                  (non_client_area_rect.bottom - non_client_area_rect.top),
              LONG(0)))));
    }

    // Report the actual physical size in the current state.
    // GetClientRect returns a rectangle with 0 origin.
    RECT shown_client_rect;
    if (GetClientRect(hwnd_, &shown_client_rect)) {
      OnActualSizeUpdate(uint32_t(shown_client_rect.right),
                         uint32_t(shown_client_rect.bottom),
                         destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        return true;
      }
    }

    OnFocusUpdate(GetFocus() == hwnd_, destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return true;
    }
  }

  // Apply the initial state from the Window that involves interaction with the
  // user.

  if (IsMouseCaptureRequested()) {
    SetCapture(hwnd_);
  }

  cursor_currently_auto_hidden_ = false;
  CursorVisibility cursor_visibility = GetCursorVisibility();
  if (cursor_visibility != CursorVisibility::kVisible) {
    if (cursor_visibility == CursorVisibility::kAutoHidden) {
      if (!GetCursorPos(&cursor_auto_hide_last_screen_pos_)) {
        cursor_auto_hide_last_screen_pos_.x = LONG_MAX;
        cursor_auto_hide_last_screen_pos_.y = LONG_MAX;
      }
      cursor_currently_auto_hidden_ = true;
    }
    // OnFocusUpdate needs to be done before this.
    SetCursorIfFocusedOnClientArea(nullptr);
  }

  return true;
}

void Win32Window::RequestCloseImpl() {
  // Note that CloseWindow doesn't close the window, rather, it only minimizes
  // it - need to send WM_CLOSE to let the Win32Window WndProc perform all the
  // shutdown.
  SendMessageW(hwnd_, WM_CLOSE, 0, 0);
  // The window might have been deleted by the close handler, don't do anything
  // with *this anymore (if that's needed, use a WindowDestructionReceiver).
}

uint32_t Win32Window::GetLatestDpiImpl() const {
  // hwnd_ may be null in this function, but the latest DPI is stored in a
  // variable anyway.
  return dpi_;
}

void Win32Window::ApplyNewFullscreen() {
  // Various functions here may send messages that may result in the
  // listeners being invoked, and potentially cause the destruction of the
  // window or fullscreen being toggled from inside this function.
  WindowDestructionReceiver destruction_receiver(this);
  if (IsFullscreen()) {
    ApplyFullscreenEntry(destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return;
    }
  } else {
    // Changing the style and the menu may change the size too, don't handle
    // the resize multiple times (also potentially with the listeners changing
    // the desired fullscreen if called from the handling of some message like
    // WM_SIZE).
    BeginBatchedSizeUpdate();

    // Reinstate the non-client area.
    SetWindowLong(hwnd_, GWL_STYLE,
                  GetWindowLong(hwnd_, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }
    const Win32MenuItem* main_menu =
        static_cast<const Win32MenuItem*>(GetMainMenu());
    if (main_menu) {
      SetMenu(hwnd_, main_menu->handle());
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        if (!destruction_receiver.IsWindowDestroyed()) {
          EndBatchedSizeUpdate(destruction_receiver);
        }
        return;
      }
    }

    // For some reason, WM_DPICHANGED is not sent when the window is borderless
    // fullscreen with per-monitor DPI awareness v1 (on Windows versions since
    // Windows 8.1 before Windows 10 1703) - refresh the current DPI explicitly.
    dpi_ = GetCurrentDpi();
    if (dpi_ != pre_fullscreen_dpi_) {
      // Rescale the pre-fullscreen non-maximized window size to the new DPI as
      // WM_DPICHANGED with the new rectangle was received for the fullscreen
      // window size, not the windowed one. Simulating the behavior of the
      // automatic resizing when changing the scale in the Windows settings (as
      // of Windows 11 21H2 at least), which keeps the physical top-left origin
      // of the entire window including the non-client area, but rescales the
      // size.
      // Note that WINDOWPLACEMENT contains workspace coordinates, which are
      // adjusted to exclude toolbars such as the taskbar - the positions and
      // rectangle origins there can't be mixed with origins of rectangles in
      // virtual screen coordinates such as those involved in functions like
      // GetWindowRect.
      RECT new_dpi_rect;
      new_dpi_rect.left = 0;
      new_dpi_rect.top = 0;
      new_dpi_rect.right = LONG(ConvertSizeDpi(
          pre_fullscreen_normal_client_width_, dpi_, pre_fullscreen_dpi_));
      new_dpi_rect.bottom = LONG(ConvertSizeDpi(
          pre_fullscreen_normal_client_height_, dpi_, pre_fullscreen_dpi_));
      AdjustWindowRectangle(new_dpi_rect);
      pre_fullscreen_placement_.rcNormalPosition.right =
          pre_fullscreen_placement_.rcNormalPosition.left +
          (new_dpi_rect.right - new_dpi_rect.left);
      pre_fullscreen_placement_.rcNormalPosition.bottom =
          pre_fullscreen_placement_.rcNormalPosition.top +
          (new_dpi_rect.bottom - new_dpi_rect.top);
    }
    SetWindowPlacement(hwnd_, &pre_fullscreen_placement_);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }

    // https://devblogs.microsoft.com/oldnewthing/20131017-00/?p=2903
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                     SWP_FRAMECHANGED);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }

    EndBatchedSizeUpdate(destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return;
    }
  }
}

void Win32Window::ApplyNewTitle() {
  SetWindowTextW(hwnd_,
                 reinterpret_cast<LPCWSTR>(xe::to_utf16(GetTitle()).c_str()));
}

void Win32Window::LoadAndApplyIcon(const void* buffer, size_t size,
                                   bool can_apply_state_in_current_phase) {
  bool reset = !buffer || !size;

  HICON new_icon, new_icon_small;
  if (reset) {
    if (!icon_) {
      // The icon is already the default one.
      return;
    }
    if (!hwnd_) {
      // Don't need to get the actual icon from the class if there's nothing to
      // set it for yet (and there's no HWND to get it from, and the class may
      // have not been registered yet also).
      DestroyIcon(icon_);
      icon_ = nullptr;
      return;
    }
    new_icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd_, GCLP_HICON));
    new_icon_small =
        reinterpret_cast<HICON>(GetClassLongPtrW(hwnd_, GCLP_HICONSM));
    // Not caring if it's null in the class, accepting anything the class
    // specifies.
  } else {
    new_icon = CreateIconFromResourceEx(
        static_cast<PBYTE>(const_cast<void*>(buffer)), DWORD(size), TRUE,
        0x00030000, 0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
    if (!new_icon) {
      return;
    }
    new_icon_small = new_icon;
  }

  if (hwnd_) {
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(new_icon));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(new_icon_small));
  }

  // The old icon is not in use anymore, safe to destroy it now.
  if (icon_) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }

  if (!reset) {
    assert_true(new_icon_small == new_icon);
    icon_ = new_icon;
  }
}

void Win32Window::ApplyNewMainMenu(MenuItem* old_main_menu) {
  if (IsFullscreen()) {
    // The menu will be set when exiting fullscreen.
    return;
  }
  const Win32MenuItem* main_menu =
      static_cast<const Win32MenuItem*>(GetMainMenu());
  WindowDestructionReceiver destruction_receiver(this);
  SetMenu(hwnd_, main_menu ? main_menu->handle() : nullptr);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void Win32Window::CompleteMainMenuItemsUpdateImpl() {
  if (IsFullscreen()) {
    return;
  }
  DrawMenuBar(hwnd_);
}

void Win32Window::ApplyNewMouseCapture() {
  WindowDestructionReceiver destruction_receiver(this);
  SetCapture(hwnd_);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void Win32Window::ApplyNewMouseRelease() {
  if (GetCapture() != hwnd_) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ReleaseCapture();
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void Win32Window::ApplyNewCursorVisibility(
    CursorVisibility old_cursor_visibility) {
  CursorVisibility new_cursor_visibility = GetCursorVisibility();
  cursor_currently_auto_hidden_ = false;
  if (new_cursor_visibility == CursorVisibility::kAutoHidden) {
    if (!GetCursorPos(&cursor_auto_hide_last_screen_pos_)) {
      cursor_auto_hide_last_screen_pos_.x = LONG_MAX;
      cursor_auto_hide_last_screen_pos_.y = LONG_MAX;
    }
    cursor_currently_auto_hidden_ = true;
  } else if (old_cursor_visibility == CursorVisibility::kAutoHidden) {
    if (cursor_auto_hide_timer_) {
      DeleteTimerQueueTimer(nullptr, cursor_auto_hide_timer_, nullptr);
      cursor_auto_hide_timer_ = nullptr;
    }
  }
  SetCursorIfFocusedOnClientArea(
      new_cursor_visibility == CursorVisibility::kVisible ? arrow_cursor_
                                                          : nullptr);
}

void Win32Window::FocusImpl() { SetFocus(hwnd_); }

std::unique_ptr<Surface> Win32Window::CreateSurfaceImpl(
    Surface::TypeFlags allowed_types) {
  HINSTANCE hInstance =
      static_cast<const Win32WindowedAppContext&>(app_context()).hinstance();
  if (allowed_types & Surface::kTypeFlag_Win32Hwnd) {
    return std::make_unique<Win32HwndSurface>(hInstance, hwnd_);
  }
  return nullptr;
}

void Win32Window::RequestPaintImpl() { InvalidateRect(hwnd_, nullptr, FALSE); }

BOOL Win32Window::AdjustWindowRectangle(RECT& rect, DWORD style, BOOL menu,
                                        DWORD ex_style, UINT dpi) const {
  const Win32WindowedAppContext& win32_app_context =
      static_cast<const Win32WindowedAppContext&>(app_context());
  const Win32WindowedAppContext::PerMonitorDpiV2Api* per_monitor_dpi_v2_api =
      win32_app_context.per_monitor_dpi_v2_api();
  if (per_monitor_dpi_v2_api) {
    return per_monitor_dpi_v2_api->adjust_window_rect_ex_for_dpi(
        &rect, style, menu, ex_style, dpi);
  }
  // Before per-monitor DPI v2, there was no rescaling of the non-client
  // area at runtime at all, so throughout the execution of the process it will
  // behave the same regardless of the DPI.
  return AdjustWindowRectEx(&rect, style, menu, ex_style);
}

BOOL Win32Window::AdjustWindowRectangle(RECT& rect) const {
  if (!hwnd_) {
    return FALSE;
  }
  return AdjustWindowRectangle(rect, GetWindowLong(hwnd_, GWL_STYLE),
                               BOOL(GetMainMenu() != nullptr),
                               GetWindowLong(hwnd_, GWL_EXSTYLE), dpi_);
}

uint32_t Win32Window::GetCurrentSystemDpi() const {
  const Win32WindowedAppContext& win32_app_context =
      static_cast<const Win32WindowedAppContext&>(app_context());
  const Win32WindowedAppContext::PerMonitorDpiV2Api* per_monitor_dpi_v2_api =
      win32_app_context.per_monitor_dpi_v2_api();
  if (per_monitor_dpi_v2_api) {
    return per_monitor_dpi_v2_api->get_dpi_for_system();
  }

  HDC screen_hdc = GetDC(nullptr);
  if (!screen_hdc) {
    return USER_DEFAULT_SCREEN_DPI;
  }
  // According to MSDN, x and y are identical.
  int logical_pixels_x = GetDeviceCaps(screen_hdc, LOGPIXELSX);
  ReleaseDC(nullptr, screen_hdc);
  return uint32_t(logical_pixels_x);
}

uint32_t Win32Window::GetCurrentDpi() const {
  if (hwnd_) {
    const Win32WindowedAppContext& win32_app_context =
        static_cast<const Win32WindowedAppContext&>(app_context());

    const Win32WindowedAppContext::PerMonitorDpiV2Api* per_monitor_dpi_v2_api =
        win32_app_context.per_monitor_dpi_v2_api();
    if (per_monitor_dpi_v2_api) {
      return per_monitor_dpi_v2_api->get_dpi_for_window(hwnd_);
    }

    const Win32WindowedAppContext::PerMonitorDpiV1Api* per_monitor_dpi_v1_api =
        win32_app_context.per_monitor_dpi_v1_api();
    if (per_monitor_dpi_v1_api) {
      HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
      UINT monitor_dpi_x, monitor_dpi_y;
      if (monitor &&
          SUCCEEDED(per_monitor_dpi_v1_api->get_dpi_for_monitor(
              monitor, MDT_DEFAULT, &monitor_dpi_x, &monitor_dpi_y))) {
        // According to MSDN, x and y are identical.
        return monitor_dpi_x;
      }
    }
  }

  return GetCurrentSystemDpi();
}

void Win32Window::ApplyFullscreenEntry(
    WindowDestructionReceiver& destruction_receiver) {
  if (!IsFullscreen()) {
    return;
  }

  // https://blogs.msdn.com/b/oldnewthing/archive/2010/04/12/9994016.aspx
  // No reason to use MONITOR_DEFAULTTOPRIMARY instead of
  // MONITOR_DEFAULTTONEAREST, however.
  pre_fullscreen_dpi_ = dpi_;
  pre_fullscreen_placement_.length = sizeof(pre_fullscreen_placement_);
  HMONITOR monitor;
  MONITORINFO monitor_info;
  monitor_info.cbSize = sizeof(monitor_info);
  if (!GetWindowPlacement(hwnd_, &pre_fullscreen_placement_) ||
      !(monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)) ||
      !GetMonitorInfo(monitor, &monitor_info)) {
    OnDesiredFullscreenUpdate(false);
    return;
  }
  // Preserve values for DPI rescaling of the window in the non-maximized state
  // if DPI is changed mid-fullscreen.
  // Get the size of the non-client area to subtract it from the size of the
  // entire window in its non-maximized state, to get the client area. For
  // safety, in case the window is somehow smaller than its non-client area
  // (AdjustWindowRect is not exact in various cases also, such as when the menu
  // becomes multiline), clamp to 0.
  RECT non_client_area_rect = {};
  AdjustWindowRectangle(non_client_area_rect);
  pre_fullscreen_normal_client_width_ = uint32_t(
      std::max((pre_fullscreen_placement_.rcNormalPosition.right -
                pre_fullscreen_placement_.rcNormalPosition.left) -
                   (non_client_area_rect.right - non_client_area_rect.left),
               LONG(0)));
  pre_fullscreen_normal_client_height_ = uint32_t(
      std::max((pre_fullscreen_placement_.rcNormalPosition.bottom -
                pre_fullscreen_placement_.rcNormalPosition.top) -
                   (non_client_area_rect.bottom - non_client_area_rect.top),
               LONG(0)));

  // Changing the style and the menu may change the size too, don't handle the
  // resize multiple times (also potentially with the listeners changing the
  // desired fullscreen if called from the handling of some message like
  // WM_SIZE).
  BeginBatchedSizeUpdate();

  // Remove the non-client area.
  if (GetMainMenu()) {
    SetMenu(hwnd_, nullptr);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }
  }
  SetWindowLong(hwnd_, GWL_STYLE,
                GetWindowLong(hwnd_, GWL_STYLE) & ~DWORD(WS_OVERLAPPEDWINDOW));
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    if (!destruction_receiver.IsWindowDestroyed()) {
      EndBatchedSizeUpdate(destruction_receiver);
    }
    return;
  }

  // Resize the window to fullscreen. It is important that this is done _after_
  // disabling the decorations and the menu, to make sure that composition will
  // not have to be done for the new size of the window at all, so independent,
  // low-latency presentation is possible immediately - if the window was
  // involved in composition, it may stay composed persistently until some other
  // state change that sometimes helps, sometimes doesn't, even if it becomes
  // borderless fullscreen again - this occurs sometimes at least on Windows 11
  // 21H2 on Nvidia GeForce GTX 1070 on driver version 472.12.
  SetWindowPos(hwnd_, HWND_TOP, monitor_info.rcMonitor.left,
               monitor_info.rcMonitor.top,
               monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
               monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    if (!destruction_receiver.IsWindowDestroyed()) {
      EndBatchedSizeUpdate(destruction_receiver);
    }
    return;
  }

  EndBatchedSizeUpdate(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void Win32Window::HandleSizeUpdate(
    WindowDestructionReceiver& destruction_receiver) {
  if (!hwnd_) {
    // Batched size update ended when the window has already been closed, for
    // instance.
    return;
  }

  {
    MonitorUpdateEvent e(this, false);
    OnMonitorUpdate(e);
  }

  // For the desired size in the normal, not maximized and not fullscreen state.
  if (!IsFullscreen()) {
    WINDOWPLACEMENT window_placement;
    window_placement.length = sizeof(window_placement);
    if (GetWindowPlacement(hwnd_, &window_placement)) {
      // window_placement.rcNormalPosition is the entire window's rectangle, not
      // only the client area - convert to client.
      // https://devblogs.microsoft.com/oldnewthing/20131017-00/?p=2903
      // For safety, in case the window is somehow smaller than its non-client
      // area (AdjustWindowRect is not exact in various cases also, such as when
      // the menu becomes multiline), clamp to 0.
      RECT non_client_area_rect = {};
      if (AdjustWindowRectangle(non_client_area_rect)) {
        OnDesiredLogicalSizeUpdate(
            SizeToLogical(uint32_t(std::max(
                (window_placement.rcNormalPosition.right -
                 window_placement.rcNormalPosition.left) -
                    (non_client_area_rect.right - non_client_area_rect.left),
                LONG(0)))),
            SizeToLogical(uint32_t(std::max(
                (window_placement.rcNormalPosition.bottom -
                 window_placement.rcNormalPosition.top) -
                    (non_client_area_rect.bottom - non_client_area_rect.top),
                LONG(0)))));
      }
    }
  }

  // For the actual state.
  // GetClientRect returns a rectangle with 0 origin.
  RECT client_rect;
  if (GetClientRect(hwnd_, &client_rect)) {
    OnActualSizeUpdate(uint32_t(client_rect.right),
                       uint32_t(client_rect.bottom), destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return;
    }
  }
}

void Win32Window::BeginBatchedSizeUpdate() {
  // It's okay if batched_size_update_contained_* are not false when beginning
  // a batched update, in case the new batched update was started by a window
  // listener called from within EndBatchedSizeUpdate.
  ++batched_size_update_depth_;
}

void Win32Window::EndBatchedSizeUpdate(
    WindowDestructionReceiver& destruction_receiver) {
  assert_not_zero(batched_size_update_depth_);
  if (--batched_size_update_depth_) {
    return;
  }
  // Resetting batched_size_update_contained_* in closing, not opening, because
  // a listener may start a new batch, and finish it, and there won't be need to
  // handle the deferred messages twice.
  if (batched_size_update_contained_wm_size_) {
    batched_size_update_contained_wm_size_ = false;
    HandleSizeUpdate(destruction_receiver);
    if (destruction_receiver.IsWindowDestroyed()) {
      return;
    }
  }
  if (batched_size_update_contained_wm_paint_) {
    batched_size_update_contained_wm_paint_ = false;
    RequestPaint();
  }
}

bool Win32Window::HandleMouse(UINT message, WPARAM wParam, LPARAM lParam,
                              WindowDestructionReceiver& destruction_receiver) {
  // Mouse messages usually contain the position in the client area in lParam,
  // but WM_MOUSEWHEEL is an exception, it passes the screen position.
  int32_t message_x = GET_X_LPARAM(lParam);
  int32_t message_y = GET_Y_LPARAM(lParam);
  bool message_pos_is_screen = message == WM_MOUSEWHEEL;

  POINT client_pos = {message_x, message_y};
  if (message_pos_is_screen) {
    ScreenToClient(hwnd_, &client_pos);
  }

  if (GetCursorVisibility() == CursorVisibility::kAutoHidden) {
    POINT screen_pos = {message_x, message_y};
    if (message_pos_is_screen || ClientToScreen(hwnd_, &screen_pos)) {
      if (screen_pos.x != cursor_auto_hide_last_screen_pos_.x ||
          screen_pos.y != cursor_auto_hide_last_screen_pos_.y) {
        // WM_MOUSEMOVE messages followed by WM_SETCURSOR may be sent for
        // reasons not always involving actual mouse movement performed by the
        // user. They're sent when the position of the cursor relative to the
        // client area has been changed, as well as other events related to
        // window management (including when creating the window), even when not
        // interacting with the OS. These should not be revealing the cursor.
        // Only revealing it if the mouse has actually been moved.
        cursor_currently_auto_hidden_ = false;
        SetCursorAutoHideTimer();
        // There's no need to SetCursor here, mouse messages relevant to the
        // cursor within the window are always followed by WM_SETCURSOR.
        cursor_auto_hide_last_screen_pos_ = screen_pos;
      }
    }
  }

  MouseEvent::Button button = MouseEvent::Button::kNone;
  int32_t scroll_y = 0;
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      button = MouseEvent::Button::kLeft;
      break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      button = MouseEvent::Button::kRight;
      break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      button = MouseEvent::Button::kMiddle;
      break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
      switch (GET_XBUTTON_WPARAM(wParam)) {
        case XBUTTON1:
          button = MouseEvent::Button::kX1;
          break;
        case XBUTTON2:
          button = MouseEvent::Button::kX2;
          break;
        default:
          // Still handle the movement.
          break;
      }
      break;
    case WM_MOUSEMOVE:
      button = MouseEvent::Button::kNone;
      break;
    case WM_MOUSEWHEEL:
      button = MouseEvent::Button::kNone;
      static_assert(
          MouseEvent::kScrollPerDetent == WHEEL_DELTA,
          "Assuming the Windows scroll amount can be passed directly to "
          "MouseEvent");
      scroll_y = GET_WHEEL_DELTA_WPARAM(wParam);
      break;
    default:
      return false;
  }

  MouseEvent e(this, button, client_pos.x, client_pos.y, 0, scroll_y);
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
      OnMouseDown(e, destruction_receiver);
      break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
      OnMouseUp(e, destruction_receiver);
      break;
    case WM_MOUSEMOVE:
      OnMouseMove(e, destruction_receiver);
      break;
    case WM_MOUSEWHEEL:
      OnMouseWheel(e, destruction_receiver);
      break;
    default:
      break;
  }
  // Returning immediately anyway - no need to check
  // destruction_receiver.IsWindowDestroyed().
  return e.is_handled();
}

bool Win32Window::HandleKeyboard(
    UINT message, WPARAM wParam, LPARAM lParam,
    WindowDestructionReceiver& destruction_receiver) {
  KeyEvent e(this, VirtualKey(wParam), lParam & 0xFFFF,
             !!(lParam & (LPARAM(1) << 30)), !!(GetKeyState(VK_SHIFT) & 0x80),
             !!(GetKeyState(VK_CONTROL) & 0x80),
             !!(GetKeyState(VK_MENU) & 0x80), !!(GetKeyState(VK_LWIN) & 0x80));
  switch (message) {
    case WM_KEYDOWN:
      OnKeyDown(e, destruction_receiver);
      break;
    case WM_KEYUP:
      OnKeyUp(e, destruction_receiver);
      break;
    case WM_CHAR:
      OnKeyChar(e, destruction_receiver);
      break;
    default:
      break;
  }
  // Returning immediately anyway - no need to check
  // destruction_receiver.IsWindowDestroyed().
  return e.is_handled();
}

void Win32Window::SetCursorIfFocusedOnClientArea(HCURSOR cursor) const {
  if (!HasFocus()) {
    return;
  }
  POINT cursor_pos;
  if (!GetCursorPos(&cursor_pos)) {
    return;
  }
  if (WindowFromPoint(cursor_pos) == hwnd_ &&
      SendMessage(hwnd_, WM_NCHITTEST, 0,
                  MAKELONG(cursor_pos.x, cursor_pos.y)) == HTCLIENT) {
    SetCursor(cursor);
  }
}

void Win32Window::SetCursorAutoHideTimer() {
  // Reset the timer by deleting the old timer and creating the new one.
  // ChangeTimerQueueTimer doesn't work if the timer has already expired.
  if (cursor_auto_hide_timer_) {
    DeleteTimerQueueTimer(nullptr, cursor_auto_hide_timer_, nullptr);
    cursor_auto_hide_timer_ = nullptr;
  }
  // After making sure that the callback is not callable anymore
  // (DeleteTimerQueueTimer waits for the completion of the callback if it has
  // been called already, or cancels it if it's hasn't), update the most recent
  // message revision.
  last_cursor_auto_hide_queued = last_cursor_auto_hide_signaled + 1;
  CreateTimerQueueTimer(&cursor_auto_hide_timer_, nullptr,
                        AutoHideCursorTimerCallback, this,
                        kDefaultCursorAutoHideMilliseconds, 0,
                        WT_EXECUTEINTIMERTHREAD | WT_EXECUTEONLYONCE);
}

void Win32Window::AutoHideCursorTimerCallback(void* parameter,
                                              BOOLEAN timer_or_wait_fired) {
  if (!timer_or_wait_fired) {
    // Not a timer callback.
    return;
  }
  Win32Window& window = *static_cast<Win32Window*>(parameter);
  window.last_cursor_auto_hide_signaled = window.last_cursor_auto_hide_queued;
  SendMessage(window.hwnd_, kUserMessageAutoHideCursor,
              window.last_cursor_auto_hide_signaled, 0);
}

LRESULT Win32Window::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
    WindowDestructionReceiver destruction_receiver(this);
    // Returning immediately anyway - no need to check
    // destruction_receiver.IsWindowDestroyed() afterwards.
    return HandleMouse(message, wParam, lParam, destruction_receiver)
               ? 0
               : DefWindowProc(hWnd, message, wParam, lParam);
  }
  if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
    WindowDestructionReceiver destruction_receiver(this);
    // Returning immediately anyway - no need to check
    // destruction_receiver.IsWindowDestroyed() afterwards.
    return HandleKeyboard(message, wParam, lParam, destruction_receiver)
               ? 0
               : DefWindowProc(hWnd, message, wParam, lParam);
  }

  switch (message) {
    case WM_CLOSE:
    // In case the Windows window was somehow forcibly destroyed without
    // WM_CLOSE.
    case WM_DESTROY: {
      if (cursor_auto_hide_timer_) {
        DeleteTimerQueueTimer(nullptr, cursor_auto_hide_timer_, nullptr);
        cursor_auto_hide_timer_ = nullptr;
      }
      {
        WindowDestructionReceiver destruction_receiver(this);
        OnBeforeClose(destruction_receiver);
        if (destruction_receiver.IsWindowDestroyed()) {
          break;
        }
      }
      // Set hwnd_ to null to ignore events from now on since this Win32Window
      // is entering an indeterminate state - this should be done at some point
      // in closing anyway.
      hwnd_ = nullptr;
      SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
      if (message != WM_DESTROY) {
        DestroyWindow(hWnd);
      }
      OnAfterClose();
    } break;

    case WM_DROPFILES: {
      HDROP drop_handle = reinterpret_cast<HDROP>(wParam);
      auto drop_count = DragQueryFileW(drop_handle, 0xFFFFFFFFu, nullptr, 0);
      if (drop_count > 0) {
        // Get required buffer size
        UINT path_size = DragQueryFileW(drop_handle, 0, nullptr, 0);
        if (path_size > 0 && path_size < 0xFFFFFFFFu) {
          std::u16string path;
          ++path_size;             // Ensure space for the null terminator
          path.resize(path_size);  // Reserve space
          // Only getting first file dropped (other files ignored)
          path_size =
              DragQueryFileW(drop_handle, 0, (LPWSTR)&path[0], path_size);
          if (path_size > 0) {
            path.resize(path_size);  // Will drop the null terminator
            FileDropEvent e(this, xe::to_path(path));
            WindowDestructionReceiver destruction_receiver(this);
            OnFileDrop(e, destruction_receiver);
            if (destruction_receiver.IsWindowDestroyedOrClosed()) {
              DragFinish(drop_handle);
              break;
            }
          }
        }
      }
      DragFinish(drop_handle);
    } break;

    case WM_MOVE: {
      // chrispy: fix clang use of temporary error
      MonitorUpdateEvent update_event{this, false};
      OnMonitorUpdate(update_event);
    } break;

    case WM_SIZE: {
      if (batched_size_update_depth_) {
        batched_size_update_contained_wm_size_ = true;
      } else {
        WindowDestructionReceiver destruction_receiver(this);
        HandleSizeUpdate(destruction_receiver);
        if (destruction_receiver.IsWindowDestroyedOrClosed()) {
          break;
        }
      }
    } break;

    case WM_PAINT: {
      if (batched_size_update_depth_) {
        // Avoid painting an outdated surface during a batched size update when
        // WM_SIZE handling is deferred.
        batched_size_update_contained_wm_paint_ = true;
      } else {
        ValidateRect(hwnd_, nullptr);
        OnPaint();
      }
      // Custom painting via OnPaint - don't pass to DefWindowProc.
      return 0;
    } break;

    case WM_ERASEBKGND: {
      if (HasSurface()) {
        // Don't erase between paints because painting may be dropped if nothing
        // has changed since the last one.
        return 0;
      }
    } break;

    case WM_DISPLAYCHANGE: {
      // chrispy: fix clang use of temporary error
      MonitorUpdateEvent update_event{this, true};
      OnMonitorUpdate(update_event);
    } break;

    case WM_DPICHANGED: {
      // Note that for some reason, WM_DPICHANGED is not sent when the window is
      // borderless fullscreen with per-monitor DPI awareness v1.

      dpi_ = GetCurrentDpi();

      WindowDestructionReceiver destruction_receiver(this);

      {
        UISetupEvent e(this);
        OnDpiChanged(e, destruction_receiver);
        // The window might have been closed by the handler, check hwnd_ too
        // since it's needed below.
        if (destruction_receiver.IsWindowDestroyedOrClosed()) {
          break;
        }
      }

      auto rect = reinterpret_cast<const RECT*>(lParam);
      if (rect) {
        // SetWindowPos arguments according to WM_DPICHANGED MSDN documentation.
        // https://docs.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
        // There's no need to handle the maximized state any special way (by
        // updating the window placement instead of the window position in this
        // case, for instance), as Windows (by design) restores the window when
        // changing the DPI to a new one.
        SetWindowPos(hwnd_, nullptr, int(rect->left), int(rect->top),
                     int(rect->right - rect->left),
                     int(rect->bottom - rect->top),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (destruction_receiver.IsWindowDestroyedOrClosed()) {
          break;
        }
      }
    } break;

    case WM_KILLFOCUS: {
      WindowDestructionReceiver destruction_receiver(this);
      OnFocusUpdate(false, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        break;
      }
    } break;

    case WM_SETFOCUS: {
      WindowDestructionReceiver destruction_receiver(this);
      OnFocusUpdate(true, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        break;
      }
    } break;

    case WM_SETCURSOR: {
      if (reinterpret_cast<HWND>(wParam) == hwnd_ && HasFocus() &&
          LOWORD(lParam) == HTCLIENT) {
        switch (GetCursorVisibility()) {
          case CursorVisibility::kAutoHidden: {
            // Always revealing the cursor in case of events like clicking, but
            // WM_MOUSEMOVE messages may be sent for reasons not always
            // involving actual mouse movement performed by the user. Revealing
            // the cursor in case of movement is done in HandleMouse instead.
            if (HIWORD(lParam) != WM_MOUSEMOVE) {
              cursor_currently_auto_hidden_ = false;
              SetCursorAutoHideTimer();
            }
            if (cursor_currently_auto_hidden_) {
              SetCursor(nullptr);
              return TRUE;
            }
          } break;
          case CursorVisibility::kHidden:
            SetCursor(nullptr);
            return TRUE;
          default:
            break;
        }
      }
      // For the non-client area, and for visible cursor, letting normal
      // processing happen, setting the cursor to an arrow or to something
      // specific to non-client parts of the window.
    } break;

    case kUserMessageAutoHideCursor: {
      // Recheck the cursor visibility - the callback might have been called
      // before or while the timer is deleted. Also ignore messages from
      // outdated mouse interactions.
      if (GetCursorVisibility() == CursorVisibility::kAutoHidden &&
          wParam == last_cursor_auto_hide_queued) {
        // The timer object is not needed anymore.
        if (cursor_auto_hide_timer_) {
          DeleteTimerQueueTimer(nullptr, cursor_auto_hide_timer_, nullptr);
          cursor_auto_hide_timer_ = nullptr;
        }
        cursor_currently_auto_hidden_ = true;
        SetCursorIfFocusedOnClientArea(nullptr);
      }
      return 0;
    } break;

    case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
      return
          // disables press and hold (right-click) gesture
          TABLET_DISABLE_PRESSANDHOLD |
          // disables UI feedback on pen up (waves)
          TABLET_DISABLE_PENTAPFEEDBACK |
          // disables UI feedback on pen button down (circle)
          TABLET_DISABLE_PENBARRELFEEDBACK |
          // disables pen flicks (back, forward, drag down, drag up)
          TABLET_DISABLE_FLICKS | TABLET_DISABLE_TOUCHSWITCH |
          TABLET_DISABLE_SMOOTHSCROLLING | TABLET_DISABLE_TOUCHUIFORCEON |
          TABLET_ENABLE_MULTITOUCHDATA;

    case WM_MENUCOMMAND: {
      MENUINFO menu_info = {0};
      menu_info.cbSize = sizeof(menu_info);
      menu_info.fMask = MIM_MENUDATA;
      GetMenuInfo(HMENU(lParam), &menu_info);
      auto parent_item = reinterpret_cast<Win32MenuItem*>(menu_info.dwMenuData);
      auto child_item =
          reinterpret_cast<Win32MenuItem*>(parent_item->child(wParam));
      assert_not_null(child_item);
      WindowDestructionReceiver destruction_receiver(this);
      child_item->OnSelected();
      if (destruction_receiver.IsWindowDestroyed()) {
        break;
      }
      // The menu item might have been destroyed by its OnSelected, don't do
      // anything with it here from now on.
    } break;
  }

  // The window might have been destroyed by the handlers, don't interact with
  // *this in this function from now on.

  // Passing the original hWnd argument rather than hwnd_ as the window might
  // have been closed or destroyed by a handler, making hwnd_ null even though
  // DefWindowProc still needs to be called to propagate the closing-related
  // messages needed by Windows, or inaccessible (due to use-after-free) at all.
  return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK Win32Window::WndProcThunk(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  if (hWnd) {
    Win32Window* window = nullptr;
    if (message == WM_NCCREATE) {
      auto create_struct = reinterpret_cast<LPCREATESTRUCT>(lParam);
      window = reinterpret_cast<Win32Window*>(create_struct->lpCreateParams);
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (__int3264)(LONG_PTR)window);
      // Don't miss any messages (as they may have effect on the actual state
      // stored in Win32Window) sent before the completion of CreateWindowExW
      // dropped because `window->hwnd_ != hWnd`, when the result of
      // CreateWindowExW still hasn't been assigned to `hwnd_` (however, don't
      // reattach this window to a closed window if WM_NCCREATE was somehow sent
      // to a window being closed).
      if (window->phase() == Phase::kOpening) {
        assert_true(!window->hwnd_ || window->hwnd_ == hWnd);
        window->hwnd_ = hWnd;
      }
      // Enable non-client area DPI scaling for AdjustWindowRectExForDpi to work
      // correctly between Windows 10 1607 (when AdjustWindowRectExForDpi and
      // EnableNonClientDpiScaling were added) and 1703 (when per-monitor
      // awareness version 2 was added with automatically enabled non-client
      // area DPI scaling).
      const Win32WindowedAppContext& win32_app_context =
          static_cast<const Win32WindowedAppContext&>(window->app_context());
      const Win32WindowedAppContext::PerMonitorDpiV2Api*
          per_monitor_dpi_v2_api = win32_app_context.per_monitor_dpi_v2_api();
      if (per_monitor_dpi_v2_api) {
        per_monitor_dpi_v2_api->enable_non_client_dpi_scaling(hWnd);
      }
      // Already fully handled, no need to call Win32Window::WndProc.
    } else {
      window =
          reinterpret_cast<Win32Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
      if (window && window->hwnd_ == hWnd) {
        return window->WndProc(hWnd, message, wParam, lParam);
      }
    }
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::string& text,
                                               const std::string& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<Win32MenuItem>(type, text, hotkey, callback);
}

Win32MenuItem::Win32MenuItem(Type type, const std::string& text,
                             const std::string& hotkey,
                             std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {
  switch (type) {
    case MenuItem::Type::kNormal:
      handle_ = CreateMenu();
      break;
    case MenuItem::Type::kPopup:
      handle_ = CreatePopupMenu();
      break;
    default:
      // May just be a placeholder.
      break;
  }
  if (handle_) {
    MENUINFO menu_info = {0};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_MENUDATA | MIM_STYLE;
    menu_info.dwMenuData = ULONG_PTR(this);
    menu_info.dwStyle = MNS_NOTIFYBYPOS;
    SetMenuInfo(handle_, &menu_info);
  }
}

Win32MenuItem::~Win32MenuItem() {
  if (handle_) {
    DestroyMenu(handle_);
  }
}

void Win32MenuItem::SetEnabled(bool enabled) {
  UINT enable_flags = MF_BYPOSITION | (enabled ? MF_ENABLED : MF_GRAYED);
  UINT i = 0;
  for (auto iter = children_.begin(); iter != children_.end(); ++iter, ++i) {
    EnableMenuItem(handle_, i, enable_flags);
  }
}

void Win32MenuItem::OnChildAdded(MenuItem* generic_child_item) {
  auto child_item = static_cast<Win32MenuItem*>(generic_child_item);

  switch (child_item->type()) {
    case MenuItem::Type::kNormal:
      // Nothing special.
      break;
    case MenuItem::Type::kPopup:
      AppendMenuW(
          handle_, MF_POPUP, reinterpret_cast<UINT_PTR>(child_item->handle()),
          reinterpret_cast<LPCWSTR>(xe::to_utf16(child_item->text()).c_str()));
      break;
    case MenuItem::Type::kSeparator:
      AppendMenuW(handle_, MF_SEPARATOR, UINT_PTR(child_item->handle_), 0);
      break;
    case MenuItem::Type::kString:
      auto full_name = child_item->text();
      if (!child_item->hotkey().empty()) {
        full_name += "\t" + child_item->hotkey();
      }
      AppendMenuW(handle_, MF_STRING, UINT_PTR(child_item->handle_),
                  reinterpret_cast<LPCWSTR>(xe::to_utf16(full_name).c_str()));
      break;
  }
}

void Win32MenuItem::OnChildRemoved(MenuItem* generic_child_item) {}

}  // namespace ui
}  // namespace xe
