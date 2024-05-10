/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_presenter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_util.h"

#if XE_PLATFORM_ANDROID
#include "xenia/ui/surface_android.h"
#endif
#if XE_PLATFORM_GNU_LINUX
#include "xenia/ui/surface_gnulinux.h"
#endif
#if XE_PLATFORM_WIN32
#include "xenia/ui/surface_win.h"
#endif

// Note: If the priorities in the description are changed, update the actual
// present mode selection logic.
DEFINE_bool(
    vulkan_allow_present_mode_immediate, true,
    "When available, allow the immediate presentation mode (1st priority), "
    "offering the lowest latency with the possibility of tearing in certain "
    "cases, and, depending on the configuration, variable refresh rate.",
    "Vulkan");
DEFINE_bool(
    vulkan_allow_present_mode_mailbox, true,
    "When available, allow the mailbox presentation mode (2nd priority), "
    "offering low latency without the possibility of tearing.",
    "Vulkan");
DEFINE_bool(
    vulkan_allow_present_mode_fifo_relaxed, true,
    "When available, allow the relaxed first-in-first-out presentation mode "
    "(3rd priority), which causes waiting for host display vertical sync, but "
    "may present with tearing if frames don't meet the host display refresh "
    "rate.",
    "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_bilinear_dither_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_bilinear_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_cas_resample_dither_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_cas_resample_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_cas_sharpen_dither_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_cas_sharpen_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_fsr_easu_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_fsr_rcas_dither_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_ffx_fsr_rcas_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/guest_output_triangle_strip_rect_vs.h"
}  // namespace shaders

VulkanPresenter::PaintContext::Submission::~Submission() {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  if (draw_command_pool_ != VK_NULL_HANDLE) {
    dfn.vkDestroyCommandPool(device, draw_command_pool_, nullptr);
  }

  if (present_semaphore_ != VK_NULL_HANDLE) {
    dfn.vkDestroySemaphore(device, present_semaphore_, nullptr);
  }
  if (acquire_semaphore_ != VK_NULL_HANDLE) {
    dfn.vkDestroySemaphore(device, acquire_semaphore_, nullptr);
  }
}

bool VulkanPresenter::PaintContext::Submission::Initialize() {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkSemaphoreCreateInfo semaphore_create_info;
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_create_info.pNext = nullptr;
  semaphore_create_info.flags = 0;
  if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                            &acquire_semaphore_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create a swapchain image acquisition "
        "semaphore");
    return false;
  }
  if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                            &present_semaphore_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create a swapchain image presentation "
        "semaphore");
    return false;
  }

  VkCommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.pNext = nullptr;
  command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_create_info.queueFamilyIndex =
      provider_.queue_family_graphics_compute();
  if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                              &draw_command_pool_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create a command pool for drawing to a "
        "swapchain");
    return false;
  }
  VkCommandBufferAllocateInfo command_buffer_allocate_info;
  command_buffer_allocate_info.sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.pNext = nullptr;
  command_buffer_allocate_info.commandPool = draw_command_pool_;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;
  if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                   &draw_command_buffer_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to allocate a command buffer for drawing to a "
        "swapchain");
    return false;
  }

  return true;
}

VulkanPresenter::~VulkanPresenter() {
  // Destroy the swapchain after its images are not used for drawing anymore.
  // This is a confusing part in Vulkan, as vkQueuePresentKHR doesn't signal a
  // fence clearly indicating when it's safe to destroy a swapchain, so we
  // assume that its lifetime is tracked internally in the WSI. This is also
  // done before destroying the semaphore awaited by vkQueuePresentKHR, hoping
  // that it will prevent the destruction during the semaphore wait in
  // vkQueuePresentKHR execution (or between the vkQueueSubmit semaphore signal
  // and the vkQueuePresentKHR semaphore wait).
  // This will await completion of all paint submissions also.
  paint_context_.DestroySwapchainAndVulkanSurface();

  // Await completion of the usage of everything before destroying anything
  // (paint submission completion already awaited).
  // From most likely the latest to most likely the earliest to be signaled, so
  // just one sleep will likely be needed.
  ui_submission_tracker_.Shutdown();
  guest_output_image_refresher_submission_tracker_.Shutdown();

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  if (paint_context_.swapchain_render_pass != VK_NULL_HANDLE) {
    dfn.vkDestroyRenderPass(device, paint_context_.swapchain_render_pass,
                            nullptr);
  }

  for (const PaintContext::UISetupCommandBuffer& ui_setup_command_buffer :
       paint_context_.ui_setup_command_buffers) {
    dfn.vkDestroyCommandPool(device, ui_setup_command_buffer.command_pool,
                             nullptr);
  }

  for (VkFramebuffer& framebuffer :
       paint_context_.guest_output_intermediate_framebuffers) {
    util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device, framebuffer);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                             paint_context_.guest_output_descriptor_pool);
  for (PaintContext::GuestOutputPaintPipeline& guest_output_paint_pipeline :
       paint_context_.guest_output_paint_pipelines) {
    util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                               guest_output_paint_pipeline.swapchain_pipeline);
    util::DestroyAndNullHandle(
        dfn.vkDestroyPipeline, device,
        guest_output_paint_pipeline.intermediate_pipeline);
  }

  util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                             guest_output_intermediate_render_pass_);
  for (VkShaderModule& shader_module : guest_output_paint_fs_) {
    util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device,
                               shader_module);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device,
                             guest_output_paint_vs_);
  for (VkPipelineLayout& pipeline_layout :
       guest_output_paint_pipeline_layouts_) {
    util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                               pipeline_layout);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                             guest_output_paint_image_descriptor_set_layout_);
}

Surface::TypeFlags VulkanPresenter::GetSupportedSurfaceTypes() const {
  if (!provider_.device_info().ext_VK_KHR_swapchain) {
    return 0;
  }
  return GetSurfaceTypesSupportedByInstance(provider_.instance_extensions());
}

bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out) {
  std::shared_ptr<GuestOutputImage> guest_output_image;
  {
    uint32_t guest_output_mailbox_index;
    std::unique_lock<std::mutex> guest_output_consumer_lock(
        ConsumeGuestOutput(guest_output_mailbox_index, nullptr, nullptr));
    if (guest_output_mailbox_index != UINT32_MAX) {
      assert_true(guest_output_images_[guest_output_mailbox_index]
                      .ever_successfully_refreshed);
      guest_output_image =
          guest_output_images_[guest_output_mailbox_index].image;
    }
    // Incremented the reference count of the guest output image - safe to leave
    // the consumer critical section now.
  }
  if (!guest_output_image) {
    return false;
  }

  VkExtent2D image_extent = guest_output_image->extent();
  size_t pixel_count = size_t(image_extent.width) * image_extent.height;
  VkDeviceSize buffer_size = VkDeviceSize(sizeof(uint32_t) * pixel_count);
  VkBuffer buffer;
  VkDeviceMemory buffer_memory;
  if (!util::CreateDedicatedAllocationBuffer(
          provider_, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          util::MemoryPurpose::kReadback, buffer, buffer_memory)) {
    XELOGE("VulkanPresenter: Failed to create the guest output capture buffer");
    return false;
  }

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  {
    VkCommandPoolCreateInfo command_pool_create_info;
    command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.pNext = nullptr;
    command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    command_pool_create_info.queueFamilyIndex =
        provider_.queue_family_graphics_compute();
    VkCommandPool command_pool;
    if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                                &command_pool) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to create the guest output capturing "
          "command pool");
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info;
    command_buffer_allocate_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.pNext = nullptr;
    command_buffer_allocate_info.commandPool = command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer;
    if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                     &command_buffer) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to allocate the guest output capturing "
          "command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    if (dfn.vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) !=
        VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to begin recording the guest output "
          "capturing command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkImageMemoryBarrier image_memory_barrier;
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = nullptr;
    image_memory_barrier.srcAccessMask = kGuestOutputInternalAccessMask;
    image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_memory_barrier.oldLayout = kGuestOutputInternalLayout;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.image = guest_output_image->image();
    image_memory_barrier.subresourceRange = util::InitializeSubresourceRange();
    dfn.vkCmdPipelineBarrier(command_buffer, kGuestOutputInternalStageMask,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &image_memory_barrier);

    VkBufferImageCopy buffer_image_copy = {};
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageExtent.width = image_extent.width;
    buffer_image_copy.imageExtent.height = image_extent.height;
    buffer_image_copy.imageExtent.depth = 1;
    dfn.vkCmdCopyImageToBuffer(command_buffer, guest_output_image->image(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                               &buffer_image_copy);

    // A fence doesn't guarantee host visibility and availability.
    VkBufferMemoryBarrier buffer_memory_barrier;
    buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_memory_barrier.pNext = nullptr;
    buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_memory_barrier.buffer = buffer;
    buffer_memory_barrier.offset = 0;
    buffer_memory_barrier.size = VK_WHOLE_SIZE;
    std::swap(image_memory_barrier.srcAccessMask,
              image_memory_barrier.dstAccessMask);
    std::swap(image_memory_barrier.oldLayout, image_memory_barrier.newLayout);
    dfn.vkCmdPipelineBarrier(
        command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT | kGuestOutputInternalStageMask, 0, 0,
        nullptr, 1, &buffer_memory_barrier, 1, &image_memory_barrier);

    if (dfn.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to end recording the guest output capturing "
          "command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    VulkanSubmissionTracker submission_tracker(provider_);
    {
      VulkanSubmissionTracker::FenceAcquisition fence_acqusition(
          submission_tracker.AcquireFenceToAdvanceSubmission());
      if (!fence_acqusition.fence()) {
        XELOGE(
            "VulkanPresenter: Failed to acquire a fence for guest output "
            "capturing");
        fence_acqusition.SubmissionFailedOrDropped();
        dfn.vkDestroyCommandPool(device, command_pool, nullptr);
        dfn.vkDestroyBuffer(device, buffer, nullptr);
        dfn.vkFreeMemory(device, buffer_memory, nullptr);
        return false;
      }
      VkResult submit_result;
      {
        VulkanProvider::QueueAcquisition queue_acquisition(
            provider_.AcquireQueue(provider_.queue_family_graphics_compute(),
                                   0));
        submit_result = dfn.vkQueueSubmit(
            queue_acquisition.queue, 1, &submit_info, fence_acqusition.fence());
      }
      if (submit_result != VK_SUCCESS) {
        XELOGE(
            "VulkanPresenter: Failed to submit the guest output capturing "
            "command buffer");
        fence_acqusition.SubmissionFailedOrDropped();
        dfn.vkDestroyCommandPool(device, command_pool, nullptr);
        dfn.vkDestroyBuffer(device, buffer, nullptr);
        dfn.vkFreeMemory(device, buffer_memory, nullptr);
        return false;
      }
    }
    if (!submission_tracker.AwaitAllSubmissionsCompletion()) {
      XELOGE(
          "VulkanPresenter: Failed to await the guest output capturing fence");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    dfn.vkDestroyCommandPool(device, command_pool, nullptr);
  }

  // Don't need the buffer anymore, just its memory.
  dfn.vkDestroyBuffer(device, buffer, nullptr);

  void* mapping;
  if (dfn.vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapping) !=
      VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to map the guest output capture memory");
    dfn.vkFreeMemory(device, buffer_memory, nullptr);
    return false;
  }

  image_out.width = image_extent.width;
  image_out.height = image_extent.height;
  image_out.stride = sizeof(uint32_t) * image_extent.width;
  image_out.data.resize(size_t(buffer_size));
  uint32_t* image_out_pixels =
      reinterpret_cast<uint32_t*>(image_out.data.data());
  for (size_t i = 0; i < pixel_count; ++i) {
    image_out_pixels[i] = Packed10bpcRGBTo8bpcBytes(
        reinterpret_cast<const uint32_t*>(mapping)[i]);
  }

  // Unmapping will be done by freeing.
  dfn.vkFreeMemory(device, buffer_memory, nullptr);

  return true;
}

