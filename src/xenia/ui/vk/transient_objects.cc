/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vk/transient_objects.h"

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vk/vulkan_context.h"

namespace xe {
namespace ui {
namespace vk {

UploadBufferChain::UploadBufferChain(VulkanContext* context,
                                     VkDeviceSize frame_page_size,
                                     VkBufferUsageFlags usage_flags)
    : context_(context),
      frame_page_size_(frame_page_size),
      usage_flags_(usage_flags) {}

UploadBufferChain::~UploadBufferChain() {
  // Allow mid-frame destruction in cases like device loss.
  EndFrame();
  ClearCache();
}

void UploadBufferChain::ClearCache() {
  assert_true(current_frame_buffer_ == 0 && current_frame_buffer_bytes_ == 0);
  auto device = context_->GetVulkanProvider()->GetDevice();
  for (UploadBuffer& upload_buffer : upload_buffers_) {
    vkDestroyBuffer(device, upload_buffer.buffer, nullptr);
    vkUnmapMemory(device, upload_buffer.memory);
    vkFreeMemory(device, upload_buffer.memory, nullptr);
  }
}

void UploadBufferChain::EndFrame() {
  EndPage();
  current_frame_buffer_ = 0;
  buffer_creation_failed_ = false;
}

void UploadBufferChain::EndPage() {
  if (current_frame_buffer_bytes_ == 0) {
    return;
  }
  if (!memory_host_coherent_) {
    auto device = context_->GetVulkanProvider()->GetDevice();
    VkMappedMemoryRange flush_range;
    flush_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    flush_range.pNext = nullptr;
    flush_range.memory = upload_buffers_[current_frame_buffer_].memory;
    flush_range.offset = frame_page_size_ * context_->GetCurrentQueueFrame();
    flush_range.size = current_frame_buffer_bytes_;
    vkFlushMappedMemoryRanges(device, 1, &flush_range);
  }
  ++current_frame_buffer_;
  current_frame_buffer_bytes_ = 0;
}

bool UploadBufferChain::EnsureCurrentBufferAllocated() {
  if (current_frame_buffer_ < upload_buffers_.size()) {
    return true;
  }
  assert_true(current_frame_buffer_ == upload_buffers_.size());
  assert_true(current_frame_buffer_bytes_ == 0);
  if (buffer_creation_failed_) {
    return false;
  }

  UploadBuffer upload_buffer;

  auto provider = context_->GetVulkanProvider();
  auto device = provider->GetDevice();

  VkBufferCreateInfo buffer_create_info;
  buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_create_info.pNext = nullptr;
  buffer_create_info.flags = 0;
  buffer_create_info.size = frame_page_size_ * VulkanContext::kQueuedFrames;
  buffer_create_info.usage = usage_flags_;
  buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_create_info.queueFamilyIndexCount = 0;
  buffer_create_info.pQueueFamilyIndices = nullptr;
  if (vkCreateBuffer(device, &buffer_create_info, nullptr,
                     &upload_buffer.buffer) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan upload buffer with %ull x %u bytes and "
        "0x%.8X usage",
        buffer_create_info.size, VulkanContext::kQueuedFrames, usage_flags_);
    buffer_creation_failed_ = true;
    return false;
  }

