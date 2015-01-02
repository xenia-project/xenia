/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl_context.h>

#include <mutex>

#include <poly/assert.h>
#include <poly/cxx_compat.h>
#include <poly/logging.h>
#include <xenia/gpu/gl4/gl4_gpu-private.h>

namespace xe {
namespace gpu {
namespace gl4 {

static std::recursive_mutex global_gl_mutex_;

thread_local GLEWContext* tls_glew_context_ = nullptr;
thread_local WGLEWContext* tls_wglew_context_ = nullptr;
extern "C" GLEWContext* glewGetContext() { return tls_glew_context_; }
extern "C" WGLEWContext* wglewGetContext() { return tls_wglew_context_; }

GLContext::GLContext() : hwnd_(nullptr), dc_(nullptr), glrc_(nullptr) {}

GLContext::GLContext(HWND hwnd, HGLRC glrc)
    : hwnd_(hwnd), dc_(nullptr), glrc_(glrc) {
  dc_ = GetDC(hwnd);
}

GLContext::~GLContext() {
  wglMakeCurrent(nullptr, nullptr);
  if (glrc_) {
    wglDeleteContext(glrc_);
  }
  if (dc_) {
    ReleaseDC(hwnd_, dc_);
  }
}

bool GLContext::Initialize(HWND hwnd) {
  hwnd_ = hwnd;
  dc_ = GetDC(hwnd);

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
  glewExperimental = GL_TRUE;
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

  int context_flags = 0;
#if DEBUG
  context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
#endif                                                        // DEBUG
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

  while (glGetError()) {
    // Clearing errors.
  }

  SetupDebugging();

  ClearCurrent();

  return true;
}

std::unique_ptr<GLContext> GLContext::CreateShared() {
  assert_not_null(glrc_);

  HGLRC new_glrc = nullptr;
  {
    GLContextLock context_lock(this);

    int context_flags = 0;
#if DEBUG
    context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
#endif                                                          // DEBUG
    int attrib_list[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 4,      //
                         WGL_CONTEXT_MINOR_VERSION_ARB, 5,      //
                         WGL_CONTEXT_FLAGS_ARB, context_flags,  //
                         0};
    new_glrc = wglCreateContextAttribsARB(dc_, glrc_, attrib_list);
    if (!new_glrc) {
      PLOGE("Could not create shared context");
      return nullptr;
    }
  }

  auto new_context = std::make_unique<GLContext>(hwnd_, new_glrc);
  if (!new_context->MakeCurrent()) {
    PLOGE("Could not make new GL context current");
    return nullptr;
  }

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    PLOGE("Unable to initialize GLEW");
    return nullptr;
  }
  if (wglewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    PLOGE("Unable to initialize WGLEW");
    return nullptr;
  }

  SetupDebugging();

  new_context->ClearCurrent();

  return new_context;
}

void GLContext::DebugMessage(GLenum source, GLenum type, GLuint id,
                             GLenum severity, GLsizei length,
                             const GLchar* message) {
  const char* source_name = nullptr;
  switch (source) {
    case GL_DEBUG_SOURCE_API_ARB:
      source_name = "OpenGL";
      break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:
      source_name = "Windows";
      break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
      source_name = "Shader Compiler";
      break;
    case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:
      source_name = "Third Party";
      break;
    case GL_DEBUG_SOURCE_APPLICATION_ARB:
      source_name = "Application";
      break;
    case GL_DEBUG_SOURCE_OTHER_ARB:
      source_name = "Other";
      break;
    default:
      source_name = "(unknown source)";
      break;
  }

  const char* type_name = nullptr;
  switch (type) {
    case GL_DEBUG_TYPE_ERROR:
      type_name = "error";
      break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
      type_name = "deprecated behavior";
      break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
      type_name = "undefined behavior";
      break;
    case GL_DEBUG_TYPE_PORTABILITY:
      type_name = "portability";
      break;
    case GL_DEBUG_TYPE_PERFORMANCE:
      type_name = "performance";
      break;
    case GL_DEBUG_TYPE_OTHER:
      type_name = "message";
      break;
    case GL_DEBUG_TYPE_MARKER:
      type_name = "marker";
      break;
    case GL_DEBUG_TYPE_PUSH_GROUP:
      type_name = "push group";
      break;
    case GL_DEBUG_TYPE_POP_GROUP:
      type_name = "pop group";
      break;
    default:
      type_name = "(unknown type)";
      break;
  }

  const char* severity_name = nullptr;
  switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH_ARB:
      severity_name = "high";
      break;
    case GL_DEBUG_SEVERITY_MEDIUM_ARB:
      severity_name = "medium";
      break;
    case GL_DEBUG_SEVERITY_LOW_ARB:
      severity_name = "low";
      break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
      severity_name = "notification";
      break;
    default:
      severity_name = "(unknown severity)";
      break;
  }

  XELOGE("GL4 %s: %s(%s) %d: %s", source_name, type_name, severity_name, id,
         message);
}

void GLAPIENTRY
GLContext::DebugMessageThunk(GLenum source, GLenum type, GLuint id,
                             GLenum severity, GLsizei length,
                             const GLchar* message, GLvoid* user_param) {
  reinterpret_cast<GLContext*>(user_param)
      ->DebugMessage(source, type, id, severity, length, message);
}

void GLContext::SetupDebugging() {
  if (!FLAGS_gl_debug_output) {
    return;
  }
  glEnable(GL_DEBUG_OUTPUT);
  if (FLAGS_gl_debug_output_synchronous) {
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  }
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL,
                        GL_TRUE);
  glDebugMessageCallback(reinterpret_cast<GLDEBUGPROC>(&DebugMessageThunk),
                         this);
}

bool GLContext::MakeCurrent() {
  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.lock();
  }

  if (!wglMakeCurrent(dc_, glrc_)) {
    if (FLAGS_thread_safe_gl) {
      global_gl_mutex_.unlock();
    }
    PLOGE("Unable to make GL context current");
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

  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.unlock();
  }
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
