/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_DEVICE_H_
#define XENIA_UI_VULKAN_VULKAN_DEVICE_H_

#include <memory>
#include <mutex>
#include <vector>

#include "xenia/ui/vulkan/vulkan_instance.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanDevice {
 public:
  static std::unique_ptr<VulkanDevice> CreateIfSupported(
      const VulkanInstance* vulkan_instance, VkPhysicalDevice physical_device,
      bool with_gpu_emulation, bool with_swapchain);

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;
  VulkanDevice(VulkanDevice&&) = delete;
  VulkanDevice& operator=(VulkanDevice&&) = delete;

  ~VulkanDevice();

  const VulkanInstance* vulkan_instance() const { return vulkan_instance_; }

  VkPhysicalDevice physical_device() const { return physical_device_; }

  // If functionality from higher API versions is used, increase this.
  // This is for VkApplicationInfo.
  // "apiVersion must be the highest version of Vulkan that the application is
  // designed to use"
  // "The patch version number specified in apiVersion is ignored when creating
  // an instance object"
  static constexpr uint32_t kHighestUsedApiMinorVersion =
      VK_MAKE_API_VERSION(0, 1, 3, 0);

  struct Properties {
    // Vulkan 1.0
    uint32_t apiVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    uint32_t driverVersion = 0;
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};

    uint32_t maxImageDimension2D = 4096;
    uint32_t maxImageDimension3D = 256;
    uint32_t maxImageDimensionCube = 4096;
    uint32_t maxImageArrayLayers = 256;
    uint32_t maxStorageBufferRange = uint32_t(1) << 27;
    uint32_t maxSamplerAllocationCount = 4000;
    uint32_t maxPerStageDescriptorSamplers = 16;
    uint32_t maxPerStageDescriptorStorageBuffers = 4;
    uint32_t maxPerStageDescriptorSampledImages = 16;
    uint32_t maxPerStageResources = 128;
    uint32_t maxVertexOutputComponents = 64;
    uint32_t maxTessellationEvaluationOutputComponents = 64;
    uint32_t maxGeometryInputComponents = 64;
    uint32_t maxGeometryOutputComponents = 64;
    uint32_t maxFragmentInputComponents = 64;
    uint32_t maxFragmentCombinedOutputResources = 4;
    float maxSamplerAnisotropy = 1.0f;
    uint32_t maxViewportDimensions[2] = {4096, 4096};
    VkDeviceSize minUniformBufferOffsetAlignment = 256;
    VkDeviceSize minStorageBufferOffsetAlignment = 256;
    uint32_t maxFramebufferWidth = 4096;
    uint32_t maxFramebufferHeight = 4096;
    VkSampleCountFlags framebufferColorSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    VkSampleCountFlags framebufferDepthSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    VkSampleCountFlags framebufferStencilSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    VkSampleCountFlags sampledImageColorSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    VkSampleCountFlags sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlags sampledImageDepthSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    VkSampleCountFlags sampledImageStencilSampleCounts =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    bool standardSampleLocations = false;
    VkDeviceSize optimalBufferCopyOffsetAlignment = 1;
    VkDeviceSize optimalBufferCopyRowPitchAlignment = 1;
    VkDeviceSize nonCoherentAtomSize = 256;

    bool robustBufferAccess = false;
    bool fullDrawIndexUint32 = false;
    bool independentBlend = false;
    bool geometryShader = false;
    bool tessellationShader = false;
    bool sampleRateShading = false;
    bool depthClamp = false;
    bool fillModeNonSolid = false;
    bool samplerAnisotropy = false;
    bool occlusionQueryPrecise = false;
    bool vertexPipelineStoresAndAtomics = false;
    bool fragmentStoresAndAtomics = false;
    bool shaderClipDistance = false;
    bool shaderCullDistance = false;
    bool sparseBinding = false;
    bool sparseResidencyBuffer = false;

    // VK_KHR_sampler_mirror_clamp_to_edge (#15, promoted to 1.2)

    bool samplerMirrorClampToEdge = false;

    // VK_KHR_portability_subset (#164)

    bool constantAlphaColorBlendFactors = false;
    bool imageViewFormatReinterpretation = false;
    bool imageViewFormatSwizzle = false;
    bool pointPolygons = false;
    bool separateStencilMaskRef = false;
    bool shaderSampleRateInterpolationFunctions = false;
    bool triangleFans = false;

    // VK_KHR_driver_properties (#197, promoted to 1.2)

    VkDriverId driverID = VkDriverId(0);

    // VK_KHR_shader_float_controls (#198, promoted to 1.2)

    bool shaderSignedZeroInfNanPreserveFloat32 = false;
    bool shaderDenormFlushToZeroFloat32 = false;
    bool shaderRoundingModeRTEFloat32 = false;

    // VK_EXT_fragment_shader_interlock (#252)

    bool fragmentShaderSampleInterlock = false;
    bool fragmentShaderPixelInterlock = false;

    // VK_EXT_shader_demote_to_helper_invocation (#277, promoted to 1.3)

    bool shaderDemoteToHelperInvocation = false;

    // VK_EXT_non_seamless_cube_map (#423)

    bool nonSeamlessCubeMap = false;
  };

  // Properties of the core API and enabled extensions, and enabled features.
  // Some supported functionality is enabled conditionally based on the
  // `with_swapchain` and `with_gpu_emulation` options.
  const Properties& properties() const { return properties_; }

  // Enabled extensions not fully covered by the device properties and optional
  // feature flags in the `Properties` structure (primarily those adding API
  // functionality rather than GPU features). Also set to true if the version of
  // the Vulkan API they were promoted to it supported (with the
  // `ext_major_minor_` prefix rather than `ext_`).
  struct Extensions {
    bool ext_KHR_swapchain = false;                     // #2
    bool ext_1_1_KHR_dedicated_allocation = false;      // #128
    bool ext_EXT_shader_stencil_export = false;         // #141
    bool ext_1_1_KHR_get_memory_requirements2 = false;  // #147
    bool ext_1_2_KHR_image_format_list = false;         // #148
    // Has optional features not implied by this being true.
    bool ext_1_1_KHR_sampler_ycbcr_conversion = false;  // #157
    bool ext_1_1_KHR_bind_memory2 = false;              // #158
    bool ext_1_2_KHR_spirv_1_4 = false;                 // #237
    bool ext_EXT_memory_budget = false;                 // #238
    // Has optional features not implied by this being true.
    bool ext_1_3_KHR_maintenance4 = false;  // #414
  };

  const Extensions& extensions() const { return extensions_; }

  VkDevice device() const { return device_; }

  struct Functions {
#define XE_UI_VULKAN_FUNCTION(name) PFN_##name name = nullptr;
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  PFN_##core_name core_name = nullptr;
#include "xenia/ui/vulkan/functions/device_1_0.inc"
    // VK_KHR_swapchain (#2)
#include "xenia/ui/vulkan/functions/device_khr_swapchain.inc"
    // VK_KHR_get_memory_requirements2 (#147, promoted to 1.1)
#include "xenia/ui/vulkan/functions/device_1_1_khr_get_memory_requirements2.inc"
    // VK_KHR_bind_memory2 (#158, promoted to 1.1)
#include "xenia/ui/vulkan/functions/device_1_1_khr_bind_memory2.inc"
    // VK_KHR_maintenance4 (#414, promoted to 1.3)
#include "xenia/ui/vulkan/functions/device_1_3_khr_maintenance4.inc"
#undef XE_UI_VULKAN_FUNCTION_PROMOTED
#undef XE_UI_VULKAN_FUNCTION
  };

  const Functions& functions() const { return functions_; }

  template <typename Object>
  void SetObjectName(const VkObjectType object_type, const Object object_handle,
                     const char* const object_name) const {
    if (!vulkan_instance()->extensions().ext_EXT_debug_utils) {
      return;
    }
    VkDebugUtilsObjectNameInfoEXT object_name_info;
    object_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    object_name_info.pNext = nullptr;
    object_name_info.objectType = object_type;
    object_name_info.objectHandle = (uint64_t)object_handle;
    object_name_info.pObjectName = object_name;
    vulkan_instance()->functions().vkSetDebugUtilsObjectNameEXT(
        device(), &object_name_info);
  }

  struct Queue {
    // Host access to queues must be externally synchronized in Vulkan.
    std::recursive_mutex mutex;
    VkQueue queue = nullptr;

    explicit Queue(const VkQueue queue) : queue(queue) {}

    class Acquisition {
     public:
      explicit Acquisition(Queue& queue)
          : lock_(queue.mutex), queue_(queue.queue) {}

      VkQueue queue() const { return queue_; }

     private:
      std::unique_lock<std::recursive_mutex> lock_;
      VkQueue queue_;
    };

    Acquisition Acquire() { return Acquisition(*this); }
  };

  struct QueueFamily {
    VkQueueFlags queue_flags = 0;
    bool may_support_presentation = false;
    std::vector<std::unique_ptr<Queue>> queues;
  };

  const std::vector<QueueFamily>& queue_families() const {
    return queue_families_;
  }
  uint32_t queue_family_graphics_compute() const {
    return queue_family_graphics_compute_;
  }
  // UINT32_MAX if not supported or not enabled.
  // May be the same as queue_family_graphics_compute().
  uint32_t queue_family_sparse_binding() const {
    return queue_family_sparse_binding_;
  }

  Queue::Acquisition AcquireQueue(const uint32_t queue_family_index,
                                  const uint32_t queue_index) const {
    return queue_families()[queue_family_index].queues[queue_index]->Acquire();
  }

  struct MemoryTypes {
    uint32_t device_local = 0b0;
    uint32_t host_visible = 0b0;
    uint32_t host_coherent = 0b0;
    uint32_t host_cached = 0b0;
  };

  const MemoryTypes& memory_types() const { return memory_types_; }

 private:
  explicit VulkanDevice(const VulkanInstance* vulkan_instance,
                        VkPhysicalDevice physical_device);

  const VulkanInstance* vulkan_instance_ = nullptr;
  VkPhysicalDevice physical_device_ = nullptr;

  Properties properties_;
  Extensions extensions_;

  VkDevice device_ = nullptr;

  Functions functions_;

  std::vector<QueueFamily> queue_families_;
  uint32_t queue_family_graphics_compute_ = UINT32_MAX;
  uint32_t queue_family_sparse_binding_ = UINT32_MAX;

  MemoryTypes memory_types_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_DEVICE_H_
