/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_GL_CONTEXT_H_
#define XENIA_UI_GL_GL_CONTEXT_H_

#include <gflags/gflags.h>

#include <memory>

#include "xenia/ui/gl/blitter.h"
#include "xenia/ui/gl/gl.h"
#include "xenia/ui/graphics_context.h"

DECLARE_bool(thread_safe_gl);

// TODO(benvanik): hide Win32 stuff.
typedef struct HDC__* HDC;
typedef struct HGLRC__* HGLRC;

namespace xe {
namespace ui {
namespace gl {

class GLContext : public GraphicsContext {
 public:
  static std::unique_ptr<GLContext> Create(Window* target_window);

  ~GLContext() override;

  HDC dc() const { return dc_; }

  std::unique_ptr<GraphicsContext> CreateShared() override;
  std::unique_ptr<ProfilerDisplay> CreateProfilerDisplay() override;
  std::unique_ptr<el::graphics::Renderer> CreateElementalRenderer() override;

  bool MakeCurrent() override;
  void ClearCurrent() override;

  void BeginSwap() override;
  void EndSwap() override;

  Blitter* blitter() { return &blitter_; }

 private:
  GLContext(Window* target_window);
  GLContext(Window* target_window, HGLRC glrc);

  bool Initialize(Window* target_window);
  void AssertExtensionsPresent();

  void SetupDebugging();
  void DebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
                    GLsizei length, const GLchar* message);
  static void GLAPIENTRY DebugMessageThunk(GLenum source, GLenum type,
                                           GLuint id, GLenum severity,
                                           GLsizei length,
                                           const GLchar* message,
                                           GLvoid* user_param);

  HDC dc_ = nullptr;
  HGLRC glrc_ = nullptr;

  std::unique_ptr<GLEWContext> glew_context_;
  std::unique_ptr<WGLEWContext> wglew_context_;

  Blitter blitter_;
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_CONTEXT_H_
