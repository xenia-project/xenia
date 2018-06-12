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
#include "xenia/ui/vulkan/vulkan_context.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::RawImage;
using xe::ui::vulkan::CheckResult;

VulkanGraphicsSystem::VulkanGraphicsSystem() {}
VulkanGraphicsSystem::~VulkanGraphicsSystem() = default;

X_STATUS VulkanGraphicsSystem::Setup(
    cpu::Processor* processor, kernel::KernelState* kernel_state,
    std::unique_ptr<ui::GraphicsContext> context) {
  VkResult status;
  device_ = static_cast<ui::vulkan::VulkanContext*>(context.get())->device();

  auto result =
      GraphicsSystem::Setup(processor, kernel_state, std::move(context));
  if (result) {
    return result;
  }

  // Create our own command pool we can use for captures.
  VkCommandPoolCreateInfo create_info = {
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      nullptr,
      VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
          VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      device_->queue_family_index(),
  };
  status = vkCreateCommandPool(*device_, &create_info, nullptr, &command_pool_);
  CheckResult(status, "vkCreateCommandPool");

  for (uint32_t i = 0; i < kNumSwapBuffers; i++) {
    // TODO(DrChat): Don't hardcode this resolution.
    status = CreateSwapImage({1280, 720}, &swap_state_.buffer_textures[i],
                             &swap_state_.buffer_resources[i]);
    if (status != VK_SUCCESS) {
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  return X_STATUS_SUCCESS;
}

void VulkanGraphicsSystem::Shutdown() {
  GraphicsSystem::Shutdown();

  for (uint32_t i = 0; i < kNumSwapBuffers; i++) {
    DestroySwapImage(&swap_state_.buffer_textures[i],
                     &swap_state_.buffer_resources[i]);
  }
  VK_SAFE_DESTROY(vkDestroyCommandPool, *device_, command_pool_, nullptr);
}

std::unique_ptr<RawImage> VulkanGraphicsSystem::Capture() {
  std::lock_guard<std::mutex> lock(swap_state_.mutex);
  VkResult status = VK_SUCCESS;

  VkCommandBufferAllocateInfo alloc_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      nullptr,
      command_pool_,
      VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      1,
  };

  VkCommandBuffer cmd = nullptr;
  status = vkAllocateCommandBuffers(*device_, &alloc_info, &cmd);
  CheckResult(status, "vkAllocateCommandBuffers");
  if (status != VK_SUCCESS) {
    return nullptr;
  }

  VkCommandBufferBeginInfo begin_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      nullptr,
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      nullptr,
  };
  vkBeginCommandBuffer(cmd, &begin_info);

  auto front_buffer = reinterpret_cast<VkImage>(
      swap_state_.buffer_textures[swap_state_.current_buffer]);

  status = CreateCaptureBuffer(cmd, {swap_state_.width, swap_state_.height});
  if (status != VK_SUCCESS) {
    vkFreeCommandBuffers(*device_, command_pool_, 1, &cmd);
    return nullptr;
  }

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
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Copy front buffer into capture image.
  VkBufferImageCopy region = {
      0,         0,
      0,         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      {0, 0, 0}, {swap_state_.width, swap_state_.height, 1},
  };

  vkCmdCopyImageToBuffer(cmd, front_buffer, VK_IMAGE_LAYOUT_GENERAL,
                         capture_buffer_, 1, &region);

  VkBufferMemoryBarrier memory_barrier = {
      VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      capture_buffer_,
      0,
      VK_WHOLE_SIZE,
  };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 1,
                       &memory_barrier, 0, nullptr);

  status = vkEndCommandBuffer(cmd);

  // Submit commands and wait.
  if (status == VK_SUCCESS) {
    std::lock_guard<std::mutex>(device_->primary_queue_mutex());
    VkSubmitInfo submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &cmd,
        0,
        nullptr,
    };
    status = vkQueueSubmit(device_->primary_queue(), 1, &submit_info, nullptr);
    CheckResult(status, "vkQueueSubmit");

    if (status == VK_SUCCESS) {
      status = vkQueueWaitIdle(device_->primary_queue());
      CheckResult(status, "vkQueueWaitIdle");
    }
  }

  vkFreeCommandBuffers(*device_, command_pool_, 1, &cmd);

  void* data;
  if (status == VK_SUCCESS) {
    status = vkMapMemory(*device_, capture_buffer_memory_, 0, VK_WHOLE_SIZE, 0,
                         &data);
    CheckResult(status, "vkMapMemory");
  }

  if (status == VK_SUCCESS) {
    std::unique_ptr<RawImage> raw_image(new RawImage());
    raw_image->width = swap_state_.width;
    raw_image->height = swap_state_.height;
    raw_image->stride = swap_state_.width * 4;
    raw_image->data.resize(raw_image->stride * raw_image->height);

    std::memcpy(raw_image->data.data(), data,
                raw_image->stride * raw_image->height);

    vkUnmapMemory(*device_, capture_buffer_memory_);
    DestroyCaptureBuffer();
    return raw_image;
  }

  DestroyCaptureBuffer();
  return nullptr;
}

