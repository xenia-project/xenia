/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_GLX_CONTEXT_H_
#define XENIA_UI_GL_GLX_CONTEXT_H_

#include <gflags/gflags.h>

#include <memory>

#include "third_party/GL/glxew.h"
#include "xenia/base/platform_linux.h"
#include "xenia/ui/gl/blitter.h"
#include "xenia/ui/gl/gl.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/graphics_context.h"

DECLARE_bool(thread_safe_gl);

namespace xe {
namespace ui {
namespace gl {

class GLImmediateDrawer;
class GLProvider;

class GLXContext : public GLContext {
 public:
  ~GLXContext() override;

  bool is_current() override;

  bool MakeCurrent() override;
  void ClearCurrent() override;

  void BeginSwap() override;
  void EndSwap() override;

 protected:
  static std::unique_ptr<GLXContext> CreateOffscreen(
      GraphicsProvider* provider, GLXContext* parent_context);

  bool Initialize(GLContext* share_context) override;
  void* handle() override { return glx_context_; }

 private:
  friend class GLContext;
  GLXContext(GraphicsProvider* provider, Window* target_window);
  std::unique_ptr<GLEWContext> glew_context_;
  std::unique_ptr<GLXEWContext> glxew_context_;
  ::GLXContext glx_context_;
  GtkWidget* window_;
  GtkWidget* draw_area_;
  Colormap cmap_;
  Display* disp_;
  int xid_;
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_CONTEXT_H_
