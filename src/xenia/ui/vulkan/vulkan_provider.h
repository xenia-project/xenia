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
#endif  // XE_PLATFORM_WIN32

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

  // Functions with a version suffix (like _1_1) are null when api_version() is
  // below this version.

  struct LibraryFunctions {
    PFN_vkCreateInstance createInstance;
    PFN_vkEnumerateInstanceExtensionProperties
        enumerateInstanceExtensionProperties;
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion_1_1;
  };
  const LibraryFunctions& library_functions() const {
    return library_functions_;
  }

  uint32_t api_version() const { return api_version_; }

  VkInstance instance() const { return instance_; }
  struct InstanceFunctions {
    PFN_vkCreateDevice createDevice;
    PFN_vkDestroyDevice destroyDevice;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
    PFN_vkGetPhysicalDeviceFeatures getPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties
        getPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR;
#if XE_PLATFORM_ANDROID
    PFN_vkCreateAndroidSurfaceKHR createAndroidSurfaceKHR;
#elif XE_PLATFORM_WIN32
    PFN_vkCreateWin32SurfaceKHR createWin32SurfaceKHR;
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
    PFN_vkGetDeviceQueue getDeviceQueue;
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

  PFN_vkGetInstanceProcAddr getInstanceProcAddr_ = nullptr;
  PFN_vkDestroyInstance destroyInstance_ = nullptr;
  LibraryFunctions library_functions_ = {};

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
