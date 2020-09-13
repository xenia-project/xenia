/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_context.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

#if XE_PLATFORM_ANDROID
#include <android/native_window.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace xe {
namespace ui {
namespace vulkan {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

bool VulkanContext::Initialize() {
  context_lost_ = false;

  if (!target_window_) {
    return true;
  }

  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkFenceCreateInfo fence_create_info;
  fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_create_info.pNext = nullptr;
  fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VkCommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.pNext = nullptr;
  command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_create_info.queueFamilyIndex =
      provider.queue_family_graphics_compute();

  VkCommandBufferAllocateInfo command_buffer_allocate_info;
  command_buffer_allocate_info.sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.pNext = nullptr;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;

  for (uint32_t i = 0; i < kSwapchainMaxImageCount; ++i) {
    SwapSubmission& submission = swap_submissions_[i];
    if (dfn.vkCreateFence(device, &fence_create_info, nullptr,
                          &submission.fence) != VK_SUCCESS) {
      XELOGE("Failed to create the Vulkan composition fences");
      Shutdown();
      return false;
    }
    if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                                &submission.command_pool) != VK_SUCCESS) {
      XELOGE("Failed to create the Vulkan composition command pools");
      Shutdown();
      return false;
    }
    command_buffer_allocate_info.commandPool = submission.command_pool;
    if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                     &submission.command_buffer) !=
        VK_SUCCESS) {
      XELOGE("Failed to allocate the Vulkan composition command buffers");
      Shutdown();
      return false;
    }
  }

  VkSemaphoreCreateInfo semaphore_create_info;
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_create_info.pNext = nullptr;
  semaphore_create_info.flags = 0;
  if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                            &swap_image_acquisition_semaphore_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the Vulkan swap chain image acquisition semaphore");
    Shutdown();
    return false;
  }
  if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                            &swap_render_completion_semaphore_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the Vulkan swap chain rendering completion "
        "semaphore");
    Shutdown();
    return false;
  }

  immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(*this);
  // TODO(Triang3l): Initialize the immediate drawer.

  swap_swapchain_or_surface_recreation_needed_ = true;

  return true;
}

void VulkanContext::Shutdown() {
  if (!target_window_) {
    return;
  }

  AwaitAllSwapSubmissionsCompletion();

  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::InstanceFunctions& ifn = provider.ifn();
  VkInstance instance = provider.instance();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  swap_swapchain_image_current_ = UINT32_MAX;
  DestroySwapchainFramebuffers();
  util::DestroyAndNullHandle(dfn.vkDestroySwapchainKHR, device,
                             swap_swapchain_);
  util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                             swap_render_pass_);
  util::DestroyAndNullHandle(ifn.vkDestroySurfaceKHR, instance, swap_surface_);
  swap_swapchain_or_surface_recreation_needed_ = false;

  util::DestroyAndNullHandle(dfn.vkDestroySemaphore, device,
                             swap_render_completion_semaphore_);
  util::DestroyAndNullHandle(dfn.vkDestroySemaphore, device,
                             swap_image_acquisition_semaphore_);

  for (uint32_t i = 0; i < kSwapchainMaxImageCount; ++i) {
    SwapSubmission& submission = swap_submissions_[i];
    util::DestroyAndNullHandle(dfn.vkDestroyCommandPool, device,
                               submission.command_pool);
    util::DestroyAndNullHandle(dfn.vkDestroyFence, device, submission.fence);
  }
  swap_submission_current_ = 1;
  swap_submission_completed_ = 0;
}

ImmediateDrawer* VulkanContext::immediate_drawer() {
  return immediate_drawer_.get();
}

bool VulkanContext::WasLost() { return context_lost_; }

