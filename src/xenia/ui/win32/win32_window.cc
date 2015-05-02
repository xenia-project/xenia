/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/win32/win32_window.h"

#include <dwmapi.h>
#include <tpcshrd.h>
#include <windowsx.h>

#include "xenia/logging.h"

namespace xe {
namespace ui {
namespace win32 {

Win32Window::Win32Window(const std::wstring& title)
    : Window(title), main_menu_(nullptr), closing_(false) {}

Win32Window::~Win32Window() {}

bool Win32Window::Initialize() {
  Create();
  return true;
}

bool Win32Window::Create() {
  HINSTANCE hInstance = GetModuleHandle(nullptr);

  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wcex.lpfnWndProc = Win32Control::WndProcThunk;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = nullptr;    // LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
  wcex.hIconSm = nullptr;  // LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = nullptr;
  wcex.lpszClassName = L"XeniaWindowClass";
  if (!RegisterClassEx(&wcex)) {
    XELOGE("RegisterClassEx failed");
    return false;
  }

  // Setup initial size.
  DWORD window_style = WS_OVERLAPPEDWINDOW;
  DWORD window_ex_style = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
  RECT rc = {0, 0, width_, height_};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

  // Create window.
  hwnd_ = CreateWindowEx(window_ex_style, L"XeniaWindowClass", L"Xenia",
                         window_style, rc.left, rc.top, rc.right - rc.left,
                         rc.bottom - rc.top, nullptr, nullptr, hInstance, this);
  if (!hwnd_) {
    XELOGE("CreateWindow failed");
    return false;
  }

  main_menu_ = CreateMenu();
  AppendMenu(main_menu_, MF_STRING, 0, L"TODO");
  SetMenu(hwnd_, main_menu_);

  // Disable flicks.
  ATOM atom = GlobalAddAtom(L"MicrosoftTabletPenServiceProperty");
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
  SetProp(hwnd_, L"MicrosoftTabletPenServiceProperty",
          reinterpret_cast<HANDLE>(dwHwndTabletProperty));
  GlobalDeleteAtom(atom);

  // Enable DWM elevation.
  EnableMMCSS();

  ShowWindow(hwnd_, SW_SHOWNORMAL);
  UpdateWindow(hwnd_);

  return 0;
}

void Win32Window::EnableMMCSS() {
  HMODULE hLibrary = LoadLibrary(L"DWMAPI.DLL");
  if (!hLibrary) {
    return;
  }

  typedef HRESULT(__stdcall * PDwmEnableMMCSS)(BOOL);
  PDwmEnableMMCSS pDwmEnableMMCSS =
      (PDwmEnableMMCSS)GetProcAddress(hLibrary, "DwmEnableMMCSS");
  if (pDwmEnableMMCSS) {
    pDwmEnableMMCSS(TRUE);
  }

  typedef HRESULT(__stdcall * PDwmSetPresentParameters)(
      HWND, DWM_PRESENT_PARAMETERS*);
  PDwmSetPresentParameters pDwmSetPresentParameters =
      (PDwmSetPresentParameters)GetProcAddress(hLibrary,
                                               "DwmSetPresentParameters");
  if (pDwmSetPresentParameters) {
    DWM_PRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof(DWM_PRESENT_PARAMETERS));
    pp.cbSize = sizeof(DWM_PRESENT_PARAMETERS);
    pp.fQueue = FALSE;
    pp.cBuffer = 2;
    pp.fUseSourceRate = FALSE;
    pp.cRefreshesPerFrame = 1;
    pp.eSampling = DWM_SOURCE_FRAME_SAMPLING_POINT;
    pDwmSetPresentParameters(hwnd_, &pp);
  }

  FreeLibrary(hLibrary);
}

bool Win32Window::set_title(const std::wstring& title) {
  if (!Window::set_title(title)) {
    return false;
  }
  SetWindowText(hwnd_, title.c_str());
  return true;
}

void Win32Window::Resize(int32_t width, int32_t height) {
  RECT rc = {0, 0, width, height};
  bool has_menu = main_menu_ ? true : false;
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, has_menu);
  if (true) {
    rc.right += 100 - rc.left;
    rc.left = 100;
    rc.bottom += 100 - rc.top;
    rc.top = 100;
  }
  Window::Resize(rc.left, rc.top, rc.right, rc.bottom);
}

void Win32Window::Resize(int32_t left, int32_t top, int32_t right,
                         int32_t bottom) {
  RECT rc = {left, top, right, bottom};
  bool has_menu = main_menu_ ? true : false;
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, has_menu);
  Window::Resize(rc.left, rc.top, rc.right, rc.bottom);
}

void Win32Window::ResizeToFill(int32_t pad_left, int32_t pad_top,
                               int32_t pad_right, int32_t pad_bottom) {
  // TODO(benvanik): fullscreen.
}

void Win32Window::OnClose() {
  if (!closing_ && hwnd_) {
    closing_ = true;
    CloseWindow(hwnd_);
  }
}

LRESULT Win32Window::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
  switch (message) {
    case WM_ACTIVATEAPP:
      if (wParam) {
        // Made active.
        OnShow();
      } else {
        // Made inactive.
        OnHide();
      }
      break;
    case WM_CLOSE:
      closing_ = true;
      Close();
      break;

    case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
      return TABLET_DISABLE_PRESSANDHOLD |    // disables press and hold
                                              // (right-click) gesture
             TABLET_DISABLE_PENTAPFEEDBACK |  // disables UI feedback on pen up
                                              // (waves)
             TABLET_DISABLE_PENBARRELFEEDBACK |  // disables UI feedback on pen
                                                 // button down (circle)
             TABLET_DISABLE_FLICKS |  // disables pen flicks (back, forward,
                                      // drag down, drag up)
             TABLET_DISABLE_TOUCHSWITCH | TABLET_DISABLE_SMOOTHSCROLLING |
             TABLET_DISABLE_TOUCHUIFORCEON | TABLET_ENABLE_MULTITOUCHDATA;

    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        // TODO(benvanik): dispatch to menu.
      }
      break;
  }

  return Win32Control::WndProc(hWnd, message, wParam, lParam);
}

}  // namespace win32
}  // namespace ui
}  // namespace xe
