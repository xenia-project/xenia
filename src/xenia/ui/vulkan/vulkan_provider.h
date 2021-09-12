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
#include <mutex>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/ui/graphics_provider.h"

#if XE_PLATFORM_ANDROID
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#elif XE_PLATFORM_WIN32
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

#define XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace vulkan {

class VulkanProvider : public GraphicsProvider {
 public:
  ~VulkanProvider() override;

  static std::unique_ptr<VulkanProvider> Create();

  std::unique_ptr<GraphicsContext> CreateHostContext(
      Window* target_window) override;
  std::unique_ptr<GraphicsContext> CreateEmulationContext() override;

  struct LibraryFunctions {
    // From the module.
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkDestroyInstance vkDestroyInstance;
    // From vkGetInstanceProcAddr.
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkEnumerateInstanceExtensionProperties
        vkEnumerateInstanceExtensionProperties;
    struct {
      PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    } v_1_1;
  };
  const LibraryFunctions& lfn() const { return lfn_; }

  struct InstanceExtensions {
    // Core since 1.1.0.
    bool khr_get_physical_device_properties2;
  };
  const InstanceExtensions& instance_extensions() const {
    return instance_extensions_;
  }
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
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    // VK_KHR_get_physical_device_properties2 or 1.1.0.
    PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
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
    // Core since 1.1.0.
    bool khr_dedicated_allocation;
    // Core since 1.2.0.
    bool khr_shader_float_controls;
    // Core since 1.2.0.
    bool khr_spirv_1_4;
  };
  const DeviceExtensions& device_extensions() const {
    return device_extensions_;
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
  // FIXME(Triang3l): Allow a separate queue for present - see
  // vulkan_provider.cc for details.
  uint32_t queue_family_graphics_compute() const {
    return queue_family_graphics_compute_;
  }
  const VkPhysicalDeviceFloatControlsPropertiesKHR&
  device_float_controls_properties() const {
    return device_float_controls_properties_;
  }

  VkDevice device() const { return device_; }
  struct DeviceFunctions {
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkBindImageMemory vkBindImageMemory;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
    PFN_vkCmdClearColorImage vkCmdClearColorImage;
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
    PFN_vkCmdDraw vkCmdDraw;
    PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCmdPushConstants vkCmdPushConstants;
    PFN_vkCmdSetScissor vkCmdSetScissor;
    PFN_vkCmdSetViewport vkCmdSetViewport;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
    PFN_vkCreateImage vkCreateImage;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
    PFN_vkCreateRenderPass vkCreateRenderPass;
    PFN_vkCreateSampler vkCreateSampler;
    PFN_vkCreateSemaphore vkCreateSemaphore;
    PFN_vkCreateShaderModule vkCreateShaderModule;
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
    PFN_vkDestroyImage vkDestroyImage;
    PFN_vkDestroyImageView vkDestroyImageView;
    PFN_vkDestroyPipeline vkDestroyPipeline;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
    PFN_vkDestroyRenderPass vkDestroyRenderPass;
    PFN_vkDestroySampler vkDestroySampler;
    PFN_vkDestroySemaphore vkDestroySemaphore;
    PFN_vkDestroyShaderModule vkDestroyShaderModule;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkResetCommandPool vkResetCommandPool;
    PFN_vkResetDescriptorPool vkResetDescriptorPool;
    PFN_vkResetFences vkResetFences;
    PFN_vkQueueBindSparse vkQueueBindSparse;
    PFN_vkQueuePresentKHR vkQueuePresentKHR;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkWaitForFences vkWaitForFences;
  };
  const DeviceFunctions& dfn() const { return dfn_; }

  VkResult SubmitToGraphicsComputeQueue(uint32_t submit_count,
                                        const VkSubmitInfo* submits,
                                        VkFence fence) {
    std::lock_guard<std::mutex> lock(queue_graphics_compute_mutex_);
    return dfn_.vkQueueSubmit(queue_graphics_compute_, submit_count, submits,
                              fence);
  }
  // Safer in Xenia context - in case a sparse binding queue was not obtained
  // for some reason.
  bool IsSparseBindingSupported() const {
    return queue_sparse_binding_ != VK_NULL_HANDLE;
  }
  VkResult BindSparse(uint32_t bind_info_count,
                      const VkBindSparseInfo* bind_info, VkFence fence) {
    assert_true(IsSparseBindingSupported());
    std::mutex& mutex = queue_sparse_binding_ == queue_graphics_compute_
                            ? queue_graphics_compute_mutex_
                            : queue_sparse_binding_separate_mutex_;
    std::lock_guard<std::mutex> lock(mutex);
    return dfn_.vkQueueBindSparse(queue_sparse_binding_, bind_info_count,
                                  bind_info, fence);
  }
  VkResult Present(const VkPresentInfoKHR* present_info) {
    // FIXME(Triang3l): Allow a separate queue for present - see
    // vulkan_provider.cc for details.
    std::lock_guard<std::mutex> lock(queue_graphics_compute_mutex_);
    return dfn_.vkQueuePresentKHR(queue_graphics_compute_, present_info);
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
  VulkanProvider() = default;

  bool Initialize();

#if XE_PLATFORM_LINUX
  void* library_ = nullptr;
#elif XE_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  LibraryFunctions lfn_ = {};

  InstanceExtensions instance_extensions_;
  VkInstance instance_ = VK_NULL_HANDLE;
  InstanceFunctions ifn_;

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties device_properties_;
  VkPhysicalDeviceFeatures device_features_;
  DeviceExtensions device_extensions_;
  uint32_t memory_types_device_local_;
  uint32_t memory_types_host_visible_;
  uint32_t memory_types_host_coherent_;
  uint32_t memory_types_host_cached_;
  uint32_t queue_family_graphics_compute_;
  VkPhysicalDeviceFloatControlsPropertiesKHR device_float_controls_properties_;

  VkDevice device_ = VK_NULL_HANDLE;
  DeviceFunctions dfn_ = {};
  VkQueue queue_graphics_compute_;
  // VkQueue access must be externally synchronized - must be locked when
  // submitting anything.
  std::mutex queue_graphics_compute_mutex_;
  // May be VK_NULL_HANDLE if not available.
  VkQueue queue_sparse_binding_;
  // If queue_sparse_binding_ == queue_graphics_compute_, lock
  // queue_graphics_compute_mutex_ instead when submitting sparse bindings.
  std::mutex queue_sparse_binding_separate_mutex_;

  VkSampler host_samplers_[size_t(HostSampler::kCount)] = {};
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