VkCommandBuffer VulkanPresenter::AcquireUISetupCommandBufferFromUIThread() {
  if (paint_context_.ui_setup_command_buffer_current_index != SIZE_MAX) {
    return paint_context_
        .ui_setup_command_buffers[paint_context_
                                      .ui_setup_command_buffer_current_index]
        .command_buffer;
  }

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;

  // Try to reuse an existing command buffer.
  if (!paint_context_.ui_setup_command_buffers.empty()) {
    uint64_t submission_index_completed =
        ui_submission_tracker_.UpdateAndGetCompletedSubmission();
    for (size_t i = 0; i < paint_context_.ui_setup_command_buffers.size();
         ++i) {
      PaintContext::UISetupCommandBuffer& ui_setup_command_buffer =
          paint_context_.ui_setup_command_buffers[i];
      if (ui_setup_command_buffer.last_usage_submission_index >
          submission_index_completed) {
        continue;
      }
      if (dfn.vkResetCommandPool(device, ui_setup_command_buffer.command_pool,
                                 0) != VK_SUCCESS) {
        XELOGE("VulkanPresenter: Failed to reset a UI setup command pool");
        return VK_NULL_HANDLE;
      }
      if (dfn.vkBeginCommandBuffer(ui_setup_command_buffer.command_buffer,
                                   &command_buffer_begin_info) != VK_SUCCESS) {
        XELOGE(
            "VulkanPresenter: Failed to begin UI setup command buffer "
            "recording");
        return VK_NULL_HANDLE;
      }
      paint_context_.ui_setup_command_buffer_current_index = i;
      ui_setup_command_buffer.last_usage_submission_index =
          ui_submission_tracker_.GetCurrentSubmission();
      return ui_setup_command_buffer.command_buffer;
    }
  }

  // Create a new command buffer.
  VkCommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.pNext = nullptr;
  command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_create_info.queueFamilyIndex =
      provider_.queue_family_graphics_compute();
  VkCommandPool new_command_pool;
  if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                              &new_command_pool) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to create a UI setup command pool");
    return VK_NULL_HANDLE;
  }
  VkCommandBufferAllocateInfo command_buffer_allocate_info;
  command_buffer_allocate_info.sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.pNext = nullptr;
  command_buffer_allocate_info.commandPool = new_command_pool;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;
  VkCommandBuffer new_command_buffer;
  if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                   &new_command_buffer) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to allocate a UI setup command buffer");
    dfn.vkDestroyCommandPool(device, new_command_pool, nullptr);
    return VK_NULL_HANDLE;
  }
  if (dfn.vkBeginCommandBuffer(new_command_buffer,
                               &command_buffer_begin_info) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to begin UI setup command buffer recording");
    dfn.vkDestroyCommandPool(device, new_command_pool, nullptr);
    return VK_NULL_HANDLE;
  }
  paint_context_.ui_setup_command_buffer_current_index =
      paint_context_.ui_setup_command_buffers.size();
  paint_context_.ui_setup_command_buffers.emplace_back(
      new_command_pool, new_command_buffer,
      ui_submission_tracker_.GetCurrentSubmission());
  return new_command_buffer;
}

Presenter::SurfacePaintConnectResult
VulkanPresenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(
    Surface& new_surface, uint32_t new_surface_width,
    uint32_t new_surface_height, bool was_paintable,
    bool& is_vsync_implicit_out) {
  const VulkanProvider::InstanceFunctions& ifn = provider_.ifn();
  VkInstance instance = provider_.instance();
  VkPhysicalDevice physical_device = provider_.physical_device();
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkFormat new_swapchain_format;

  // ConnectOrReconnectToSurfaceFromUIThread may be called only for the
  // ui::Surface of the current swapchain or when the old swapchain and
  // VkSurface have, if the ui::Surface is the same, try using the existing
  // VkSurface and creating the swapchain smoothly from the existing one - if
  // this doesn't succeed, start from scratch.
  // The retirement or destruction of the swapchain here will also cause
  // awaiting completion of the usage of the swapchain and the surface on the
  // GPU.
  if (paint_context_.vulkan_surface != VK_NULL_HANDLE) {
    VkSwapchainKHR old_swapchain =
        paint_context_.PrepareForSwapchainRetirement();
    bool surface_unusable;
    paint_context_.swapchain = PaintContext::CreateSwapchainForVulkanSurface(
        provider_, paint_context_.vulkan_surface, new_surface_width,
        new_surface_height, old_swapchain, paint_context_.present_queue_family,
        new_swapchain_format, paint_context_.swapchain_extent,
        paint_context_.swapchain_is_fifo, surface_unusable);
    // Destroy the old swapchain that may be retired now.
    if (old_swapchain != VK_NULL_HANDLE) {
      dfn.vkDestroySwapchainKHR(device, old_swapchain, nullptr);
    }
    if (paint_context_.swapchain == VK_NULL_HANDLE) {
      // Couldn't create the swapchain for the existing surface - start over.
      paint_context_.DestroySwapchainAndVulkanSurface();
    }
  }

  // If failed to create the swapchain for the previous surface, recreate the
  // surface and create the new swapchain.
  if (paint_context_.swapchain == VK_NULL_HANDLE) {
    // DestroySwapchainAndVulkanSurface should have been called previously.
    assert_true(paint_context_.vulkan_surface == VK_NULL_HANDLE);
    Surface::TypeIndex surface_type = new_surface.GetType();
    // Check if the surface type is supported according to the Vulkan
    // extensions.
    if (!(GetSupportedSurfaceTypes() &
          (Surface::TypeFlags(1) << surface_type))) {
      XELOGE(
          "VulkanPresenter: Tried to create a Vulkan surface for an "
          "unsupported Xenia surface type");
      return SurfacePaintConnectResult::kFailureSurfaceUnusable;
    }
    VkResult vulkan_surface_create_result = VK_ERROR_UNKNOWN;
    switch (surface_type) {
#if XE_PLATFORM_ANDROID
      case Surface::kTypeIndex_AndroidNativeWindow: {
        auto& android_native_window_surface =
            static_cast<const AndroidNativeWindowSurface&>(new_surface);
        VkAndroidSurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType =
            VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.window = android_native_window_surface.window();
        vulkan_surface_create_result = ifn.vkCreateAndroidSurfaceKHR(
            instance, &surface_create_info, nullptr,
            &paint_context_.vulkan_surface);
      } break;
#endif
#if XE_PLATFORM_GNU_LINUX
      case Surface::kTypeIndex_XcbWindow: {
        auto& xcb_window_surface =
            static_cast<const XcbWindowSurface&>(new_surface);
        VkXcbSurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType =
            VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.connection = xcb_window_surface.connection();
        surface_create_info.window = xcb_window_surface.window();
        vulkan_surface_create_result =
            ifn.vkCreateXcbSurfaceKHR(instance, &surface_create_info, nullptr,
                                      &paint_context_.vulkan_surface);
      } break;
#endif
#if XE_PLATFORM_WIN32
      case Surface::kTypeIndex_Win32Hwnd: {
        auto& win32_hwnd_surface =
            static_cast<const Win32HwndSurface&>(new_surface);
        VkWin32SurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType =
            VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.hinstance = win32_hwnd_surface.hinstance();
        surface_create_info.hwnd = win32_hwnd_surface.hwnd();
        vulkan_surface_create_result =
            ifn.vkCreateWin32SurfaceKHR(instance, &surface_create_info, nullptr,
                                        &paint_context_.vulkan_surface);
      } break;
#endif
      default:
        assert_unhandled_case(surface_type);
        XELOGE(
            "VulkanPresenter: Tried to create a Vulkan surface for an "
            "unknown Xenia surface type");
        return SurfacePaintConnectResult::kFailureSurfaceUnusable;
    }
    if (vulkan_surface_create_result != VK_SUCCESS) {
      XELOGE("VulkanPresenter: Failed to create a Vulkan surface");
      return SurfacePaintConnectResult::kFailure;
    }
    bool surface_unusable;
    paint_context_.swapchain = PaintContext::CreateSwapchainForVulkanSurface(
        provider_, paint_context_.vulkan_surface, new_surface_width,
        new_surface_height, VK_NULL_HANDLE, paint_context_.present_queue_family,
        new_swapchain_format, paint_context_.swapchain_extent,
        paint_context_.swapchain_is_fifo, surface_unusable);
    if (paint_context_.swapchain == VK_NULL_HANDLE) {
      // Failed to create the swapchain for the new Vulkan surface - destroy the
      // Vulkan surface.
      ifn.vkDestroySurfaceKHR(instance, paint_context_.vulkan_surface, nullptr);
      paint_context_.vulkan_surface = VK_NULL_HANDLE;
      return surface_unusable
                 ? SurfacePaintConnectResult::kFailureSurfaceUnusable
                 : SurfacePaintConnectResult::kFailure;
    }
    // Successfully attached (at least for now).
  }

  // From now on, in case of failure,
  // paint_context_.DestroySwapchainAndVulkanSurface must be called before
  // returning.

  // Update the render pass to the new format.
  if (paint_context_.swapchain_render_pass_format != new_swapchain_format) {
    util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                               paint_context_.swapchain_render_pass);
    paint_context_.swapchain_render_pass_format = new_swapchain_format;
  }
  if (paint_context_.swapchain_render_pass == VK_NULL_HANDLE) {
    VkAttachmentDescription render_pass_attachment;
    render_pass_attachment.flags = 0;
    render_pass_attachment.format = new_swapchain_format;
    render_pass_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    render_pass_attachment.loadOp = cvars::present_render_pass_clear
                                        ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                        : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_pass_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_pass_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_pass_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    render_pass_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference render_pass_color_attachment;
    render_pass_color_attachment.attachment = 0;
    render_pass_color_attachment.layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription render_pass_subpass = {};
    render_pass_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    render_pass_subpass.colorAttachmentCount = 1;
    render_pass_subpass.pColorAttachments = &render_pass_color_attachment;
    VkSubpassDependency render_pass_dependencies[2];
    render_pass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    render_pass_dependencies[0].dstSubpass = 0;
    // srcStageMask is the semaphore wait stage.
    render_pass_dependencies[0].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    render_pass_dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    render_pass_dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    // The main target can be used for UI drawing at any moment, which requires
    // blending, so VK_ACCESS_COLOR_ATTACHMENT_READ_BIT is also included.
    render_pass_dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    render_pass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    render_pass_dependencies[1].srcSubpass = 0;
    render_pass_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    render_pass_dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    // Semaphores are signaled at VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT.
    render_pass_dependencies[1].dstStageMask =
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    render_pass_dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    render_pass_dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    render_pass_dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    VkRenderPassCreateInfo render_pass_create_info;
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.pNext = nullptr;
    render_pass_create_info.flags = 0;
    render_pass_create_info.attachmentCount = 1;
    render_pass_create_info.pAttachments = &render_pass_attachment;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &render_pass_subpass;
    render_pass_create_info.dependencyCount =
        uint32_t(xe::countof(render_pass_dependencies));
    render_pass_create_info.pDependencies = render_pass_dependencies;
    VkRenderPass new_render_pass;
    if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                               &new_render_pass) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to create the render pass for drawing to "
          "swapchain images");
      paint_context_.DestroySwapchainAndVulkanSurface();
      return SurfacePaintConnectResult::kFailure;
    }
    paint_context_.swapchain_render_pass = new_render_pass;
    paint_context_.swapchain_render_pass_format = new_swapchain_format;
    paint_context_.swapchain_render_pass_clear_load_op =
        render_pass_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
  }

  // Get the swapchain images.
  paint_context_.swapchain_images.clear();
  VkResult swapchain_images_get_result;
  for (;;) {
    uint32_t swapchain_image_count =
        uint32_t(paint_context_.swapchain_images.size());
    bool swapchain_images_were_empty = !swapchain_image_count;
    swapchain_images_get_result = dfn.vkGetSwapchainImagesKHR(
        device, paint_context_.swapchain, &swapchain_image_count,
        swapchain_images_were_empty ? nullptr
                                    : paint_context_.swapchain_images.data());
    // If the original swapchain image count was 0 (first call), SUCCESS is
    // returned, not INCOMPLETE.
    if (swapchain_images_get_result == VK_SUCCESS ||
        swapchain_images_get_result == VK_INCOMPLETE) {
      paint_context_.swapchain_images.resize(swapchain_image_count);
      if (swapchain_images_get_result == VK_SUCCESS &&
          (!swapchain_images_were_empty || !swapchain_image_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (swapchain_images_get_result != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to get swapchain images");
    paint_context_.DestroySwapchainAndVulkanSurface();
    return SurfacePaintConnectResult::kFailure;
  }

  // Create the image views and the framebuffers.
  assert_true(paint_context_.swapchain_framebuffers.empty());
  paint_context_.swapchain_framebuffers.reserve(
      paint_context_.swapchain_images.size());
  VkImageViewCreateInfo image_view_create_info;
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.pNext = nullptr;
  image_view_create_info.flags = 0;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = new_swapchain_format;
  image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_create_info.subresourceRange.baseMipLevel = 0;
  image_view_create_info.subresourceRange.levelCount = 1;
  image_view_create_info.subresourceRange.baseArrayLayer = 0;
  image_view_create_info.subresourceRange.layerCount = 1;
  VkImageView image_view;
  VkFramebufferCreateInfo framebuffer_create_info;
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = nullptr;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = paint_context_.swapchain_render_pass;
  framebuffer_create_info.attachmentCount = 1;
  framebuffer_create_info.pAttachments = &image_view;
  framebuffer_create_info.width = paint_context_.swapchain_extent.width;
  framebuffer_create_info.height = paint_context_.swapchain_extent.height;
  framebuffer_create_info.layers = 1;
  for (VkImage image : paint_context_.swapchain_images) {
    image_view_create_info.image = image;
    if (dfn.vkCreateImageView(device, &image_view_create_info, nullptr,
                              &image_view) != VK_SUCCESS) {
      XELOGE("VulkanPresenter: Failed to create a swapchain image view");
      paint_context_.DestroySwapchainAndVulkanSurface();
      return SurfacePaintConnectResult::kFailure;
    }
    VkFramebuffer framebuffer;
    if (dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                                &framebuffer) != VK_SUCCESS) {
      XELOGE("VulkanPresenter: Failed to create a swapchain framebuffer");
      dfn.vkDestroyImageView(device, image_view, nullptr);
      paint_context_.DestroySwapchainAndVulkanSurface();
      return SurfacePaintConnectResult::kFailure;
    }
    paint_context_.swapchain_framebuffers.emplace_back(image_view, framebuffer);
  }

  is_vsync_implicit_out = paint_context_.swapchain_is_fifo;
  return SurfacePaintConnectResult::kSuccess;
}

void VulkanPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  paint_context_.DestroySwapchainAndVulkanSurface();
}

bool VulkanPresenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height,
    std::function<bool(GuestOutputRefreshContext& context)> refresher,
    bool& is_8bpc_out_ref) {
  assert_not_zero(frontbuffer_width);
  assert_not_zero(frontbuffer_height);
  VkExtent2D max_framebuffer_extent =
      util::GetMax2DFramebufferExtent(provider_);
  if (frontbuffer_width > max_framebuffer_extent.width ||
      frontbuffer_height > max_framebuffer_extent.height) {
    // Writing the guest output isn't supposed to rescale, and a guest texture
    // exceeding the maximum size won't be loadable anyway.
    return false;
  }

  GuestOutputImageInstance& image_instance =
      guest_output_images_[mailbox_index];
  if (image_instance.image &&
      (image_instance.image->extent().width != frontbuffer_width ||
       image_instance.image->extent().height != frontbuffer_height)) {
    guest_output_image_refresher_submission_tracker_.AwaitSubmissionCompletion(
        image_instance.last_refresher_submission);
    image_instance.image.reset();
  }
  if (!image_instance.image) {
    std::unique_ptr<GuestOutputImage> new_image = GuestOutputImage::Create(
        provider_, frontbuffer_width, frontbuffer_height);
    if (!new_image) {
      return false;
    }
    image_instance.SetToNewImage(std::move(new_image),
                                 guest_output_image_next_version_++);
  }

  VulkanGuestOutputRefreshContext context(
      is_8bpc_out_ref, image_instance.image->image(),
      image_instance.image->view(), image_instance.version,
      image_instance.ever_successfully_refreshed);
  bool refresher_succeeded = refresher(context);
  if (refresher_succeeded) {
    image_instance.ever_successfully_refreshed = true;
  }
  // Even if the refresher has returned false, it still might have submitted
  // some commands referencing the image. It's better to put an excessive
  // signal and wait slightly longer, for nothing important, while shutting down
  // than to destroy the image while it's still in use.
  image_instance.last_refresher_submission =
      guest_output_image_refresher_submission_tracker_.GetCurrentSubmission();
  // No need to make the refresher signal the fence by itself - signal it here
  // instead to have more control:
  // "Fence signal operations that are defined by vkQueueSubmit additionally
  //  include in the first synchronization scope all commands that occur earlier
  //  in submission order."
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  {
    VulkanSubmissionTracker::FenceAcquisition fence_acqusition(
        guest_output_image_refresher_submission_tracker_
            .AcquireFenceToAdvanceSubmission());
    VulkanProvider::QueueAcquisition queue_acquisition(
        provider_.AcquireQueue(provider_.queue_family_graphics_compute(), 0));
    if (dfn.vkQueueSubmit(queue_acquisition.queue, 0, nullptr,
                          fence_acqusition.fence()) != VK_SUCCESS) {
      fence_acqusition.SubmissionSucceededSignalFailed();
    }
  }

  return refresher_succeeded;
}

