/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_UTIL_H_
#define XENIA_UI_VULKAN_VULKAN_UTIL_H_

#include <memory>
#include <string>

#include "xenia/ui/vulkan/vulkan.h"

namespace xe {
namespace ui {
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace ui {
namespace vulkan {

#define VK_SAFE_DESTROY(fn, dev, obj, alloc) \
  if (obj) {                                 \
    fn(dev, obj, alloc);                     \
    obj = nullptr;                           \
  }

class Fence {
 public:
  Fence(VkDevice device) : device_(device) {
    VkFenceCreateInfo fence_info;
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = nullptr;
    fence_info.flags = 0;
    vkCreateFence(device, &fence_info, nullptr, &fence_);
  }
  ~Fence() {
    vkDestroyFence(device_, fence_, nullptr);
    fence_ = nullptr;
  }

  VkResult status() const { return vkGetFenceStatus(device_, fence_); }

  VkFence fence() const { return fence_; }
  operator VkFence() const { return fence_; }

 private:
  VkDevice device_;
  VkFence fence_ = nullptr;
};

struct Version {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  std::string pretty_string;

  static uint32_t Make(uint32_t major, uint32_t minor, uint32_t patch);

  static Version Parse(uint32_t value);
};

const char* to_string(VkFormat format);
const char* to_string(VkPhysicalDeviceType type);
const char* to_string(VkSharingMode sharing_mode);
const char* to_string(VkResult result);

std::string to_flags_string(VkImageUsageFlagBits flags);
std::string to_flags_string(VkFormatFeatureFlagBits flags);
std::string to_flags_string(VkSurfaceTransformFlagBitsKHR flags);

const char* to_string(VkColorSpaceKHR color_space);
const char* to_string(VkPresentModeKHR present_mode);

// Throws a fatal error with some Vulkan help text.
void FatalVulkanError(std::string error);

// Logs and assets expecting the result to be VK_SUCCESS.
void CheckResult(VkResult result, const char* action);

struct LayerInfo {
  VkLayerProperties properties;
  std::vector<VkExtensionProperties> extensions;
};

struct DeviceInfo {
  VkPhysicalDevice handle;
  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory_properties;
  std::vector<VkQueueFamilyProperties> queue_family_properties;
  std::vector<LayerInfo> layers;
  std::vector<VkExtensionProperties> extensions;
};

// Defines a requirement for a layer or extension, used to both verify and
// enable them on initialization.
struct Requirement {
  // Layer or extension name.
  std::string name;
  // Minimum required spec version of the layer or extension.
  uint32_t min_version;
  // True if the requirement is optional (will not cause verification to fail).
  bool is_optional;
};

// Gets a list of enabled layer names based on the given layer requirements and
// available layer info.
// Returns a boolean indicating whether all required layers are present.
std::pair<bool, std::vector<const char*>> CheckRequirements(
    const std::vector<Requirement>& requirements,
    const std::vector<LayerInfo>& layer_infos);

// Gets a list of enabled extension names based on the given extension
// requirements and available extensions.
// Returns a boolean indicating whether all required extensions are present.
std::pair<bool, std::vector<const char*>> CheckRequirements(
    const std::vector<Requirement>& requirements,
    const std::vector<VkExtensionProperties>& extension_properties);

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_UTIL_H_
