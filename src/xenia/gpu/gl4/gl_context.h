/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL_CONTEXT_H_
#define XENIA_GPU_GL4_GL_CONTEXT_H_

#include <memory>

#include <third_party/GL/glew.h>
#include <third_party/GL/wglew.h>

namespace xe {
namespace gpu {
namespace gl4 {

class GLContext {
 public:
  GLContext();
  GLContext(HWND hwnd, HGLRC glrc);
  ~GLContext();

  bool Initialize(HWND hwnd);

  HDC dc() const { return dc_; }

  std::unique_ptr<GLContext> CreateShared();

  bool MakeCurrent();
  void ClearCurrent();

 private:
  void SetupDebugging();
  void DebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
                    GLsizei length, const GLchar* message);
  static void GLAPIENTRY
  DebugMessageThunk(GLenum source, GLenum type, GLuint id, GLenum severity,
                    GLsizei length, const GLchar* message, GLvoid* user_param);

  HWND hwnd_;
  HDC dc_;
  HGLRC glrc_;

  GLEWContext glew_context_;
  WGLEWContext wglew_context_;
};

struct GLContextLock {
  GLContextLock(GLContext* context) : context_(context) {
    context_->MakeCurrent();
  }
  ~GLContextLock() { context_->ClearCurrent(); }

 private:
  GLContext* context_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL_CONTEXT_H_
