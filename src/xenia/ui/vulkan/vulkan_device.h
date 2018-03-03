/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_DEVICE_H_
#define XENIA_UI_VULKAN_VULKAN_DEVICE_H_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanInstance;

// Wrapper and utilities for VkDevice.
// Prefer passing this around over a VkDevice and casting as needed to call
// APIs.
class VulkanDevice {
 public:
  VulkanDevice(VulkanInstance* instance);
  ~VulkanDevice();

  VkDevice handle = nullptr;

  operator VkDevice() const { return handle; }
  operator VkPhysicalDevice() const { return device_info_.handle; }

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

  // Initializes the device, querying and enabling extensions and layers and
  // preparing the device for general use.
  // If initialization succeeds it's likely that no more failures beyond runtime
  // issues will occur.
  bool Initialize(DeviceInfo device_info);

  bool HasEnabledExtension(const char* name);

  uint32_t queue_family_index() const { return queue_family_index_; }
  std::mutex& primary_queue_mutex() { return queue_mutex_; }
  // Access to the primary queue must be synchronized with primary_queue_mutex.
  VkQueue primary_queue() const { return primary_queue_; }
  const DeviceInfo& device_info() const { return device_info_; }

  // Acquires a queue for exclusive use by the caller.
  // The queue will not be touched by any other code until it's returned with
  // ReleaseQueue.
  // Not all devices support queues or only support a limited number. If this
  // returns null the primary_queue should be used with the
  // primary_queue_mutex.
  // This method is thread safe.
  VkQueue AcquireQueue(uint32_t queue_family_index);
  // Releases a queue back to the device pool.
  // This method is thread safe.
  void ReleaseQueue(VkQueue queue, uint32_t queue_family_index);

  void DbgSetObjectName(uint64_t object, VkDebugReportObjectTypeEXT object_type,
                        std::string name);

  // True if RenderDoc is attached and available for use.
  bool is_renderdoc_attached() const;
  // Begins capturing the current frame in RenderDoc, if it is attached.
  // Must be paired with EndRenderDocCapture. Multiple frames cannot be
  // captured at the same time.
  void BeginRenderDocFrameCapture();
  // Ends a capture.
  void EndRenderDocFrameCapture();

  // Allocates memory of the given size matching the required properties.
  VkDeviceMemory AllocateMemory(
      const VkMemoryRequirements& requirements,
      VkFlags required_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

 private:
  VulkanInstance* instance_ = nullptr;

  std::vector<Requirement> required_layers_;
  std::vector<Requirement> required_extensions_;
  std::vector<const char*> enabled_extensions_;

  bool debug_marker_ena_ = false;
  PFN_vkDebugMarkerSetObjectNameEXT pfn_vkDebugMarkerSetObjectNameEXT_;

  DeviceInfo device_info_;
  uint32_t queue_family_index_ = 0;
  std::mutex queue_mutex_;
  VkQueue primary_queue_ = nullptr;
  std::vector<std::vector<VkQueue>> free_queues_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_DEVICE_H_
