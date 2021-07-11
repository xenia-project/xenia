/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_INSTANCE_H_
#define XENIA_UI_VULKAN_VULKAN_INSTANCE_H_

#include <memory>
#include <string>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace vulkan {

// Wrappers and utilities for VkInstance.
class VulkanInstance {
 public:
  VulkanInstance();
  ~VulkanInstance();

  struct LibraryFunctions {
    // From the module.
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkDestroyInstance vkDestroyInstance;
    // From vkGetInstanceProcAddr.
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkEnumerateInstanceExtensionProperties
        vkEnumerateInstanceExtensionProperties;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
  };
  const LibraryFunctions& lfn() const { return lfn_; }

  VkInstance handle = nullptr;

  operator VkInstance() const { return handle; }

  struct InstanceFunctions {
#define XE_UI_VULKAN_FUNCTION(name) PFN_##name name;
#include "xenia/ui/vulkan/functions/instance_1_0.inc"
#include "xenia/ui/vulkan/functions/instance_ext_debug_report.inc"
#include "xenia/ui/vulkan/functions/instance_khr_surface.inc"
#if XE_PLATFORM_ANDROID
#include "xenia/ui/vulkan/functions/instance_khr_android_surface.inc"
#elif XE_PLATFORM_GNULINUX
#include "xenia/ui/vulkan/functions/instance_khr_xcb_surface.inc"
#elif XE_PLATFORM_WIN32
#include "xenia/ui/vulkan/functions/instance_khr_win32_surface.inc"
#endif
#undef XE_UI_VULKAN_FUNCTION
  };
  const InstanceFunctions& ifn() const { return ifn_; }

  // Declares a layer to verify and enable upon initialization.
  // Must be called before Initialize.
  void DeclareRequiredLayer(std::string name, uint32_t min_version,
                            bool is_optional) {
    required_layers_.push_back({name, min_version, is_optional});
  }

  // Declares an extension to verify and enable upon initialization.
  // Must be called before Initialize.
  void DeclareRequiredExtension(std::string name, uint32_t min_version,
                                bool is_optional) {
    required_extensions_.push_back({name, min_version, is_optional});
  }

  // Initializes the instance, querying and enabling extensions and layers and
  // preparing the instance for general use.
  // If initialization succeeds it's likely that no more failures beyond runtime
  // issues will occur.
  bool Initialize();

  // Returns a list of all available devices as detected during initialization.
  const std::vector<DeviceInfo>& available_devices() const {
    return available_devices_;
  }

  // True if RenderDoc is attached and available for use.
  bool is_renderdoc_attached() const { return is_renderdoc_attached_; }
  // RenderDoc API handle, if attached.
  void* renderdoc_api() const { return renderdoc_api_; }

 private:
  // Attempts to enable RenderDoc via the API, if it is attached.
  bool EnableRenderDoc();

  // Queries the system to find global extensions and layers.
  bool QueryGlobals();

  // Creates the instance, enabling required extensions and layers.
  bool CreateInstance();
  void DestroyInstance();

  // Enables debugging info and callbacks for supported layers.
  void EnableDebugValidation();
  void DisableDebugValidation();

  // Queries all available physical devices.
  bool QueryDevices();

  void DumpLayers(const std::vector<LayerInfo>& layers, const char* indent);
  void DumpExtensions(const std::vector<VkExtensionProperties>& extensions,
                      const char* indent);
  void DumpDeviceInfo(const DeviceInfo& device_info);

#if XE_PLATFORM_LINUX
  void* library_ = nullptr;
#elif XE_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  LibraryFunctions lfn_ = {};

  std::vector<Requirement> required_layers_;
  std::vector<Requirement> required_extensions_;

  InstanceFunctions ifn_ = {};

  std::vector<LayerInfo> global_layers_;
  std::vector<VkExtensionProperties> global_extensions_;
  std::vector<DeviceInfo> available_devices_;

  bool dbg_report_ena_ = false;
  VkDebugReportCallbackEXT dbg_report_callback_ = nullptr;

  void* renderdoc_api_ = nullptr;
  bool is_renderdoc_attached_ = false;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_INSTANCE_H_