VkSwapchainKHR VulkanPresenter::PaintContext::CreateSwapchainForVulkanSurface(
    const VulkanProvider& provider, VkSurfaceKHR surface, uint32_t width,
    uint32_t height, VkSwapchainKHR old_swapchain,
    uint32_t& present_queue_family_out, VkFormat& image_format_out,
    VkExtent2D& image_extent_out, bool& is_fifo_out,
    bool& ui_surface_unusable_out) {
  ui_surface_unusable_out = false;

  const VulkanProvider::InstanceFunctions& ifn = provider.ifn();
  VkInstance instance = provider.instance();
  VkPhysicalDevice physical_device = provider.physical_device();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Get surface capabilities.
  VkSurfaceCapabilitiesKHR surface_capabilities;
  if (ifn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          physical_device, surface, &surface_capabilities) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to get Vulkan surface capabilities");
    // Some strange error, try again later.
    return VK_NULL_HANDLE;
  }

  // First, check if the surface is not zero-area because in this case, the rest
  // of the fields in theory may not be informative as the surface doesn't need
  // to go to presentation anyway, thus there's no need to return real
  // information, and ui_surface_unusable_out (from which it may not be possible
  // to recover on certain platforms at all) may be set to true spuriously if
  // any checks set it to true. VkSurfaceKHR may have zero-area bounds in some
  // window state cases on Windows, for example. Also clamp the requested size
  // to the maximum supported by the physical device as long as minImageExtent
  // in the instance's WSI allows that (if not, there's no way to satisfy both
  // requirements - the maximum 2D framebuffer size on the specific physical
  // device, and the minimum swap chain size on the whole instance - fail to
  // create until the surface becomes smaller).
  VkExtent2D max_framebuffer_extent = util::GetMax2DFramebufferExtent(provider);
  VkExtent2D image_extent;
  image_extent.width =
      std::min(std::max(std::min(width, max_framebuffer_extent.width),
                        surface_capabilities.minImageExtent.width),
               surface_capabilities.maxImageExtent.width);
  image_extent.height =
      std::min(std::max(std::min(height, max_framebuffer_extent.height),
                        surface_capabilities.minImageExtent.height),
               surface_capabilities.maxImageExtent.height);
  if (!image_extent.width || !image_extent.height ||
      image_extent.width > max_framebuffer_extent.width ||
      image_extent.height > max_framebuffer_extent.height) {
    return VK_NULL_HANDLE;
  }

  // Get the queue family for presentation.
  uint32_t queue_family_index_present = UINT32_MAX;
  const std::vector<VulkanProvider::QueueFamily>& queue_families =
      provider.queue_families();
  VkBool32 queue_family_present_supported;
  // First try the graphics and compute queue, prefer it to avoid the concurrent
  // image sharing mode.
  uint32_t queue_family_index_graphics_compute =
      provider.queue_family_graphics_compute();
  const VulkanProvider::QueueFamily& queue_family_graphics_compute =
      queue_families[queue_family_index_graphics_compute];
  if (queue_family_graphics_compute.potentially_supports_present &&
      queue_family_graphics_compute.queue_count &&
      ifn.vkGetPhysicalDeviceSurfaceSupportKHR(
          physical_device, queue_family_index_graphics_compute, surface,
          &queue_family_present_supported) == VK_SUCCESS &&
      queue_family_present_supported) {
    queue_family_index_present = queue_family_index_graphics_compute;
  } else {
    for (uint32_t i = 0; i < uint32_t(queue_families.size()); ++i) {
      const VulkanProvider::QueueFamily& queue_family = queue_families[i];
      if (queue_family.potentially_supports_present &&
          queue_family.queue_count &&
          ifn.vkGetPhysicalDeviceSurfaceSupportKHR(
              physical_device, i, surface, &queue_family_present_supported) ==
              VK_SUCCESS &&
          queue_family_present_supported) {
        queue_family_index_present = i;
        break;
      }
    }
  }
  if (queue_family_index_present == UINT32_MAX) {
    // Not unusable though - may become presentable if the window (with the same
    // surface) moved to a different display, for instance.
    return VK_NULL_HANDLE;
  }

  // TODO(Triang3l): Support transforms.
  if (!(surface_capabilities.supportedTransforms &
        (VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
         VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR))) {
    XELOGE(
        "VulkanPresenter: The surface doesn't support identity or "
        "window-system-controlled transform");
    return VK_NULL_HANDLE;
  }

  // Get the surface format.
  std::vector<VkSurfaceFormatKHR> surface_formats;
  VkResult surface_formats_get_result;
  for (;;) {
    uint32_t surface_format_count = uint32_t(surface_formats.size());
    bool surface_formats_were_empty = !surface_format_count;
    surface_formats_get_result = ifn.vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &surface_format_count,
        surface_formats_were_empty ? nullptr : surface_formats.data());
    // If the original presentation mode count was 0 (first call), SUCCESS is
    // returned, not INCOMPLETE.
    if (surface_formats_get_result == VK_SUCCESS ||
        surface_formats_get_result == VK_INCOMPLETE) {
      surface_formats.resize(surface_format_count);
      if (surface_formats_get_result == VK_SUCCESS &&
          (!surface_formats_were_empty || !surface_format_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (surface_formats_get_result != VK_SUCCESS) {
    // Assuming any format in case of an error (or as some fallback in case of
    // specification violation).
    surface_formats.clear();
  }
#if XE_PLATFORM_ANDROID
  // Android uses R8G8B8A8.
  static const VkFormat kFormat8888Primary = VK_FORMAT_R8G8B8A8_UNORM;
  static const VkFormat kFormat8888Secondary = VK_FORMAT_B8G8R8A8_UNORM;
#else
  // GNU/Linux X11 and Windows DWM use B8G8R8A8.
  static const VkFormat kFormat8888Primary = VK_FORMAT_B8G8R8A8_UNORM;
  static const VkFormat kFormat8888Secondary = VK_FORMAT_R8G8B8A8_UNORM;
#endif
  VkSurfaceFormatKHR image_format;
  if (surface_formats.empty() ||
      (surface_formats.size() == 1 ||
       surface_formats[0].format == VK_FORMAT_UNDEFINED)) {
    // Can choose any format if the implementation specifies only UNDEFINED.
    image_format.format = kFormat8888Primary;
    image_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  } else {
    // Pick the sRGB 8888 format preferred by the OS, fall back to any sRGB
    // 8888, then to 8888 with an unknown color space, and then to the first
    // sRGB available, and then to the first.
    auto format_8888_primary_it = surface_formats.cend();
    auto format_8888_secondary_it = surface_formats.cend();
    auto any_non_8888_srgb_it = surface_formats.cend();
    for (auto it = surface_formats.cbegin(); it != surface_formats.cend();
         ++it) {
      if (it->format != kFormat8888Primary &&
          it->format != kFormat8888Secondary) {
        if (it->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            any_non_8888_srgb_it == surface_formats.cend()) {
          any_non_8888_srgb_it = it;
        }
        continue;
      }
      auto& preferred_8888_it = it->format == kFormat8888Primary
                                    ? format_8888_primary_it
                                    : format_8888_secondary_it;
      if (preferred_8888_it == surface_formats.cend()) {
        // First primary or secondary 8888 encounter.
        preferred_8888_it = it;
        continue;
      }
      // Is this a better primary or secondary 8888, that is, this is sRGB,
      // while the previous encounter was not?
      if (it->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
          preferred_8888_it->colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        preferred_8888_it = it;
      }
    }
    if (format_8888_primary_it != surface_formats.cend() &&
        format_8888_secondary_it != surface_formats.cend()) {
      // Both the primary and the secondary 8888 formats are available - prefer
      // sRGB, if both are sRGB or not, prefer the primary format.
      if (format_8888_primary_it->colorSpace ==
              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
          format_8888_secondary_it->colorSpace !=
              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        image_format = *format_8888_primary_it;
      } else {
        image_format = *format_8888_secondary_it;
      }
    } else if (format_8888_primary_it != surface_formats.cend()) {
      // Only primary 8888.
      image_format = *format_8888_primary_it;
    } else if (format_8888_secondary_it != surface_formats.cend()) {
      // Only secondary 8888.
      image_format = *format_8888_secondary_it;
    } else if (any_non_8888_srgb_it != surface_formats.cend()) {
      // No 8888, but some sRGB format is available.
      image_format = *any_non_8888_srgb_it;
    } else {
      // Just pick any format.
      image_format = surface_formats.front();
    }
  }

  // Get presentation modes.
  std::vector<VkPresentModeKHR> present_modes;
  VkResult present_modes_get_result;
  for (;;) {
    uint32_t present_mode_count = uint32_t(present_modes.size());
    bool present_modes_were_empty = !present_mode_count;
    present_modes_get_result = ifn.vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_mode_count,
        present_modes_were_empty ? nullptr : present_modes.data());
    // If the original presentation mode count was 0 (first call), SUCCESS is
    // returned, not INCOMPLETE.
    if (present_modes_get_result == VK_SUCCESS ||
        present_modes_get_result == VK_INCOMPLETE) {
      present_modes.resize(present_mode_count);
      if (present_modes_get_result == VK_SUCCESS &&
          (!present_modes_were_empty || !present_mode_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (present_modes_get_result != VK_SUCCESS) {
    // Assuming FIFO only (required everywhere) in case of an error.
    present_modes.clear();
  }

  // Create the swapchain.
  VkSwapchainCreateInfoKHR swapchain_create_info;
  swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_create_info.pNext = nullptr;
  swapchain_create_info.flags = 0;
  swapchain_create_info.surface = surface;
  swapchain_create_info.minImageCount =
      std::max(kSubmissionCount, surface_capabilities.minImageCount);
  if (surface_capabilities.maxImageCount) {
    swapchain_create_info.minImageCount =
        std::min(swapchain_create_info.minImageCount,
                 surface_capabilities.maxImageCount);
  }
  swapchain_create_info.imageFormat = image_format.format;
  swapchain_create_info.imageColorSpace = image_format.colorSpace;
  swapchain_create_info.imageExtent = image_extent;
  swapchain_create_info.imageArrayLayers = 1;
  swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  uint32_t swapchain_queue_family_indices[2];
  if (queue_family_index_graphics_compute != queue_family_index_present) {
    // Using concurrent sharing mode to avoid an explicit ownership transfer
    // before presenting, which would require an additional command buffer
    // submission with the acquire barrier and a semaphore between the graphics
    // queue and the present queue. Different queues are an extremely rare case,
    // and Xenia only uses the swapchain for the final guest output and the
    // internal UI, so keeping framebuffer compression is not worth the
    // additional submission complexity and possibly latency.
    swapchain_queue_family_indices[0] = queue_family_index_graphics_compute;
    swapchain_queue_family_indices[1] = queue_family_index_present;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchain_create_info.queueFamilyIndexCount = 2;
    swapchain_create_info.pQueueFamilyIndices = swapchain_queue_family_indices;
  } else {
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices = nullptr;
  }
  swapchain_create_info.preTransform =
      (surface_capabilities.supportedTransforms &
       VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
          : VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR;
  // Prefer opaque to avoid blending in the window system, or let that be
  // specified via the window system if it can't be forced. As a last resort,
  // just pick any - guest output will write alpha of 1 anyway.
  if (surface_capabilities.supportedCompositeAlpha &
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  } else if (surface_capabilities.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  } else {
    uint32_t composite_alpha_shift;
    if (!xe::bit_scan_forward(
            uint32_t(surface_capabilities.supportedCompositeAlpha),
            &composite_alpha_shift)) {
      // Against the Vulkan specification, but breaks the logic here.
      XELOGE(
          "VulkanPresenter: The surface doesn't support any composite alpha "
          "mode");
      return VK_NULL_HANDLE;
    }
    swapchain_create_info.compositeAlpha =
        VkCompositeAlphaFlagBitsKHR(uint32_t(1) << composite_alpha_shift);
  }
  // As presentation is usually controlled by the GPU command processor, it's
  // better to use modes that allow as quick acquisition as possible to avoid
  // interfering with GPU command processing, and also to allow tearing so
  // variable refresh rate may be used where it's available.
  // Note: If the priorities here are changes, update the cvar descriptions.
  if (cvars::vulkan_allow_present_mode_immediate &&
      std::find(present_modes.cbegin(), present_modes.cend(),
                VK_PRESENT_MODE_IMMEDIATE_KHR) != present_modes.cend()) {
    // Allowing tearing to reduce latency, and possibly variable refresh rate
    // (though on Windows with borderless fullscreen, GDI copying is used
    // instead of independent flip, so it's not supported there).
    swapchain_create_info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  } else if (cvars::vulkan_allow_present_mode_mailbox &&
             std::find(present_modes.cbegin(), present_modes.cend(),
                       VK_PRESENT_MODE_MAILBOX_KHR) != present_modes.cend()) {
    // Allowing dropping frames to reduce latency, but no tearing.
    swapchain_create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
  } else if (cvars::vulkan_allow_present_mode_fifo_relaxed &&
             std::find(present_modes.cbegin(), present_modes.cend(),
                       VK_PRESENT_MODE_FIFO_RELAXED_KHR) !=
                 present_modes.cend()) {
    // Limiting the frame rate, but lets too long frames cause tearing not to
    // make the latency even worse.
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  } else {
    // Highest latency (but always guaranteed to be available).
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  }
  swapchain_create_info.clipped = VK_TRUE;
  swapchain_create_info.oldSwapchain = old_swapchain;
  VkSwapchainKHR swapchain;
  if (dfn.vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr,
                               &swapchain) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to create a swapchain");
    return VK_NULL_HANDLE;
  }
  XELOGVK(
      "VulkanPresenter: Created {}x{} swapchain with format {}, color space "
      "{}, presentation mode {}",
      swapchain_create_info.imageExtent.width,
      swapchain_create_info.imageExtent.height,
      uint32_t(swapchain_create_info.imageFormat),
      uint32_t(swapchain_create_info.imageColorSpace),
      uint32_t(swapchain_create_info.presentMode));

  present_queue_family_out = queue_family_index_present;
  image_format_out = swapchain_create_info.imageFormat;
  image_extent_out = swapchain_create_info.imageExtent;
  is_fifo_out =
      swapchain_create_info.presentMode == VK_PRESENT_MODE_FIFO_KHR ||
      swapchain_create_info.presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  return swapchain;
}

VkSwapchainKHR VulkanPresenter::PaintContext::PrepareForSwapchainRetirement() {
  if (swapchain != VK_NULL_HANDLE) {
    submission_tracker.AwaitAllSubmissionsCompletion();
  }
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  for (const SwapchainFramebuffer& framebuffer : swapchain_framebuffers) {
    dfn.vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);
    dfn.vkDestroyImageView(device, framebuffer.image_view, nullptr);
  }
  swapchain_framebuffers.clear();
  swapchain_images.clear();
  swapchain_extent.width = 0;
  swapchain_extent.height = 0;
  // The old swapchain must be destroyed externally.
  VkSwapchainKHR old_swapchain = swapchain;
  swapchain = nullptr;
  return old_swapchain;
}

void VulkanPresenter::PaintContext::DestroySwapchainAndVulkanSurface() {
  VkSwapchainKHR old_swapchain = PrepareForSwapchainRetirement();
  if (old_swapchain != VK_NULL_HANDLE) {
    const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
    VkDevice device = provider.device();
    dfn.vkDestroySwapchainKHR(device, old_swapchain, nullptr);
  }
  present_queue_family = UINT32_MAX;
  if (vulkan_surface != VK_NULL_HANDLE) {
    const VulkanProvider::InstanceFunctions& ifn = provider.ifn();
    VkInstance instance = provider.instance();
    ifn.vkDestroySurfaceKHR(instance, vulkan_surface, nullptr);
    vulkan_surface = VK_NULL_HANDLE;
  }
}

VulkanPresenter::GuestOutputImage::~GuestOutputImage() {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  if (view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_, nullptr);
  }
  if (image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, image_, nullptr);
  }
  if (memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, memory_, nullptr);
  }
}

bool VulkanPresenter::GuestOutputImage::Initialize() {
  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = kGuestOutputFormat;
  image_create_info.extent.width = extent_.width;
  image_create_info.extent.height = extent_.height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          provider_, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, image_, memory_)) {
    XELOGE("VulkanPresenter: Failed to create a guest output image");
    return false;
  }

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkImageViewCreateInfo image_view_create_info;
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.pNext = nullptr;
  image_view_create_info.flags = 0;
  image_view_create_info.image = image_;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = kGuestOutputFormat;
  image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_create_info.subresourceRange.baseMipLevel = 0;
  image_view_create_info.subresourceRange.levelCount = 1;
  image_view_create_info.subresourceRange.baseArrayLayer = 0;
  image_view_create_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &image_view_create_info, nullptr, &view_) !=
      VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to create a guest output image view");
    return false;
  }

  return true;
}