  if (memory_type_ == UINT32_MAX) {
    // Page memory requirements not known yet.
    VkMemoryRequirements buffer_memory_requirements;
    vkGetBufferMemoryRequirements(device, upload_buffer.buffer,
                                  &buffer_memory_requirements);
    memory_type_ =
        provider->FindMemoryType(buffer_memory_requirements.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (memory_type_ == UINT32_MAX) {
      XELOGE(
          "Failed to find a memory type for an upload buffer with %ull bytes "
          "and 0x%.8X usage",
          buffer_memory_requirements.size, usage_flags_);
      vkDestroyBuffer(device, upload_buffer.buffer, nullptr);
      buffer_creation_failed_ = true;
      return false;
    }
    const VkPhysicalDeviceMemoryProperties& device_memory_properties =
        provider->GetPhysicalDeviceMemoryProperties();
    memory_host_coherent_ =
        (device_memory_properties.memoryTypes[memory_type_].propertyFlags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    memory_page_size_ = buffer_memory_requirements.size;
  }

  VkMemoryAllocateInfo memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_allocate_info.pNext = nullptr;
  memory_allocate_info.allocationSize = memory_page_size_;
  memory_allocate_info.memoryTypeIndex = memory_type_;
  if (vkAllocateMemory(device, &memory_allocate_info, nullptr,
                       &upload_buffer.memory) != VK_SUCCESS) {
    XELOGE("Failed to allocate %ull for a Vulkan upload buffer",
           memory_page_size_);
    vkDestroyBuffer(device, upload_buffer.buffer, nullptr);
    buffer_creation_failed_ = true;
    return false;
  }

  if (vkBindBufferMemory(device, upload_buffer.buffer, upload_buffer.memory,
                         0) != VK_SUCCESS) {
    XELOGE("Failed to bind a %ull-byte memory object to a Vulkan upload buffer",
           memory_page_size_);
    vkDestroyBuffer(device, upload_buffer.buffer, nullptr);
    vkFreeMemory(device, upload_buffer.memory, nullptr);
    buffer_creation_failed_ = true;
    return false;
  }

  if (vkMapMemory(device, upload_buffer.memory, 0, memory_page_size_, 0,
                  &upload_buffer.mapping) != VK_SUCCESS) {
    XELOGE("Failed to map a %ull-byte memory object of a Vulkan upload buffer",
           memory_page_size_);
    vkDestroyBuffer(device, upload_buffer.buffer, nullptr);
    vkFreeMemory(device, upload_buffer.memory, nullptr);
    buffer_creation_failed_ = true;
    return false;
  }

  upload_buffers_.push_back(upload_buffer);

  return true;
}

uint8_t* UploadBufferChain::RequestFull(VkDeviceSize size, VkBuffer& buffer_out,
                                        VkDeviceSize& offset_out) {
  assert_true(size <= frame_page_size_);
  if (size > frame_page_size_) {
    return nullptr;
  }
  if (frame_page_size_ - current_frame_buffer_bytes_ < size) {
    EndPage();
  }
  if (!EnsureCurrentBufferAllocated()) {
    return nullptr;
  }
  VkDeviceSize offset = current_frame_buffer_bytes_ +
                        context_->GetCurrentQueueFrame() * frame_page_size_;
  current_frame_buffer_bytes_ += size;
  UploadBuffer& upload_buffer = upload_buffers_[current_frame_buffer_];
  buffer_out = upload_buffer.buffer;
  offset_out = offset;
  return reinterpret_cast<uint8_t*>(upload_buffer.mapping) + offset;
}

uint8_t* UploadBufferChain::RequestPartial(VkDeviceSize size,
                                           VkBuffer& buffer_out,
                                           VkDeviceSize& offset_out,
                                           VkDeviceSize& size_out) {
  if (current_frame_buffer_bytes_ >= frame_page_size_) {
    EndPage();
  }
  if (!EnsureCurrentBufferAllocated()) {
    return nullptr;
  }
  size = std::min(size, frame_page_size_ - current_frame_buffer_bytes_);
  size_out = size;
  VkDeviceSize offset = current_frame_buffer_bytes_ +
                        context_->GetCurrentQueueFrame() * frame_page_size_;
  current_frame_buffer_bytes_ += size;
  UploadBuffer& upload_buffer = upload_buffers_[current_frame_buffer_];
  buffer_out = upload_buffer.buffer;
  offset_out = offset;
  return reinterpret_cast<uint8_t*>(upload_buffer.mapping) + offset;
}

}  // namespace vk
}  // namespace ui
}  // namespace xe
