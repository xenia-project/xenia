/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl_context_win.h"

#include <gflags/gflags.h>

#include <mutex>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/gl/gl_immediate_drawer.h"
#include "xenia/ui/window.h"

#include "third_party/GL/wglew.h"

namespace xe {
namespace ui {
namespace gl {

thread_local GLEWContext* tls_glew_context_ = nullptr;
thread_local WGLEWContext* tls_wglew_context_ = nullptr;
extern "C" GLEWContext* glewGetContext() { return tls_glew_context_; }
extern "C" WGLEWContext* wglewGetContext() { return tls_wglew_context_; }

std::unique_ptr<GLContext> GLContext::Create(GraphicsProvider* provider,
                                             Window* target_window,
                                             GLContext* share_context) {
  auto context =
      std::unique_ptr<GLContext>(new WGLContext(provider, target_window));
  if (!context->Initialize(share_context)) {
    return nullptr;
  }
  context->AssertExtensionsPresent();
  return context;
}

std::unique_ptr<GLContext> GLContext::CreateOffscreen(
    GraphicsProvider* provider, GLContext* parent_context) {
  return WGLContext::CreateOffscreen(provider,
                                     static_cast<WGLContext*>(parent_context));
}

WGLContext::WGLContext(GraphicsProvider* provider, Window* target_window)
    : GLContext(provider, target_window) {
  glew_context_.reset(new GLEWContext());
  wglew_context_.reset(new WGLEWContext());
}

WGLContext::~WGLContext() {
  MakeCurrent();
  blitter_.Shutdown();
  immediate_drawer_.reset();
  ClearCurrent();
  if (glrc_) {
    wglDeleteContext(glrc_);
  }
  if (dc_) {
    ReleaseDC(HWND(target_window_->native_handle()), dc_);
  }
}

bool WGLContext::Initialize(GLContext* share_context_) {
  WGLContext* share_context = static_cast<WGLContext*>(share_context_);
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

  if (GLEW_ARB_robustness) {
    robust_access_supported_ = true;
  }

  int context_flags = 0;
  if (FLAGS_gl_debug) {
    context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
  }
  if (robust_access_supported_) {
    context_flags |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
  }

  int attrib_list[] = {
      WGL_CONTEXT_MAJOR_VERSION_ARB,
      4,
      WGL_CONTEXT_MINOR_VERSION_ARB,
      5,
      WGL_CONTEXT_FLAGS_ARB,
      context_flags,
      WGL_CONTEXT_PROFILE_MASK_ARB,
      WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
      WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
      robust_access_supported_ ? WGL_LOSE_CONTEXT_ON_RESET_ARB : 0,
      0};

  glrc_ = wglCreateContextAttribsARB(
      dc_, share_context ? share_context->glrc_ : nullptr, attrib_list);
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

  immediate_drawer_ = std::make_unique<GLImmediateDrawer>(this);

  ClearCurrent();

  return true;
}

std::unique_ptr<WGLContext> WGLContext::CreateOffscreen(
    GraphicsProvider* provider, WGLContext* parent_context) {
  assert_not_null(parent_context->glrc_);

  HGLRC new_glrc = nullptr;
  {
    GraphicsContextLock context_lock(parent_context);

    int context_flags = 0;
    if (FLAGS_gl_debug) {
      context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
    }

    bool robust_access_supported = parent_context->robust_access_supported_;
    if (robust_access_supported) {
      context_flags |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
    }

    int attrib_list[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB,
        4,
        WGL_CONTEXT_MINOR_VERSION_ARB,
        5,
        WGL_CONTEXT_FLAGS_ARB,
        context_flags,
        WGL_CONTEXT_PROFILE_MASK_ARB,
        WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
        WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
        robust_access_supported ? WGL_LOSE_CONTEXT_ON_RESET_ARB : 0,
        0};
    new_glrc = wglCreateContextAttribsARB(parent_context->dc_,
                                          parent_context->glrc_, attrib_list);
    if (!new_glrc) {
      FatalGLError("Could not create shared context.");
      return nullptr;
    }
  }

  auto new_context = std::unique_ptr<WGLContext>(
      new WGLContext(provider, parent_context->target_window_));
  new_context->glrc_ = new_glrc;
  new_context->dc_ =
      GetDC(HWND(parent_context->target_window_->native_handle()));
  new_context->robust_access_supported_ =
      parent_context->robust_access_supported_;
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

  new_context->SetupDebugging();

  if (!new_context->blitter_.Initialize()) {
    FatalGLError("Unable to initialize blitter on shared context.");
    return nullptr;
  }

  new_context->ClearCurrent();

  return new_context;
}

bool WGLContext::is_current() {
  return tls_glew_context_ == glew_context_.get();
}

bool WGLContext::MakeCurrent() {
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

void WGLContext::ClearCurrent() {
  if (!FLAGS_disable_gl_context_reset) {
    wglMakeCurrent(nullptr, nullptr);
  }
  tls_glew_context_ = nullptr;
  tls_wglew_context_ = nullptr;

  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.unlock();
  }
}

void WGLContext::BeginSwap() {
  SCOPE_profile_cpu_i("gpu", "xe::ui::gl::WGLContext::BeginSwap");
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

void WGLContext::EndSwap() {
  SCOPE_profile_cpu_i("gpu", "xe::ui::gl::WGLContext::EndSwap");
  SwapBuffers(dc_);
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
