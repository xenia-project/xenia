/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GRAPHICS_PROVIDER_H_
#define XENIA_UI_GRAPHICS_PROVIDER_H_

#include <memory>

namespace xe {
namespace ui {

class GraphicsContext;
class Window;

// Factory for graphics contexts.
// All contexts created by the same provider will be able to share resources
// according to the rules of the backing graphics API.
class GraphicsProvider {
 public:
  virtual ~GraphicsProvider() = default;

  // The 'main' window of an application, used to query provider information.
  Window* main_window() const { return main_window_; }

  // Creates a new graphics context and swapchain for presenting to a window.
  virtual std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) = 0;

  // Creates a new offscreen graphics context without a swapchain or immediate
  // drawer.
  virtual std::unique_ptr<GraphicsContext> CreateOffscreenContext() = 0;

 protected:
  explicit GraphicsProvider(Window* main_window) : main_window_(main_window) {}

  Window* main_window_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GRAPHICS_PROVIDER_H_