bool VulkanContext::BeginSwap() {
  if (!target_window_ || context_lost_) {
    return false;
  }

  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::InstanceFunctions& ifn = provider.ifn();
  VkPhysicalDevice physical_device = provider.physical_device();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  uint32_t window_width = uint32_t(target_window_->scaled_width());
  uint32_t window_height = uint32_t(target_window_->scaled_height());
  if (swap_swapchain_ != VK_NULL_HANDLE) {
    // Check if need to resize.
    assert_true(swap_surface_ != VK_NULL_HANDLE);
    // Win32 has minImageExtent == maxImageExtent == currentExtent, so the
    // capabilities need to be requested every time they are needed.
    VkSurfaceCapabilitiesKHR surface_capabilities;
    if (ifn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physical_device, swap_surface_, &surface_capabilities) ==
        VK_SUCCESS) {
      if (swap_swapchain_extent_.width !=
              xe::clamp(window_width, surface_capabilities.minImageExtent.width,
                        surface_capabilities.maxImageExtent.width) ||
          swap_swapchain_extent_.height !=
              xe::clamp(window_height,
                        surface_capabilities.minImageExtent.height,
                        surface_capabilities.maxImageExtent.height)) {
        swap_swapchain_or_surface_recreation_needed_ = true;
      }
    }
  }

  // If the swap chain turns out to be out of date, try to recreate it on the
  // second attempt (to avoid skipping the frame entirely in this case).
  for (uint32_t attempt = 0; attempt < 2; ++attempt) {
    if (swap_swapchain_or_surface_recreation_needed_) {
      // If recreation fails, don't retry until some change happens.
      swap_swapchain_or_surface_recreation_needed_ = false;

      AwaitAllSwapSubmissionsCompletion();

      uint32_t queue_family_graphics_compute =
          provider.queue_family_graphics_compute();

      if (swap_surface_ == VK_NULL_HANDLE) {
        assert_true(swap_swapchain_ == VK_NULL_HANDLE);
        assert_true(swap_swapchain_image_views_.empty());
        assert_true(swap_swapchain_framebuffers_.empty());

        VkInstance instance = provider.instance();

        VkResult surface_create_result;
#if XE_PLATFORM_ANDROID
        VkAndroidSurfaceCreateInfoKHR surface_create_info;
        surface_create_info.window =
            reinterpret_cast<ANativeWindow*>(target_window_->native_handle());
        if (!surface_create_info.window) {
          // The activity is in background - try again when the window is
          // created.
          return false;
        }
        surface_create_info.sType =
            VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_result = ifn.vkCreateAndroidSurfaceKHR(
            instance, &surface_create_info, nullptr, &swap_surface_);
#elif XE_PLATFORM_WIN32
        VkWin32SurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType =
            VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.hinstance = reinterpret_cast<HINSTANCE>(
            target_window_->native_platform_handle());
        surface_create_info.hwnd =
            reinterpret_cast<HWND>(target_window_->native_handle());
        surface_create_result = ifn.vkCreateWin32SurfaceKHR(
            instance, &surface_create_info, nullptr, &swap_surface_);
#else
#error No Vulkan surface creation for the target platform.
#endif
        if (surface_create_result != VK_SUCCESS) {
          XELOGE("Failed to create a Vulkan surface");
          return false;
        }

        // FIXME(Triang3l): Allow a separate queue for present - see
        // vulkan_provider.cc for details.
        VkBool32 surface_supported;
        if (ifn.vkGetPhysicalDeviceSurfaceSupportKHR(
                physical_device, queue_family_graphics_compute, swap_surface_,
                &surface_supported) != VK_SUCCESS ||
            !surface_supported) {
          XELOGE(
              "The Vulkan graphics and compute queue doesn't support "
              "presentation");
          ifn.vkDestroySurfaceKHR(instance, swap_surface_, nullptr);
          swap_surface_ = VK_NULL_HANDLE;
          return false;
        }

        // Choose an SDR format, 8.8.8.8 preferred, or if not available, any
        // supported. Windows and GNU/Linux use B8G8R8A8, Android uses R8G8B8A8.
        std::vector<VkSurfaceFormatKHR> surface_formats;
        VkResult surface_formats_get_result;
        for (;;) {
          uint32_t surface_format_count = uint32_t(surface_formats.size());
          bool surface_formats_was_empty = !surface_format_count;
          surface_formats_get_result = ifn.vkGetPhysicalDeviceSurfaceFormatsKHR(
              physical_device, swap_surface_, &surface_format_count,
              surface_formats_was_empty ? nullptr : surface_formats.data());
          // If the original surface format count was 0 (first call), SUCCESS is
          // returned, not INCOMPLETE.
          if (surface_formats_get_result == VK_SUCCESS ||
              surface_formats_get_result == VK_INCOMPLETE) {
            surface_formats.resize(surface_format_count);
            if (surface_formats_get_result == VK_SUCCESS &&
                (!surface_formats_was_empty || !surface_format_count)) {
              break;
            }
          } else {
            break;
          }
        }
        if (surface_formats_get_result != VK_SUCCESS ||
            surface_formats.empty()) {
          XELOGE("Failed to get Vulkan surface formats");
          ifn.vkDestroySurfaceKHR(instance, swap_surface_, nullptr);
          swap_surface_ = VK_NULL_HANDLE;
          return false;
        }
        VkSurfaceFormatKHR surface_format;
        if (surface_formats.size() == 1 &&
            surface_formats[0].format == VK_FORMAT_UNDEFINED) {
#if XE_PLATFORM_ANDROID
          surface_format.format = VK_FORMAT_R8G8B8A8_UNORM;
#else
          surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
#endif
          surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        } else {
          surface_format = surface_formats.front();
          for (const VkSurfaceFormatKHR& surface_format_current :
               surface_formats) {
            if (surface_format_current.format == VK_FORMAT_B8G8R8A8_UNORM ||
                surface_format_current.format == VK_FORMAT_R8G8B8A8_UNORM ||
                surface_format_current.format ==
                    VK_FORMAT_A8B8G8R8_UNORM_PACK32) {
              surface_format = surface_format_current;
              break;
            }
          }
        }
        if (swap_surface_format_.format != surface_format.format) {
          util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                                     swap_render_pass_);
        }
        swap_surface_format_ = surface_format;

        // Prefer a low-latency present mode because emulation is done on the
        // same queue, ordered by the decreasing amount of tearing, fall back to
        // FIFO if no other options.
        swap_surface_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
        std::vector<VkPresentModeKHR> surface_present_modes;
        VkResult surface_present_modes_get_result;
        for (;;) {
          uint32_t surface_present_mode_count =
              uint32_t(surface_present_modes.size());
          bool surface_present_modes_was_empty = !surface_present_mode_count;
          surface_present_modes_get_result =
              ifn.vkGetPhysicalDeviceSurfacePresentModesKHR(
                  physical_device, swap_surface_, &surface_present_mode_count,
                  surface_present_modes_was_empty
                      ? nullptr
                      : surface_present_modes.data());
          // If the original surface present mode count was 0 (first call),
          // SUCCESS is returned, not INCOMPLETE.
          if (surface_present_modes_get_result == VK_SUCCESS ||
              surface_present_modes_get_result == VK_INCOMPLETE) {
            surface_present_modes.resize(surface_present_mode_count);
            if (surface_present_modes_get_result == VK_SUCCESS &&
                (!surface_present_modes_was_empty ||
                 !surface_present_mode_count)) {
              break;
            }
          } else {
            break;
          }
        }
        if (surface_present_modes_get_result == VK_SUCCESS) {
          static const VkPresentModeKHR present_modes_preferred[] = {
              VK_PRESENT_MODE_MAILBOX_KHR,
              VK_PRESENT_MODE_FIFO_RELAXED_KHR,
              VK_PRESENT_MODE_IMMEDIATE_KHR,
          };
          for (size_t i = 0; i < xe::countof(present_modes_preferred); ++i) {
            VkPresentModeKHR present_mode_preferred =
                present_modes_preferred[i];
            if (std::find(surface_present_modes.cbegin(),
                          surface_present_modes.cend(),
                          present_mode_preferred) !=
                surface_present_modes.cend()) {
              swap_surface_present_mode_ = present_mode_preferred;
              break;
            }
          }
        }
      }

      // Recreate the swap chain unconditionally because a request was made.
      // The old swapchain will be retired even if vkCreateSwapchainKHR fails,
      // so destroy the framebuffers and the image views unconditionally.
      // If anything fails before the vkCreateSwapchainKHR call, also destroy
      // the swapchain to fulfill the request.
      // It was safe to handle errors while creating the surface without caring
      // about destroying the swapchain, because there can't be swapchain when
      // there is no surface.
      DestroySwapchainFramebuffers();
      // Win32 has minImageExtent == maxImageExtent == currentExtent, so the
      // capabilities need to be requested every time they are needed.
      VkSurfaceCapabilitiesKHR surface_capabilities;
      if (ifn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
              physical_device, swap_surface_, &surface_capabilities) !=
          VK_SUCCESS) {
        XELOGE("Failed to get Vulkan surface capabilities");
        util::DestroyAndNullHandle(dfn.vkDestroySwapchainKHR, device,
                                   swap_swapchain_);
        return false;
      }
      // TODO(Triang3l): Support rotated transforms.
      if (!(surface_capabilities.supportedTransforms &
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        XELOGE("The Vulkan surface doesn't support identity transform");
        util::DestroyAndNullHandle(dfn.vkDestroySwapchainKHR, device,
                                   swap_swapchain_);
        return false;
      }
      VkSwapchainCreateInfoKHR swapchain_create_info;
      swapchain_create_info.imageExtent.width =
          xe::clamp(window_width, surface_capabilities.minImageExtent.width,
                    surface_capabilities.maxImageExtent.width);
      swapchain_create_info.imageExtent.height =
          xe::clamp(window_height, surface_capabilities.minImageExtent.height,
                    surface_capabilities.maxImageExtent.height);
      if (!swapchain_create_info.imageExtent.width ||
          !swapchain_create_info.imageExtent.height) {
        // Everything else is fine with the surface, but the window is too
        // small, try again when the window may be resized (won't try to do some
        // vkCreate* every BeginSwap, will reach this part again, so okay to set
        // swap_swapchain_or_surface_recreation_needed_ back to true).
        swap_swapchain_or_surface_recreation_needed_ = true;
        util::DestroyAndNullHandle(dfn.vkDestroySwapchainKHR, device,
                                   swap_swapchain_);
        return false;
      }
      swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
      swapchain_create_info.pNext = nullptr;
      swapchain_create_info.flags = 0;
      swapchain_create_info.surface = swap_surface_;
      swapchain_create_info.minImageCount = kSwapchainMaxImageCount;
      if (surface_capabilities.maxImageCount) {
        swapchain_create_info.minImageCount =
            std::min(swapchain_create_info.minImageCount,
                     surface_capabilities.maxImageCount);
      }
      swapchain_create_info.imageFormat = swap_surface_format_.format;
      swapchain_create_info.imageColorSpace = swap_surface_format_.colorSpace;
      swapchain_create_info.imageArrayLayers = 1;
      swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      // FIXME(Triang3l): Allow a separate queue for present - see
      // vulkan_provider.cc for details.
      swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      swapchain_create_info.queueFamilyIndexCount = 1;
      swapchain_create_info.pQueueFamilyIndices =
          &queue_family_graphics_compute;
      // TODO(Triang3l): Support rotated transforms.
      swapchain_create_info.preTransform =
          VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
      swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
      if (!(surface_capabilities.supportedCompositeAlpha &
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (surface_capabilities.supportedCompositeAlpha &
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
          swapchain_create_info.compositeAlpha =
              VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        } else {
          // Whatever. supportedCompositeAlpha must have at least one bit set,
          // but if it somehow doesn't (impossible situation according to the
          // specification, but who knows), just assume opaque.
          uint32_t composite_alpha_bit_index;
          if (xe::bit_scan_forward(
                  uint32_t(surface_capabilities.supportedCompositeAlpha),
                  &composite_alpha_bit_index)) {
            swapchain_create_info.compositeAlpha = VkCompositeAlphaFlagBitsKHR(
                uint32_t(1) << composite_alpha_bit_index);
          }
        }
      }
      swapchain_create_info.presentMode = swap_surface_present_mode_;
      swapchain_create_info.clipped = VK_TRUE;
      swapchain_create_info.oldSwapchain = swap_swapchain_;
      VkResult swapchain_create_result = dfn.vkCreateSwapchainKHR(
          device, &swapchain_create_info, nullptr, &swap_swapchain_);
      // The old swapchain is retired even if vkCreateSwapchainKHR has failed.
      if (swapchain_create_info.oldSwapchain != VK_NULL_HANDLE) {
        dfn.vkDestroySwapchainKHR(device, swapchain_create_info.oldSwapchain,
                                  nullptr);
      }
      if (swapchain_create_result != VK_SUCCESS) {
        XELOGE("Failed to create a Vulkan swapchain");
        swap_swapchain_ = VK_NULL_HANDLE;
        return false;
      }
      swap_swapchain_extent_ = swapchain_create_info.imageExtent;

      // The render pass is needed to create framebuffers for swapchain images.
      // It depends on the surface format, and thus can be reused with different
      // surfaces by different swapchains, so it has separate lifetime tracking.
      // It's safe to fail now (though destroying the new swapchain), because
      // the request to destroy the old VkSwapchain somehow (after retiring, or
      // directly) has been fulfilled.
      if (swap_render_pass_ == VK_NULL_HANDLE) {
        VkAttachmentDescription render_pass_color_attachment;
        render_pass_color_attachment.flags = 0;
        render_pass_color_attachment.format = swap_surface_format_.format;
        render_pass_color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        render_pass_color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        render_pass_color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        render_pass_color_attachment.stencilLoadOp =
            VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        render_pass_color_attachment.stencilStoreOp =
            VK_ATTACHMENT_STORE_OP_DONT_CARE;
        render_pass_color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        render_pass_color_attachment.finalLayout =
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference render_pass_color_attachment_reference;
        render_pass_color_attachment_reference.attachment = 0;
        render_pass_color_attachment_reference.layout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription render_pass_subpass;
        render_pass_subpass.flags = 0;
        render_pass_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        render_pass_subpass.inputAttachmentCount = 0;
        render_pass_subpass.pInputAttachments = nullptr;
        render_pass_subpass.colorAttachmentCount = 1;
        render_pass_subpass.pColorAttachments =
            &render_pass_color_attachment_reference;
        render_pass_subpass.pResolveAttachments = nullptr;
        render_pass_subpass.pDepthStencilAttachment = nullptr;
        render_pass_subpass.preserveAttachmentCount = 0;
        render_pass_subpass.pPreserveAttachments = nullptr;
        VkRenderPassCreateInfo render_pass_create_info;
        render_pass_create_info.sType =
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.pNext = nullptr;
        render_pass_create_info.flags = 0;
        render_pass_create_info.attachmentCount = 1;
        render_pass_create_info.pAttachments = &render_pass_color_attachment;
        render_pass_create_info.subpassCount = 1;
        render_pass_create_info.pSubpasses = &render_pass_subpass;
        render_pass_create_info.dependencyCount = 0;
        render_pass_create_info.pDependencies = nullptr;
        if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                                   &swap_render_pass_) != VK_SUCCESS) {
          XELOGE("Failed to create the Vulkan presentation render pass.");
          dfn.vkDestroySwapchainKHR(device, swap_swapchain_, nullptr);
          swap_swapchain_ = VK_NULL_HANDLE;
          return false;
        }
      }

      std::vector<VkImage> swapchain_images;
      uint32_t swapchain_image_count;
      VkResult swapchain_images_get_result;
      for (;;) {
        swapchain_image_count = uint32_t(swapchain_images.size());
        bool swapchain_images_was_empty = !swapchain_image_count;
        swapchain_images_get_result = dfn.vkGetSwapchainImagesKHR(
            device, swap_swapchain_, &swapchain_image_count,
            swapchain_images_was_empty ? nullptr : swapchain_images.data());
        // If the original swapchain image count was 0 (first call), SUCCESS is
        // returned, not INCOMPLETE.
        if (swapchain_images_get_result == VK_SUCCESS ||
            swapchain_images_get_result == VK_INCOMPLETE) {
          swapchain_images.resize(swapchain_image_count);
          if (swapchain_images_get_result == VK_SUCCESS &&
              (!swapchain_images_was_empty || !swapchain_image_count)) {
            break;
          }
        } else {
          break;
        }
      }
      if (swapchain_images_get_result != VK_SUCCESS ||
          swapchain_images.empty()) {
        XELOGE("Failed to get Vulkan swapchain images");
        dfn.vkDestroySwapchainKHR(device, swap_swapchain_, nullptr);
        swap_swapchain_ = VK_NULL_HANDLE;
        return false;
      }
      assert_true(swap_swapchain_image_views_.empty());
      swap_swapchain_image_views_.reserve(swapchain_image_count);
      assert_true(swap_swapchain_framebuffers_.empty());
      swap_swapchain_framebuffers_.reserve(swapchain_image_count);
      VkImageViewCreateInfo swapchain_image_view_create_info;
      swapchain_image_view_create_info.sType =
          VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      swapchain_image_view_create_info.pNext = nullptr;
      swapchain_image_view_create_info.flags = 0;
      swapchain_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
      swapchain_image_view_create_info.format = swap_surface_format_.format;
      swapchain_image_view_create_info.components.r =
          VK_COMPONENT_SWIZZLE_IDENTITY;
      swapchain_image_view_create_info.components.g =
          VK_COMPONENT_SWIZZLE_IDENTITY;
      swapchain_image_view_create_info.components.b =
          VK_COMPONENT_SWIZZLE_IDENTITY;
      swapchain_image_view_create_info.components.a =
          VK_COMPONENT_SWIZZLE_IDENTITY;
      swapchain_image_view_create_info.subresourceRange.aspectMask =
          VK_IMAGE_ASPECT_COLOR_BIT;
      swapchain_image_view_create_info.subresourceRange.baseMipLevel = 0;
      swapchain_image_view_create_info.subresourceRange.levelCount = 1;
      swapchain_image_view_create_info.subresourceRange.baseArrayLayer = 0;
      swapchain_image_view_create_info.subresourceRange.layerCount = 1;
      VkFramebufferCreateInfo swapchain_framebuffer_create_info;
      swapchain_framebuffer_create_info.sType =
          VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      swapchain_framebuffer_create_info.pNext = nullptr;
      swapchain_framebuffer_create_info.flags = 0;
      swapchain_framebuffer_create_info.renderPass = swap_render_pass_;
      swapchain_framebuffer_create_info.attachmentCount = 1;
      swapchain_framebuffer_create_info.width = swap_swapchain_extent_.width;
      swapchain_framebuffer_create_info.height = swap_swapchain_extent_.height;
      swapchain_framebuffer_create_info.layers = 1;
      for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VkImage swapchain_image = swapchain_images[i];
        swapchain_image_view_create_info.image = swapchain_image;
        VkImageView swapchain_image_view;
        if (dfn.vkCreateImageView(device, &swapchain_image_view_create_info,
                                  nullptr,
                                  &swapchain_image_view) != VK_SUCCESS) {
          XELOGE("Failed to create Vulkan swapchain image views");
          DestroySwapchainFramebuffers();
          dfn.vkDestroySwapchainKHR(device, swap_swapchain_, nullptr);
          swap_swapchain_ = VK_NULL_HANDLE;
          return false;
        }
        swap_swapchain_image_views_.push_back(swapchain_image_view);
        swapchain_framebuffer_create_info.pAttachments = &swapchain_image_view;
        VkFramebuffer swapchain_framebuffer;
        if (dfn.vkCreateFramebuffer(device, &swapchain_framebuffer_create_info,
                                    nullptr,
                                    &swapchain_framebuffer) != VK_SUCCESS) {
          XELOGE("Failed to create Vulkan swapchain framebuffers");
          DestroySwapchainFramebuffers();
          dfn.vkDestroySwapchainKHR(device, swap_swapchain_, nullptr);
          swap_swapchain_ = VK_NULL_HANDLE;
          return false;
        }
        swap_swapchain_framebuffers_.push_back(swapchain_framebuffer);
      }
    }

    if (swap_swapchain_ == VK_NULL_HANDLE) {
      return false;
    }
    assert_true(swap_surface_ != VK_NULL_HANDLE);
    assert_true(swap_render_pass_ != VK_NULL_HANDLE);
    assert_false(swap_swapchain_image_views_.empty());
    assert_false(swap_swapchain_framebuffers_.empty());

    // Await the frame data to be available before doing anything else.
    if (swap_submission_completed_ + kSwapchainMaxImageCount <
        swap_submission_current_) {
      uint64_t submission_awaited =
          swap_submission_current_ - kSwapchainMaxImageCount;
      VkFence submission_fences[kSwapchainMaxImageCount];
      uint32_t submission_fence_count = 0;
      while (swap_submission_completed_ + 1 + submission_fence_count <=
             submission_awaited) {
        assert_true(submission_fence_count < kSwapchainMaxImageCount);
        uint32_t submission_index =
            (swap_submission_completed_ + 1 + submission_fence_count) %
            kSwapchainMaxImageCount;
        submission_fences[submission_fence_count++] =
            swap_submissions_[submission_index].fence;
      }
      if (submission_fence_count) {
        if (dfn.vkWaitForFences(device, submission_fence_count,
                                submission_fences, VK_TRUE,
                                UINT64_MAX) != VK_SUCCESS) {
          XELOGE("Failed to await the Vulkan presentation submission fences");
          return false;
        }
        swap_submission_completed_ += submission_fence_count;
      }
    }

    const SwapSubmission& submission =
        swap_submissions_[swap_submission_current_ % kSwapchainMaxImageCount];
    if (dfn.vkResetCommandPool(device, submission.command_pool, 0) !=
        VK_SUCCESS) {
      XELOGE("Failed to reset the Vulkan presentation command pool");
      return false;
    }

    // After the image is acquired, this function must not fail before the
    // semaphore has been signaled, and the image also must be returned to the
    // swapchain.
    uint32_t acquired_image_index;
    switch (dfn.vkAcquireNextImageKHR(device, swap_swapchain_, UINT64_MAX,
                                      swap_image_acquisition_semaphore_,
                                      nullptr, &acquired_image_index)) {
      case VK_SUCCESS:
      case VK_SUBOPTIMAL_KHR:
        // Not recreating in case of suboptimal, just to prevent a recreation
        // loop in case the newly created swapchain is suboptimal too.
        break;
      case VK_ERROR_DEVICE_LOST:
        context_lost_ = true;
        return false;
      case VK_ERROR_OUT_OF_DATE_KHR:
      case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        swap_swapchain_or_surface_recreation_needed_ = true;
        continue;
      case VK_ERROR_SURFACE_LOST_KHR:
        RequestSurfaceRecreation();
        continue;
      default:
        return false;
    }
    swap_swapchain_image_current_ = acquired_image_index;

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    dfn.vkBeginCommandBuffer(submission.command_buffer,
                             &command_buffer_begin_info);
    VkClearValue clear_value;
    GetClearColor(clear_value.color.float32);
    VkRenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.pNext = nullptr;
    render_pass_begin_info.renderPass = swap_render_pass_;
    render_pass_begin_info.framebuffer =
        swap_swapchain_framebuffers_[acquired_image_index];
    render_pass_begin_info.renderArea.offset.x = 0;
    render_pass_begin_info.renderArea.offset.y = 0;
    render_pass_begin_info.renderArea.extent = swap_swapchain_extent_;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_value;
    dfn.vkCmdBeginRenderPass(submission.command_buffer, &render_pass_begin_info,
                             VK_SUBPASS_CONTENTS_INLINE);

    return true;
  }

  // vkAcquireNextImageKHR returned VK_ERROR_OUT_OF_DATE_KHR even after
  // recreation.
  return false;
}

