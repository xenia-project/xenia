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

#include <third_party/GL/glew.h>
#include <third_party/GL/wglew.h>

namespace xe {
namespace gpu {
namespace gl4 {

class GLContext {
 public:
  GLContext();
  ~GLContext();

  bool Initialize(HDC dc);

  HDC dc() const { return dc_; }

  bool MakeCurrent();
  void ClearCurrent();

 private:
  HDC dc_;
  HGLRC glrc_;

  GLEWContext glew_context_;
  WGLEWContext wglew_context_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL_CONTEXT_H_