Presenter::PaintResult VulkanPresenter::PaintAndPresentImpl(
    bool execute_ui_drawers) {
  // Begin the submission in place of the one not currently potentially used on
  // the GPU.
  uint64_t current_paint_submission_index =
      paint_context_.submission_tracker.GetCurrentSubmission();
  uint64_t paint_submission_count = uint64_t(paint_context_.submissions.size());
  if (current_paint_submission_index >= paint_submission_count) {
    paint_context_.submission_tracker.AwaitSubmissionCompletion(
        current_paint_submission_index - paint_submission_count);
  }
  const PaintContext::Submission& paint_submission =
      *paint_context_.submissions[current_paint_submission_index %
                                  paint_submission_count];

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkCommandPool draw_command_pool = paint_submission.draw_command_pool();
  if (dfn.vkResetCommandPool(device, draw_command_pool, 0) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to reset a command buffer for drawing to the "
        "swapchain");
    return PaintResult::kNotPresented;
  }

  VkCommandBuffer draw_command_buffer = paint_submission.draw_command_buffer();
  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;
  if (dfn.vkBeginCommandBuffer(draw_command_buffer,
                               &command_buffer_begin_info) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to being recording the command buffer for "
        "drawing to the swapchain");
    return PaintResult::kNotPresented;
  }
  // vkResetCommandPool resets from both initial and recording states, still
  // safe to return early from this function in case of an error.

  VkSemaphore acquire_semaphore = paint_submission.acquire_semaphore();
  uint32_t swapchain_image_index;
  VkResult acquire_result = dfn.vkAcquireNextImageKHR(
      device, paint_context_.swapchain, UINT64_MAX, acquire_semaphore,
      VK_NULL_HANDLE, &swapchain_image_index);
  switch (acquire_result) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      break;
    case VK_ERROR_DEVICE_LOST:
      XELOGE(
          "VulkanPresenter: Failed to acquire the swapchain image as the "
          "device has been lost");
      return PaintResult::kGpuLostResponsible;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_SURFACE_LOST_KHR:
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
      // Not an error, reporting just as info (may normally occur while resizing
      // on some platforms).
      XELOGVK(
          "VulkanPresenter: Presentation to the swapchain image has been "
          "dropped as the swapchain or the surface has become outdated");
      return PaintResult::kNotPresentedConnectionOutdated;
    default:
      XELOGE("VulkanPresenter: Failed to acquire the swapchain image");
      return PaintResult::kNotPresented;
  }

  // Non-zero extents needed for both the viewport (width must not be zero) and
  // the guest output rectangle.
  assert_not_zero(paint_context_.swapchain_extent.width);
  assert_not_zero(paint_context_.swapchain_extent.height);

  bool swapchain_image_clear_needed = true;
  VkClearAttachment swapchain_image_clear_attachment;
  swapchain_image_clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  swapchain_image_clear_attachment.colorAttachment = 0;
  swapchain_image_clear_attachment.clearValue.color.float32[0] = 0.0f;
  swapchain_image_clear_attachment.clearValue.color.float32[1] = 0.0f;
  swapchain_image_clear_attachment.clearValue.color.float32[2] = 0.0f;
  swapchain_image_clear_attachment.clearValue.color.float32[3] = 1.0f;

  VkRenderPassBeginInfo swapchain_render_pass_begin_info;
  swapchain_render_pass_begin_info.sType =
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  swapchain_render_pass_begin_info.pNext = nullptr;
  swapchain_render_pass_begin_info.renderPass =
      paint_context_.swapchain_render_pass;
  swapchain_render_pass_begin_info.framebuffer =
      paint_context_.swapchain_framebuffers[swapchain_image_index].framebuffer;
  swapchain_render_pass_begin_info.renderArea.offset.x = 0;
  swapchain_render_pass_begin_info.renderArea.offset.y = 0;
  swapchain_render_pass_begin_info.renderArea.extent =
      paint_context_.swapchain_extent;
  swapchain_render_pass_begin_info.clearValueCount = 0;
  swapchain_render_pass_begin_info.pClearValues = nullptr;
  if (paint_context_.swapchain_render_pass_clear_load_op) {
    swapchain_render_pass_begin_info.clearValueCount = 1;
    swapchain_render_pass_begin_info.pClearValues =
        &swapchain_image_clear_attachment.clearValue;
    swapchain_image_clear_needed = false;
  }

  bool swapchain_image_pass_begun = false;

  GuestOutputProperties guest_output_properties;
  GuestOutputPaintConfig guest_output_paint_config;
  std::shared_ptr<GuestOutputImage> guest_output_image;
  {
    uint32_t guest_output_mailbox_index;
    std::unique_lock<std::mutex> guest_output_consumer_lock(
        ConsumeGuestOutput(guest_output_mailbox_index, &guest_output_properties,
                           &guest_output_paint_config));
    if (guest_output_mailbox_index != UINT32_MAX) {
      assert_true(guest_output_images_[guest_output_mailbox_index]
                      .ever_successfully_refreshed);
      guest_output_image =
          guest_output_images_[guest_output_mailbox_index].image;
    }
    // Incremented the reference count of the guest output image - safe to leave
    // the consumer critical section now as everything here either will be using
    // the new reference or is exclusively owned by main target painting (and
    // multiple threads can't paint the main target at the same time).
  }

  if (guest_output_image) {
    VkExtent2D max_framebuffer_extent =
        util::GetMax2DFramebufferExtent(provider_);
    GuestOutputPaintFlow guest_output_flow = GetGuestOutputPaintFlow(
        guest_output_properties, paint_context_.swapchain_extent.width,
        paint_context_.swapchain_extent.height, max_framebuffer_extent.width,
        max_framebuffer_extent.height, guest_output_paint_config);
    if (guest_output_flow.effect_count) {
      // Store the main target reference to the guest output image so it's not
      // destroyed while it's still potentially in use by main target painting
      // queued on the GPU.
      size_t guest_output_image_paint_ref_index = SIZE_MAX;
      size_t guest_output_image_paint_ref_new_index = SIZE_MAX;
      // Try to find the existing reference to the same image, or an already
      // released (or a taken, but never actually used) slot.
      for (size_t i = 0;
           i < paint_context_.guest_output_image_paint_refs.size(); ++i) {
        const std::pair<uint64_t, std::shared_ptr<GuestOutputImage>>&
            guest_output_image_paint_ref =
                paint_context_.guest_output_image_paint_refs[i];
        if (guest_output_image_paint_ref.second == guest_output_image) {
          guest_output_image_paint_ref_index = i;
          break;
        }
        if (guest_output_image_paint_ref_new_index == SIZE_MAX &&
            (!guest_output_image_paint_ref.second ||
             !guest_output_image_paint_ref.first)) {
          guest_output_image_paint_ref_new_index = i;
        }
      }
      if (guest_output_image_paint_ref_index == SIZE_MAX) {
        // New image - store the reference and create the descriptors.
        if (guest_output_image_paint_ref_new_index == SIZE_MAX) {
          // Replace the earliest used reference.
          guest_output_image_paint_ref_new_index = 0;
          for (size_t i = 1;
               i < paint_context_.guest_output_image_paint_refs.size(); ++i) {
            if (paint_context_.guest_output_image_paint_refs[i].first <
                paint_context_
                    .guest_output_image_paint_refs
                        [guest_output_image_paint_ref_new_index]
                    .first) {
              guest_output_image_paint_ref_new_index = i;
            }
          }
          // Await the completion of the usage of the old guest output image and
          // its descriptors.
          paint_context_.submission_tracker.AwaitSubmissionCompletion(
              paint_context_
                  .guest_output_image_paint_refs
                      [guest_output_image_paint_ref_new_index]
                  .first);
        }
        guest_output_image_paint_ref_index =
            guest_output_image_paint_ref_new_index;
        // The actual submission index will be set if the image is actually
        // used, not dropped due to some error.
        paint_context_
            .guest_output_image_paint_refs[guest_output_image_paint_ref_index] =
            std::make_pair(uint64_t(0), guest_output_image);
        // Create the descriptors of the new image.
        VkDescriptorImageInfo guest_output_image_descriptor_image_info;
        guest_output_image_descriptor_image_info.sampler = VK_NULL_HANDLE;
        guest_output_image_descriptor_image_info.imageView =
            guest_output_image->view();
        guest_output_image_descriptor_image_info.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet guest_output_image_descriptor_write;
        guest_output_image_descriptor_write.sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        guest_output_image_descriptor_write.pNext = nullptr;
        guest_output_image_descriptor_write.dstSet =
            paint_context_.guest_output_descriptor_sets
                [PaintContext::kGuestOutputDescriptorSetGuestOutput0Sampled +
                 guest_output_image_paint_ref_index];
        guest_output_image_descriptor_write.dstBinding = 0;
        guest_output_image_descriptor_write.dstArrayElement = 0;
        guest_output_image_descriptor_write.descriptorCount = 1;
        guest_output_image_descriptor_write.descriptorType =
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        guest_output_image_descriptor_write.pImageInfo =
            &guest_output_image_descriptor_image_info;
        guest_output_image_descriptor_write.pBufferInfo = nullptr;
        guest_output_image_descriptor_write.pTexelBufferView = nullptr;
        dfn.vkUpdateDescriptorSets(
            device, 1, &guest_output_image_descriptor_write, 0, nullptr);
      }

      // Make sure intermediate textures of the needed size are available, and
      // unneeded intermediate textures are destroyed.
      for (size_t i = 0; i < kMaxGuestOutputPaintEffects - 1; ++i) {
        std::pair<uint32_t, uint32_t> intermediate_needed_size(0, 0);
        if (i + 1 < guest_output_flow.effect_count) {
          intermediate_needed_size = guest_output_flow.effect_output_sizes[i];
        }
        std::unique_ptr<GuestOutputImage>& intermediate_image_ptr_ref =
            paint_context_.guest_output_intermediate_images[i];
        VkExtent2D intermediate_current_extent(
            intermediate_image_ptr_ref ? intermediate_image_ptr_ref->extent()
                                       : VkExtent2D{});
        if (intermediate_current_extent.width !=
                intermediate_needed_size.first ||
            intermediate_current_extent.height !=
                intermediate_needed_size.second) {
          if (intermediate_needed_size.first &&
              intermediate_needed_size.second) {
            // Need to replace immediately as a new image with the requested
            // size is needed.
            if (intermediate_image_ptr_ref) {
              paint_context_.submission_tracker.AwaitSubmissionCompletion(
                  paint_context_
                      .guest_output_intermediate_image_last_submission);
              intermediate_image_ptr_ref.reset();
              util::DestroyAndNullHandle(
                  dfn.vkDestroyFramebuffer, device,
                  paint_context_.guest_output_intermediate_framebuffers[i]);
            }
            // Image.
            intermediate_image_ptr_ref = GuestOutputImage::Create(
                provider_, intermediate_needed_size.first,
                intermediate_needed_size.second);
            if (!intermediate_image_ptr_ref) {
              // Don't display the guest output, and don't try to create more
              // intermediate textures (only destroy them).
              guest_output_flow.effect_count = 0;
              continue;
            }
            // Framebuffer.
            VkImageView intermediate_framebuffer_attachment =
                intermediate_image_ptr_ref->view();
            VkFramebufferCreateInfo intermediate_framebuffer_create_info;
            intermediate_framebuffer_create_info.sType =
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            intermediate_framebuffer_create_info.pNext = nullptr;
            intermediate_framebuffer_create_info.flags = 0;
            intermediate_framebuffer_create_info.renderPass =
                guest_output_intermediate_render_pass_;
            intermediate_framebuffer_create_info.attachmentCount = 1;
            intermediate_framebuffer_create_info.pAttachments =
                &intermediate_framebuffer_attachment;
            intermediate_framebuffer_create_info.width =
                intermediate_needed_size.first;
            intermediate_framebuffer_create_info.height =
                intermediate_needed_size.second;
            intermediate_framebuffer_create_info.layers = 1;
            if (dfn.vkCreateFramebuffer(
                    device, &intermediate_framebuffer_create_info, nullptr,
                    &paint_context_
                         .guest_output_intermediate_framebuffers[i]) !=
                VK_SUCCESS) {
              XELOGE(
                  "VulkanPresenter: Failed to create a guest output "
                  "intermediate framebuffer");
              // Don't display the guest output, and don't try to create more
              // intermediate textures (only destroy them).
              guest_output_flow.effect_count = 0;
              continue;
            }
            // Descriptors.
            VkDescriptorImageInfo intermediate_descriptor_image_info;
            intermediate_descriptor_image_info.sampler = VK_NULL_HANDLE;
            intermediate_descriptor_image_info.imageView =
                intermediate_image_ptr_ref->view();
            intermediate_descriptor_image_info.imageLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet intermediate_descriptor_write;
            intermediate_descriptor_write.sType =
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            intermediate_descriptor_write.pNext = nullptr;
            intermediate_descriptor_write.dstSet =
                paint_context_.guest_output_descriptor_sets
                    [PaintContext ::
                         kGuestOutputDescriptorSetIntermediate0Sampled +
                     i];
            intermediate_descriptor_write.dstBinding = 0;
            intermediate_descriptor_write.dstArrayElement = 0;
            intermediate_descriptor_write.descriptorCount = 1;
            intermediate_descriptor_write.descriptorType =
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            intermediate_descriptor_write.pImageInfo =
                &intermediate_descriptor_image_info;
            intermediate_descriptor_write.pBufferInfo = nullptr;
            intermediate_descriptor_write.pTexelBufferView = nullptr;
            dfn.vkUpdateDescriptorSets(
                device, 1, &intermediate_descriptor_write, 0, nullptr);
          } else {
            // Was previously needed, but not anymore - destroy when possible.
            if (intermediate_image_ptr_ref &&
                paint_context_.submission_tracker
                        .UpdateAndGetCompletedSubmission() >=
                    paint_context_
                        .guest_output_intermediate_image_last_submission) {
              intermediate_image_ptr_ref.reset();
              util::DestroyAndNullHandle(
                  dfn.vkDestroyFramebuffer, device,
                  paint_context_.guest_output_intermediate_framebuffers[i]);
            }
          }
        }
      }

      if (guest_output_flow.effect_count) {
        // Check if all the intermediate effects are supported by the
        // implementation.
        for (size_t i = 0; i + 1 < guest_output_flow.effect_count; ++i) {
          if (paint_context_
                  .guest_output_paint_pipelines[size_t(
                      guest_output_flow.effects[i])]
                  .intermediate_pipeline == VK_NULL_HANDLE) {
            guest_output_flow.effect_count = 0;
            break;
          }
        }
        // Ensure the pipeline exists for the final effect drawing to the
        // swapchain, for the render pass with the up-to-date image format.
        GuestOutputPaintEffect swapchain_effect =
            guest_output_flow.effects[guest_output_flow.effect_count - 1];
        PaintContext::GuestOutputPaintPipeline& swapchain_effect_pipeline =
            paint_context_
                .guest_output_paint_pipelines[size_t(swapchain_effect)];
        if (swapchain_effect_pipeline.swapchain_pipeline != VK_NULL_HANDLE &&
            swapchain_effect_pipeline.swapchain_format !=
                paint_context_.swapchain_render_pass_format) {
          paint_context_.submission_tracker.AwaitSubmissionCompletion(
              paint_context_.guest_output_image_paint_last_submission);
          util::DestroyAndNullHandle(
              dfn.vkDestroyPipeline, device,
              swapchain_effect_pipeline.swapchain_pipeline);
        }
        if (swapchain_effect_pipeline.swapchain_pipeline == VK_NULL_HANDLE) {
          assert_true(CanGuestOutputPaintEffectBeFinal(swapchain_effect));
          assert_true(guest_output_paint_fs_[size_t(swapchain_effect)] !=
                      VK_NULL_HANDLE);
          swapchain_effect_pipeline.swapchain_pipeline =
              CreateGuestOutputPaintPipeline(
                  swapchain_effect, paint_context_.swapchain_render_pass);
          if (swapchain_effect_pipeline.swapchain_pipeline == VK_NULL_HANDLE) {
            guest_output_flow.effect_count = 0;
          }
        }
      }

      if (guest_output_flow.effect_count) {
        // Actually draw the guest output.
        paint_context_
            .guest_output_image_paint_refs[guest_output_image_paint_ref_index]
            .first = current_paint_submission_index;
        paint_context_.guest_output_image_paint_last_submission =
            current_paint_submission_index;
        VkViewport guest_output_viewport;
        guest_output_viewport.x = 0.0f;
        guest_output_viewport.y = 0.0f;
        guest_output_viewport.minDepth = 0.0f;
        guest_output_viewport.maxDepth = 1.0f;
        VkRect2D guest_output_scissor;
        guest_output_scissor.offset.x = 0;
        guest_output_scissor.offset.y = 0;
        if (guest_output_flow.effect_count > 1) {
          paint_context_.guest_output_intermediate_image_last_submission =
              current_paint_submission_index;
        }
        for (size_t i = 0; i < guest_output_flow.effect_count; ++i) {
          bool is_final_effect = i + 1 >= guest_output_flow.effect_count;

          int32_t effect_rect_x, effect_rect_y;
          std::pair<uint32_t, uint32_t> effect_rect_size =
              guest_output_flow.effect_output_sizes[i];
          if (is_final_effect) {
            effect_rect_x = guest_output_flow.output_x;
            effect_rect_y = guest_output_flow.output_y;
            dfn.vkCmdBeginRenderPass(draw_command_buffer,
                                     &swapchain_render_pass_begin_info,
                                     VK_SUBPASS_CONTENTS_INLINE);
            swapchain_image_pass_begun = true;
            guest_output_viewport.width =
                float(paint_context_.swapchain_extent.width);
            guest_output_viewport.height =
                float(paint_context_.swapchain_extent.height);
            guest_output_scissor.extent.width =
                paint_context_.swapchain_extent.width;
            guest_output_scissor.extent.height =
                paint_context_.swapchain_extent.height;
          } else {
            effect_rect_x = 0;
            effect_rect_y = 0;
            VkRenderPassBeginInfo intermediate_render_pass_begin_info;
            intermediate_render_pass_begin_info.sType =
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            intermediate_render_pass_begin_info.pNext = nullptr;
            intermediate_render_pass_begin_info.renderPass =
                guest_output_intermediate_render_pass_;
            intermediate_render_pass_begin_info.framebuffer =
                paint_context_.guest_output_intermediate_framebuffers[i];
            intermediate_render_pass_begin_info.renderArea.offset.x = 0;
            intermediate_render_pass_begin_info.renderArea.offset.y = 0;
            intermediate_render_pass_begin_info.renderArea.extent.width =
                effect_rect_size.first;
            intermediate_render_pass_begin_info.renderArea.extent.height =
                effect_rect_size.second;
            intermediate_render_pass_begin_info.clearValueCount = 0;
            intermediate_render_pass_begin_info.pClearValues = nullptr;
            dfn.vkCmdBeginRenderPass(draw_command_buffer,
                                     &intermediate_render_pass_begin_info,
                                     VK_SUBPASS_CONTENTS_INLINE);
            guest_output_viewport.width = float(effect_rect_size.first);
            guest_output_viewport.height = float(effect_rect_size.second);
            guest_output_scissor.extent.width = effect_rect_size.first;
            guest_output_scissor.extent.height = effect_rect_size.second;
          }
          dfn.vkCmdSetViewport(draw_command_buffer, 0, 1,
                               &guest_output_viewport);
          dfn.vkCmdSetScissor(draw_command_buffer, 0, 1, &guest_output_scissor);

          GuestOutputPaintEffect effect = guest_output_flow.effects[i];

          const PaintContext::GuestOutputPaintPipeline& effect_pipeline =
              paint_context_.guest_output_paint_pipelines[size_t(effect)];
          VkPipeline effect_vulkan_pipeline =
              is_final_effect ? effect_pipeline.swapchain_pipeline
                              : effect_pipeline.intermediate_pipeline;
          assert_true(effect_vulkan_pipeline != VK_NULL_HANDLE);
          dfn.vkCmdBindPipeline(draw_command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                effect_vulkan_pipeline);

          GuestOutputPaintPipelineLayoutIndex
              guest_output_paint_pipeline_layout_index =
                  GetGuestOutputPaintPipelineLayoutIndex(effect);
          VkPipelineLayout guest_output_paint_pipeline_layout =
              guest_output_paint_pipeline_layouts_
                  [guest_output_paint_pipeline_layout_index];

          PaintContext::GuestOutputDescriptorSet effect_src_descriptor_set;
          if (i) {
            effect_src_descriptor_set = PaintContext::GuestOutputDescriptorSet(
                PaintContext::kGuestOutputDescriptorSetIntermediate0Sampled +
                (i - 1));
          } else {
            effect_src_descriptor_set = PaintContext::GuestOutputDescriptorSet(
                PaintContext::kGuestOutputDescriptorSetGuestOutput0Sampled +
                guest_output_image_paint_ref_index);
          }
          dfn.vkCmdBindDescriptorSets(
              draw_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
              guest_output_paint_pipeline_layout, 0, 1,
              &paint_context_
                   .guest_output_descriptor_sets[effect_src_descriptor_set],
              0, nullptr);

          GuestOutputPaintRectangleConstants effect_rect_constants;
          float effect_x_to_ndc = 2.0f / guest_output_viewport.width;
          float effect_y_to_ndc = 2.0f / guest_output_viewport.height;
          effect_rect_constants.x =
              -1.0f + float(effect_rect_x) * effect_x_to_ndc;
          effect_rect_constants.y =
              -1.0f + float(effect_rect_y) * effect_y_to_ndc;
          effect_rect_constants.width =
              float(effect_rect_size.first) * effect_x_to_ndc;
          effect_rect_constants.height =
              float(effect_rect_size.second) * effect_y_to_ndc;
          dfn.vkCmdPushConstants(
              draw_command_buffer, guest_output_paint_pipeline_layout,
              VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(effect_rect_constants),
              &effect_rect_constants);

          uint32_t effect_constants_size = 0;
          union {
            BilinearConstants bilinear;
            CasSharpenConstants cas_sharpen;
            CasResampleConstants cas_resample;
            FsrEasuConstants fsr_easu;
            FsrRcasConstants fsr_rcas;
          } effect_constants;
          switch (guest_output_paint_pipeline_layout_index) {
            case kGuestOutputPaintPipelineLayoutIndexBilinear: {
              effect_constants_size = sizeof(effect_constants.bilinear);
              effect_constants.bilinear.Initialize(guest_output_flow, i);
            } break;
            case kGuestOutputPaintPipelineLayoutIndexCasSharpen: {
              effect_constants_size = sizeof(effect_constants.cas_sharpen);
              effect_constants.cas_sharpen.Initialize(
                  guest_output_flow, i, guest_output_paint_config);
            } break;
            case kGuestOutputPaintPipelineLayoutIndexCasResample: {
              effect_constants_size = sizeof(effect_constants.cas_resample);
              effect_constants.cas_resample.Initialize(
                  guest_output_flow, i, guest_output_paint_config);
            } break;
            case kGuestOutputPaintPipelineLayoutIndexFsrEasu: {
              effect_constants_size = sizeof(effect_constants.fsr_easu);
              effect_constants.fsr_easu.Initialize(guest_output_flow, i);
            } break;
            case kGuestOutputPaintPipelineLayoutIndexFsrRcas: {
              effect_constants_size = sizeof(effect_constants.fsr_rcas);
              effect_constants.fsr_rcas.Initialize(guest_output_flow, i,
                                                   guest_output_paint_config);
            } break;
            default:
              break;
          }
          if (effect_constants_size) {
            dfn.vkCmdPushConstants(
                draw_command_buffer, guest_output_paint_pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(effect_rect_constants),
                effect_constants_size, &effect_constants);
          }

          dfn.vkCmdDraw(draw_command_buffer, 4, 1, 0, 0);

          if (is_final_effect) {
            // Clear the letterbox around the guest output if the guest output
            // doesn't cover the entire swapchain image.
            if (swapchain_image_clear_needed &&
                guest_output_flow.letterbox_clear_rectangle_count) {
              VkClearRect letterbox_clear_vulkan_rectangles
                  [GuestOutputPaintFlow::kMaxClearRectangles];
              for (size_t i = 0;
                   i < guest_output_flow.letterbox_clear_rectangle_count; ++i) {
                VkClearRect& letterbox_clear_vulkan_rectangle =
                    letterbox_clear_vulkan_rectangles[i];
                const GuestOutputPaintFlow::ClearRectangle&
                    letterbox_clear_rectangle =
                        guest_output_flow.letterbox_clear_rectangles[i];
                letterbox_clear_vulkan_rectangle.rect.offset.x =
                    int32_t(letterbox_clear_rectangle.x);
                letterbox_clear_vulkan_rectangle.rect.offset.y =
                    int32_t(letterbox_clear_rectangle.y);
                letterbox_clear_vulkan_rectangle.rect.extent.width =
                    letterbox_clear_rectangle.width;
                letterbox_clear_vulkan_rectangle.rect.extent.height =
                    letterbox_clear_rectangle.height;
                letterbox_clear_vulkan_rectangle.baseArrayLayer = 0;
                letterbox_clear_vulkan_rectangle.layerCount = 1;
              }
              dfn.vkCmdClearAttachments(
                  draw_command_buffer, 1, &swapchain_image_clear_attachment,
                  uint32_t(guest_output_flow.letterbox_clear_rectangle_count),
                  letterbox_clear_vulkan_rectangles);
            }
            swapchain_image_clear_needed = false;
          } else {
            // Still need the swapchain pass to be open for UI drawing.
            dfn.vkCmdEndRenderPass(draw_command_buffer);
          }
        }
      }
    }
  }

  // Release main target guest output image references that aren't needed
  // anymore (this is done after various potential guest-output-related main
  // target submission tracker waits so the completed submission value is the
  // most actual).
  uint64_t completed_paint_submission =
      paint_context_.submission_tracker.UpdateAndGetCompletedSubmission();
  for (std::pair<uint64_t, std::shared_ptr<GuestOutputImage>>&
           guest_output_image_paint_ref :
       paint_context_.guest_output_image_paint_refs) {
    if (!guest_output_image_paint_ref.second ||
        guest_output_image_paint_ref.second == guest_output_image) {
      continue;
    }
    if (completed_paint_submission >= guest_output_image_paint_ref.first) {
      guest_output_image_paint_ref.second.reset();
    }
  }

  // If hasn't presented the guest output, begin the pass to clear and, if
  // needed, to draw the UI.
  if (!swapchain_image_pass_begun) {
    dfn.vkCmdBeginRenderPass(draw_command_buffer,
                             &swapchain_render_pass_begin_info,
                             VK_SUBPASS_CONTENTS_INLINE);
  }
  if (swapchain_image_clear_needed) {
    VkClearRect swapchain_image_clear_rectangle;
    swapchain_image_clear_rectangle.rect.offset.x = 0;
    swapchain_image_clear_rectangle.rect.offset.y = 0;
    swapchain_image_clear_rectangle.rect.extent =
        paint_context_.swapchain_extent;
    swapchain_image_clear_rectangle.baseArrayLayer = 0;
    swapchain_image_clear_rectangle.layerCount = 1;
    dfn.vkCmdClearAttachments(draw_command_buffer, 1,
                              &swapchain_image_clear_attachment, 1,
                              &swapchain_image_clear_rectangle);
    swapchain_image_clear_needed = false;
  }

  if (execute_ui_drawers) {
    // Draw the UI.
    VulkanUIDrawContext ui_draw_context(
        *this, paint_context_.swapchain_extent.width,
        paint_context_.swapchain_extent.height, draw_command_buffer,
        ui_submission_tracker_.GetCurrentSubmission(),
        ui_submission_tracker_.UpdateAndGetCompletedSubmission(),
        paint_context_.swapchain_render_pass,
        paint_context_.swapchain_render_pass_format);
    ExecuteUIDrawersFromUIThread(ui_draw_context);
  }

  dfn.vkCmdEndRenderPass(draw_command_buffer);

  dfn.vkEndCommandBuffer(draw_command_buffer);

  VkPipelineStageFlags acquire_semaphore_wait_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkCommandBuffer command_buffers[2];
  uint32_t command_buffer_count = 0;
  // UI setup command buffers must be accessed only if execute_ui_drawers is not
  // null, to identify the UI thread.
  size_t ui_setup_command_buffer_index =
      execute_ui_drawers ? paint_context_.ui_setup_command_buffer_current_index
                         : SIZE_MAX;
  if (ui_setup_command_buffer_index != SIZE_MAX) {
    PaintContext::UISetupCommandBuffer& ui_setup_command_buffer =
        paint_context_.ui_setup_command_buffers[ui_setup_command_buffer_index];
    dfn.vkEndCommandBuffer(ui_setup_command_buffer.command_buffer);
    command_buffers[command_buffer_count++] =
        ui_setup_command_buffer.command_buffer;
    // Release the current UI setup command buffer regardless of submission
    // result. Failed submissions (if the UI submission index wasn't incremented
    // since the previous draw) should be handled by UI drawers themselves by
    // retrying all the failed work if needed.
    paint_context_.ui_setup_command_buffer_current_index = SIZE_MAX;
  }
  command_buffers[command_buffer_count++] = draw_command_buffer;
  VkSemaphore present_semaphore = paint_submission.present_semaphore();
  VkSubmitInfo submit_info;
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = nullptr;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &acquire_semaphore;
  submit_info.pWaitDstStageMask = &acquire_semaphore_wait_stage;
  submit_info.commandBufferCount = command_buffer_count;
  submit_info.pCommandBuffers = command_buffers;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &present_semaphore;
  {
    VulkanSubmissionTracker::FenceAcquisition fence_acqusition(
        paint_context_.submission_tracker.AcquireFenceToAdvanceSubmission());
    // Also update the submission tracker giving submission indices to UI draw
    // callbacks if submission is successful.
    VulkanSubmissionTracker::FenceAcquisition ui_fence_acquisition;
    if (execute_ui_drawers) {
      ui_fence_acquisition =
          ui_submission_tracker_.AcquireFenceToAdvanceSubmission();
    }
    VkResult submit_result;
    {
      VulkanProvider::QueueAcquisition queue_acquisition(
          provider_.AcquireQueue(provider_.queue_family_graphics_compute(), 0));
      submit_result = dfn.vkQueueSubmit(queue_acquisition.queue, 1,
                                        &submit_info, fence_acqusition.fence());
      if (ui_fence_acquisition.fence() != VK_NULL_HANDLE &&
          submit_result == VK_SUCCESS) {
        if (dfn.vkQueueSubmit(queue_acquisition.queue, 0, nullptr,
                              ui_fence_acquisition.fence()) != VK_SUCCESS) {
          ui_fence_acquisition.SubmissionSucceededSignalFailed();
        }
      }
    }
    if (submit_result != VK_SUCCESS) {
      XELOGE("VulkanPresenter: Failed to submit command buffers");
      fence_acqusition.SubmissionFailedOrDropped();
      ui_fence_acquisition.SubmissionFailedOrDropped();
      if (ui_setup_command_buffer_index != SIZE_MAX) {
        // If failed to submit, make the UI setup command buffer available for
        // immediate reuse, as the completed submission index won't be updated
        // to the current index, and failing submissions with setup command
        // buffer over and over will result in never reusing the setup command
        // buffers.
        paint_context_.ui_setup_command_buffers[ui_setup_command_buffer_index]
            .last_usage_submission_index = 0;
      }
      // The image is in an acquired state - but now, it will be in it forever.
      // To avoid that, recreate the swapchain - don't return just
      // kNotPresented.
      return PaintResult::kNotPresentedConnectionOutdated;
    }
  }

  VkPresentInfoKHR present_info;
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = nullptr;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &present_semaphore;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &paint_context_.swapchain;
  present_info.pImageIndices = &swapchain_image_index;
  present_info.pResults = nullptr;
  VkResult present_result;
  {
    VulkanProvider::QueueAcquisition queue_acquisition(
        provider_.AcquireQueue(paint_context_.present_queue_family, 0));
    present_result =
        dfn.vkQueuePresentKHR(queue_acquisition.queue, &present_info);
  }
  switch (present_result) {
    case VK_SUCCESS:
      return PaintResult::kPresented;
    case VK_SUBOPTIMAL_KHR:
      return PaintResult::kPresentedSuboptimal;
    case VK_ERROR_DEVICE_LOST:
      XELOGE(
          "VulkanPresenter: Failed to present the swapchain image as the "
          "device has been lost");
      return PaintResult::kGpuLostResponsible;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_SURFACE_LOST_KHR:
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
      // Not an error, reporting just as info (may normally occur while resizing
      // on some platforms).
      XELOGVK(
          "VulkanPresenter: Presentation to the swapchain image has been "
          "dropped as the swapchain or the surface has become outdated");
      // Note that the semaphore wait (followed by reset) has been enqueued,
      // however, this should have no effect on anything here likely.
      return PaintResult::kNotPresentedConnectionOutdated;
    default:
      XELOGE("VulkanPresenter: Failed to present the swapchain image");
      // The image is in an acquired state - but now, it will be in it forever.
      // To avoid that, recreate the swapchain - don't return just
      // kNotPresented.
      return PaintResult::kNotPresentedConnectionOutdated;
  }
}