void VulkanContext::EndSwap() {
  if (!target_window_ || context_lost_) {
    return;
  }
  assert_true(swap_swapchain_image_current_ != UINT32_MAX);
  if (swap_swapchain_image_current_ == UINT32_MAX) {
    return;
  }

  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VkQueue queue_graphics_compute = provider.queue_graphics_compute();

  const SwapSubmission& submission =
      swap_submissions_[swap_submission_current_ % kSwapchainMaxImageCount];
  dfn.vkCmdEndRenderPass(submission.command_buffer);
  dfn.vkEndCommandBuffer(submission.command_buffer);
  dfn.vkResetFences(device, 1, &submission.fence);
  VkSubmitInfo submit_info;
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = nullptr;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &swap_image_acquisition_semaphore_;
  VkPipelineStageFlags image_acquisition_semaphore_wait_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  submit_info.pWaitDstStageMask = &image_acquisition_semaphore_wait_stage;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &submission.command_buffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &swap_render_completion_semaphore_;
  VkResult submit_result = dfn.vkQueueSubmit(queue_graphics_compute, 1,
                                             &submit_info, submission.fence);
  if (submit_result != VK_SUCCESS) {
    // If failed, can't even return the swapchain image - so treat all errors as
    // context loss.
    context_lost_ = true;
    return;
  }
  ++swap_submission_current_;

  VkPresentInfoKHR present_info;
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = nullptr;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &swap_render_completion_semaphore_;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swap_swapchain_;
  present_info.pImageIndices = &swap_swapchain_image_current_;
  present_info.pResults = nullptr;
  // FIXME(Triang3l): Allow a separate queue for present - see
  // vulkan_provider.cc for details.
  VkResult present_result =
      dfn.vkQueuePresentKHR(queue_graphics_compute, &present_info);
  swap_swapchain_image_current_ = UINT32_MAX;
  switch (present_result) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      // Not recreating in case of suboptimal, just to prevent a recreation
      // loop in case the newly created swapchain is suboptimal too.
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
      swap_swapchain_or_surface_recreation_needed_ = true;
      return;
    case VK_ERROR_SURFACE_LOST_KHR:
      // Safe to await submission completion now - swap_submission_current_ has
      // already been incremented to the next frame.
      RequestSurfaceRecreation();
      return;
    default:
      // Treat any error as device loss since it would leave the semaphore
      // forever signaled anyway, and the image won't be returned to the
      // swapchain.
      context_lost_ = true;
      return;
  }
}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

