/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vk/vulkan_context.h"

#include "xenia/base/logging.h"
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
    vkGetPhysicalDeviceSurfaceSupportKHR(provider->GetPhysicalDevice(),
                                         provider->GetGraphicsQueueFamily(),
                                         surface_, &surface_supported);
    if (!surface_supported) {
      XELOGE(
          "Surface not supported by the graphics queue of the Vulkan physical "
          "device");
      Shutdown();
      return false;
    }

    // Initialize the immediate mode drawer if not offscreen.
    immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(this);
    if (!immediate_drawer_->Initialize()) {
      Shutdown();
      return false;
    }
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

  immediate_drawer_.reset();

  util::DestroyAndNullHandle(vkDestroySurfaceKHR, instance, surface_);

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

  auto device = GetVulkanProvider()->GetDevice();

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
}

void VulkanContext::EndSwap() {
  if (context_lost_) {
    return;
  }

  // Go to the next transient object frame.
  auto queue = GetVulkanProvider()->GetGraphicsQueue();
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
  // TODO(Triang3l): Read back swap chain front buffer.
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
