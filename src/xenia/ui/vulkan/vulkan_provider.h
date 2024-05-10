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
  struct DeviceInfo {
    // "ext_1_X"-prefixed extension fields are set to true not only if the
    // extension itself is actually exposed, but also if it was promoted to the
    // device's API version. Therefore, merely the field being set to true
    // doesn't imply that all the required features in the extension are
    // supported - actual properties and features must be checked rather than
    // the extension itself where they matter.

    // Vulkan 1.0.

    uint32_t memory_types_device_local;
    uint32_t memory_types_host_visible;
    uint32_t memory_types_host_coherent;
    uint32_t memory_types_host_cached;

    uint32_t apiVersion;
    uint32_t maxImageDimension2D;
    uint32_t maxImageDimension3D;
    uint32_t maxImageDimensionCube;
    uint32_t maxImageArrayLayers;
    uint32_t maxStorageBufferRange;
    uint32_t maxSamplerAllocationCount;
    uint32_t maxPerStageDescriptorSamplers;
    uint32_t maxPerStageDescriptorStorageBuffers;
    uint32_t maxPerStageDescriptorSampledImages;
    uint32_t maxPerStageResources;
    uint32_t maxVertexOutputComponents;
    uint32_t maxTessellationEvaluationOutputComponents;
    uint32_t maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents;
    uint32_t maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents;
    uint32_t maxFragmentCombinedOutputResources;
    float maxSamplerAnisotropy;
    uint32_t maxViewportDimensions[2];
    float viewportBoundsRange[2];
    VkDeviceSize minUniformBufferOffsetAlignment;
    VkDeviceSize minStorageBufferOffsetAlignment;
    uint32_t maxFramebufferWidth;
    uint32_t maxFramebufferHeight;
    VkSampleCountFlags framebufferColorSampleCounts;
    VkSampleCountFlags framebufferDepthSampleCounts;
    VkSampleCountFlags framebufferStencilSampleCounts;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts;
    VkSampleCountFlags sampledImageColorSampleCounts;
    VkSampleCountFlags sampledImageIntegerSampleCounts;
    VkSampleCountFlags sampledImageDepthSampleCounts;
    VkSampleCountFlags sampledImageStencilSampleCounts;
    VkSampleCountFlags standardSampleLocations;
    VkDeviceSize optimalBufferCopyOffsetAlignment;
    VkDeviceSize optimalBufferCopyRowPitchAlignment;
    VkDeviceSize nonCoherentAtomSize;

    bool fullDrawIndexUint32;
    bool independentBlend;
    bool geometryShader;
    bool tessellationShader;
    bool sampleRateShading;
    bool depthClamp;
    bool fillModeNonSolid;
    bool samplerAnisotropy;
    bool vertexPipelineStoresAndAtomics;
    bool fragmentStoresAndAtomics;
    bool shaderClipDistance;
    bool shaderCullDistance;
    bool sparseBinding;
    bool sparseResidencyBuffer;

    // VK_KHR_swapchain (#2).

    bool ext_VK_KHR_swapchain;

    // VK_KHR_sampler_mirror_clamp_to_edge (#15, Vulkan 1.2).

    bool ext_1_2_VK_KHR_sampler_mirror_clamp_to_edge;

    bool samplerMirrorClampToEdge;

    // VK_KHR_dedicated_allocation (#128, Vulkan 1.1).

    bool ext_1_1_VK_KHR_dedicated_allocation;

    // VK_EXT_shader_stencil_export (#141).

    bool ext_VK_EXT_shader_stencil_export;

    // VK_KHR_get_memory_requirements2 (#147, Vulkan 1.1).

    bool ext_1_1_VK_KHR_get_memory_requirements2;

    // VK_KHR_image_format_list (#148, Vulkan 1.2).

    bool ext_1_2_VK_KHR_image_format_list;

    // VK_KHR_sampler_ycbcr_conversion (#157, Vulkan 1.1).

    bool ext_1_1_VK_KHR_sampler_ycbcr_conversion;

    // VK_KHR_bind_memory2 (#158, Vulkan 1.1).

    bool ext_1_1_VK_KHR_bind_memory2;

    // VK_KHR_portability_subset (#164).

    bool ext_VK_KHR_portability_subset;

    bool constantAlphaColorBlendFactors;
    bool imageViewFormatReinterpretation;
    bool imageViewFormatSwizzle;
    bool pointPolygons;
    bool separateStencilMaskRef;
    bool shaderSampleRateInterpolationFunctions;
    bool triangleFans;

    // VK_KHR_shader_float_controls (#198, Vulkan 1.2).

    bool ext_1_2_VK_KHR_shader_float_controls;

    bool shaderSignedZeroInfNanPreserveFloat32;
    bool shaderDenormFlushToZeroFloat32;
    bool shaderRoundingModeRTEFloat32;

    // VK_KHR_spirv_1_4 (#237, Vulkan 1.2).

    bool ext_1_2_VK_KHR_spirv_1_4;

    // VK_EXT_memory_budget (#238).

    bool ext_VK_EXT_memory_budget;

    // VK_EXT_fragment_shader_interlock (#252).

    bool ext_VK_EXT_fragment_shader_interlock;

    bool fragmentShaderSampleInterlock;
    bool fragmentShaderPixelInterlock;

    // VK_EXT_shader_demote_to_helper_invocation (#277, Vulkan 1.3).

    bool ext_1_3_VK_EXT_shader_demote_to_helper_invocation;

    bool shaderDemoteToHelperInvocation;

    // VK_KHR_maintenance4 (#414, Vulkan 1.3).

    bool ext_1_3_VK_KHR_maintenance4;

    // VK_EXT_non_seamless_cube_map (#423).

    bool ext_VK_EXT_non_seamless_cube_map;

    bool nonSeamlessCubeMap;
  };

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
  PFN_##core_name core_name;
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

  const DeviceInfo& device_info() const { return device_info_; }

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
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  PFN_##core_name core_name;
#include "xenia/ui/vulkan/functions/device_1_0.inc"
#include "xenia/ui/vulkan/functions/device_khr_bind_memory2.inc"
#include "xenia/ui/vulkan/functions/device_khr_get_memory_requirements2.inc"
#include "xenia/ui/vulkan/functions/device_khr_maintenance4.inc"
#include "xenia/ui/vulkan/functions/device_khr_swapchain.inc"
#undef XE_UI_VULKAN_FUNCTION_PROMOTED
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

  // For the current `physical_device_`, sets up the members obtained from the
  // physical device info, and tries to create a device and get the needed
  // queues.
  // The call is successful if `device_` is not VK_NULL_HANDLE as a result.
  void TryCreateDevice();

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

  DeviceInfo device_info_ = {};

  std::vector<QueueFamily> queue_families_;
  uint32_t queue_family_graphics_compute_;
  uint32_t queue_family_sparse_binding_;

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
