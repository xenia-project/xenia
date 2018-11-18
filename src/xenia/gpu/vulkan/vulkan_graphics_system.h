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

struct SwapState : public gpu::SwapState {
  // front buffer / back buffer memory
  struct BufferResources {
    VkDeviceMemory buf_memory = nullptr;
    VkImageView buf_image_view = nullptr;
    VkImageLayout buf_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFramebuffer buf_framebuffer = nullptr;  // Used and created by CP.
    VkFence buf_fence = nullptr;              // Completion fence. Used by CP.
  } buffer_resources[kNumSwapBuffers];
};

class VulkanGraphicsSystem : public GraphicsSystem {
 public:
  VulkanGraphicsSystem();
  ~VulkanGraphicsSystem() override;

  std::wstring name() const override { return L"Vulkan"; }
  SwapState* swap_state() override { return &swap_state_; }

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 std::unique_ptr<ui::GraphicsContext> context) override;
  void Shutdown() override;

  std::unique_ptr<xe::ui::RawImage> Capture() override;

 private:
  VkResult CreateCaptureBuffer(VkCommandBuffer cmd, VkExtent2D extents);
  void DestroyCaptureBuffer();

  VkResult CreateSwapImage(VkExtent2D extents, uintptr_t* image_out,
                           SwapState::BufferResources* buffer_resources);
  void DestroySwapImage(uintptr_t* image,
                        SwapState::BufferResources* buffer_resources);

  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  ui::vulkan::VulkanDevice* device_ = nullptr;
  ui::vulkan::VulkanContext* display_context_ = nullptr;

  SwapState swap_state_;

  VkCommandPool command_pool_ = nullptr;

  VkBuffer capture_buffer_ = nullptr;
  VkDeviceMemory capture_buffer_memory_ = nullptr;
  VkDeviceSize capture_buffer_size_ = 0;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_GRAPHICS_SYSTEM_H_
