/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
#define XENIA_UI_VULKAN_VULKAN_PROVIDER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/ui/graphics_provider.h"

#if XE_PLATFORM_WIN32
// Must be included before vulkan.h with VK_USE_PLATFORM_WIN32_KHR because it
// includes Windows.h too.
#include "xenia/base/platform_win.h"
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#endif

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif
#include "third_party/vulkan/vulkan.h"

#define XELOGVK XELOGI

namespace xe {
namespace ui {
namespace vulkan {

class VulkanProvider : public GraphicsProvider {
 public:
  ~VulkanProvider() override;

  static std::unique_ptr<VulkanProvider> Create(Window* main_window);

  std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) override;
  std::unique_ptr<GraphicsContext> CreateOffscreenContext() override;

  struct LibraryFunctions {
    // From the module.
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkDestroyInstance vkDestroyInstance;
    // From vkGetInstanceProcAddr.
    PFN_vkCreateInstance vkCreateInstance;
    struct {
      PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    } v_1_1;
  };
  const LibraryFunctions& lfn() const { return lfn_; }

  uint32_t api_version() const { return api_version_; }

  VkInstance instance() const { return instance_; }
  struct InstanceFunctions {
    PFN_vkCreateDevice vkCreateDevice;
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
    PFN_vkEnumerateDeviceExtensionProperties
        vkEnumerateDeviceExtensionProperties;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
    PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties
        vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR
        vkGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR
        vkGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR
        vkGetPhysicalDeviceSurfaceSupportKHR;
#if XE_PLATFORM_ANDROID
    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#elif XE_PLATFORM_WIN32
    PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
  };
  const InstanceFunctions& ifn() const { return ifn_; }

  VkPhysicalDevice physical_device() const { return physical_device_; }
  const VkPhysicalDeviceProperties& device_properties() const {
    return device_properties_;
  }
  const VkPhysicalDeviceFeatures& device_features() const {
    return device_features_;
  }
  struct DeviceExtensions {
    bool ext_fragment_shader_interlock;
  };
  const DeviceExtensions& device_extensions() const {
    return device_extensions_;
  }
  // FIXME(Triang3l): Allow a separate queue for present - see
  // vulkan_provider.cc for details.
  uint32_t queue_family_graphics_compute() const {
    return queue_family_graphics_compute_;
  }

  VkDevice device() const { return device_; }
  struct DeviceFunctions {
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkCreateRenderPass vkCreateRenderPass;
    PFN_vkCreateSemaphore vkCreateSemaphore;
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
    PFN_vkDestroyImageView vkDestroyImageView;
    PFN_vkDestroyRenderPass vkDestroyRenderPass;
    PFN_vkDestroySemaphore vkDestroySemaphore;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
    PFN_vkResetCommandPool vkResetCommandPool;
    PFN_vkResetFences vkResetFences;
    PFN_vkQueuePresentKHR vkQueuePresentKHR;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkWaitForFences vkWaitForFences;
  };
  const DeviceFunctions& dfn() const { return dfn_; }

  VkQueue queue_graphics_compute() const { return queue_graphics_compute_; }
  // May be VK_NULL_HANDLE if not available.
  VkQueue queue_sparse_binding() const { return queue_sparse_binding_; }

 private:
  explicit VulkanProvider(Window* main_window);

  bool Initialize();

#if XE_PLATFORM_LINUX
  void* library_ = nullptr;
#elif XE_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  LibraryFunctions lfn_ = {};

  uint32_t api_version_ = VK_API_VERSION_1_0;

  VkInstance instance_ = VK_NULL_HANDLE;
  InstanceFunctions ifn_ = {};

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties device_properties_;
  VkPhysicalDeviceFeatures device_features_;
  DeviceExtensions device_extensions_;
  uint32_t queue_family_graphics_compute_;

  VkDevice device_ = VK_NULL_HANDLE;
  DeviceFunctions dfn_ = {};
  VkQueue queue_graphics_compute_;
  // May be VK_NULL_HANDLE if not available.
  VkQueue queue_sparse_binding_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
