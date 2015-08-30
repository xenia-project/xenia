/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl_context.h"

#include <gflags/gflags.h>

#include <mutex>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/profiling.h"
#include "xenia/ui/gl/gl_profiler_display.h"
#include "xenia/ui/gl/gl4_elemental_renderer.h"
#include "xenia/ui/window.h"

// TODO(benvanik): move win32 code to _win?
#include "xenia/base/platform_win.h"
#include "third_party/GL/wglew.h"

DEFINE_bool(thread_safe_gl, false,
            "Only allow one GL context to be active at a time.");

DEFINE_bool(disable_gl_context_reset, false,
            "Do not aggressively reset the GL context (helps with capture "
            "programs such as OBS or FRAPS).");

DEFINE_bool(random_clear_color, false, "Randomizes GL clear color.");

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

void FatalGLError(std::string error) {
  xe::FatalError(
      error +
      "\nEnsure you have the latest drivers for your GPU and that it supports "
      "OpenGL 4.5. See http://xenia.jp/faq/ for more information and a list"
      "of supported GPUs.");
}

std::unique_ptr<GLContext> GLContext::Create(Window* target_window) {
  auto context = std::unique_ptr<GLContext>(new GLContext(target_window));
  if (!context->Initialize(target_window)) {
    return nullptr;
  }
  context->AssertExtensionsPresent();
  return context;
}

GLContext::GLContext(Window* target_window) : GraphicsContext(target_window) {
  glew_context_.reset(new GLEWContext());
  wglew_context_.reset(new WGLEWContext());
}

GLContext::GLContext(Window* target_window, HGLRC glrc)
    : GraphicsContext(target_window), glrc_(glrc) {
  dc_ = GetDC(HWND(target_window_->native_handle()));
  glew_context_.reset(new GLEWContext());
  wglew_context_.reset(new WGLEWContext());
}

GLContext::~GLContext() {
  MakeCurrent();
  blitter_.Shutdown();
  ClearCurrent();
  if (glrc_) {
    wglDeleteContext(glrc_);
  }
  if (dc_) {
    ReleaseDC(HWND(target_window_->native_handle()), dc_);
  }
}

bool GLContext::Initialize(Window* target_window) {
  target_window_ = target_window;
  dc_ = GetDC(HWND(target_window_->native_handle()));

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
    FatalGLError("Unable to choose pixel format.");
    return false;
  }
  if (!SetPixelFormat(dc_, pixel_format, &pfd)) {
    FatalGLError("Unable to set pixel format.");
    return false;
  }

  HGLRC temp_context = wglCreateContext(dc_);
  if (!temp_context) {
    FatalGLError("Unable to create temporary GL context.");
    return false;
  }
  wglMakeCurrent(dc_, temp_context);

  tls_glew_context_ = glew_context_.get();
  tls_wglew_context_ = wglew_context_.get();
  if (glewInit() != GLEW_OK) {
    FatalGLError("Unable to initialize GLEW.");
    return false;
  }
  if (wglewInit() != GLEW_OK) {
    FatalGLError("Unable to initialize WGLEW.");
    return false;
  }

  if (!WGLEW_ARB_create_context) {
    FatalGLError("WGL_ARG_create_context not supported by GL ICD.");
    return false;
  }

  int context_flags = 0;
  if (FLAGS_gl_debug) {
    context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
  }

  int attrib_list[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                       4,
                       WGL_CONTEXT_MINOR_VERSION_ARB,
                       5,
                       WGL_CONTEXT_FLAGS_ARB,
                       context_flags,
                       WGL_CONTEXT_PROFILE_MASK_ARB,
                       WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                       0};

  glrc_ = wglCreateContextAttribsARB(dc_, nullptr, attrib_list);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(temp_context);
  if (!glrc_) {
    FatalGLError("Unable to create real GL context.");
    return false;
  }

  if (!MakeCurrent()) {
    FatalGLError("Could not make real GL context current.");
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
    FatalGLError("Unable to initialize blitter.");
    ClearCurrent();
    return false;
  }

  ClearCurrent();

  return true;
}

