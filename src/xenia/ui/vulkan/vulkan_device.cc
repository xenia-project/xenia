/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_device.h"

#include <gflags/gflags.h>

#include <cinttypes>
#include <mutex>
#include <string>

#include "third_party/renderdoc/renderdoc_app.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanDevice::VulkanDevice(VulkanInstance* instance) : instance_(instance) {
  if (FLAGS_vulkan_validation) {
    DeclareRequiredLayer("VK_LAYER_LUNARG_standard_validation",
                         Version::Make(0, 0, 0), true);
    // DeclareRequiredLayer("VK_LAYER_GOOGLE_unique_objects", Version::Make(0,
    // 0, 0), true);
    /*
    DeclareRequiredLayer("VK_LAYER_GOOGLE_threading", Version::Make(0, 0, 0),
    true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_core_validation",
    Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_object_tracker",
    Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_draw_state", Version::Make(0, 0, 0),
    true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_parameter_validation",
    Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_swapchain", Version::Make(0, 0, 0),
    true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_device_limits",
    Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_image", Version::Make(0, 0, 0), true);
    */
  }

  DeclareRequiredExtension(VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
                           Version::Make(0, 0, 0), true);
}

VulkanDevice::~VulkanDevice() {
  if (handle) {
    vkDestroyDevice(handle, nullptr);
    handle = nullptr;
  }
}

bool VulkanDevice::Initialize(DeviceInfo device_info) {
  // Gather list of enabled layer names.
  auto layers_result = CheckRequirements(required_layers_, device_info.layers);
  auto& enabled_layers = layers_result.second;

  // Gather list of enabled extension names.
  auto extensions_result =
      CheckRequirements(required_extensions_, device_info.extensions);
  enabled_extensions_ = extensions_result.second;

  // We wait until both extensions and layers are checked before failing out so
  // that the user gets a complete list of what they have/don't.
  if (!extensions_result.first || !layers_result.first) {
    FatalVulkanError(
        "Layer and extension verification failed; aborting initialization");
    return false;
  }

  // Query supported features so we can make sure we have what we need.
  VkPhysicalDeviceFeatures supported_features;
  vkGetPhysicalDeviceFeatures(device_info.handle, &supported_features);
  VkPhysicalDeviceFeatures enabled_features = {0};
  bool any_features_missing = false;
#define ENABLE_AND_EXPECT(name)                                  \
  if (!supported_features.name) {                                \
    any_features_missing = true;                                 \
    FatalVulkanError("Vulkan device is missing feature " #name); \
  } else {                                                       \
    enabled_features.name = VK_TRUE;                             \
  }
  ENABLE_AND_EXPECT(shaderClipDistance);
  ENABLE_AND_EXPECT(shaderCullDistance);
  ENABLE_AND_EXPECT(shaderTessellationAndGeometryPointSize);
  ENABLE_AND_EXPECT(geometryShader);
  ENABLE_AND_EXPECT(depthClamp);
  ENABLE_AND_EXPECT(multiViewport);
  ENABLE_AND_EXPECT(independentBlend);
  // TODO(benvanik): add other features.
  if (any_features_missing) {
    XELOGE(
        "One or more required device features are missing; aborting "
        "initialization");
    return false;
  }

  // Pick a queue.
  // Any queue we use must support both graphics and presentation.
  // TODO(benvanik): use multiple queues (DMA-only, compute-only, etc).
  if (device_info.queue_family_properties.empty()) {
    FatalVulkanError("No queue families available");
    return false;
  }
  uint32_t ideal_queue_family_index = UINT_MAX;
  uint32_t queue_count = 1;
  for (size_t i = 0; i < device_info.queue_family_properties.size(); ++i) {
    auto queue_flags = device_info.queue_family_properties[i].queueFlags;
    if (!device_info.queue_family_supports_present[i]) {
      // Can't present from this queue, so ignore it.
      continue;
    }
    if (queue_flags & VK_QUEUE_GRAPHICS_BIT) {
      // Can do graphics and present - good!
      ideal_queue_family_index = static_cast<uint32_t>(i);
      // Grab all the queues we can.
      queue_count = device_info.queue_family_properties[i].queueCount;
      break;
    }
  }
  if (ideal_queue_family_index == UINT_MAX) {
    FatalVulkanError(
        "No queue families available that can both do graphics and present");
    return false;
  }

  // Some tools *cough* renderdoc *cough* can't handle multiple queues.
  if (FLAGS_vulkan_primary_queue_only) {
    queue_count = 1;
  }

  VkDeviceQueueCreateInfo queue_info;
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.pNext = nullptr;
  queue_info.flags = 0;
  queue_info.queueFamilyIndex = ideal_queue_family_index;
  queue_info.queueCount = queue_count;
  std::vector<float> queue_priorities(queue_count);
  // Prioritize the primary queue.
  queue_priorities[0] = 1.0f;
  queue_info.pQueuePriorities = queue_priorities.data();

  VkDeviceCreateInfo create_info;
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pNext = nullptr;
  create_info.flags = 0;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = &queue_info;
  create_info.enabledLayerCount = static_cast<uint32_t>(enabled_layers.size());
  create_info.ppEnabledLayerNames = enabled_layers.data();
  create_info.enabledExtensionCount =
      static_cast<uint32_t>(enabled_extensions_.size());
  create_info.ppEnabledExtensionNames = enabled_extensions_.data();
  create_info.pEnabledFeatures = &enabled_features;

  auto err = vkCreateDevice(device_info.handle, &create_info, nullptr, &handle);
  switch (err) {
    case VK_SUCCESS:
      // Ok!
      break;
    case VK_ERROR_INITIALIZATION_FAILED:
      FatalVulkanError("Device initialization failed; generic");
      return false;
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      FatalVulkanError(
          "Device initialization failed; requested extension not present");
      return false;
    case VK_ERROR_LAYER_NOT_PRESENT:
      FatalVulkanError(
          "Device initialization failed; requested layer not present");
      return false;
    default:
      FatalVulkanError(std::string("Device initialization failed; unknown: ") +
                       to_string(err));
      return false;
  }

  // Set flags so we can track enabled extensions easily.
  for (auto& ext : enabled_extensions_) {
    if (!std::strcmp(ext, VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
      debug_marker_ena_ = true;
    }
  }

  device_info_ = std::move(device_info);
  queue_family_index_ = ideal_queue_family_index;

  // Get the primary queue used for most submissions/etc.
  vkGetDeviceQueue(handle, queue_family_index_, 0, &primary_queue_);

  // Get all additional queues, if we got any.
  for (uint32_t i = 0; i < queue_count - 1; ++i) {
    VkQueue queue;
    vkGetDeviceQueue(handle, queue_family_index_, i, &queue);
    free_queues_.push_back(queue);
  }

  XELOGVK("Device initialized successfully!");
  return true;
}

VkQueue VulkanDevice::AcquireQueue() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (free_queues_.empty()) {
    return nullptr;
  }
  auto queue = free_queues_.back();
  free_queues_.pop_back();
  return queue;
}

void VulkanDevice::ReleaseQueue(VkQueue queue) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  free_queues_.push_back(queue);
}

