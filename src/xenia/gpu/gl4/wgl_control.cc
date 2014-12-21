/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <xenia/gpu/gl4/wgl_control.h>

#include <poly/logging.h>

namespace xe {
namespace gpu {
namespace gl4 {

WGLControl::WGLControl()
    : poly::ui::win32::Win32Control(Flags::kFlagOwnPaint) {}

WGLControl::~WGLControl() = default;

bool WGLControl::Create() {
  HINSTANCE hInstance = GetModuleHandle(nullptr);

  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wcex.lpfnWndProc = Win32Control::WndProcThunk;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = nullptr;
  wcex.hIconSm = nullptr;
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = nullptr;
  wcex.lpszClassName = L"XeniaWglClass";
  if (!RegisterClassEx(&wcex)) {
    PLOGE("WGL RegisterClassEx failed");
    return false;
  }

  // Create window.
  DWORD window_style = WS_CHILD | WS_VISIBLE;
  DWORD window_ex_style = 0;
  hwnd_ =
      CreateWindowEx(window_ex_style, L"XeniaWglClass", L"Xenia", window_style,
                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                     parent_hwnd(), nullptr, hInstance, this);
  if (!hwnd_) {
    PLOGE("WGL CreateWindow failed");
    return false;
  }

  HDC dc = GetDC(hwnd_);
  if (!dc) {
    PLOGE("No DC for WGL window");
    return false;
  }

  if (!context_.Initialize(dc)) {
    PFATAL("Unable to initialize GL context");
    return false;
  }

  OnCreate();
  return true;
}

void WGLControl::OnLayout(poly::ui::UIEvent& e) {
  Control::ResizeToFill();
}

LRESULT WGLControl::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {
  case WM_PAINT:
    context_.MakeCurrent();
    glViewport(0, 0, width_, height_);
    glClearColor(1.0f, 0, 0, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers(context_.dc());
    return 0;
  }
  return Win32Control::WndProc(hWnd, message, wParam, lParam);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
