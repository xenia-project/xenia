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

  static bool IsAvailable() { return true; }

  std::wstring name() const override { return L"Vulkan"; }

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::Window* target_window) override;
  void Shutdown() override;

  std::unique_ptr<xe::ui::RawImage> Capture() override;

 private:
  VkResult CreateCaptureBuffer(VkCommandBuffer cmd, VkExtent2D extents);
  void DestroyCaptureBuffer();

  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
  void Swap(xe::ui::UIEvent* e) override;

  xe::ui::vulkan::VulkanDevice* device_ = nullptr;
  xe::ui::vulkan::VulkanContext* display_context_ = nullptr;

  VkCommandPool command_pool_ = nullptr;

  VkBuffer capture_buffer_ = nullptr;
  VkDeviceMemory capture_buffer_memory_ = nullptr;
  VkDeviceSize capture_buffer_size_ = 0;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_GRAPHICS_SYSTEM_H_
