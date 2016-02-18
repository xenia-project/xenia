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

  uint32_t queue_family_index() const { return queue_family_index_; }
  VkQueue primary_queue() const { return primary_queue_; }
  const DeviceInfo& device_info() const { return device_info_; }

  // Allocates memory of the given size matching the required properties.
  VkDeviceMemory AllocateMemory(
      const VkMemoryRequirements& requirements,
      VkFlags required_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

 private:
  VulkanInstance* instance_ = nullptr;

  std::vector<Requirement> required_layers_;
  std::vector<Requirement> required_extensions_;

  DeviceInfo device_info_;
  uint32_t queue_family_index_ = 0;
  VkQueue primary_queue_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_DEVICE_H_