bool VulkanPresenter::InitializeSurfaceIndependent() {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkDescriptorSetLayoutBinding guest_output_image_sampler_bindings[2];
  guest_output_image_sampler_bindings[0].binding = 0;
  guest_output_image_sampler_bindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  guest_output_image_sampler_bindings[0].descriptorCount = 1;
  guest_output_image_sampler_bindings[0].stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT;
  guest_output_image_sampler_bindings[0].pImmutableSamplers = nullptr;
  VkSampler sampler_linear_clamp =
      provider_.GetHostSampler(VulkanProvider::HostSampler::kLinearClamp);
  guest_output_image_sampler_bindings[1].binding = 1;
  guest_output_image_sampler_bindings[1].descriptorType =
      VK_DESCRIPTOR_TYPE_SAMPLER;
  guest_output_image_sampler_bindings[1].descriptorCount = 1;
  guest_output_image_sampler_bindings[1].stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT;
  guest_output_image_sampler_bindings[1].pImmutableSamplers =
      &sampler_linear_clamp;
  VkDescriptorSetLayoutCreateInfo
      guest_output_paint_image_descriptor_set_layout_create_info;
  guest_output_paint_image_descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  guest_output_paint_image_descriptor_set_layout_create_info.pNext = nullptr;
  guest_output_paint_image_descriptor_set_layout_create_info.flags = 0;
  guest_output_paint_image_descriptor_set_layout_create_info.bindingCount =
      uint32_t(xe::countof(guest_output_image_sampler_bindings));
  guest_output_paint_image_descriptor_set_layout_create_info.pBindings =
      guest_output_image_sampler_bindings;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &guest_output_paint_image_descriptor_set_layout_create_info,
          nullptr,
          &guest_output_paint_image_descriptor_set_layout_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create the guest output image descriptor "
        "set layout");
    return false;
  }

  VkPushConstantRange guest_output_paint_push_constant_ranges[2];
  VkPushConstantRange& guest_output_paint_push_constant_range_rect =
      guest_output_paint_push_constant_ranges[0];
  guest_output_paint_push_constant_range_rect.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT;
  guest_output_paint_push_constant_range_rect.offset = 0;
  guest_output_paint_push_constant_range_rect.size =
      sizeof(GuestOutputPaintRectangleConstants);
  VkPushConstantRange& guest_output_paint_push_constant_range_ffx =
      guest_output_paint_push_constant_ranges[1];
  guest_output_paint_push_constant_range_ffx.stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT;
  guest_output_paint_push_constant_range_ffx.offset =
      guest_output_paint_push_constant_ranges[0].offset +
      guest_output_paint_push_constant_ranges[0].size;
  VkPipelineLayoutCreateInfo guest_output_paint_pipeline_layout_create_info;
  guest_output_paint_pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  guest_output_paint_pipeline_layout_create_info.pNext = nullptr;
  guest_output_paint_pipeline_layout_create_info.flags = 0;
  guest_output_paint_pipeline_layout_create_info.setLayoutCount = 1;
  guest_output_paint_pipeline_layout_create_info.pSetLayouts =
      &guest_output_paint_image_descriptor_set_layout_;
  guest_output_paint_pipeline_layout_create_info.pPushConstantRanges =
      guest_output_paint_push_constant_ranges;
  for (size_t i = 0; i < size_t(kGuestOutputPaintPipelineLayoutCount); ++i) {
    switch (GuestOutputPaintPipelineLayoutIndex(i)) {
      case kGuestOutputPaintPipelineLayoutIndexBilinear:
        guest_output_paint_push_constant_range_ffx.size =
            sizeof(BilinearConstants);
        break;
      case kGuestOutputPaintPipelineLayoutIndexCasSharpen:
        guest_output_paint_push_constant_range_ffx.size =
            sizeof(CasSharpenConstants);
        break;
      case kGuestOutputPaintPipelineLayoutIndexCasResample:
        guest_output_paint_push_constant_range_ffx.size =
            sizeof(CasResampleConstants);
        break;
      case kGuestOutputPaintPipelineLayoutIndexFsrEasu:
        guest_output_paint_push_constant_range_ffx.size =
            sizeof(FsrEasuConstants);
        break;
      case kGuestOutputPaintPipelineLayoutIndexFsrRcas:
        guest_output_paint_push_constant_range_ffx.size =
            sizeof(FsrRcasConstants);
        break;
      default:
        assert_unhandled_case(GuestOutputPaintPipelineLayoutIndex(i));
        continue;
    }
    guest_output_paint_pipeline_layout_create_info.pushConstantRangeCount =
        1 + uint32_t(guest_output_paint_push_constant_range_ffx.size != 0);
    if (dfn.vkCreatePipelineLayout(
            device, &guest_output_paint_pipeline_layout_create_info, nullptr,
            &guest_output_paint_pipeline_layouts_[i]) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to create a guest output presentation "
          "pipeline layout with {} bytes of push constants",
          guest_output_paint_push_constant_range_rect.size +
              guest_output_paint_push_constant_range_ffx.size);
      return false;
    }
  }

  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize =
      sizeof(shaders::guest_output_triangle_strip_rect_vs);
  shader_module_create_info.pCode =
      shaders::guest_output_triangle_strip_rect_vs;
  if (dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,
                               &guest_output_paint_vs_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create the guest output presentation "
        "vertex shader module");
    return false;
  }
  for (size_t i = 0; i < size_t(GuestOutputPaintEffect::kCount); ++i) {
    GuestOutputPaintEffect guest_output_paint_effect =
        GuestOutputPaintEffect(i);
    switch (guest_output_paint_effect) {
      case GuestOutputPaintEffect::kBilinear:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_bilinear_ps);
        shader_module_create_info.pCode = shaders::guest_output_bilinear_ps;
        break;
      case GuestOutputPaintEffect::kBilinearDither:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_bilinear_dither_ps);
        shader_module_create_info.pCode =
            shaders::guest_output_bilinear_dither_ps;
        break;
      case GuestOutputPaintEffect::kCasSharpen:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_cas_sharpen_ps);
        shader_module_create_info.pCode =
            shaders::guest_output_ffx_cas_sharpen_ps;
        break;
      case GuestOutputPaintEffect::kCasSharpenDither:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_cas_sharpen_dither_ps);
        shader_module_create_info.pCode =
            shaders::guest_output_ffx_cas_sharpen_dither_ps;
        break;
      case GuestOutputPaintEffect::kCasResample:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_cas_resample_ps);
        shader_module_create_info.pCode =
            shaders::guest_output_ffx_cas_resample_ps;
        break;
      case GuestOutputPaintEffect::kCasResampleDither:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_cas_resample_dither_ps);
        shader_module_create_info.pCode =
            shaders::guest_output_ffx_cas_resample_dither_ps;
        break;
      case GuestOutputPaintEffect::kFsrEasu:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_fsr_easu_ps);
        shader_module_create_info.pCode = shaders::guest_output_ffx_fsr_easu_ps;
        break;
      case GuestOutputPaintEffect::kFsrRcas:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_fsr_rcas_ps);
        shader_module_create_info.pCode = shaders::guest_output_ffx_fsr_rcas_ps;
        break;
      case GuestOutputPaintEffect::kFsrRcasDither:
        shader_module_create_info.codeSize =
            sizeof(shaders::guest_output_ffx_fsr_rcas_dither_ps);
        shader_module_create_info.pCode =
            shaders::guest_output_ffx_fsr_rcas_dither_ps;
        break;
      default:
        // Not supported by this implementation.
        continue;
    }
    if (dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,
                                 &guest_output_paint_fs_[i]) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to create the guest output painting shader "
          "module for effect {}",
          i);
      return false;
    }
  }

  VkAttachmentDescription intermediate_render_pass_attachment;
  intermediate_render_pass_attachment.flags = 0;
  intermediate_render_pass_attachment.format = kGuestOutputFormat;
  intermediate_render_pass_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  intermediate_render_pass_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  intermediate_render_pass_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  intermediate_render_pass_attachment.stencilLoadOp =
      VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  intermediate_render_pass_attachment.stencilStoreOp =
      VK_ATTACHMENT_STORE_OP_DONT_CARE;
  intermediate_render_pass_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  intermediate_render_pass_attachment.finalLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference intermediate_render_pass_color_attachment;
  intermediate_render_pass_color_attachment.attachment = 0;
  intermediate_render_pass_color_attachment.layout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription intermediate_render_pass_subpass = {};
  intermediate_render_pass_subpass.pipelineBindPoint =
      VK_PIPELINE_BIND_POINT_GRAPHICS;
  intermediate_render_pass_subpass.colorAttachmentCount = 1;
  intermediate_render_pass_subpass.pColorAttachments =
      &intermediate_render_pass_color_attachment;
  VkSubpassDependency intermediate_render_pass_dependencies[2];
  intermediate_render_pass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  intermediate_render_pass_dependencies[0].dstSubpass = 0;
  intermediate_render_pass_dependencies[0].srcStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  intermediate_render_pass_dependencies[0].dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  intermediate_render_pass_dependencies[0].srcAccessMask =
      VK_ACCESS_SHADER_READ_BIT;
  intermediate_render_pass_dependencies[0].dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  intermediate_render_pass_dependencies[0].dependencyFlags = 0;
  intermediate_render_pass_dependencies[1].srcSubpass = 0;
  intermediate_render_pass_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  intermediate_render_pass_dependencies[1].srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  intermediate_render_pass_dependencies[1].dstStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  intermediate_render_pass_dependencies[1].srcAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  intermediate_render_pass_dependencies[1].dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT;
  intermediate_render_pass_dependencies[1].dependencyFlags = 0;
  VkRenderPassCreateInfo intermediate_render_pass_create_info;
  intermediate_render_pass_create_info.sType =
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  intermediate_render_pass_create_info.pNext = nullptr;
  intermediate_render_pass_create_info.flags = 0;
  intermediate_render_pass_create_info.attachmentCount = 1;
  intermediate_render_pass_create_info.pAttachments =
      &intermediate_render_pass_attachment;
  intermediate_render_pass_create_info.subpassCount = 1;
  intermediate_render_pass_create_info.pSubpasses =
      &intermediate_render_pass_subpass;
  intermediate_render_pass_create_info.dependencyCount =
      uint32_t(xe::countof(intermediate_render_pass_dependencies));
  intermediate_render_pass_create_info.pDependencies =
      intermediate_render_pass_dependencies;
  if (dfn.vkCreateRenderPass(
          device, &intermediate_render_pass_create_info, nullptr,
          &guest_output_intermediate_render_pass_) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create the guest output intermediate image "
        "render pass");
    return false;
  }

  // Initialize connection-independent parts of the painting context.

  for (size_t i = 0; i < paint_context_.submissions.size(); ++i) {
    paint_context_.submissions[i] = PaintContext::Submission::Create(provider_);
    if (!paint_context_.submissions[i]) {
      return false;
    }
  }

  // Guest output paint pipelines drawing to intermediate images, not depending
  // on runtime state unlike ones drawing to the swapchain images as those
  // depend on the swapchain format.
  for (size_t i = 0; i < size_t(GuestOutputPaintEffect::kCount); ++i) {
    if (!CanGuestOutputPaintEffectBeIntermediate(GuestOutputPaintEffect(i)) ||
        guest_output_paint_fs_[i] == VK_NULL_HANDLE) {
      continue;
    }
    VkPipeline guest_output_paint_intermediate_pipeline =
        CreateGuestOutputPaintPipeline(GuestOutputPaintEffect(i),
                                       guest_output_intermediate_render_pass_);
    if (guest_output_paint_intermediate_pipeline == VK_NULL_HANDLE) {
      return false;
    }
    paint_context_.guest_output_paint_pipelines[i].intermediate_pipeline =
        guest_output_paint_intermediate_pipeline;
  }

  // Guest output painting descriptor sets.
  VkDescriptorPoolSize guest_output_paint_descriptor_pool_sizes[2];
  guest_output_paint_descriptor_pool_sizes[0].type =
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  guest_output_paint_descriptor_pool_sizes[0].descriptorCount =
      PaintContext::kGuestOutputDescriptorSetCount;
  // Required even when using immutable samplers, otherwise failing to allocate
  // descriptor sets (tested on AMD Software: Adrenalin Edition 22.3.2 on
  // Windows 10 on AMD Radeon RX Vega 10 with Vulkan validation enabled).
  guest_output_paint_descriptor_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  guest_output_paint_descriptor_pool_sizes[1].descriptorCount =
      PaintContext::kGuestOutputDescriptorSetCount;
  VkDescriptorPoolCreateInfo guest_output_paint_descriptor_pool_create_info;
  guest_output_paint_descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  guest_output_paint_descriptor_pool_create_info.pNext = nullptr;
  guest_output_paint_descriptor_pool_create_info.flags = 0;
  guest_output_paint_descriptor_pool_create_info.maxSets =
      PaintContext::kGuestOutputDescriptorSetCount;
  guest_output_paint_descriptor_pool_create_info.poolSizeCount =
      uint32_t(xe::countof(guest_output_paint_descriptor_pool_sizes));
  guest_output_paint_descriptor_pool_create_info.pPoolSizes =
      guest_output_paint_descriptor_pool_sizes;
  if (dfn.vkCreateDescriptorPool(
          device, &guest_output_paint_descriptor_pool_create_info, nullptr,
          &paint_context_.guest_output_descriptor_pool) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create the guest output painting "
        "descriptor pool");
    return false;
  }
  VkDescriptorSetLayout guest_output_paint_descriptor_set_layouts
      [PaintContext::kGuestOutputDescriptorSetCount];
  std::fill(guest_output_paint_descriptor_set_layouts,
            guest_output_paint_descriptor_set_layouts +
                xe::countof(guest_output_paint_descriptor_set_layouts),
            guest_output_paint_image_descriptor_set_layout_);
  VkDescriptorSetAllocateInfo guest_output_paint_descriptor_set_allocate_info;
  guest_output_paint_descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  guest_output_paint_descriptor_set_allocate_info.pNext = nullptr;
  guest_output_paint_descriptor_set_allocate_info.descriptorPool =
      paint_context_.guest_output_descriptor_pool;
  guest_output_paint_descriptor_set_allocate_info.descriptorSetCount =
      PaintContext::kGuestOutputDescriptorSetCount;
  guest_output_paint_descriptor_set_allocate_info.pSetLayouts =
      guest_output_paint_descriptor_set_layouts;
  if (dfn.vkAllocateDescriptorSets(
          device, &guest_output_paint_descriptor_set_allocate_info,
          paint_context_.guest_output_descriptor_sets) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to allocate the guest output painting "
        "descriptor sets");
    return false;
  }

  return InitializeCommonSurfaceIndependent();
}

