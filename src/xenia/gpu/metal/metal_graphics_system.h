/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_METAL_METAL_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "third_party/metal-cpp/Foundation/Foundation.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalGraphicsSystem : public GraphicsSystem {
 public:
  MetalGraphicsSystem();
  ~MetalGraphicsSystem() override;

  static bool IsAvailable();

  std::string name() const override;

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::WindowedAppContext* app_context,
                 bool is_surface_required) override;

  // Metal device and command queue access
  MTL::Device* metal_device() const { return metal_device_; }
  MTL::CommandQueue* metal_command_queue() const { return metal_command_queue_; }

 protected:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

 private:
  // Metal device resources
  MTL::Device* metal_device_ = nullptr;
  MTL::CommandQueue* metal_command_queue_ = nullptr;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_GRAPHICS_SYSTEM_H_
