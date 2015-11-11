/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_GL_PROVIDER_H_
#define XENIA_UI_GL_GL_PROVIDER_H_

#include <memory>

#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace ui {
namespace gl {

class GLProvider : public GraphicsProvider {
 public:
  ~GLProvider() override;

  static std::unique_ptr<GraphicsProvider> Create(Window* main_window);

  std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) override;

  std::unique_ptr<GraphicsContext> CreateOffscreenContext() override;

 protected:
  explicit GLProvider(Window* main_window);
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_PROVIDER_H_
