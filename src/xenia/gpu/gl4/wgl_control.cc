/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/wgl_control.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/gl4/gl4_gpu-private.h"
#include "xenia/profiling.h"

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();
extern "C" WGLEWContext* wglewGetContext();

WGLControl::WGLControl(xe::ui::Loop* loop)
    : xe::ui::win32::Win32Control(Flags::kFlagOwnPaint), loop_(loop) {}

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
    XELOGE("WGL RegisterClassEx failed");
    return false;
  }

  // Create window.
  DWORD window_style = WS_CHILD | WS_VISIBLE | SS_NOTIFY;
  DWORD window_ex_style = 0;
  hwnd_ =
      CreateWindowEx(window_ex_style, L"XeniaWglClass", L"Xenia", window_style,
                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                     parent_hwnd(), nullptr, hInstance, this);
  if (!hwnd_) {
    XELOGE("WGL CreateWindow failed");
    return false;
  }

  if (!context_.Initialize(hwnd_)) {
    XEFATAL(
        "Unable to initialize GL context. Xenia requires OpenGL 4.5. Ensure "
        "you have the latest drivers for your GPU and that it supports OpenGL "
        "4.5. See http://xenia.jp/faq/ for more information.");
    return false;
  }

  context_.AssertExtensionsPresent();

  SetFocus(hwnd_);

  OnCreate();
  return true;
}

void WGLControl::OnLayout(xe::ui::UIEvent& e) { Control::ResizeToFill(); }

LRESULT WGLControl::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {
    case WM_PAINT: {
      SCOPE_profile_cpu_i("gpu", "xe::gpu::gl4::WGLControl::WM_PAINT");
      {
        GLContextLock context_lock(&context_);
        wglSwapIntervalEXT(0);

        float clear_color[] = {rand() / (float)RAND_MAX, 1.0f, 0, 1.0f};
        glClearNamedFramebufferfv(0, GL_COLOR, 0, clear_color);

        if (current_paint_callback_) {
          current_paint_callback_();
          current_paint_callback_ = nullptr;
        }

        xe::ui::UIEvent e(this);
        OnPaint(e);

        // TODO(benvanik): profiler present.
        Profiler::Present();
      }
      {
        SCOPE_profile_cpu_i("gpu", "xe::gpu::gl4::WGLControl::SwapBuffers");
        SwapBuffers(context_.dc());
      }
    } break;
  }
  return Win32Control::WndProc(hWnd, message, wParam, lParam);
}

void WGLControl::SynchronousRepaint(std::function<void()> paint_callback) {
  SCOPE_profile_cpu_f("gpu");

  // We may already have a pending paint from a previous request when we
  // were minimized. We just overwrite it.
  current_paint_callback_ = std::move(paint_callback);

  // This will not return until the WM_PAINT has completed.
  // Note, if we are minimized this won't do anything.
  RedrawWindow(hwnd(), nullptr, nullptr,
               RDW_INTERNALPAINT | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