std::unique_ptr<GraphicsContext> GLContext::CreateShared() {
  assert_not_null(glrc_);

  HGLRC new_glrc = nullptr;
  {
    GraphicsContextLock context_lock(this);

    int context_flags = 0;
    if (FLAGS_gl_debug) {
      context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
    }

    int attrib_list[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                         4,
                         WGL_CONTEXT_MINOR_VERSION_ARB,
                         5,
                         WGL_CONTEXT_FLAGS_ARB,
                         context_flags,
                         WGL_CONTEXT_PROFILE_MASK_ARB,
                         WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                         0};
    new_glrc = wglCreateContextAttribsARB(dc_, glrc_, attrib_list);
    if (!new_glrc) {
      FatalGLError("Could not create shared context.");
      return nullptr;
    }
  }

  auto new_context =
      std::unique_ptr<GLContext>(new GLContext(target_window_, new_glrc));
  if (!new_context->MakeCurrent()) {
    FatalGLError("Could not make new GL context current.");
    return nullptr;
  }
  if (!glGetString(GL_EXTENSIONS)) {
    new_context->ClearCurrent();
    FatalGLError("New GL context did not have extensions.");
    return nullptr;
  }

  if (glewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    FatalGLError("Unable to initialize GLEW on shared context.");
    return nullptr;
  }
  if (wglewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    FatalGLError("Unable to initialize WGLEW on shared context.");
    return nullptr;
  }

  SetupDebugging();

  if (!new_context->blitter_.Initialize()) {
    FatalGLError("Unable to initialize blitter on shared context.");
    return nullptr;
  }

  new_context->ClearCurrent();

  return std::unique_ptr<GraphicsContext>(new_context.release());
}

void GLContext::AssertExtensionsPresent() {
  if (!MakeCurrent()) {
    FatalGLError("Unable to make GL context current.");
    return;
  }

  // Check shader version at least 4.5 (matching GL 4.5).
  auto glsl_version_raw =
      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  std::string glsl_version(glsl_version_raw);
  if (glsl_version.find("4.50") != 0) {
    XELOGW("GLSL version reported as %s; you may have a bad time!",
           glsl_version_raw);
  }

  if (!GLEW_ARB_bindless_texture || !glMakeTextureHandleResidentARB) {
    FatalGLError("OpenGL extension ARB_bindless_texture is required.");
    return;
  }

  if (!GLEW_ARB_fragment_coord_conventions) {
    FatalGLError(
        "OpenGL extension ARB_fragment_coord_conventions is required.");
    return;
  }
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

void GLAPIENTRY GLContext::DebugMessageThunk(GLenum source, GLenum type,
                                             GLuint id, GLenum severity,
                                             GLsizei length,
                                             const GLchar* message,
                                             GLvoid* user_param) {
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

std::unique_ptr<ProfilerDisplay> GLContext::CreateProfilerDisplay() {
  return std::make_unique<GLProfilerDisplay>(target_window_);
}

std::unique_ptr<el::graphics::Renderer> GLContext::CreateElementalRenderer() {
  return GL4ElementalRenderer::Create(this);
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
    FatalGLError("Unable to make GL context current.");
    return false;
  }
  tls_glew_context_ = glew_context_.get();
  tls_wglew_context_ = wglew_context_.get();
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

void GLContext::BeginSwap() {
  SCOPE_profile_cpu_i("gpu", "xe::ui::gl::GLContext::BeginSwap");
  float clear_color[] = {238 / 255.0f, 238 / 255.0f, 238 / 255.0f, 1.0f};
  if (FLAGS_random_clear_color) {
    clear_color[0] =
        rand() / static_cast<float>(RAND_MAX);  // NOLINT(runtime/threadsafe_fn)
    clear_color[1] = 1.0f;
    clear_color[2] = 0.0f;
    clear_color[3] = 1.0f;
  }
  glClearNamedFramebufferfv(0, GL_COLOR, 0, clear_color);
}

void GLContext::EndSwap() {
  SCOPE_profile_cpu_i("gpu", "xe::ui::gl::GLContext::EndSwap");
  SwapBuffers(dc());
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
