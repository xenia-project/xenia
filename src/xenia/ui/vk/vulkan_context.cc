/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vk/vulkan_context.h"

#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vk/vulkan_immediate_drawer.h"
#include "xenia/ui/vk/vulkan_util.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace vk {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

VulkanContext::~VulkanContext() { Shutdown(); }

bool VulkanContext::Initialize() {
  auto provider = GetVulkanProvider();
  auto instance = provider->GetInstance();
  auto physical_device = provider->GetPhysicalDevice();
  auto device = provider->GetDevice();

  context_lost_ = false;

  current_frame_ = 1;
  // No frames have been completed yet.
  last_completed_frame_ = 0;
  // Keep in sync with the modulo because why not.
  current_queue_frame_ = 1;

  // Create fences for synchronization of reuse and destruction of transient
  // objects (like command buffers) and for global shutdown.
  VkFenceCreateInfo fence_create_info;
  fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_create_info.pNext = nullptr;
  fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (uint32_t i = 0; i < kQueuedFrames; ++i) {
    if (vkCreateFence(device, &fence_create_info, nullptr, &fences_[i]) !=
        VK_SUCCESS) {
      XELOGE("Failed to create a Vulkan fence");
      Shutdown();
      return false;
    }
  }

  if (target_window_) {
    // Create the surface.
    VkResult surface_create_result;
#if XE_PLATFORM_WIN32
    VkWin32SurfaceCreateInfoKHR surface_create_info;
    surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_create_info.pNext = nullptr;
    surface_create_info.flags = 0;
    surface_create_info.hinstance =
        static_cast<HINSTANCE>(target_window_->native_platform_handle());
    surface_create_info.hwnd =
        static_cast<HWND>(target_window_->native_handle());
    surface_create_result = vkCreateWin32SurfaceKHR(
        instance, &surface_create_info, nullptr, &surface_);
#elif XE_PLATFORM_LINUX
#ifdef GDK_WINDOWING_X11
    VkXcbSurfaceCreateInfoKHR surface_create_info;
    surface_create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surface_create_info.pNext = nullptr;
    surface_create_info.flags = 0;
    surface_create_info.connection = static_cast<xcb_connection_t*>(
        target_window_->native_platform_handle());
    surface_create_info.window = gdk_x11_window_get_xid(gtk_widget_get_window(
        static_cast<GtkWidget*>(target_window_->native_handle())));
    surface_create_result = vkCreateXcbSurfaceKHR(
        instance, &surface_create_info, nullptr, &surface_);
#else
#error No Vulkan surface creation for the GDK backend implemented yet.
#endif
#else
#error No Vulkan surface creation for the platform implemented yet.
#endif
    if (surface_create_result != VK_SUCCESS) {
      XELOGE("Failed to create a Vulkan surface");
      Shutdown();
      return false;
    }

    // Check if the graphics queue can present to the surface.
    // FIXME(Triang3l): Separate present queue not supported - would require
    // deferring VkDevice creation because vkCreateDevice needs all used queues.
    VkBool32 surface_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device,
                                         provider->GetGraphicsQueueFamily(),
                                         surface_, &surface_supported);
    if (!surface_supported) {
      XELOGE(
          "Surface not supported by the graphics queue of the Vulkan physical "
          "device");
      Shutdown();
      return false;
    }

    // Choose the number of swapchain images and the alpha compositing mode.
    VkSurfaceCapabilitiesKHR surface_capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physical_device, surface_, &surface_capabilities) != VK_SUCCESS) {
      XELOGE("Failed to get Vulkan surface capabilities");
      Shutdown();
      return false;
    }
    surface_min_image_count_ =
        std::max(uint32_t(3), surface_capabilities.minImageCount);
    if (surface_capabilities.maxImageCount) {
      surface_min_image_count_ = std::min(surface_min_image_count_,
                                          surface_capabilities.maxImageCount);
    }
    surface_transform_ = surface_capabilities.currentTransform;
    if (surface_capabilities.supportedCompositeAlpha &
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
      surface_composite_alpha_ = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (surface_capabilities.supportedCompositeAlpha &
               VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
      surface_composite_alpha_ = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    } else {
      surface_composite_alpha_ = VkCompositeAlphaFlagBitsKHR(
          uint32_t(1) << xe::tzcnt(
              surface_capabilities.supportedCompositeAlpha));
    }

    // Get the preferred surface format.
    uint32_t surface_format_count;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_,
                                             &surface_format_count,
                                             nullptr) != VK_SUCCESS) {
      XELOGE("Failed to get Vulkan surface format count");
      Shutdown();
      return false;
    }
    std::vector<VkSurfaceFormatKHR> surface_formats;
    surface_formats.resize(surface_format_count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical_device, surface_, &surface_format_count,
            surface_formats.data()) != VK_SUCCESS ||
        !surface_format_count) {
      XELOGE("Failed to get Vulkan surface formats");
      Shutdown();
      return false;
    }
    // Prefer RGB8.
    for (uint32_t i = 0; i < surface_format_count; ++i) {
      surface_format_ = surface_formats[i];
      if (surface_format_.format == VK_FORMAT_UNDEFINED) {
        surface_format_.format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
      }
      if (surface_format_.format == VK_FORMAT_R8G8B8A8_UNORM ||
          surface_format_.format == VK_FORMAT_B8G8R8A8_UNORM) {
        break;
      }
    }

    // Prefer non-vsyncing presentation modes (better without tearing), or fall
    // back to FIFO.
    surface_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t present_mode_count;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface_,
                                                  &present_mode_count,
                                                  nullptr) == VK_SUCCESS) {
      std::vector<VkPresentModeKHR> present_modes;
      present_modes.resize(present_mode_count);
      if (vkGetPhysicalDeviceSurfacePresentModesKHR(
              physical_device, surface_, &present_mode_count,
              present_modes.data()) == VK_SUCCESS) {
        for (uint32_t i = 0; i < present_mode_count; ++i) {
          VkPresentModeKHR present_mode = present_modes[i];
          if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR ||
              present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            surface_present_mode_ = present_mode;
            if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
              break;
            }
          }
        }
      }
    }

    // Create presentation semaphores.
    VkSemaphoreCreateInfo semaphore_create_info;
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.pNext = nullptr;
    semaphore_create_info.flags = 0;
    if (vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                          &semaphore_present_complete_) != VK_SUCCESS) {
      XELOGE(
          "Failed to create a Vulkan semaphone for awaiting swapchain image "
          "acquisition");
      Shutdown();
      return false;
    }
    if (vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                          &semaphore_draw_complete_) != VK_SUCCESS) {
      XELOGE(
          "Failed to create a Vulkan semaphone for awaiting drawing before "
          "presentation");
      Shutdown();
      return false;
    }

    // Initialize the immediate mode drawer if not offscreen.
    immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(this);
    if (!immediate_drawer_->Initialize()) {
      Shutdown();
      return false;
    }

    swapchain_ = VK_NULL_HANDLE;
  }

  return true;
}

