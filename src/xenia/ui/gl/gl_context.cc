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
#include "xenia/base/profiling.h"
#include "xenia/ui/gl/gl_immediate_drawer.h"
#include "xenia/ui/window.h"

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

std::recursive_mutex GLContext::global_gl_mutex_;

void GLContext::FatalGLError(std::string error) {
  xe::FatalError(
      error +
      "\nEnsure you have the latest drivers for your GPU and that it supports "
      "OpenGL 4.5. See http://xenia.jp/faq/ for more information and a list"
      "of supported GPUs.");
}

GLContext::GLContext(GraphicsProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

GLContext::~GLContext() {}

void GLContext::AssertExtensionsPresent() {
  if (!MakeCurrent()) {
    FatalGLError("Unable to make GL context current.");
    return;
  }

  // Check shader version at least 4.5 (matching GL 4.5).
  auto glsl_version_raw =
      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  std::string glsl_version(glsl_version_raw);
  if (glsl_version.find("4.5") == std::string::npos &&
      glsl_version.find("4.6") == std::string::npos) {
    FatalGLError("OpenGL GLSL version 4.50 or higher is required.");
    return;
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

  ClearCurrent();
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

ImmediateDrawer* GLContext::immediate_drawer() {
  return immediate_drawer_.get();
}

bool GLContext::WasLost() {
  if (!robust_access_supported_) {
    // Can't determine if we lost the context.
    return false;
  }

  if (context_lost_) {
    return true;
  }

  auto status = glGetGraphicsResetStatusARB();
  if (status != GL_NO_ERROR) {
    // Graphics card reset.
    XELOGE("============= TDR detected on context %p! Context %s =============",
           handle(), status == GL_GUILTY_CONTEXT_RESET ? "guilty" : "innocent");
    context_lost_ = true;
    return true;
  }

  return false;
}

std::unique_ptr<RawImage> GLContext::Capture() {
  GraphicsContextLock lock(this);

  std::unique_ptr<RawImage> raw_image(new RawImage());
  raw_image->width = target_window_->width();
  raw_image->stride = raw_image->width * 4;
  raw_image->height = target_window_->height();
  raw_image->data.resize(raw_image->stride * raw_image->height);

  glReadPixels(0, 0, target_window_->width(), target_window_->height(), GL_RGBA,
               GL_UNSIGNED_BYTE, raw_image->data.data());

  // Flip vertically in-place.
  size_t yt = 0;
  size_t yb = (raw_image->height - 1) * raw_image->stride;
  while (yt < yb) {
    for (size_t i = 0; i < raw_image->stride; ++i) {
      std::swap(raw_image->data[yt + i], raw_image->data[yb + i]);
    }
    yt += raw_image->stride;
    yb -= raw_image->stride;
  }

  return raw_image;
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
