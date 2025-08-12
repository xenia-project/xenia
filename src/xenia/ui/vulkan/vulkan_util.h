/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_UTIL_H_
#define XENIA_UI_VULKAN_VULKAN_UTIL_H_

#include <algorithm>
#include <cstdint>

#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {
namespace util {

template <typename DestroyFunction, typename Object>
inline bool DestroyAndNullHandle(DestroyFunction* const destroy_function,
                                 Object& handle) {
  if (handle != VK_NULL_HANDLE) {
    destroy_function(handle, nullptr);
    handle = VK_NULL_HANDLE;
    return true;
  }
  return false;
}

template <typename DestroyFunction, typename Parent, typename Object>
inline bool DestroyAndNullHandle(DestroyFunction* const destroy_function,
                                 const Parent parent, Object& handle) {
  if (handle != VK_NULL_HANDLE) {
    destroy_function(parent, handle, nullptr);
    handle = VK_NULL_HANDLE;
    return true;
  }
  return false;
}

enum class MemoryPurpose {
  kDeviceLocal,
  kUpload,
  kReadback,
};

inline uint32_t ChooseHostMemoryType(
    const VulkanDevice::MemoryTypes& memory_types, uint32_t supported_types,
    const bool is_readback) {
  supported_types &= memory_types.host_visible;
  uint32_t memory_type;
  // For upload, uncached is preferred so writes do not pollute the CPU cache.
  // For readback, cached is preferred so multiple CPU reads are fast.
  // If the preferred caching behavior is not available, pick any host-visible.
  if (xe::bit_scan_forward(
          supported_types & (is_readback ? memory_types.host_cached
                                         : ~memory_types.host_cached),
          &memory_type) ||
      xe::bit_scan_forward(supported_types, &memory_type)) {
    return memory_type;
  }
  return UINT32_MAX;
}

inline uint32_t ChooseMemoryType(const VulkanDevice::MemoryTypes& memory_types,
                                 const uint32_t supported_types,
                                 const MemoryPurpose purpose) {
  switch (purpose) {
    case MemoryPurpose::kDeviceLocal: {
      uint32_t memory_type;
      if (xe::bit_scan_forward(supported_types & memory_types.device_local,
                               &memory_type)) {
        return memory_type;
      }
      if (xe::bit_scan_forward(supported_types, &memory_type)) {
        return memory_type;
      }
      return UINT32_MAX;
    } break;
    case MemoryPurpose::kUpload:
    case MemoryPurpose::kReadback:
      return ChooseHostMemoryType(memory_types, supported_types,
                                  purpose == MemoryPurpose::kReadback);
    default:
      assert_unhandled_case(purpose);
      return UINT32_MAX;
  }
}

// Actual memory size is required if explicit size is specified for clamping to
// the actual memory allocation size while rounding to the non-coherent atom
// size (offset + size passed to vkFlushMappedMemoryRanges inside this function
// must be either a multiple of nonCoherentAtomSize (but not exceeding the
// memory size) or equal to the memory size).
void FlushMappedMemoryRange(const VulkanDevice* vulkan_device,
                            VkDeviceMemory memory, uint32_t memory_type,
                            VkDeviceSize offset = 0,
                            VkDeviceSize memory_size = VK_WHOLE_SIZE,
                            VkDeviceSize size = VK_WHOLE_SIZE);

inline VkExtent2D GetMax2DFramebufferExtent(
    const VulkanDevice::Properties& device_properties) {
  VkExtent2D max_extent;
  max_extent.width = std::min(device_properties.maxFramebufferWidth,
                              device_properties.maxImageDimension2D);
  max_extent.height = std::min(device_properties.maxFramebufferHeight,
                               device_properties.maxImageDimension2D);
  return max_extent;
}

inline VkImageSubresourceRange InitializeSubresourceRange(
    const VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    const uint32_t base_mip_level = 0,
    const uint32_t level_count = VK_REMAINING_MIP_LEVELS,
    const uint32_t base_array_layer = 0,
    const uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS) {
  VkImageSubresourceRange range;
  range.aspectMask = aspect_mask;
  range.baseMipLevel = base_mip_level;
  range.levelCount = level_count;
  range.baseArrayLayer = base_array_layer;
  range.layerCount = layer_count;
  return range;
}

// Creates a buffer backed by a dedicated allocation. The allocation size will
// NOT be aligned to nonCoherentAtomSize - if mapping or flushing not the whole
// size, memory_size_out must be used for clamping the range.
bool CreateDedicatedAllocationBuffer(const VulkanDevice* vulkan_device,
                                     VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     MemoryPurpose memory_purpose,
                                     VkBuffer& buffer_out,
                                     VkDeviceMemory& memory_out,
                                     uint32_t* memory_type_out = nullptr,
                                     VkDeviceSize* memory_size_out = nullptr);

bool CreateDedicatedAllocationImage(const VulkanDevice* vulkan_device,
                                    const VkImageCreateInfo& create_info,
                                    MemoryPurpose memory_purpose,
                                    VkImage& image_out,
                                    VkDeviceMemory& memory_out,
                                    uint32_t* memory_type_out = nullptr,
                                    VkDeviceSize* memory_size_out = nullptr);

// Explicitly accepting const uint32_t* to make sure attention is paid to the
// alignment where this is called for safety on different host architectures.
inline VkShaderModule CreateShaderModule(
    const VulkanDevice* const vulkan_device, const uint32_t* const code,
    const size_t code_size_bytes) {
  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = code_size_bytes;
  shader_module_create_info.pCode = code;
  VkShaderModule shader_module;
  return vulkan_device->functions().vkCreateShaderModule(
             vulkan_device->device(), &shader_module_create_info, nullptr,
             &shader_module) == VK_SUCCESS
             ? shader_module
             : VK_NULL_HANDLE;
}

VkPipeline CreateComputePipeline(
    const VulkanDevice* vulkan_device, VkPipelineLayout layout,
    VkShaderModule shader,
    const VkSpecializationInfo* specialization_info = nullptr,
    const char* entry_point = "main");
VkPipeline CreateComputePipeline(
    const VulkanDevice* vulkan_device, VkPipelineLayout layout,
    const uint32_t* shader_code, size_t shader_code_size_bytes,
    const VkSpecializationInfo* specialization_info = nullptr,
    const char* entry_point = "main");

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_UTIL_H_
