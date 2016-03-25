/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"

#include "xenia/ui/vulkan/circular_buffer.h"

namespace xe {
namespace ui {
namespace vulkan {

CircularBuffer::CircularBuffer(VulkanDevice* device) : device_(device) {}
CircularBuffer::~CircularBuffer() { Shutdown(); }

bool CircularBuffer::Initialize(VkDeviceSize capacity, VkBufferUsageFlags usage,
                                VkDeviceSize alignment) {
  VkResult status = VK_SUCCESS;
  capacity = xe::round_up(capacity, alignment);

  // Create our internal buffer.
  VkBufferCreateInfo buffer_info;
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = nullptr;
  buffer_info.flags = 0;
  buffer_info.size = capacity;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_info.queueFamilyIndexCount = 0;
  buffer_info.pQueueFamilyIndices = nullptr;
  status = vkCreateBuffer(*device_, &buffer_info, nullptr, &gpu_buffer_);
  CheckResult(status, "vkCreateBuffer");
  if (status != VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(*device_, gpu_buffer_, &reqs);

  // Allocate memory from the device to back the buffer.
  assert_true(reqs.size == capacity);
  reqs.alignment = std::max(alignment, reqs.alignment);
  gpu_memory_ = device_->AllocateMemory(reqs);
  if (!gpu_memory_) {
    XELOGE("CircularBuffer::Initialize - Failed to allocate memory!");
    Shutdown();
    return false;
  }

  alignment_ = reqs.alignment;
  capacity_ = reqs.size;
  gpu_base_ = 0;

  // Bind the buffer to its backing memory.
  status = vkBindBufferMemory(*device_, gpu_buffer_, gpu_memory_, gpu_base_);
  CheckResult(status, "vkBindBufferMemory");
  if (status != VK_SUCCESS) {
    XELOGE("CircularBuffer::Initialize - Failed to bind memory!");
    Shutdown();
    return false;
  }

  // Map the memory so we can access it.
  status = vkMapMemory(*device_, gpu_memory_, gpu_base_, capacity_, 0,
                       reinterpret_cast<void**>(&host_base_));
  CheckResult(status, "vkMapMemory");
  if (status != VK_SUCCESS) {
    XELOGE("CircularBuffer::Initialize - Failed to map memory!");
    Shutdown();
    return false;
  }

  return true;
}

void CircularBuffer::Shutdown() {
  Clear();
  if (host_base_) {
    vkUnmapMemory(*device_, gpu_memory_);
    host_base_ = nullptr;
  }
  if (gpu_buffer_) {
    vkDestroyBuffer(*device_, gpu_buffer_, nullptr);
    gpu_buffer_ = nullptr;
  }
  if (gpu_memory_) {
    vkFreeMemory(*device_, gpu_memory_, nullptr);
    gpu_memory_ = nullptr;
  }
}

bool CircularBuffer::CanAcquire(VkDeviceSize length) {
  // Make sure the length is aligned.
  length = xe::round_up(length, alignment_);
  if (allocations_.empty()) {
    // Read head has caught up to write head (entire buffer available for write)
    assert(read_head_ == write_head_);
    return capacity_ > length;
  } else if (write_head_ < read_head_) {
    // Write head wrapped around and is behind read head.
    // |  write  |---- read ----|
    return (read_head_ - write_head_) > length;
  } else {
    // Read head behind write head.
    // 1. Check if there's enough room from write -> capacity
    // |  |---- read ----|    write     |
    if ((capacity_ - write_head_) > length) {
      return true;
    }

    // 2. Check if there's enough room from 0 -> read
    // |    write     |---- read ----|  |
    if ((read_head_) > length) {
      return true;
    }
  }

  return false;
}

CircularBuffer::Allocation* CircularBuffer::Acquire(
    VkDeviceSize length, std::shared_ptr<Fence> fence) {
  if (!CanAcquire(length)) {
    return nullptr;
  }

  VkDeviceSize aligned_length = xe::round_up(length, alignment_);
  if (allocations_.empty()) {
    // Entire buffer available.
    assert(read_head_ == write_head_);
    assert(capacity_ > aligned_length);

    read_head_ = 0;
    write_head_ = length;

    auto alloc = new Allocation();
    alloc->host_ptr = host_base_ + 0;
    alloc->gpu_memory = gpu_memory_;
    alloc->offset = gpu_base_ + 0;
    alloc->length = length;
    alloc->aligned_length = aligned_length;
    alloc->fence = fence;
    allocations_.push_back(alloc);
    return alloc;
  } else if (write_head_ < read_head_) {
    // Write head behind read head.
    assert_true(read_head_ - write_head_ >= aligned_length);

    auto alloc = new Allocation();
    alloc->host_ptr = host_base_ + write_head_;
    alloc->gpu_memory = gpu_memory_;
    alloc->offset = gpu_base_ + write_head_;
    alloc->length = length;
    alloc->aligned_length = aligned_length;
    alloc->fence = fence;
    write_head_ += aligned_length;
    allocations_.push_back(alloc);

    return alloc;
  } else {
    // Write head after read head
    if (capacity_ - write_head_ >= aligned_length) {
      // Free space from write -> capacity
      auto alloc = new Allocation();
      alloc->host_ptr = host_base_ + write_head_;
      alloc->gpu_memory = gpu_memory_;
      alloc->offset = gpu_base_ + write_head_;
      alloc->length = length;
      alloc->aligned_length = aligned_length;
      alloc->fence = fence;
      write_head_ += aligned_length;
      allocations_.push_back(alloc);

      return alloc;
    } else if ((read_head_ - 0) > aligned_length) {
      // Free space from begin -> read
      auto alloc = new Allocation();
      alloc->host_ptr = host_base_ + write_head_;
      alloc->gpu_memory = gpu_memory_;
      alloc->offset = gpu_base_ + 0;
      alloc->length = length;
      alloc->aligned_length = aligned_length;
      alloc->fence = fence;
      write_head_ = aligned_length;
      allocations_.push_back(alloc);

      return alloc;
    }
  }

  return nullptr;
}

void CircularBuffer::Discard(Allocation* allocation) {
  // TODO: Revert write_head_ (only if this is the last alloc though)
  // Or maybe just disallow discards.
  for (auto it = allocations_.begin(); it != allocations_.end(); ++it) {
    if (*it == allocation) {
      allocations_.erase(it);
      break;
    }
  }

  delete allocation;
}

void CircularBuffer::Flush(Allocation* allocation) {
  VkMappedMemoryRange range;
  range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range.pNext = nullptr;
  range.memory = gpu_memory_;
  range.offset = gpu_base_ + allocation->offset;
  range.size = allocation->length;
  vkFlushMappedMemoryRanges(*device_, 1, &range);
}

void CircularBuffer::Clear() {
  for (auto it = allocations_.begin(); it != allocations_.end();) {
    delete *it;
    it = allocations_.erase(it);
  }

  write_head_ = read_head_ = 0;
}

void CircularBuffer::Scavenge() {
  for (auto it = allocations_.begin(); it != allocations_.end();) {
    if ((*it)->fence->status() != VK_SUCCESS) {
      // Don't bother freeing following allocations to ensure proper ordering.
      break;
    }

    read_head_ = (read_head_ + (*it)->aligned_length) % capacity_;
    delete *it;
    it = allocations_.erase(it);
  }

  if (allocations_.empty()) {
    // Reset R/W heads.
    read_head_ = write_head_ = 0;
  } else {
    // FIXME: Haven't verified this works correctly when actually rotating :P
    assert_always();
  }
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe