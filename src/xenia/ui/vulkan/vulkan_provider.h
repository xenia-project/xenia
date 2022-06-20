/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
#define XENIA_UI_VULKAN_VULKAN_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/renderdoc_api.h"

#if XE_PLATFORM_ANDROID
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#elif XE_PLATFORM_GNU_LINUX
#ifndef VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XCB_KHR 1
#endif
#elif XE_PLATFORM_WIN32
// Must be included before vulkan.h with VK_USE_PLATFORM_WIN32_KHR because it
// includes Windows.h too.
#include "xenia/base/platform_win.h"
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#endif

#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS 1
#endif
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif
#include "third_party/Vulkan-Headers/include/vulkan/vulkan.h"

#define XELOGVK XELOGI

#define XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace vulkan {

class VulkanProvider : public GraphicsProvider {
 public:
  ~VulkanProvider();

  static std::unique_ptr<VulkanProvider> Create(bool is_surface_required);

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) override;

  std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() override;

  const RenderdocApi& renderdoc_api() const { return renderdoc_api_; }

  struct LibraryFunctions {
    // From the module.
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkDestroyInstance vkDestroyInstance;
    // From vkGetInstanceProcAddr.
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkEnumerateInstanceExtensionProperties
        vkEnumerateInstanceExtensionProperties;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
    struct {
      PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    } v_1_1;
  };
  const LibraryFunctions& lfn() const { return lfn_; }

  struct InstanceExtensions {
    bool ext_debug_utils;
    // Core since 1.1.0.
    bool khr_get_physical_device_properties2;

    // Surface extensions.
    bool khr_surface;
#if XE_PLATFORM_ANDROID
    bool khr_android_surface;
#elif XE_PLATFORM_GNU_LINUX
    bool khr_xcb_surface;
#elif XE_PLATFORM_WIN32
    bool khr_win32_surface;
#endif
  };
  const InstanceExtensions& instance_extensions() const {
    return instance_extensions_;
  }
  VkInstance instance() const { return instance_; }
  struct InstanceFunctions {
#define XE_UI_VULKAN_FUNCTION(name) PFN_##name name;
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  PFN_##extension_name extension_name;
#include "xenia/ui/vulkan/functions/instance_1_0.inc"
#include "xenia/ui/vulkan/functions/instance_ext_debug_utils.inc"
#include "xenia/ui/vulkan/functions/instance_khr_get_physical_device_properties2.inc"
#include "xenia/ui/vulkan/functions/instance_khr_surface.inc"
#if XE_PLATFORM_ANDROID
#include "xenia/ui/vulkan/functions/instance_khr_android_surface.inc"
#elif XE_PLATFORM_GNU_LINUX
#include "xenia/ui/vulkan/functions/instance_khr_xcb_surface.inc"
#elif XE_PLATFORM_WIN32
#include "xenia/ui/vulkan/functions/instance_khr_win32_surface.inc"
#endif
#undef XE_UI_VULKAN_FUNCTION_PROMOTED
#undef XE_UI_VULKAN_FUNCTION
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
    bool ext_shader_stencil_export;
    // Core since 1.1.0.
    bool khr_dedicated_allocation;
    // Core since 1.2.0.
    bool khr_image_format_list;
    // Requires the VK_KHR_get_physical_device_properties2 instance extension.
    bool khr_portability_subset;
    // Core since 1.2.0.
    bool khr_shader_float_controls;
    // Core since 1.2.0.
    bool khr_spirv_1_4;
    bool khr_swapchain;
  };
  const DeviceExtensions& device_extensions() const {
    return device_extensions_;
  }
  // Returns nullptr if the device is fully compliant with Vulkan 1.0.
  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
  device_portability_subset_features() const {
    if (!device_extensions_.khr_portability_subset) {
      return nullptr;
    }
    return &device_portability_subset_features_;
  }
  uint32_t memory_types_device_local() const {
    return memory_types_device_local_;
  }
  uint32_t memory_types_host_visible() const {
    return memory_types_host_visible_;
  }
  uint32_t memory_types_host_coherent() const {
    return memory_types_host_coherent_;
  }
  uint32_t memory_types_host_cached() const {
    return memory_types_host_cached_;
  }
  struct QueueFamily {
    uint32_t queue_first_index = 0;
    uint32_t queue_count = 0;
    bool potentially_supports_present = false;
  };
  const std::vector<QueueFamily>& queue_families() const {
    return queue_families_;
  }
  // Required.
  uint32_t queue_family_graphics_compute() const {
    return queue_family_graphics_compute_;
  }
  // Optional, if sparse binding is supported (UINT32_MAX otherwise). May be the
  // same as queue_family_graphics_compute_.
  uint32_t queue_family_sparse_binding() const {
    return queue_family_sparse_binding_;
  }
  const VkPhysicalDeviceFloatControlsPropertiesKHR&
  device_float_controls_properties() const {
    return device_float_controls_properties_;
  }

  struct Queue {
    VkQueue queue = VK_NULL_HANDLE;
    std::recursive_mutex mutex;
  };
  struct QueueAcquisition {
    QueueAcquisition(std::unique_lock<std::recursive_mutex>&& lock,
                     VkQueue queue)
        : lock(std::move(lock)), queue(queue) {}
    std::unique_lock<std::recursive_mutex> lock;
    VkQueue queue;
  };
  QueueAcquisition AcquireQueue(uint32_t index) {
    Queue& queue = queues_[index];
    return QueueAcquisition(std::unique_lock<std::recursive_mutex>(queue.mutex),
                            queue.queue);
  }
  QueueAcquisition AcquireQueue(uint32_t family_index, uint32_t index) {
    assert_true(family_index != UINT32_MAX);
    return AcquireQueue(queue_families_[family_index].queue_first_index +
                        index);
  }

  VkDevice device() const { return device_; }
  struct DeviceFunctions {
#define XE_UI_VULKAN_FUNCTION(name) PFN_##name name;
#include "xenia/ui/vulkan/functions/device_1_0.inc"
#include "xenia/ui/vulkan/functions/device_khr_swapchain.inc"
#undef XE_UI_VULKAN_FUNCTION
  };
  const DeviceFunctions& dfn() const { return dfn_; }

  template <typename T>
  void SetDeviceObjectName(VkObjectType type, T handle,
                           const char* name) const {
    if (!debug_names_used_) {
      return;
    }
    VkDebugUtilsObjectNameInfoEXT name_info;
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    name_info.pNext = nullptr;
    name_info.objectType = type;
    name_info.objectHandle = uint64_t(handle);
    name_info.pObjectName = name;
    ifn_.vkSetDebugUtilsObjectNameEXT(device_, &name_info);
  }

  bool IsSparseBindingSupported() const {
    return queue_family_sparse_binding_ != UINT32_MAX;
  }

  // Samplers that may be useful for host needs. Only these samplers should be
  // used in host, non-emulation contexts, because the total number of samplers
  // is heavily limited (4000) on Nvidia GPUs - the rest of samplers are
  // allocated for emulation.
  enum class HostSampler {
    kNearestClamp,
    kLinearClamp,
    kNearestRepeat,
    kLinearRepeat,

    kCount,
  };
  VkSampler GetHostSampler(HostSampler sampler) const {
    return host_samplers_[size_t(sampler)];
  }

 private:
  explicit VulkanProvider(bool is_surface_required)
      : is_surface_required_(is_surface_required) {}

  bool Initialize();

  static void AccumulateInstanceExtensions(
      size_t properties_count, const VkExtensionProperties* properties,
      bool request_debug_utils, InstanceExtensions& instance_extensions,
      std::vector<const char*>& instance_extensions_enabled);

  static VkBool32 VKAPI_CALL DebugMessengerCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
      VkDebugUtilsMessageTypeFlagsEXT message_types,
      const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
      void* user_data);

  bool is_surface_required_;

  RenderdocApi renderdoc_api_;

#if XE_PLATFORM_LINUX
  void* library_ = nullptr;
#elif XE_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  LibraryFunctions lfn_ = {};

  InstanceExtensions instance_extensions_;
  VkInstance instance_ = VK_NULL_HANDLE;
  InstanceFunctions ifn_;
  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
  bool debug_names_used_ = false;

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties device_properties_;
  VkPhysicalDeviceFeatures device_features_;
  DeviceExtensions device_extensions_;
  VkPhysicalDevicePortabilitySubsetFeaturesKHR
      device_portability_subset_features_;
  uint32_t memory_types_device_local_;
  uint32_t memory_types_host_visible_;
  uint32_t memory_types_host_coherent_;
  uint32_t memory_types_host_cached_;
  std::vector<QueueFamily> queue_families_;
  uint32_t queue_family_graphics_compute_;
  uint32_t queue_family_sparse_binding_;
  VkPhysicalDeviceFloatControlsPropertiesKHR device_float_controls_properties_;

  VkDevice device_ = VK_NULL_HANDLE;
  DeviceFunctions dfn_ = {};
  // Queues contain a mutex, can't use std::vector.
  std::unique_ptr<Queue[]> queues_;

  VkSampler host_samplers_[size_t(HostSampler::kCount)] = {};
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
