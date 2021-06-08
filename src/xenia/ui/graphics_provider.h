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
  enum class GpuVendorID {
    kAMD = 0x1002,
    kApple = 0x106B,
    kArm = 0x13B5,
    kImagination = 0x1010,
    kIntel = 0x8086,
    kMicrosoft = 0x1414,
    kNvidia = 0x10DE,
    kQualcomm = 0x5143,
  };

  virtual ~GraphicsProvider() = default;

  // The 'main' window of an application, used to query provider information.
  Window* main_window() const { return main_window_; }

  // Creates a new host-side graphics context and swapchain, possibly presenting
  // to a window and using the immediate drawer.
  virtual std::unique_ptr<GraphicsContext> CreateHostContext(
      Window* target_window) = 0;

  // Creates a new offscreen emulation graphics context.
  virtual std::unique_ptr<GraphicsContext> CreateEmulationContext() = 0;

 protected:
  explicit GraphicsProvider(Window* main_window) : main_window_(main_window) {}

  Window* main_window_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GRAPHICS_PROVIDER_H_
