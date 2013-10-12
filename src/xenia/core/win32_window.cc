/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/win32_window.h>

#include <dwmapi.h>
#include <tpcshrd.h>


using namespace xe;
using namespace xe::core;


namespace {

static LRESULT CALLBACK Win32WindowWndProc(
    HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  Win32Window* window = 0;
  if (message == WM_NCCREATE) {
    LPCREATESTRUCT create_struct = (LPCREATESTRUCT)lParam;
    window = (Win32Window*)create_struct->lpCreateParams;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (__int3264)(LONG_PTR)window);
  } else {
    window = (Win32Window*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
  }
  if (window) {
    return window->WndProc(hWnd, message, wParam, lParam);
  } else {
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
}

}


Win32Window::Win32Window(xe_run_loop_ref run_loop) :
    handle_(0),
    Window(run_loop) {
  Create();
}

Win32Window::~Win32Window() {
  if (handle_) {
    CloseWindow(handle_);
  }
}

void Win32Window::Create() {
  HINSTANCE hInstance = GetModuleHandle(NULL);
  
  width_ = 1280;
  height_ = 720;

  WNDCLASSEX wcex;
  wcex.cbSize         = sizeof(WNDCLASSEX);
  wcex.style          = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc    = Win32WindowWndProc;
  wcex.cbClsExtra     = 0;
  wcex.cbWndExtra     = 0;
  wcex.hInstance      = hInstance;
  wcex.hIcon          = NULL; // LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
  wcex.hIconSm        = NULL; // LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
  wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground  = ( HBRUSH )( COLOR_WINDOW + 1 );
  wcex.lpszMenuName   = NULL;
  wcex.lpszClassName  = L"XeniaWindowClass";
  if (!RegisterClassEx(&wcex)) {
    XELOGE("RegisterClassEx failed");
    exit(1);
    return;
  }

  // Setup initial size.
  DWORD window_style   = WS_OVERLAPPEDWINDOW;
  DWORD window_ex_style = WS_EX_APPWINDOW;
  RECT rc = {
    0, 0,
    width_, height_
  };
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

  // Create window.
  handle_ = CreateWindowEx(
      window_ex_style,
      L"XeniaWindowClass",
      L"Xenia",
      window_style,
      CW_USEDEFAULT, CW_USEDEFAULT,
      rc.right - rc.left, rc.bottom - rc.top,
      NULL,
      NULL,
      hInstance,
      this);
  if (!handle_) {
    XELOGE("CreateWindow failed");
    exit(1);
    return;
  }

  // Disable flicks.
  ATOM atom = GlobalAddAtom(L"MicrosoftTabletPenServiceProperty");
  const DWORD_PTR dwHwndTabletProperty =
      TABLET_DISABLE_PRESSANDHOLD         | // disables press and hold (right-click) gesture
      TABLET_DISABLE_PENTAPFEEDBACK       | // disables UI feedback on pen up (waves)
      TABLET_DISABLE_PENBARRELFEEDBACK    | // disables UI feedback on pen button down (circle)
      TABLET_DISABLE_FLICKS               | // disables pen flicks (back, forward, drag down, drag up)
      TABLET_DISABLE_TOUCHSWITCH          |
      TABLET_DISABLE_SMOOTHSCROLLING      |
      TABLET_DISABLE_TOUCHUIFORCEON       |
      TABLET_ENABLE_MULTITOUCHDATA;
  SetProp(
      handle_,
      L"MicrosoftTabletPenServiceProperty",
      reinterpret_cast<HANDLE>(dwHwndTabletProperty));
  GlobalDeleteAtom(atom);

  // Enable DWM elevation.
  EnableMMCSS();

  ShowWindow(handle_, SW_SHOWNORMAL);
  UpdateWindow(handle_);
}

void Win32Window::EnableMMCSS() {
  HMODULE hLibrary = LoadLibrary(L"DWMAPI.DLL");
  if (!hLibrary) {
    return;
  }

  typedef HRESULT (__stdcall *PDwmEnableMMCSS)(BOOL);
  PDwmEnableMMCSS pDwmEnableMMCSS =
      (PDwmEnableMMCSS)GetProcAddress(
          hLibrary, "DwmEnableMMCSS");
  if (pDwmEnableMMCSS) {
    pDwmEnableMMCSS(TRUE);
  }

  typedef HRESULT (__stdcall *PDwmSetPresentParameters)(HWND, DWM_PRESENT_PARAMETERS*);
  PDwmSetPresentParameters pDwmSetPresentParameters =
      (PDwmSetPresentParameters)GetProcAddress(
          hLibrary, "DwmSetPresentParameters");
  if (pDwmSetPresentParameters) {
    DWM_PRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof(DWM_PRESENT_PARAMETERS));
    pp.cbSize               = sizeof(DWM_PRESENT_PARAMETERS);
    pp.fQueue               = FALSE;
    pp.cBuffer              = 2;
    pp.fUseSourceRate       = FALSE;
    pp.cRefreshesPerFrame   = 1;
    pp.eSampling            = DWM_SOURCE_FRAME_SAMPLING_POINT;
    pDwmSetPresentParameters(handle_, &pp);
  }

  FreeLibrary( hLibrary );
}

void Win32Window::set_title(const xechar_t* title) {
  XEIGNORE(SetWindowText(handle_, title));
}

void Win32Window::Resize(uint32_t width, uint32_t height) {
  if (width == width_ && height == height_) {
    return;
  }
  width_  = width;
  height_ = height;

  RECT rc = {
    0, 0,
    width_, height_
  };
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  // TODO(benvanik): center.
  MoveWindow(handle_, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);

  OnResize(width_, height_);
}

void Win32Window::ShowError(const xechar_t* message) {
  XEIGNORE(MessageBox(handle_, message, L"Xenia Error", MB_ICONERROR | MB_OK));
}

LRESULT Win32Window::WndProc(HWND hWnd, UINT message,
                             WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_NCCREATE:
    handle_ = hWnd;
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_ACTIVATEAPP:
    if (wParam) {
      // Made active.
    } else {
      // Made inactive.
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_CLOSE:
    OnClose();
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  
  case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      EndPaint(hWnd, &ps);
    }
    return 0;
  case WM_ERASEBKGND:
    return 0; // ignore
  case WM_DISPLAYCHANGE:
    return DefWindowProc(hWnd, message, wParam, lParam);

  case WM_MOVE:
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_SIZE:
    {
      RECT frame;
      GetClientRect(handle_, &frame);
      OnResize(frame.right - frame.left, frame.bottom - frame.top);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);

  case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
    return 
        TABLET_DISABLE_PRESSANDHOLD         | // disables press and hold (right-click) gesture
        TABLET_DISABLE_PENTAPFEEDBACK       | // disables UI feedback on pen up (waves)
        TABLET_DISABLE_PENBARRELFEEDBACK    | // disables UI feedback on pen button down (circle)
        TABLET_DISABLE_FLICKS               | // disables pen flicks (back, forward, drag down, drag up)
        TABLET_DISABLE_TOUCHSWITCH          |
        TABLET_DISABLE_SMOOTHSCROLLING      |
        TABLET_DISABLE_TOUCHUIFORCEON       |
        TABLET_ENABLE_MULTITOUCHDATA;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
}
