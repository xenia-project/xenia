/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VK_VULKAN_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_VK_VULKAN_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/ui/vk/vulkan_context.h"

namespace xe {
namespace gpu {
namespace vk {

class VulkanGraphicsSystem : public GraphicsSystem {
 public:
  VulkanGraphicsSystem();
  ~VulkanGraphicsSystem() override;

  static bool IsAvailable() { return true; }

  std::wstring name() const override;

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::Window* target_window) override;
  void Shutdown() override;

 protected:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  void Swap(xe::ui::UIEvent* e) override;

 private:
  ui::vk::VulkanContext* display_context_ = nullptr;
};

}  // namespace vk
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VK_VULKAN_GRAPHICS_SYSTEM_H_
