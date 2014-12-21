/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <xenia/gpu/gl4/gl_context.h>

#include <poly/logging.h>

namespace xe {
namespace gpu {
namespace gl4 {

thread_local GLEWContext* tls_glew_context_ = nullptr;
thread_local WGLEWContext* tls_wglew_context_ = nullptr;
extern "C" GLEWContext* glewGetContext() { return tls_glew_context_; }
extern "C" WGLEWContext* wglewGetContext() { return tls_wglew_context_; }

GLContext::GLContext() : dc_(nullptr), glrc_(nullptr) {}

GLContext::~GLContext() {
  wglMakeCurrent(nullptr, nullptr);
  if (glrc_) {
    wglDeleteContext(glrc_);
  }
}

bool GLContext::Initialize(HDC dc) {
  dc_ = dc;

  PIXELFORMATDESCRIPTOR pfd = {0};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 32;
  pfd.iLayerType = PFD_MAIN_PLANE;
  int pixel_format = ChoosePixelFormat(dc_, &pfd);
  if (!pixel_format) {
    PLOGE("Unable to choose pixel format");
    return false;
  }
  if (!SetPixelFormat(dc_, pixel_format, &pfd)) {
    PLOGE("Unable to set pixel format");
    return false;
  }

  HGLRC temp_context = wglCreateContext(dc_);
  if (!temp_context) {
    PLOGE("Unable to create temporary GL context");
    return false;
  }
  wglMakeCurrent(dc_, temp_context);

  tls_glew_context_ = &glew_context_;
  tls_wglew_context_ = &wglew_context_;
  if (glewInit() != GLEW_OK) {
    PLOGE("Unable to initialize GLEW");
    return false;
  }
  if (wglewInit() != GLEW_OK) {
    PLOGE("Unable to initialize WGLEW");
    return false;
  }

  if (!WGLEW_ARB_create_context) {
    PLOGE("WGL_ARG_create_context not supported by GL ICD");
    return false;
  }

  int context_flags = WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
#if DEBUG
  context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
#endif  // DEBUG

  int attrib_list[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 4,      //
                       WGL_CONTEXT_MINOR_VERSION_ARB, 5,      //
                       WGL_CONTEXT_FLAGS_ARB, context_flags,  //
                       0};

  glrc_ = wglCreateContextAttribsARB(dc_, nullptr, attrib_list);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(temp_context);
  if (!glrc_) {
    PLOGE("Unable to create real GL context");
    return false;
  }

  if (!MakeCurrent()) {
    PLOGE("Could not make real GL context current");
    return false;
  }

  return true;
}

bool GLContext::MakeCurrent() {
  if (!wglMakeCurrent(dc_, glrc_)) {
    return false;
  }
  tls_glew_context_ = &glew_context_;
  tls_wglew_context_ = &wglew_context_;
  return true;
}

void GLContext::ClearCurrent() {
  wglMakeCurrent(nullptr, nullptr);
  tls_glew_context_ = nullptr;
  tls_wglew_context_ = nullptr;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
