/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/ui/window_win.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(Loop* loop, const std::wstring& title) {
  return std::make_unique<Win32Window>(loop, title);
}

Win32Window::Win32Window(Loop* loop, const std::wstring& title)
    : Window(loop, title) {}

Win32Window::~Win32Window() {
  OnDestroy();
  if (hwnd_) {
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, 0);
    CloseWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

NativePlatformHandle Win32Window::native_platform_handle() const {
  return ::GetModuleHandle(nullptr);
}

bool Win32Window::Initialize() { return OnCreate(); }

bool Win32Window::OnCreate() {
  HINSTANCE hInstance = GetModuleHandle(nullptr);

  static bool has_registered_class = false;
  if (!has_registered_class) {
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = Win32Window::WndProcThunk;
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
    has_registered_class = true;
  }

  // Setup initial size.
  DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  DWORD window_ex_style = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
  RECT rc = {0, 0, width_, height_};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

  // Create window.
  hwnd_ = CreateWindowEx(window_ex_style, L"XeniaWindowClass", title_.c_str(),
                         window_style, rc.left, rc.top, rc.right - rc.left,
                         rc.bottom - rc.top, nullptr, nullptr, hInstance, this);
  if (!hwnd_) {
    XELOGE("CreateWindow failed");
    return false;
  }

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

  // Initial state.
  if (!is_cursor_visible_) {
    ShowCursor(FALSE);
  }
  if (has_focus_) {
    SetFocus(hwnd_);
  }

  return super::OnCreate();
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
    std::memset(&pp, 0, sizeof(DWM_PRESENT_PARAMETERS));
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

void Win32Window::OnDestroy() { super::OnDestroy(); }

void Win32Window::OnClose() {
  if (!closing_ && hwnd_) {
    closing_ = true;
    CloseWindow(hwnd_);
  }
  super::OnClose();
}

bool Win32Window::set_title(const std::wstring& title) {
  if (!super::set_title(title)) {
    return false;
  }
  SetWindowText(hwnd_, title.c_str());
  return true;
}

bool Win32Window::is_fullscreen() const { return fullscreen_; }

void Win32Window::ToggleFullscreen(bool fullscreen) {
  if (fullscreen == is_fullscreen()) {
    return;
  }

  fullscreen_ = fullscreen;

  DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
  if (fullscreen) {
    // Kill our borders and resize to take up entire primary monitor.
    // http://blogs.msdn.com/b/oldnewthing/archive/2010/04/12/9994016.aspx
    MONITORINFO mi = {sizeof(mi)};
    if (GetWindowPlacement(hwnd_, &windowed_pos_) &&
        GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY),
                       &mi)) {
      SetWindowLong(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
      ::SetMenu(hwnd_, NULL);

      // Call into parent class to get around menu resizing code.
      Resize(mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right,
             mi.rcMonitor.bottom);
    }
  } else {
    // Reinstate borders, resize to 1280x720
    SetWindowLong(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(hwnd_, &windowed_pos_);

    auto main_menu = reinterpret_cast<Win32MenuItem*>(main_menu_.get());
    if (main_menu) {
      ::SetMenu(hwnd_, main_menu->handle());
    }
  }
}

bool Win32Window::is_bordered() const {
  DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
  return (style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW;
}

void Win32Window::set_bordered(bool enabled) {
  if (is_fullscreen()) {
    // Don't screw with the borders if we're fullscreen.
    return;
  }

  DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
  if (enabled) {
    SetWindowLong(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
  } else {
    SetWindowLong(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
  }
}

void Win32Window::set_cursor_visible(bool value) {
  if (is_cursor_visible_ == value) {
    return;
  }
  if (value) {
    ShowCursor(TRUE);
    SetCursor(nullptr);
  } else {
    ShowCursor(FALSE);
  }
}

void Win32Window::set_focus(bool value) {
  if (has_focus_ == value) {
    return;
  }
  if (hwnd_) {
    if (value) {
      SetFocus(hwnd_);
    } else {
      SetFocus(nullptr);
    }
  } else {
    has_focus_ = value;
  }
}

void Win32Window::Resize(int32_t width, int32_t height) {
  RECT rc = {0, 0, width, height};
  bool has_menu = !is_fullscreen() && (main_menu_ ? true : false);
  AdjustWindowRect(&rc, GetWindowLong(hwnd_, GWL_STYLE), has_menu);
  if (true) {
    rc.right += 100 - rc.left;
    rc.left = 100;
    rc.bottom += 100 - rc.top;
    rc.top = 100;
  }
  MoveWindow(hwnd_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             TRUE);
}

void Win32Window::Resize(int32_t left, int32_t top, int32_t right,
                         int32_t bottom) {
  RECT rc = {left, top, right, bottom};
  bool has_menu = !is_fullscreen() && (main_menu_ ? true : false);
  AdjustWindowRect(&rc, GetWindowLong(hwnd_, GWL_STYLE), has_menu);
  MoveWindow(hwnd_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             TRUE);
}

void Win32Window::OnResize(UIEvent* e) {
  RECT client_rect;
  GetClientRect(hwnd_, &client_rect);
  int32_t width = client_rect.right - client_rect.left;
  int32_t height = client_rect.bottom - client_rect.top;
  if (width != width_ || height != height_) {
    width_ = width;
    height_ = height;
    Layout();
  }
  super::OnResize(e);
}

void Win32Window::Invalidate() {
  super::Invalidate();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Win32Window::Close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  Close();
  OnClose();
  DestroyWindow(hwnd_);
}

void Win32Window::OnMainMenuChange() {
  auto main_menu = reinterpret_cast<Win32MenuItem*>(main_menu_.get());
  // Don't actually set the menu if we're fullscreen. We'll do that later.
  if (main_menu && !is_fullscreen()) {
    ::SetMenu(hwnd_, main_menu->handle());
  }
}

LRESULT CALLBACK Win32Window::WndProcThunk(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  Win32Window* window = nullptr;
  if (message == WM_NCCREATE) {
    auto create_struct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    window = reinterpret_cast<Win32Window*>(create_struct->lpCreateParams);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (__int3264)(LONG_PTR)window);
  } else {
    window =
        reinterpret_cast<Win32Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  }
  if (window) {
    return window->WndProc(hWnd, message, wParam, lParam);
  } else {
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
}

LRESULT Win32Window::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
  if (hWnd != hwnd_) {
    return DefWindowProc(hWnd, message, wParam, lParam);
  }

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
      break;
    case WM_CREATE:
      break;
    case WM_DESTROY:
      OnDestroy();
      break;
    case WM_CLOSE:
      closing_ = true;
      Close();
      OnClose();
      break;

    case WM_MOVING:
      break;
    case WM_MOVE:
      break;
    case WM_SIZING:
      break;
    case WM_SIZE: {
      auto e = UIEvent(this);
      OnResize(&e);
      break;
    }

    case WM_PAINT: {
      ValidateRect(hwnd_, nullptr);
      static bool in_paint = false;
      if (!in_paint) {
        in_paint = true;
        auto e = UIEvent(this);
        OnPaint(&e);
        in_paint = false;
      }
      return 0;  // Ignored because of custom paint.
    }
    case WM_ERASEBKGND:
      return 0;  // Ignored because of custom paint.
    case WM_DISPLAYCHANGE:
      break;

    case WM_ACTIVATEAPP:
    case WM_SHOWWINDOW: {
      if (wParam == TRUE) {
        auto e = UIEvent(this);
        OnVisible(&e);
      } else {
        auto e = UIEvent(this);
        OnHidden(&e);
      }
      break;
    }

    case WM_KILLFOCUS: {
      has_focus_ = false;
      auto e = UIEvent(this);
      OnLostFocus(&e);
      break;
    }
    case WM_SETFOCUS: {
      has_focus_ = true;
      auto e = UIEvent(this);
      OnGotFocus(&e);
      break;
    }

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
      // TODO(benvanik): Redirect this to MenuItem's on_selected delegate.
      MENUINFO menu_info = {0};
      menu_info.cbSize = sizeof(menu_info);
      menu_info.fMask = MIM_MENUDATA;
      GetMenuInfo(HMENU(lParam), &menu_info);
      auto parent_item = reinterpret_cast<Win32MenuItem*>(menu_info.dwMenuData);
      auto child_item =
          reinterpret_cast<Win32MenuItem*>(parent_item->child(wParam));
      assert_not_null(child_item);
      UIEvent e(this);
      child_item->OnSelected(&e);
    } break;
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

bool Win32Window::HandleMouse(UINT message, WPARAM wParam, LPARAM lParam) {
  int32_t x = GET_X_LPARAM(lParam);
  int32_t y = GET_Y_LPARAM(lParam);
  if (message == WM_MOUSEWHEEL) {
    POINT pt = {x, y};
    ScreenToClient(hwnd_, &pt);
    x = pt.x;
    y = pt.y;
  }

  MouseEvent::Button button = MouseEvent::Button::kNone;
  int32_t dx = 0;
  int32_t dy = 0;
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
          return false;
      }
      break;
    case WM_MOUSEMOVE:
      button = MouseEvent::Button::kNone;
      break;
    case WM_MOUSEWHEEL:
      button = MouseEvent::Button::kNone;
      dx = 0;  // ?
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
      OnMouseDown(&e);
      break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
      OnMouseUp(&e);
      break;
    case WM_MOUSEMOVE:
      OnMouseMove(&e);
      break;
    case WM_MOUSEWHEEL:
      OnMouseWheel(&e);
      break;
  }
  return e.is_handled();
}

bool Win32Window::HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam) {
  auto e = KeyEvent(this, static_cast<int>(wParam), lParam & 0xFFFF0000,
                    !!(lParam & 0x2));
  switch (message) {
    case WM_KEYDOWN:
      OnKeyDown(&e);
      break;
    case WM_KEYUP:
      OnKeyUp(&e);
      break;
    case WM_CHAR:
      OnKeyChar(&e);
      break;
  }
  return e.is_handled();
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::wstring& text,
                                               const std::wstring& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<Win32MenuItem>(type, text, hotkey, callback);
}

Win32MenuItem::Win32MenuItem(Type type, const std::wstring& text,
                             const std::wstring& hotkey,
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

void Win32MenuItem::OnChildAdded(MenuItem* generic_child_item) {
  auto child_item = static_cast<Win32MenuItem*>(generic_child_item);

  switch (child_item->type()) {
    case MenuItem::Type::kNormal:
      // Nothing special.
      break;
    case MenuItem::Type::kPopup:
      AppendMenuW(handle_, MF_POPUP,
                  reinterpret_cast<UINT_PTR>(child_item->handle()),
                  child_item->text().c_str());
      break;
    case MenuItem::Type::kSeparator:
      AppendMenuW(handle_, MF_SEPARATOR, UINT_PTR(child_item->handle_), 0);
      break;
    case MenuItem::Type::kString:
      auto full_name = child_item->text();
      if (!child_item->hotkey().empty()) {
        full_name += L"\t" + child_item->hotkey();
      }
      AppendMenuW(handle_, MF_STRING, UINT_PTR(child_item->handle_),
                  full_name.c_str());
      break;
  }
}

void Win32MenuItem::OnChildRemoved(MenuItem* generic_child_item) {}

}  // namespace ui
}  // namespace xe