void VulkanContext::Shutdown() {
  auto provider = GetVulkanProvider();
  auto instance = provider->GetInstance();
  auto device = provider->GetDevice();

  if (initialized_fully_ && !context_lost_) {
    AwaitAllFramesCompletion();
  }

  initialized_fully_ = false;

  if (target_window_) {
    util::DestroyAndNullHandle(vkDestroySwapchainKHR, device, swapchain_);

    immediate_drawer_.reset();

    util::DestroyAndNullHandle(vkDestroySemaphore, device,
                               semaphore_draw_complete_);
    util::DestroyAndNullHandle(vkDestroySemaphore, device,
                               semaphore_present_complete_);

    util::DestroyAndNullHandle(vkDestroySurfaceKHR, instance, surface_);
  }

  for (uint32_t i = 0; i < kQueuedFrames; ++i) {
    util::DestroyAndNullHandle(vkDestroyFence, device, fences_[i]);
  }
}

ImmediateDrawer* VulkanContext::immediate_drawer() {
  return immediate_drawer_.get();
}

bool VulkanContext::is_current() { return false; }

bool VulkanContext::MakeCurrent() { return true; }

void VulkanContext::ClearCurrent() {}

void VulkanContext::BeginSwap() {
  if (context_lost_) {
    return;
  }

  auto provider = GetVulkanProvider();
  auto device = provider->GetDevice();

  // Await the availability of transient objects for the new frame.
  // The frame number is incremented in EndSwap so it can be treated the same
  // way both when inside a frame and when outside of it (it's tied to actual
  // submissions).
  if (vkWaitForFences(device, 1, &fences_[current_queue_frame_], VK_TRUE,
                      UINT64_MAX) != VK_SUCCESS) {
    context_lost_ = true;
    return;
  }
  // Update the completed frame if didn't explicitly await all queued frames.
  if (last_completed_frame_ + kQueuedFrames < current_frame_) {
    last_completed_frame_ = current_frame_ - kQueuedFrames;
  }

  if (target_window_) {
    VkExtent2D target_window_extent = {
        uint32_t(target_window_->scaled_width()),
        uint32_t(target_window_->scaled_height())};
    // On certain platforms (like Windows), swapchain size must match the window
    // size.
    VkSurfaceTransformFlagBitsKHR current_transform = surface_transform_;
    VkSurfaceCapabilitiesKHR surface_capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            provider->GetPhysicalDevice(), surface_, &surface_capabilities) ==
        VK_SUCCESS) {
      current_transform = surface_capabilities.currentTransform;
    }
    bool create_swapchain =
        swapchain_ == VK_NULL_HANDLE ||
        swapchain_extent_.width != target_window_extent.width ||
        swapchain_extent_.height != target_window_extent.height ||
        surface_transform_ != current_transform;
    if (!create_swapchain) {
      // Try to acquire the image for the existing swapchain or check if out of
      // date.
      VkResult acquire_result = vkAcquireNextImageKHR(
          device, swapchain_, UINT64_MAX, semaphore_present_complete_,
          VK_NULL_HANDLE, &swapchain_acquired_image_index_);
      if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        create_swapchain = true;
      } else if (acquire_result != VK_SUCCESS &&
                 acquire_result != VK_SUBOPTIMAL_KHR) {
        // Checking for resizing externally. For proper suboptimal handling,
        // either the swapchain needs to be recreated in the next frame, or the
        // semaphore must be awaited right now (since suboptimal is a successful
        // result). Assume all other errors are fatal.
        context_lost_ = true;
        return;
      }
    }
    if (create_swapchain) {
      if (swapchain_ != VK_NULL_HANDLE) {
        AwaitAllFramesCompletion();
        if (context_lost_) {
          return;
        }
      }
      // TODO(Triang3l): Destroy the old pass, framebuffer and image views.
      // Destroying the old swapchain after creating the new one because
      // swapchain creation needs the old one.
      VkSwapchainKHR swapchain;
      VkSwapchainCreateInfoKHR swapchain_create_info;
      swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
      swapchain_create_info.pNext = nullptr;
      swapchain_create_info.flags = 0;
      swapchain_create_info.surface = surface_;
      swapchain_create_info.minImageCount = surface_min_image_count_;
      swapchain_create_info.imageFormat = surface_format_.format;
      swapchain_create_info.imageColorSpace = surface_format_.colorSpace;
      swapchain_create_info.imageExtent = target_window_extent;
      swapchain_create_info.imageArrayLayers = 1;
      swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      swapchain_create_info.queueFamilyIndexCount = 0;
      swapchain_create_info.pQueueFamilyIndices = nullptr;
      swapchain_create_info.preTransform = current_transform;
      swapchain_create_info.compositeAlpha = surface_composite_alpha_;
      swapchain_create_info.presentMode = surface_present_mode_;
      swapchain_create_info.clipped = VK_TRUE;
      swapchain_create_info.oldSwapchain = swapchain_;
      if (vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr,
                               &swapchain) != VK_SUCCESS) {
        context_lost_ = true;
        return;
      }
      if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
      }
      swapchain_ = swapchain;
      swapchain_extent_ = target_window_extent;
      surface_transform_ = current_transform;
      // TODO(Triang3l): Get images, create the framebuffer and the passes.
      VkResult acquire_result = vkAcquireNextImageKHR(
          device, swapchain_, UINT64_MAX, semaphore_present_complete_,
          VK_NULL_HANDLE, &swapchain_acquired_image_index_);
      if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
        context_lost_ = true;
        return;
      }
    }
    // TODO(Triang3l): Insert a barrier and clear.
  }
}

