/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_util.h"

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {
namespace util {

void FlushMappedMemoryRange(const VulkanProvider& provider,
                            VkDeviceMemory memory, uint32_t memory_type,
                            VkDeviceSize offset, VkDeviceSize memory_size,
                            VkDeviceSize size) {
  assert_false(size != VK_WHOLE_SIZE && memory_size == VK_WHOLE_SIZE);
  assert_true(memory_size == VK_WHOLE_SIZE || offset <= memory_size);
  assert_true(memory_size == VK_WHOLE_SIZE || size <= memory_size - offset);
  if (!size ||
      (provider.memory_types_host_coherent() & (uint32_t(1) << memory_type))) {
    return;
  }
  VkMappedMemoryRange range;
  range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range.pNext = nullptr;
  range.memory = memory;
  range.offset = offset;
  range.size = size;
  VkDeviceSize non_coherent_atom_size =
      provider.device_properties().limits.nonCoherentAtomSize;
  // On some Android implementations, nonCoherentAtomSize is 0, not 1.
  if (non_coherent_atom_size > 1) {
    range.offset = offset / non_coherent_atom_size * non_coherent_atom_size;
    if (size != VK_WHOLE_SIZE) {
      range.size = std::min(xe::round_up(offset + size, non_coherent_atom_size),
                            memory_size) -
                   range.offset;
    }
  }
  provider.dfn().vkFlushMappedMemoryRanges(provider.device(), 1, &range);
}

bool CreateDedicatedAllocationBuffer(
    const VulkanProvider& provider, VkDeviceSize size, VkBufferUsageFlags usage,
    MemoryPurpose memory_purpose, VkBuffer& buffer_out,
    VkDeviceMemory& memory_out, uint32_t* memory_type_out,
    VkDeviceSize* memory_size_out) {
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkBufferCreateInfo buffer_create_info;
  buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_create_info.pNext = nullptr;
  buffer_create_info.flags = 0;
  buffer_create_info.size = size;
  buffer_create_info.usage = usage;
  buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_create_info.queueFamilyIndexCount = 0;
  buffer_create_info.pQueueFamilyIndices = nullptr;
  VkBuffer buffer;
  if (dfn.vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer) !=
      VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements memory_requirements;
  dfn.vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
  uint32_t memory_type = ChooseMemoryType(
      provider, memory_requirements.memoryTypeBits, memory_purpose);
  if (memory_type == UINT32_MAX) {
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    return false;
  }

  VkMemoryAllocateInfo memory_allocate_info;
  VkMemoryAllocateInfo* memory_allocate_info_last = &memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_allocate_info.pNext = nullptr;
  memory_allocate_info.allocationSize = memory_requirements.size;
  memory_allocate_info.memoryTypeIndex = memory_type;
  VkMemoryDedicatedAllocateInfoKHR memory_dedicated_allocate_info;
  if (provider.device_extensions().khr_dedicated_allocation) {
    memory_allocate_info_last->pNext = &memory_dedicated_allocate_info;
    memory_allocate_info_last = reinterpret_cast<VkMemoryAllocateInfo*>(
        &memory_dedicated_allocate_info);
    memory_dedicated_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    memory_dedicated_allocate_info.pNext = nullptr;
    memory_dedicated_allocate_info.image = VK_NULL_HANDLE;
    memory_dedicated_allocate_info.buffer = buffer;
  }
  VkDeviceMemory memory;
  if (dfn.vkAllocateMemory(device, &memory_allocate_info, nullptr, &memory) !=
      VK_SUCCESS) {
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    return false;
  }

  if (dfn.vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return false;
  }

  buffer_out = buffer;
  memory_out = memory;
  if (memory_type_out) {
    *memory_type_out = memory_type;
  }
  if (memory_size_out) {
    *memory_size_out = memory_allocate_info.allocationSize;
  }
  return true;
}

bool CreateDedicatedAllocationImage(const VulkanProvider& provider,
                                    const VkImageCreateInfo& create_info,
                                    MemoryPurpose memory_purpose,
                                    VkImage& image_out,
                                    VkDeviceMemory& memory_out,
                                    uint32_t* memory_type_out,
                                    VkDeviceSize* memory_size_out) {
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkImage image;
  if (dfn.vkCreateImage(device, &create_info, nullptr, &image) != VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements memory_requirements;
  dfn.vkGetImageMemoryRequirements(device, image, &memory_requirements);
  uint32_t memory_type = ChooseMemoryType(
      provider, memory_requirements.memoryTypeBits, memory_purpose);
  if (memory_type == UINT32_MAX) {
    dfn.vkDestroyImage(device, image, nullptr);
    return false;
  }

  VkMemoryAllocateInfo memory_allocate_info;
  VkMemoryAllocateInfo* memory_allocate_info_last = &memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_allocate_info.pNext = nullptr;
  memory_allocate_info.allocationSize = memory_requirements.size;
  memory_allocate_info.memoryTypeIndex = memory_type;
  VkMemoryDedicatedAllocateInfoKHR memory_dedicated_allocate_info;
  if (provider.device_extensions().khr_dedicated_allocation) {
    memory_allocate_info_last->pNext = &memory_dedicated_allocate_info;
    memory_allocate_info_last = reinterpret_cast<VkMemoryAllocateInfo*>(
        &memory_dedicated_allocate_info);
    memory_dedicated_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    memory_dedicated_allocate_info.pNext = nullptr;
    memory_dedicated_allocate_info.image = image;
    memory_dedicated_allocate_info.buffer = VK_NULL_HANDLE;
  }
  VkDeviceMemory memory;
  if (dfn.vkAllocateMemory(device, &memory_allocate_info, nullptr, &memory) !=
      VK_SUCCESS) {
    dfn.vkDestroyImage(device, image, nullptr);
    return false;
  }

  if (dfn.vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return false;
  }

  image_out = image;
  memory_out = memory;
  if (memory_type_out) {
    *memory_type_out = memory_type;
  }
  if (memory_size_out) {
    *memory_size_out = memory_allocate_info.allocationSize;
  }
  return true;
}

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe
