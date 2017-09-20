/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl_context_x11.h"

#include <gflags/gflags.h>

#include <gdk/gdkx.h>
#include <mutex>
#include <string>

#include "third_party/GL/glxew.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform_linux.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/gl/gl_immediate_drawer.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace gl {

thread_local GLEWContext* tls_glew_context_ = nullptr;
thread_local GLXEWContext* tls_glxew_context_ = nullptr;
extern "C" GLEWContext* glewGetContext() { return tls_glew_context_; }
extern "C" GLXEWContext* glxewGetContext() { return tls_glxew_context_; }

std::unique_ptr<GLContext> GLContext::Create(GraphicsProvider* provider,
                                             Window* target_window,
                                             GLContext* share_context) {
  auto context =
      std::unique_ptr<GLContext>(new GLXContext(provider, target_window));
  if (!context->Initialize(share_context)) {
    return nullptr;
  }
  context->AssertExtensionsPresent();
  return context;
}

std::unique_ptr<GLContext> GLContext::CreateOffscreen(
    GraphicsProvider* provider, GLContext* parent_context) {
  return GLXContext::CreateOffscreen(provider,
                                     static_cast<GLXContext*>(parent_context));
}

GLXContext::GLXContext(GraphicsProvider* provider, Window* target_window)
    : GLContext(provider, target_window) {
  glew_context_.reset(new GLEWContext());
  glxew_context_.reset(new GLXEWContext());
}

GLXContext::~GLXContext() {
  MakeCurrent();
  blitter_.Shutdown();
  immediate_drawer_.reset();
  ClearCurrent();
  if (glx_context_) {
    glXDestroyContext(disp_, glx_context_);
  }
  if (draw_area_) {
    gtk_widget_destroy(draw_area_);
  }
}

bool GLXContext::Initialize(GLContext* share_context) {
  GtkWidget* window = GTK_WIDGET(target_window_->native_handle());
  GtkWidget* draw_area = gtk_drawing_area_new();
  int32_t width;
  int32_t height;
  gtk_window_get_size(GTK_WINDOW(window), &width, &height);
  gtk_widget_set_size_request(draw_area, width, height);
  gtk_container_add(GTK_CONTAINER(window), draw_area);
  GdkVisual* visual = gdk_screen_get_system_visual(gdk_screen_get_default());

  GdkDisplay* gdk_display = gtk_widget_get_display(window);
  Display* display = gdk_x11_display_get_xdisplay(gdk_display);
  disp_ = display;
  ::Window root = gdk_x11_get_default_root_xwindow();
  static int vis_attrib_list[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24,
                                  GLX_DOUBLEBUFFER, None};
  XVisualInfo* vi = glXChooseVisual(display, 0, vis_attrib_list);
  if (vi == NULL) {
    FatalGLError("No matching visuals for X display");
    return false;
  }

  cmap_ = XCreateColormap(display, root, vi->visual, AllocNone);

  ::GLXContext temp_context = glXCreateContext(display, vi, NULL, GL_TRUE);
  if (!temp_context) {
    FatalGLError("Unable to create temporary GLX context");
    return false;
  }
  xid_ = GDK_WINDOW_XID(gtk_widget_get_window(window));
  glXMakeCurrent(display, xid_, temp_context);

  tls_glew_context_ = glew_context_.get();
  tls_glxew_context_ = glxew_context_.get();
  if (glewInit() != GLEW_OK) {
    FatalGLError("Unable to initialize GLEW.");
    return false;
  }
  if (glxewInit() != GLEW_OK) {
    FatalGLError("Unable to initialize GLXEW.");
    return false;
  }

  if (!GLXEW_ARB_create_context) {
    FatalGLError("GLX_ARB_create_context not supported by GL ICD.");
    return false;
  }

  if (GLEW_ARB_robustness) {
    robust_access_supported_ = true;
  }

  int context_flags = 0;
  if (FLAGS_gl_debug) {
    context_flags |= GLX_CONTEXT_DEBUG_BIT_ARB;
  }
  if (robust_access_supported_) {
    context_flags |= GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
  }

  int attrib_list[] = {
      GLX_CONTEXT_MAJOR_VERSION_ARB,
      4,
      GLX_CONTEXT_MINOR_VERSION_ARB,
      5,
      GLX_CONTEXT_FLAGS_ARB,
      context_flags,
      GLX_CONTEXT_PROFILE_MASK_ARB,
      GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
      GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
      robust_access_supported_ ? GLX_LOSE_CONTEXT_ON_RESET_ARB : 0,
      0};
  GLXContext* share_context_glx = static_cast<GLXContext*>(share_context);
  glx_context_ = glXCreateContextAttribsARB(
      display, nullptr,
      share_context ? share_context_glx->glx_context_ : nullptr, True,
      attrib_list);
  glXMakeCurrent(display, 0, nullptr);
  glXDestroyContext(display, temp_context);
  if (!glx_context_) {
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

std::unique_ptr<GLXContext> GLXContext::CreateOffscreen(
    GraphicsProvider* provider, GLXContext* parent_context) {
  assert_not_null(parent_context->glx_context_);

  ::GLXContext new_glrc;
  {
    GraphicsContextLock context_lock(parent_context);

    int context_flags = 0;
    if (FLAGS_gl_debug) {
      context_flags |= GLX_CONTEXT_DEBUG_BIT_ARB;
    }

    bool robust_access_supported = parent_context->robust_access_supported_;
    if (robust_access_supported) {
      context_flags |= GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
    }

    int attrib_list[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB,
        4,
        GLX_CONTEXT_MINOR_VERSION_ARB,
        5,
        GLX_CONTEXT_FLAGS_ARB,
        context_flags,
        GLX_CONTEXT_PROFILE_MASK_ARB,
        GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
        GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
        robust_access_supported ? GLX_LOSE_CONTEXT_ON_RESET_ARB : 0,
        0};
    new_glrc = glXCreateContextAttribsARB(parent_context->disp_, nullptr,
                                          parent_context->glx_context_, True,
                                          attrib_list);
    if (!new_glrc) {
      FatalGLError("Could not create shared context.");
      return nullptr;
    }
  }

  auto new_context = std::unique_ptr<GLXContext>(
      new GLXContext(provider, parent_context->target_window_));
  new_context->glx_context_ = new_glrc;
  new_context->window_ = parent_context->window_;
  new_context->draw_area_ = parent_context->draw_area_;
  new_context->disp_ = parent_context->disp_;
  new_context->xid_ = parent_context->xid_;
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
  if (glxewInit() != GLEW_OK) {
    new_context->ClearCurrent();
    FatalGLError("Unable to initialize GLXEW on shared context.");
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

bool GLXContext::is_current() {
  return tls_glew_context_ == glew_context_.get();
}

bool GLXContext::MakeCurrent() {
  SCOPE_profile_cpu_f("gpu");
  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.lock();
  }

  if (!glXMakeCurrent(disp_, xid_, glx_context_)) {
    if (FLAGS_thread_safe_gl) {
      global_gl_mutex_.unlock();
    }
    FatalGLError("Unable to make GL context current.");
    return false;
  }
  tls_glew_context_ = glew_context_.get();
  tls_glxew_context_ = glxew_context_.get();
  return true;
}

void GLXContext::ClearCurrent() {
  if (!FLAGS_disable_gl_context_reset) {
    glXMakeCurrent(disp_, 0, nullptr);
  }
  tls_glew_context_ = nullptr;
  tls_glxew_context_ = nullptr;

  if (FLAGS_thread_safe_gl) {
    global_gl_mutex_.unlock();
  }
}

void GLXContext::BeginSwap() {
  SCOPE_profile_cpu_i("gpu", "xe::ui::gl::GLXContext::BeginSwap");
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

void GLXContext::EndSwap() {
  SCOPE_profile_cpu_i("gpu", "xe::ui::gl::GLXContext::EndSwap");
  glXSwapBuffers(disp_, xid_);
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