VkPipeline VulkanPresenter::CreateGuestOutputPaintPipeline(
    GuestOutputPaintEffect effect, VkRenderPass render_pass) {
  VkPipelineShaderStageCreateInfo stages[2] = {};
  for (uint32_t i = 0; i < 2; ++i) {
    VkPipelineShaderStageCreateInfo& stage = stages[i];
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    stage.pName = "main";
  }
  stages[0].module = guest_output_paint_vs_;
  stages[1].module = guest_output_paint_fs_[size_t(effect)];
  assert_true(stages[1].module != VK_NULL_HANDLE);

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
  vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
  input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

  VkPipelineViewportStateCreateInfo viewport_state = {};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterization_state = {};
  rasterization_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_state.cullMode = VK_CULL_MODE_NONE;
  rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample_state = {};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
  color_blend_attachment_state.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend_state = {};
  color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.attachmentCount = 1;
  color_blend_state.pAttachments = &color_blend_attachment_state;

  static const VkDynamicState kPipelineDynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo dynamic_state = {};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount =
      uint32_t(xe::countof(kPipelineDynamicStates));
  dynamic_state.pDynamicStates = kPipelineDynamicStates;

  VkGraphicsPipelineCreateInfo pipeline_create_info;
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = nullptr;
  pipeline_create_info.flags = 0;
  pipeline_create_info.stageCount = uint32_t(xe::countof(stages));
  pipeline_create_info.pStages = stages;
  pipeline_create_info.pVertexInputState = &vertex_input_state;
  pipeline_create_info.pInputAssemblyState = &input_assembly_state;
  pipeline_create_info.pTessellationState = nullptr;
  pipeline_create_info.pViewportState = &viewport_state;
  pipeline_create_info.pRasterizationState = &rasterization_state;
  pipeline_create_info.pMultisampleState = &multisample_state;
  pipeline_create_info.pDepthStencilState = nullptr;
  pipeline_create_info.pColorBlendState = &color_blend_state;
  pipeline_create_info.pDynamicState = &dynamic_state;
  pipeline_create_info.layout = guest_output_paint_pipeline_layouts_
      [GetGuestOutputPaintPipelineLayoutIndex(effect)];
  pipeline_create_info.renderPass = render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = -1;

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkPipeline pipeline;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                    &pipeline_create_info, nullptr,
                                    &pipeline) != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to create the guest output painting pipeline "
        "for effect {}",
        size_t(effect));
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
