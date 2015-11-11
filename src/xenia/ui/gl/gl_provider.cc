/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl_provider.h"

#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace gl {

std::unique_ptr<GraphicsProvider> GLProvider::Create(Window* main_window) {
  std::unique_ptr<GLProvider> provider(new GLProvider(main_window));

  //

  return std::unique_ptr<GraphicsProvider>(provider.release());
}

GLProvider::GLProvider(Window* main_window) : GraphicsProvider(main_window) {}

GLProvider::~GLProvider() = default;

std::unique_ptr<GraphicsContext> GLProvider::CreateContext(
    Window* target_window) {
  auto share_context = main_window_->context();
  return std::unique_ptr<GraphicsContext>(
      GLContext::Create(this, target_window,
                        static_cast<GLContext*>(share_context))
          .release());
}

std::unique_ptr<GraphicsContext> GLProvider::CreateOffscreenContext() {
  auto share_context = main_window_->context();
  return std::unique_ptr<GraphicsContext>(
      GLContext::CreateOffscreen(this, static_cast<GLContext*>(share_context))
          .release());
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