VkResult VulkanGraphicsSystem::CreateCaptureBuffer(VkCommandBuffer cmd,
                                                   VkExtent2D extents) {
  VkResult status = VK_SUCCESS;

  VkBufferCreateInfo buffer_info = {
      VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      nullptr,
      0,
      extents.width * extents.height * 4,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0,
      nullptr,
  };
  status = vkCreateBuffer(*device_, &buffer_info, nullptr, &capture_buffer_);
  if (status != VK_SUCCESS) {
    return status;
  }

  capture_buffer_size_ = extents.width * extents.height * 4;

  // Bind memory to buffer.
  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(*device_, capture_buffer_, &mem_requirements);
  capture_buffer_memory_ = device_->AllocateMemory(
      mem_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  assert_not_null(capture_buffer_memory_);

  status =
      vkBindBufferMemory(*device_, capture_buffer_, capture_buffer_memory_, 0);
  CheckResult(status, "vkBindImageMemory");
  if (status != VK_SUCCESS) {
    vkDestroyBuffer(*device_, capture_buffer_, nullptr);
    return status;
  }

  return status;
}

void VulkanGraphicsSystem::DestroyCaptureBuffer() {
  vkDestroyBuffer(*device_, capture_buffer_, nullptr);
  vkFreeMemory(*device_, capture_buffer_memory_, nullptr);
  capture_buffer_ = nullptr;
  capture_buffer_memory_ = nullptr;
  capture_buffer_size_ = 0;
}

VkResult VulkanGraphicsSystem::CreateSwapImage(
    VkExtent2D extents, uintptr_t* image_out,
    SwapState::BufferResources* buffer_resources) {
  VkResult status;

  VkFenceCreateInfo fence_info = {
      VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      nullptr,
      VK_FENCE_CREATE_SIGNALED_BIT,
  };
  status = vkCreateFence(*device_, &fence_info, nullptr,
                         &buffer_resources->buf_fence);

  VkImageCreateInfo image_info;
  std::memset(&image_info, 0, sizeof(VkImageCreateInfo));
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent = {extents.width, extents.height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image_buf;
  status = vkCreateImage(*device_, &image_info, nullptr, &image_buf);
  CheckResult(status, "vkCreateImage");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Bind memory to image.
  VkMemoryRequirements mem_requirements;
  vkGetImageMemoryRequirements(*device_, image_buf, &mem_requirements);
  buffer_resources->buf_memory = device_->AllocateMemory(mem_requirements, 0);
  assert_not_null(buffer_resources->buf_memory);

  status =
      vkBindImageMemory(*device_, image_buf, buffer_resources->buf_memory, 0);
  CheckResult(status, "vkBindImageMemory");
  if (status != VK_SUCCESS) {
    return status;
  }

  std::lock_guard<std::mutex> lock(swap_state_.mutex);
  *image_out = reinterpret_cast<uintptr_t>(image_buf);
  swap_state_.width = extents.width;
  swap_state_.height = extents.height;
  buffer_resources->buf_image_layout = image_info.initialLayout;

  VkImageViewCreateInfo view_create_info = {
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      nullptr,
      0,
      image_buf,
      VK_IMAGE_VIEW_TYPE_2D,
      VK_FORMAT_R8G8B8A8_UNORM,
      {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
       VK_COMPONENT_SWIZZLE_A},
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
  };
  status = vkCreateImageView(*device_, &view_create_info, nullptr,
                             &buffer_resources->buf_image_view);
  CheckResult(status, "vkCreateImageView");
  if (status != VK_SUCCESS) {
    return status;
  }

  return VK_SUCCESS;
}

void VulkanGraphicsSystem::DestroySwapImage(
    uintptr_t* image, SwapState::BufferResources* buffer_resources) {
  VK_SAFE_DESTROY(vkDestroyFence, *device_, buffer_resources->buf_fence,
                  nullptr);
  VK_SAFE_DESTROY(vkDestroyFramebuffer, *device_,
                  buffer_resources->buf_framebuffer, nullptr);
  VK_SAFE_DESTROY(vkDestroyImageView, *device_,
                  buffer_resources->buf_image_view, nullptr);

  VK_SAFE_DESTROY(vkFreeMemory, *device_, buffer_resources->buf_memory,
                  nullptr);
  if (image) {
    vkDestroyImage(*device_, reinterpret_cast<VkImage>(image), nullptr);
  }
}

std::unique_ptr<CommandProcessor>
VulkanGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<VulkanCommandProcessor>(this, kernel_state_);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
