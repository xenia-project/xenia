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
#include <mutex>

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

class GLImmediateDrawer;
class GLProvider;

class GLContext : public GraphicsContext {
 public:
  ~GLContext() override;

  ImmediateDrawer* immediate_drawer() override;

  bool is_current() override;
  bool MakeCurrent() override;
  void ClearCurrent() override;
  bool WasLost() override;

  void BeginSwap() override;
  void EndSwap() override;
  std::unique_ptr<RawImage> Capture() override;

  Blitter* blitter() { return &blitter_; }

 protected:
  Blitter blitter_;
  std::unique_ptr<GLImmediateDrawer> immediate_drawer_;

  static std::recursive_mutex global_gl_mutex_;
  bool context_lost_ = false;
  bool robust_access_supported_ = false;
  static void FatalGLError(std::string error);
  virtual bool Initialize(GLContext* share_context) = 0;
  virtual void* handle() = 0;
  GLContext(GraphicsProvider* provider, Window* target_window);
  void SetupDebugging();
  void AssertExtensionsPresent();
  void DebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
                    GLsizei length, const GLchar* message);
 private:
  friend class GLProvider;

  static std::unique_ptr<GLContext> Create(GraphicsProvider* provider,
                                           Window* target_window,
                                           GLContext* share_context = nullptr);
  static std::unique_ptr<GLContext> CreateOffscreen(GraphicsProvider* provider,
                                                    GLContext* parent_context);

 private:


  static void GLAPIENTRY DebugMessageThunk(GLenum source, GLenum type,
                                           GLuint id, GLenum severity,
                                           GLsizei length,
                                           const GLchar* message,
                                           GLvoid* user_param);


};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_CONTEXT_H_
