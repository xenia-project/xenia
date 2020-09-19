/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanUploadBufferPool::VulkanUploadBufferPool(const VulkanProvider& provider,
                                               VkBufferUsageFlags usage,
                                               size_t page_size)
    : GraphicsUploadBufferPool(page_size), provider_(provider), usage_(usage) {
  VkDeviceSize non_coherent_atom_size =
      provider_.device_properties().limits.nonCoherentAtomSize;
  // Memory mappings are always aligned to nonCoherentAtomSize, so for
  // simplicity, round the page size to it now. On some Android implementations,
  // nonCoherentAtomSize is 0, not 1.
  if (non_coherent_atom_size > 1) {
    page_size_ = xe::round_up(page_size_, non_coherent_atom_size);
  }
}

uint8_t* VulkanUploadBufferPool::Request(uint64_t submission_index, size_t size,
                                         size_t alignment, VkBuffer& buffer_out,
                                         VkDeviceSize& offset_out) {
  size_t offset;
  const VulkanPage* page =
      static_cast<const VulkanPage*>(GraphicsUploadBufferPool::Request(
          submission_index, size, alignment, offset));
  if (!page) {
    return nullptr;
  }
  buffer_out = page->buffer_;
  offset_out = VkDeviceSize(offset);
  return reinterpret_cast<uint8_t*>(page->mapping_) + offset;
}

uint8_t* VulkanUploadBufferPool::RequestPartial(uint64_t submission_index,
                                                size_t size, size_t alignment,
                                                VkBuffer& buffer_out,
                                                VkDeviceSize& offset_out,
                                                VkDeviceSize& size_out) {
  size_t offset, size_obtained;
  const VulkanPage* page =
      static_cast<const VulkanPage*>(GraphicsUploadBufferPool::RequestPartial(
          submission_index, size, alignment, offset, size_obtained));
  if (!page) {
    return nullptr;
  }
  buffer_out = page->buffer_;
  offset_out = VkDeviceSize(offset);
  size_out = VkDeviceSize(size_obtained);
  return reinterpret_cast<uint8_t*>(page->mapping_) + offset;
}