void VulkanDevice::DbgSetObjectName(VkDevice device, uint64_t object,
                                    VkDebugReportObjectTypeEXT object_type,
                                    std::string name) {
  // Check to see if the extension is even loaded
  if (vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT") == nullptr) {
    return;
  }

  // TODO(DrChat): fix linkage errors
  VkDebugMarkerObjectNameInfoEXT info;
  info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
  info.pNext = nullptr;
  info.objectType = object_type;
  info.object = object;
  info.pObjectName = name.c_str();
  // vkDebugMarkerSetObjectNameEXT(device, &info);
}

void VulkanDevice::DbgSetObjectName(uint64_t object,
                                    VkDebugReportObjectTypeEXT object_type,
                                    std::string name) {
  if (!debug_marker_ena_) {
    // Extension disabled.
    return;
  }

  DbgSetObjectName(*this, object, object_type, name);
}

bool VulkanDevice::is_renderdoc_attached() const {
  return instance_->is_renderdoc_attached();
}

void VulkanDevice::BeginRenderDocFrameCapture() {
  auto api = reinterpret_cast<RENDERDOC_API_1_0_1*>(instance_->renderdoc_api());
  if (!api) {
    return;
  }
  assert_true(api->IsFrameCapturing() == 0);

  api->StartFrameCapture(nullptr, nullptr);
}

void VulkanDevice::EndRenderDocFrameCapture() {
  auto api = reinterpret_cast<RENDERDOC_API_1_0_1*>(instance_->renderdoc_api());
  if (!api) {
    return;
  }
  assert_true(api->IsFrameCapturing() == 1);

  api->EndFrameCapture(nullptr, nullptr);
}

VkDeviceMemory VulkanDevice::AllocateMemory(
    const VkMemoryRequirements& requirements, VkFlags required_properties) {
  // Search memory types to find one matching our requirements and our
  // properties.
  uint32_t type_index = UINT_MAX;
  for (uint32_t i = 0; i < device_info_.memory_properties.memoryTypeCount;
       ++i) {
    const auto& memory_type = device_info_.memory_properties.memoryTypes[i];
    if (((requirements.memoryTypeBits >> i) & 1) == 1) {
      // Type is available for use; check for a match on properties.
      if ((memory_type.propertyFlags & required_properties) ==
          required_properties) {
        type_index = i;
        break;
      }
    }
  }
  if (type_index == UINT_MAX) {
    XELOGE("Unable to find a matching memory type");
    return nullptr;
  }

  // Allocate the memory.
  VkMemoryAllocateInfo memory_info;
  memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_info.pNext = nullptr;
  memory_info.allocationSize = requirements.size;
  memory_info.memoryTypeIndex = type_index;
  VkDeviceMemory memory = nullptr;
  auto err = vkAllocateMemory(handle, &memory_info, nullptr, &memory);
  CheckResult(err, "vkAllocateMemory");
  return memory;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
