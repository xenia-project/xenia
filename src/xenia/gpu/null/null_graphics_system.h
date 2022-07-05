/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_NULL_NULL_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_NULL_NULL_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"

namespace xe {
namespace gpu {
namespace null {

class NullGraphicsSystem : public GraphicsSystem {
 public:
  NullGraphicsSystem();
  ~NullGraphicsSystem() override;

  static bool IsAvailable() { return true; }

  std::string name() const override { return "null"; }

  X_STATUS Setup(Memory* memory, cpu::Processor* processor,
                 kernel::KernelState* kernel_state,
                 ui::WindowedAppContext* app_context,
                 bool is_surface_required) override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

}  // namespace null
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_NULL_NULL_GRAPHICS_SYSTEM_H_