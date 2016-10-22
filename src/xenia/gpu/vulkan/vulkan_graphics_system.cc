/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_swap_chain.h"
#include "xenia/ui/window.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanGraphicsSystem::VulkanGraphicsSystem() {}
VulkanGraphicsSystem::~VulkanGraphicsSystem() = default;

X_STATUS VulkanGraphicsSystem::Setup(cpu::Processor* processor,
                                     kernel::KernelState* kernel_state,
                                     ui::Window* target_window) {
  // Must create the provider so we can create contexts.
  provider_ = xe::ui::vulkan::VulkanProvider::Create(target_window);

  auto result = GraphicsSystem::Setup(processor, kernel_state, target_window);
  if (result) {
    return result;
  }

  display_context_ = reinterpret_cast<xe::ui::vulkan::VulkanContext*>(
      target_window->context());

  return X_STATUS_SUCCESS;
}

void VulkanGraphicsSystem::Shutdown() { GraphicsSystem::Shutdown(); }

std::unique_ptr<CommandProcessor>
VulkanGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new VulkanCommandProcessor(this, kernel_state_));
}

void VulkanGraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (!command_processor_) {
    return;
  }
  // Check for pending swap.
  auto& swap_state = command_processor_->swap_state();
  {
    std::lock_guard<std::mutex> lock(swap_state.mutex);
    if (swap_state.pending) {
      swap_state.pending = false;
    }
  }

  if (!swap_state.front_buffer_texture) {
    // Not yet ready.
    return;
  }

  auto semaphore = reinterpret_cast<VkSemaphore>(swap_state.backend_data);
  auto swap_chain = display_context_->swap_chain();
  auto copy_cmd_buffer = swap_chain->copy_cmd_buffer();
  auto front_buffer =
      reinterpret_cast<VkImage>(swap_state.front_buffer_texture);

  // Wait on and signal the swap semaphore.
  // TODO(DrChat): Interacting with the window causes the device to be lost in
  // some games.
  // swap_chain->WaitAndSignalSemaphore(semaphore);

  VkImageMemoryBarrier barrier;
  std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = front_buffer;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(copy_cmd_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  VkImageBlit region;
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffsets[0] = {0, 0, 0};
  region.srcOffsets[1] = {static_cast<int32_t>(swap_state.width),
                          static_cast<int32_t>(swap_state.height), 1};

  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstOffsets[0] = {0, 0, 0};
  region.dstOffsets[1] = {static_cast<int32_t>(swap_chain->surface_width()),
                          static_cast<int32_t>(swap_chain->surface_height()),
                          1};
  vkCmdBlitImage(copy_cmd_buffer, front_buffer, VK_IMAGE_LAYOUT_GENERAL,
                 swap_chain->surface_image(),
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                 VK_FILTER_LINEAR);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
