/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_WGL_CONTEXT_H_
#define XENIA_UI_GL_WGL_CONTEXT_H_

#include <gflags/gflags.h>

#include <memory>

#include "xenia/ui/gl/blitter.h"
#include "xenia/ui/gl/gl.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/graphics_context.h"

typedef struct HDC__* HDC;
typedef struct HGLRC__* HGLRC;

namespace xe {
namespace ui {
namespace gl {

class GLImmediateDrawer;
class GLProvider;

class WGLContext : public GLContext {
 public:
  ~WGLContext() override;

  bool is_current() override;
  bool MakeCurrent() override;
  void ClearCurrent() override;

  void BeginSwap() override;
  void EndSwap() override;

 protected:
  friend class GLContext;
  WGLContext(GraphicsProvider* provider, Window* target_window);
  static std::unique_ptr<WGLContext> CreateOffscreen(
      GraphicsProvider* provider, WGLContext* parent_context);

  bool Initialize(GLContext* share_context) override;
  void* handle() override { return glrc_; }

 private:
  HDC dc_ = nullptr;
  HGLRC glrc_ = nullptr;

  std::unique_ptr<GLEWContext> glew_context_;
  std::unique_ptr<WGLEWContext> wglew_context_;
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_CONTEXT_H_
