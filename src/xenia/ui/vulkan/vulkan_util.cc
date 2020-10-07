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
                            VkDeviceSize offset, VkDeviceSize size) {
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
      range.size =
          xe::round_up(offset + size, non_coherent_atom_size) - range.offset;
    }
  }
  provider.dfn().vkFlushMappedMemoryRanges(provider.device(), 1, &range);
}

bool CreateDedicatedAllocationBuffer(
    const VulkanProvider& provider, VkDeviceSize size, VkBufferUsageFlags usage,
    MemoryPurpose memory_purpose, VkBuffer& buffer_out,
    VkDeviceMemory& memory_out, uint32_t* memory_type_out) {
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
  uint32_t memory_type = UINT32_MAX;
  switch (memory_purpose) {
    case MemoryPurpose::kDeviceLocal:
      if (!xe::bit_scan_forward(memory_requirements.memoryTypeBits &
                                    provider.memory_types_device_local(),
                                &memory_type)) {
        memory_type = UINT32_MAX;
      }
      break;
    case MemoryPurpose::kUpload:
    case MemoryPurpose::kReadback:
      memory_type =
          ChooseHostMemoryType(provider, memory_requirements.memoryTypeBits,
                               memory_purpose == MemoryPurpose::kReadback);
      break;
    default:
      assert_unhandled_case(memory_purpose);
  }
  if (memory_type == UINT32_MAX) {
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    return false;
  }

  VkMemoryAllocateInfo memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  VkMemoryDedicatedAllocateInfoKHR memory_dedicated_allocate_info;
  if (provider.device_extensions().khr_dedicated_allocation) {
    memory_dedicated_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    memory_dedicated_allocate_info.pNext = nullptr;
    memory_dedicated_allocate_info.image = VK_NULL_HANDLE;
    memory_dedicated_allocate_info.buffer = buffer;
    memory_allocate_info.pNext = &memory_dedicated_allocate_info;
  } else {
    memory_allocate_info.pNext = nullptr;
  }
  memory_allocate_info.allocationSize = memory_requirements.size;
  if (memory_purpose == MemoryPurpose::kUpload ||
      memory_purpose == MemoryPurpose::kReadback) {
    memory_allocate_info.allocationSize =
        GetMappableMemorySize(provider, memory_allocate_info.allocationSize);
  }
  memory_allocate_info.memoryTypeIndex = memory_type;
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
  return true;
}

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe
