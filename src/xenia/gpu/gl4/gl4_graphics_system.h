/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/graphics_system.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace gpu {
namespace gl4 {

class GL4GraphicsSystem : public GraphicsSystem {
 public:
  GL4GraphicsSystem();
  ~GL4GraphicsSystem() override;

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::Window* target_window) override;
  void Shutdown() override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  void Swap(xe::ui::UIEvent* e) override;

  xe::ui::gl::GLContext* display_context_ = nullptr;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_
