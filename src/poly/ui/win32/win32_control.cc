/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "poly/ui/win32/win32_control.h"

namespace poly {
namespace ui {
namespace win32 {

Win32Control::Win32Control(uint32_t flags) : Control(flags), hwnd_(nullptr) {}

Win32Control::~Win32Control() {
  if (hwnd_) {
    CloseWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void Win32Control::OnCreate() {
  Control::OnCreate();

  // Create all children, if needed.
  for (auto& child_control : children_) {
    auto win32_control = static_cast<Win32Control*>(child_control.get());
    if (!win32_control->hwnd()) {
      win32_control->Create();
    } else {
      SetParent(win32_control->hwnd(), hwnd());
    }
    win32_control->Layout();
  }

  if (!is_cursor_visible_) {
    ShowCursor(FALSE);
  }
  if (!is_enabled_) {
    EnableWindow(hwnd_, false);
  }
  if (!is_visible_) {
    ShowWindow(hwnd_, SW_HIDE);
  }
  if (has_focus_) {
    SetFocus(hwnd_);
  }
}

void Win32Control::OnDestroy() {
  // Destroy all children, if needed.
  for (auto& child_control : children_) {
    auto win32_control = static_cast<Win32Control*>(child_control.get());
    if (!win32_control->hwnd()) {
      win32_control->Destroy();
    }
  }
}

void Win32Control::OnChildAdded(Control* child_control) {
  auto win32_control = static_cast<Win32Control*>(child_control);
  if (hwnd_) {
    if (!win32_control->hwnd()) {
      win32_control->Create();
    } else {
      SetParent(win32_control->hwnd(), hwnd());
    }
    child_control->Layout();
  }
}

void Win32Control::OnChildRemoved(Control* child_control) {
  auto win32_control = static_cast<Win32Control*>(child_control);
  if (win32_control->hwnd()) {
    SetParent(win32_control->hwnd(), nullptr);
  }
}

void Win32Control::Resize(int32_t width, int32_t height) {
  MoveWindow(hwnd_, 0, 0, width, height, TRUE);
}

void Win32Control::Resize(int32_t left, int32_t top, int32_t right,
                          int32_t bottom) {
  MoveWindow(hwnd_, left, top, right - left, bottom - top, TRUE);
}

void Win32Control::ResizeToFill(int32_t pad_left, int32_t pad_top,
                                int32_t pad_right, int32_t pad_bottom) {
  if (!parent_) {
    // TODO(benvanik): screen?
    return;
  }
  RECT parent_rect;
  auto parent_control = static_cast<Win32Control*>(parent_);
  GetClientRect(parent_control->hwnd(), &parent_rect);
  MoveWindow(hwnd_, parent_rect.left + pad_left, parent_rect.top + pad_top,
             parent_rect.right - pad_right - pad_left,
             parent_rect.bottom - pad_bottom - pad_top, TRUE);
}

void Win32Control::OnResize(UIEvent& e) {
  RECT client_rect;
  GetClientRect(hwnd_, &client_rect);
  int32_t width = client_rect.right - client_rect.left;
  int32_t height = client_rect.bottom - client_rect.top;
  if (width != width_ || height != height_) {
    width_ = width;
    height_ = height;
    Layout();
    for (auto& child_control : children_) {
      auto win32_control = static_cast<Win32Control*>(child_control.get());
      win32_control->OnResize(e);
      win32_control->Invalidate();
    }
  }
}

void Win32Control::Invalidate() {
  InvalidateRect(hwnd_, nullptr, FALSE);
  for (auto& child_control : children_) {
    auto win32_control = static_cast<Win32Control*>(child_control.get());
    win32_control->Invalidate();
  }
}

void Win32Control::set_cursor_visible(bool value) {
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

void Win32Control::set_enabled(bool value) {
  if (is_enabled_ == value) {
    return;
  }
  if (hwnd_) {
    EnableWindow(hwnd_, value);
  } else {
    is_enabled_ = value;
  }
}

void Win32Control::set_visible(bool value) {
  if (is_visible_ == value) {
    return;
  }
  if (hwnd_) {
    ShowWindow(hwnd_, value ? SW_SHOWNOACTIVATE : SW_HIDE);
  } else {
    is_visible_ = value;
  }
}

void Win32Control::set_focus(bool value) {
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

LRESULT CALLBACK Win32Control::WndProcThunk(HWND hWnd, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
  Win32Control* control = nullptr;
  if (message == WM_NCCREATE) {
    auto create_struct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    control = reinterpret_cast<Win32Control*>(create_struct->lpCreateParams);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (__int3264)(LONG_PTR)control);
  } else {
    control =
        reinterpret_cast<Win32Control*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  }
  if (control) {
    return control->WndProc(hWnd, message, wParam, lParam);
  } else {
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
}

LRESULT Win32Control::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                              LPARAM lParam) {
  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
    if (HandleMouse(message, wParam, lParam)) {
      SetFocus(hwnd_);
      return 0;
    } else {
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  } else if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
    if (HandleKeyboard(message, wParam, lParam)) {
      SetFocus(hwnd_);
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

    case WM_MOVING:
      break;
    case WM_MOVE:
      break;
    case WM_SIZING:
      break;
    case WM_SIZE: {
      auto e = UIEvent(this);
      OnResize(e);
      break;
    }

    case WM_PAINT:
      break;
    case WM_ERASEBKGND:
      if (flags_ & kFlagOwnPaint) {
        return 0;  // ignore
      }
      break;
    case WM_DISPLAYCHANGE:
      break;

    case WM_SHOWWINDOW: {
      if (wParam == TRUE) {
        auto e = UIEvent(this);
        OnVisible(e);
      } else {
        auto e = UIEvent(this);
        OnHidden(e);
      }
      break;
    }

    case WM_KILLFOCUS: {
      has_focus_ = false;
      auto e = UIEvent(this);
      OnLostFocus(e);
      break;
    }
    case WM_SETFOCUS: {
      has_focus_ = true;
      auto e = UIEvent(this);
      OnGotFocus(e);
      break;
    }
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

bool Win32Control::HandleMouse(UINT message, WPARAM wParam, LPARAM lParam) {
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
      OnMouseDown(e);
      break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
      OnMouseUp(e);
      break;
    case WM_MOUSEMOVE:
      OnMouseMove(e);
      break;
    case WM_MOUSEWHEEL:
      OnMouseWheel(e);
      break;
  }
  return e.is_handled();
}

bool Win32Control::HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam) {
  auto e = KeyEvent(this, (int)wParam);
  switch (message) {
    case WM_KEYDOWN:
      OnKeyDown(e);
      break;
    case WM_KEYUP:
      OnKeyUp(e);
      break;
    case WM_CHAR:
      OnKeyChar(e);
      break;
  }
  return e.is_handled();
}

}  // namespace win32
}  // namespace ui
}  // namespace poly