void VulkanContext::RequestSurfaceRecreation() {
#if XE_PLATFORM_ANDROID
  // The surface doesn't exist when the activity is in background.
  swap_swapchain_or_surface_recreation_needed_ =
      target_window_->native_handle() != nullptr;
#else
  swap_swapchain_or_surface_recreation_needed_ = true;
#endif
  if (swap_surface_ == VK_NULL_HANDLE) {
    return;
  }
  AwaitAllSwapSubmissionsCompletion();
  DestroySwapchainFramebuffers();
  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::InstanceFunctions& ifn = provider.ifn();
  VkInstance instance = provider.instance();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  util::DestroyAndNullHandle(dfn.vkDestroySwapchainKHR, device,
                             swap_swapchain_);
  ifn.vkDestroySurfaceKHR(instance, swap_surface_, nullptr);
  swap_surface_ = VK_NULL_HANDLE;
}

void VulkanContext::AwaitAllSwapSubmissionsCompletion() {
  assert_not_null(target_window_);
  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VkFence fences[kSwapchainMaxImageCount];
  uint32_t fence_count = 0;
  while (swap_submission_completed_ + 1 < swap_submission_current_) {
    assert_true(fence_count < kSwapchainMaxImageCount);
    uint32_t submission_index =
        ++swap_submission_completed_ % kSwapchainMaxImageCount;
    fences[fence_count++] = swap_submissions_[submission_index].fence;
  }
  if (fence_count && !context_lost_) {
    dfn.vkWaitForFences(device, fence_count, fences, VK_TRUE, UINT64_MAX);
  }
}

void VulkanContext::DestroySwapchainFramebuffers() {
  assert_not_null(target_window_);
  const VulkanProvider& provider = GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  for (VkFramebuffer framebuffer : swap_swapchain_framebuffers_) {
    dfn.vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
  swap_swapchain_framebuffers_.clear();
  for (VkImageView image_view : swap_swapchain_image_views_) {
    dfn.vkDestroyImageView(device, image_view, nullptr);
  }
  swap_swapchain_image_views_.clear();
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