void VulkanContext::EndSwap() {
  if (context_lost_) {
    return;
  }

  auto provider = GetVulkanProvider();
  auto queue = provider->GetGraphicsQueue();

  if (target_window_ != nullptr) {
    // Present.
    // TODO(Triang3l): Insert a barrier.
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = nullptr;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &semaphore_draw_complete_;
    if (vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
      context_lost_ = true;
      return;
    }
    VkPresentInfoKHR present_info;
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &semaphore_draw_complete_;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &swapchain_acquired_image_index_;
    present_info.pResults = nullptr;
    VkResult present_result = vkQueuePresentKHR(queue, &present_info);
    if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR &&
        present_result != VK_ERROR_OUT_OF_DATE_KHR) {
      // VK_ERROR_OUT_OF_DATE_KHR will be handled in the next BeginSwap. In case
      // of it, the semaphore will be unsignaled anyway:
      // https://github.com/KhronosGroup/Vulkan-Docs/issues/572
      context_lost_ = true;
      return;
    }
  }

  // Go to the next transient object frame.
  VkFence fence = fences_[current_queue_frame_];
  vkResetFences(provider->GetDevice(), 1, &fence);
  if (vkQueueSubmit(queue, 0, nullptr, fences_[current_queue_frame_]) !=
      VK_SUCCESS) {
    context_lost_ = true;
    return;
  }
  ++current_queue_frame_;
  if (current_queue_frame_ >= kQueuedFrames) {
    current_queue_frame_ -= kQueuedFrames;
  }
  ++current_frame_;
}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  // TODO(Triang3l): Read back swapchain front buffer.
  return nullptr;
}

void VulkanContext::AwaitAllFramesCompletion() {
  // Await the last frame since previous frames must be completed before it.
  if (context_lost_) {
    return;
  }
  auto device = GetVulkanProvider()->GetDevice();
  uint32_t await_frame = current_queue_frame_ + (kQueuedFrames - 1);
  if (await_frame >= kQueuedFrames) {
    await_frame -= kQueuedFrames;
  }
  if (vkWaitForFences(device, 1, &fences_[await_frame], VK_TRUE, UINT64_MAX) !=
      VK_SUCCESS) {
    context_lost_ = true;
    return;
  }
  last_completed_frame_ = current_frame_ - 1;
}

}  // namespace vk
}  // namespace ui
}  // namespace xe
