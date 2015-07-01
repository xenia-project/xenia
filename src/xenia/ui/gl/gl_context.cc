/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl_context.h"

#include <mutex>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/profiling.h"

DEFINE_bool(thread_safe_gl, false,
            "Only allow one GL context to be active at a time.");

DEFINE_bool(disable_gl_context_reset, false,
            "Do not aggressively reset the GL context (helps with capture "
            "programs such as OBS or FRAPS).");

DEFINE_bool(gl_debug, false, "Enable OpenGL debug validation layer.");
DEFINE_bool(gl_debug_output, false, "Dump ARB_debug_output to stderr.");
DEFINE_bool(gl_debug_output_synchronous, true,
            "ARB_debug_output will synchronize to be thread safe.");

namespace xe {
namespace ui {
namespace gl {

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
  MakeCurrent();
  blitter_.Shutdown();
  ClearCurrent();
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
    XELOGE("Unable to choose pixel format");
    return false;
  }
  if (!SetPixelFormat(dc_, pixel_format, &pfd)) {
    XELOGE("Unable to set pixel format");
    return false;
  }

  HGLRC temp_context = wglCreateContext(dc_);
  if (!temp_context) {
    XELOGE("Unable to create temporary GL context");
    return false;
  }
  wglMakeCurrent(dc_, temp_context);

  tls_glew_context_ = &glew_context_;
  tls_wglew_context_ = &wglew_context_;
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    XELOGE("Unable to initialize GLEW");
    return false;
  }
  if (wglewInit() != GLEW_OK) {
    XELOGE("Unable to initialize WGLEW");
    return false;
  }

  if (!WGLEW_ARB_create_context) {
    XELOGE("WGL_ARG_create_context not supported by GL ICD");
    return false;
  }

  int context_flags = 0;
  if (FLAGS_gl_debug) {
    context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
  }

  int attrib_list[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 4,      //
                       WGL_CONTEXT_MINOR_VERSION_ARB, 5,      //
                       WGL_CONTEXT_FLAGS_ARB, context_flags,  //
                       0};

  glrc_ = wglCreateContextAttribsARB(dc_, nullptr, attrib_list);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(temp_context);
  if (!glrc_) {
    XELOGE("Unable to create real GL context");
    return false;
  }

  if (!MakeCurrent()) {
    XELOGE("Could not make real GL context current");
    return false;
  }

  XELOGI("Successfully created OpenGL context:");
  XELOGI("  GL_VENDOR: %s", glGetString(GL_VENDOR));
  XELOGI("  GL_VERSION: %s", glGetString(GL_VERSION));
  XELOGI("  GL_RENDERER: %s", glGetString(GL_RENDERER));
  XELOGI("  GL_SHADING_LANGUAGE_VERSION: %s",
         glGetString(GL_SHADING_LANGUAGE_VERSION));

  while (glGetError()) {
    // Clearing errors.
  }

  SetupDebugging();

  if (!blitter_.Initialize()) {
    XELOGE("Unable to initialize blitter");
    ClearCurrent();
    return false;
  }

  ClearCurrent();

  return true;
}

std::unique_ptr<GLContext> GLContext::CreateShared() {
  assert_not_null(glrc_);

  HGLRC new_glrc = nullptr;
  {
    GLContextLock context_lock(this);

    int context_flags = 0;
    // int profile = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
    int profile = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
    if (FLAGS_gl_debug) {
      context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
    }

    int attrib_list[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 4,       //
                         WGL_CONTEXT_MINOR_VERSION_ARB, 5,       //
                         WGL_CONTEXT_FLAGS_ARB, context_flags,   //
                         WGL_CONTEXT_PROFILE_MASK_ARB, profile,  //
                         0};
    new_glrc = wglCreateContextAttribsARB(dc_, glrc_, attrib_list);
    if (!new_glrc) {
      XELOGE("Could not create shared context");
      return nullptr;
    }
  }

  auto new_context = std::make_unique<GLContext>(hwnd_, new_glrc);
  if (!new_context->MakeCurrent()) {
    XELOGE("Could not make new GL context current");
    return nullptr;
  }

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    XELOGE("Unable to initialize GLEW");
    return nullptr;
  }
  if (wglewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    XELOGE("Unable to initialize WGLEW");
    return nullptr;
  }

  SetupDebugging();

  if (!new_context->blitter_.Initialize()) {
    XELOGE("Unable to initialize blitter");
    return nullptr;
  }

  new_context->ClearCurrent();

  return new_context;
}

void FatalGLError(std::string error) {
  XEFATAL(
      (error +
       "\nEnsure you have the latest drivers for your GPU and that it supports "
       "OpenGL 4.5. See http://xenia.jp/faq/ for more information.").c_str());
}

void GLContext::AssertExtensionsPresent() {
  if (!MakeCurrent()) {
    XEFATAL("Unable to make GL context current");
    return;
  }

  // Check shader version at least 4.5 (matching GL 4.5).
  auto glsl_version_raw =
      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  std::string glsl_version(glsl_version_raw);
  if (glsl_version.find("4.50") != 0) {
    FatalGLError("OpenGL GLSL version 4.50 is required.");
    return;
  }

  if (!GLEW_ARB_bindless_texture) {
    FatalGLError("OpenGL extension ARB_bindless_texture is required.");
    return;
  }

  // TODO(benvanik): figure out why this query fails.
  // if (!GLEW_ARB_fragment_coord_conventions) {
  //   FatalGLError(
  //       "OpenGL extension ARB_fragment_coord_conventions is required.");
  //   return;
  // }
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
  if (!FLAGS_gl_debug || !FLAGS_gl_debug_output) {
    return;
  }

  glEnable(GL_DEBUG_OUTPUT);

  // Synchronous output hurts, but is required if we want to line up the logs.
  if (FLAGS_gl_debug_output_synchronous) {
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  } else {
    glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  }

  // Enable everything by default.
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL,
                        GL_TRUE);

  // Disable annoying messages.
  GLuint disable_message_ids[] = {
      0x00020004,  // Usage warning: Generic vertex attribute array 0 uses a
                   // pointer with a small value (0x0000000000000000). Is this
                   // intended to be used as an offset into a buffer object?
  };
  glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE,
                        GLsizei(xe::countof(disable_message_ids)),
                        disable_message_ids, GL_FALSE);

  // Callback will be made from driver threads.
  glDebugMessageCallback(reinterpret_cast<GLDEBUGPROC>(&DebugMessageThunk),
                         this);
}

bool GLContext::MakeCurrent() {
  SCOPE_profile_cpu_f("gpu");
  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.lock();
  }

  if (!wglMakeCurrent(dc_, glrc_)) {
    if (FLAGS_thread_safe_gl) {
      global_gl_mutex_.unlock();
    }
    XELOGE("Unable to make GL context current");
    return false;
  }
  tls_glew_context_ = &glew_context_;
  tls_wglew_context_ = &wglew_context_;
  return true;
}

void GLContext::ClearCurrent() {
  if (!FLAGS_disable_gl_context_reset) {
    wglMakeCurrent(nullptr, nullptr);
  }
  tls_glew_context_ = nullptr;
  tls_wglew_context_ = nullptr;

  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.unlock();
  }
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
