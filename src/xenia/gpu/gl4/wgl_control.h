/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_GPU_GL4_WGL_CONTROL_H_
#define XENIA_GPU_GL4_WGL_CONTROL_H_

#include <functional>

#include <poly/threading.h>
#include <poly/ui/loop.h>
#include <poly/ui/win32/win32_control.h>
#include <xenia/gpu/gl4/gl_context.h>

namespace xe {
namespace gpu {
namespace gl4 {

class WGLControl : public poly::ui::win32::Win32Control {
 public:
  WGLControl(poly::ui::Loop* loop);
  ~WGLControl() override;

  GLContext* context() { return &context_; }

  void SynchronousRepaint(std::function<void()> paint_callback);

 protected:
  bool Create() override;

  void OnLayout(poly::ui::UIEvent& e) override;

  LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam,
                  LPARAM lParam) override;

 private:
  poly::ui::Loop* loop_;
  GLContext context_;
  std::function<void()> current_paint_callback_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_WGL_CONTROL_H_
