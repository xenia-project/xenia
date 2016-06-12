/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_VULKAN_VULKAN_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/graphics_system.h"
#include "xenia/ui/vulkan/vulkan_context.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanGraphicsSystem : public GraphicsSystem {
 public:
  VulkanGraphicsSystem();
  ~VulkanGraphicsSystem() override;

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::Window* target_window) override;
  void Shutdown() override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  void Swap(xe::ui::UIEvent* e) override;

  xe::ui::vulkan::VulkanContext* display_context_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_GRAPHICS_SYSTEM_H_
