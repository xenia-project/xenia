/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_UTIL_H_
#define XENIA_UI_VULKAN_VULKAN_UTIL_H_

#include <cstdint>

#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {
namespace util {

template <typename F, typename T>
inline bool DestroyAndNullHandle(F* destroy_function, T& handle) {
  if (handle != VK_NULL_HANDLE) {
    destroy_function(handle, nullptr);
    handle = VK_NULL_HANDLE;
    return true;
  }
  return false;
}

template <typename F, typename P, typename T>
inline bool DestroyAndNullHandle(F* destroy_function, P parent, T& handle) {
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

inline VkDeviceSize GetMappableMemorySize(const VulkanProvider& provider,
                                          VkDeviceSize size) {
  VkDeviceSize non_coherent_atom_size =
      provider.device_properties().limits.nonCoherentAtomSize;
  // On some Android implementations, nonCoherentAtomSize is 0, not 1.
  if (non_coherent_atom_size > 1) {
    size = xe::round_up(size, non_coherent_atom_size, false);
  }
  return size;
}

inline uint32_t ChooseHostMemoryType(const VulkanProvider& provider,
                                     uint32_t supported_types,
                                     bool is_readback) {
  supported_types &= provider.memory_types_host_visible();
  uint32_t host_cached = provider.memory_types_host_cached();
  uint32_t memory_type;
  // For upload, uncached is preferred so writes do not pollute the CPU cache.
  // For readback, cached is preferred so multiple CPU reads are fast.
  // If the preferred caching behavior is not available, pick any host-visible.
  if (xe::bit_scan_forward(
          supported_types & (is_readback ? host_cached : ~host_cached),
          &memory_type) ||
      xe::bit_scan_forward(supported_types, &memory_type)) {
    return memory_type;
  }
  return UINT32_MAX;
}

void FlushMappedMemoryRange(const VulkanProvider& provider,
                            VkDeviceMemory memory, uint32_t memory_type,
                            VkDeviceSize offset = 0,
                            VkDeviceSize size = VK_WHOLE_SIZE);

inline void InitializeSubresourceRange(
    VkImageSubresourceRange& range,
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t base_mip_level = 0, uint32_t level_count = VK_REMAINING_MIP_LEVELS,
    uint32_t base_array_layer = 0,
    uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS) {
  range.aspectMask = aspect_mask;
  range.baseMipLevel = base_mip_level;
  range.levelCount = level_count;
  range.baseArrayLayer = base_array_layer;
  range.layerCount = layer_count;
}

// Creates a buffer backed by a dedicated allocation. If using a mappable memory
// purpose (upload/readback), the allocation size will be aligned to
// nonCoherentAtomSize.
bool CreateDedicatedAllocationBuffer(
    const VulkanProvider& provider, VkDeviceSize size, VkBufferUsageFlags usage,
    MemoryPurpose memory_purpose, VkBuffer& buffer_out,
    VkDeviceMemory& memory_out, uint32_t* memory_type_out = nullptr);

inline VkShaderModule CreateShaderModule(const VulkanProvider& provider,
                                         const void* code, size_t code_size) {
  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = code_size;
  shader_module_create_info.pCode = reinterpret_cast<const uint32_t*>(code);
  VkShaderModule shader_module;
  return provider.dfn().vkCreateShaderModule(
             provider.device(), &shader_module_create_info, nullptr,
             &shader_module) == VK_SUCCESS
             ? shader_module
             : nullptr;
}

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_UTIL_H_