GraphicsUploadBufferPool::Page*
VulkanUploadBufferPool::CreatePageImplementation() {
  if (memory_type_ == kMemoryTypeUnavailable) {
    // Don't try to create everything again and again if totally broken.
    return nullptr;
  }

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  // For the first call, the page size is already aligned to nonCoherentAtomSize
  // for mapping.
  VkBufferCreateInfo buffer_create_info;
  buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_create_info.pNext = nullptr;
  buffer_create_info.flags = 0;
  buffer_create_info.size = VkDeviceSize(page_size_);
  buffer_create_info.usage = usage_;
  buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_create_info.queueFamilyIndexCount = 0;
  buffer_create_info.pQueueFamilyIndices = nullptr;
  VkBuffer buffer;
  if (dfn.vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer) !=
      VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan upload buffer with {} bytes", page_size_);
    return nullptr;
  }

  if (memory_type_ == kMemoryTypeUnknown) {
    VkMemoryRequirements memory_requirements;
    dfn.vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
    uint32_t memory_types_host_visible = provider_.memory_types_host_visible();
    if (!xe::bit_scan_forward(
            memory_requirements.memoryTypeBits & memory_types_host_visible,
            &memory_type_)) {
      XELOGE(
          "No host-visible memory types can store an Vulkan upload buffer with "
          "{} bytes",
          page_size_);
      memory_type_ = kMemoryTypeUnavailable;
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      return nullptr;
    }
    allocation_size_ = memory_requirements.size;
    // On some Android implementations, nonCoherentAtomSize is 0, not 1.
    VkDeviceSize non_coherent_atom_size =
        std::max(provider_.device_properties().limits.nonCoherentAtomSize,
                 VkDeviceSize(1));
    VkDeviceSize allocation_size_aligned =
        allocation_size_ / non_coherent_atom_size * non_coherent_atom_size;
    if (allocation_size_aligned > page_size_) {
      // Try to occupy all the allocation padding. If that's going to require
      // even more memory for some reason, don't.
      buffer_create_info.size = allocation_size_aligned;
      VkBuffer buffer_expanded;
      if (dfn.vkCreateBuffer(device, &buffer_create_info, nullptr,
                             &buffer_expanded) == VK_SUCCESS) {
        VkMemoryRequirements memory_requirements_expanded;
        dfn.vkGetBufferMemoryRequirements(device, buffer_expanded,
                                          &memory_requirements_expanded);
        uint32_t memory_type_expanded;
        if (memory_requirements_expanded.size <= allocation_size_ &&
            xe::bit_scan_forward(memory_requirements_expanded.memoryTypeBits &
                                     memory_types_host_visible,
                                 &memory_type_expanded)) {
          // page_size_ must be aligned to nonCoherentAtomSize.
          page_size_ = size_t(allocation_size_aligned);
          allocation_size_ = memory_requirements_expanded.size;
          memory_type_ = memory_type_expanded;
          dfn.vkDestroyBuffer(device, buffer, nullptr);
          buffer = buffer_expanded;
        } else {
          dfn.vkDestroyBuffer(device, buffer_expanded, nullptr);
        }
      }
    }
  }

  VkMemoryAllocateInfo memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  VkMemoryDedicatedAllocateInfoKHR memory_dedicated_allocate_info;
  if (provider_.device_extensions().khr_dedicated_allocation) {
    memory_dedicated_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    memory_dedicated_allocate_info.pNext = nullptr;
    memory_dedicated_allocate_info.image = VK_NULL_HANDLE;
    memory_dedicated_allocate_info.buffer = buffer;
    memory_allocate_info.pNext = &memory_dedicated_allocate_info;
  } else {
    memory_allocate_info.pNext = nullptr;
  }
  memory_allocate_info.allocationSize = allocation_size_;
  memory_allocate_info.memoryTypeIndex = memory_type_;
  VkDeviceMemory memory;
  if (dfn.vkAllocateMemory(device, &memory_allocate_info, nullptr, &memory) !=
      VK_SUCCESS) {
    XELOGE("Failed to allocate {} bytes of Vulkan upload buffer memory",
           allocation_size_);
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    return nullptr;
  }

  if (dfn.vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
    XELOGE("Failed to bind memory to a Vulkan upload buffer with {} bytes",
           page_size_);
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return nullptr;
  }

  void* mapping;
  // page_size_ is aligned to nonCoherentAtomSize.
  if (dfn.vkMapMemory(device, memory, 0, page_size_, 0, &mapping) !=
      VK_SUCCESS) {
    XELOGE("Failed to map {} bytes of Vulkan upload buffer memory", page_size_);
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return nullptr;
  }

  return new VulkanPage(provider_, buffer, memory, mapping);
}

void VulkanUploadBufferPool::FlushPageWrites(Page* page, size_t offset,
                                             size_t size) {
  if (provider_.memory_types_host_coherent() & (uint32_t(1) << memory_type_)) {
    return;
  }
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  VkMappedMemoryRange range;
  range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range.pNext = nullptr;
  range.memory = static_cast<const VulkanPage*>(page)->memory_;
  range.offset = VkDeviceSize(offset);
  range.size = VkDeviceSize(size);
  VkDeviceSize non_coherent_atom_size =
      provider_.device_properties().limits.nonCoherentAtomSize;
  // On some Android implementations, nonCoherentAtomSize is 0, not 1.
  if (non_coherent_atom_size > 1) {
    VkDeviceSize end =
        xe::round_up(range.offset + range.size, non_coherent_atom_size);
    range.offset =
        range.offset / non_coherent_atom_size * non_coherent_atom_size;
    range.size = end - range.offset;
  }
  dfn.vkFlushMappedMemoryRanges(device, 1, &range);
}

VulkanUploadBufferPool::VulkanPage::~VulkanPage() {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  dfn.vkDestroyBuffer(device, buffer_, nullptr);
  // Unmapping is done implicitly when the memory is freed.
  dfn.vkFreeMemory(device, memory_, nullptr);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
