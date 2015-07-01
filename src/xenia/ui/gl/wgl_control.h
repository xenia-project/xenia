/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_WGL_CONTROL_H_
#define XENIA_UI_GL_WGL_CONTROL_H_

#include <functional>

#include "xenia/base/threading.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/loop.h"
#include "xenia/ui/win32/win32_control.h"

namespace xe {
namespace ui {
namespace gl {

class WGLControl : public xe::ui::win32::Win32Control {
 public:
  WGLControl(Loop* loop);
  ~WGLControl() override;

  GLContext* context() { return &context_; }

  void SynchronousRepaint(std::function<void()> paint_callback);

 protected:
  bool Create() override;

  void OnLayout(UIEvent& e) override;

  LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam,
                  LPARAM lParam) override;

 private:
  Loop* loop_;
  GLContext context_;
  std::function<void()> current_paint_callback_;
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_WGL_CONTROL_H_
