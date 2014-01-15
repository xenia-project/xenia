/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/win32/win32_window.h>

#include <dwmapi.h>
#include <tpcshrd.h>
#include <windowsx.h>


using namespace xe;
using namespace xe::ui;
using namespace xe::ui::win32;


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

}  // namespace


Win32Window::Win32Window(xe_run_loop_ref run_loop) :
    handle_(0), closing_(false),
    Window(run_loop) {
}

Win32Window::~Win32Window() {
  if (handle_) {
    CloseWindow(handle_);
    handle_ = NULL;
  }
}

int Win32Window::Initialize(const char* title, uint32_t width, uint32_t height) {
  int result = Window::Initialize(title, width, height);
  if (result) {
    return result;
  }

  HINSTANCE hInstance = GetModuleHandle(NULL);

  WNDCLASSEX wcex;
  wcex.cbSize         = sizeof(WNDCLASSEX);
  wcex.style          = 0;
  wcex.lpfnWndProc    = Win32WindowWndProc;
  wcex.cbClsExtra     = 0;
  wcex.cbWndExtra     = 0;
  wcex.hInstance      = hInstance;
  wcex.hIcon          = NULL; // LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
  wcex.hIconSm        = NULL; // LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
  wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName   = NULL;
  wcex.lpszClassName  = L"XeniaWindowClass";
  if (!RegisterClassEx(&wcex)) {
    XELOGE("RegisterClassEx failed");
    return 1;
  }

  // Setup initial size.
  DWORD window_style    = WS_OVERLAPPEDWINDOW;
  DWORD window_ex_style = WS_EX_APPWINDOW;
  RECT rc = {
    0, 0,
    width, height
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
    return 1;
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

  return 0;
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

  FreeLibrary(hLibrary);
}

bool Win32Window::set_title(const char* title) {
  if (!Window::set_title(title)) {
    return false;
  }
  XEIGNORE(SetWindowTextA(handle_, title));
  return true;
}

bool Win32Window::set_cursor_visible(bool value) {
  if (!Window::set_cursor_visible(value)) {
    return false;
  }
  if (value) {
    ShowCursor(TRUE);
    SetCursor(NULL);
  } else {
    ShowCursor(FALSE);
  }
  return true;
}

bool Win32Window::SetSize(uint32_t width, uint32_t height) {
  RECT rc = {
    0, 0,
    width, height
  };
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  // TODO(benvanik): center?
  MoveWindow(handle_, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
  return true;
}

void Win32Window::OnClose() {
  if (!closing_ && handle_) {
    closing_ = true;
    CloseWindow(handle_);
    handle_ = NULL;
  }
}

bool Win32Window::HandleMouse(UINT message, WPARAM wParam, LPARAM lParam) {
  int32_t x = GET_X_LPARAM(lParam);
  int32_t y = GET_Y_LPARAM(lParam);
  if (message == WM_MOUSEWHEEL) {
    POINT pt = { x, y };
    ScreenToClient(handle_, &pt);
    x = pt.x;
    y = pt.y;
  }

  MouseEvent::Button button = MouseEvent::MOUSE_BUTTON_NONE;
  int32_t dx = 0;
  int32_t dy = 0;
  switch (message) {
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
    button = MouseEvent::MOUSE_BUTTON_LEFT;
    break;
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
    button = MouseEvent::MOUSE_BUTTON_RIGHT;
    break;
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
    button = MouseEvent::MOUSE_BUTTON_MIDDLE;
    break;
  case WM_XBUTTONDOWN:
  case WM_XBUTTONUP:
    switch (GET_XBUTTON_WPARAM(wParam)) {
    case XBUTTON1:
      button = MouseEvent::MOUSE_BUTTON_X1;
      break;
    case XBUTTON2:
      button = MouseEvent::MOUSE_BUTTON_X2;
      break;
    default:
      return false;
    }
    break;
  case WM_MOUSEMOVE:
    button = MouseEvent::MOUSE_BUTTON_NONE;
    break;
  case WM_MOUSEWHEEL:
    button = MouseEvent::MOUSE_BUTTON_NONE;
    dx = 0; // ?
    dy = GET_WHEEL_DELTA_WPARAM(wParam);
    break;
  default:
    // Double click/etc?
    return true;
  }

  auto e = MouseEvent(this, button, x, y, dx, dy);
  switch (message) {
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_XBUTTONDOWN:
    mouse_down(e);
    break;
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  case WM_XBUTTONUP:
    mouse_up(e);
    break;
  case WM_MOUSEMOVE:
    mouse_move(e);
    break;
  case WM_MOUSEWHEEL:
    mouse_wheel(e);
    break;
  }
  return true;
}

bool Win32Window::HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_KEYDOWN:
    (byte)wParam;
    return true;
  case WM_KEYUP:
    (byte)wParam;
    return true;
  default:
    return false;
  }
}

LRESULT Win32Window::WndProc(HWND hWnd, UINT message,
                             WPARAM wParam, LPARAM lParam) {
  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
    if (HandleMouse(message, wParam, lParam)) {
      return 0;
    } else {
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  } else if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
    if (HandleKeyboard(message, wParam, lParam)) {
      return 0;
    } else {
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  }

  switch (message) {
  case WM_NCCREATE:
    handle_ = hWnd;
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_ACTIVATEAPP:
    if (wParam) {
      // Made active.
      OnShow();
    } else {
      // Made inactive.
      OnHide();
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_CLOSE:
    closing_ = true;
    Close();
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_DESTROY:
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
  case WM_SIZING:
    BeginResizing();
    return DefWindowProc(hWnd, message, wParam, lParam);
  case WM_SIZE:
    {
      RECT frame;
      GetClientRect(handle_, &frame);
      OnResize(frame.right - frame.left, frame.bottom - frame.top);
      EndResizing();
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
