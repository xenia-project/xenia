/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_CIRCULAR_BUFFER_H_
#define XENIA_UI_VULKAN_CIRCULAR_BUFFER_H_

#include <unordered_map>

#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

// A circular buffer, intended to hold (fairly) temporary memory that will be
// released when a fence is signaled. Best used when allocations are taken
// in-order with command buffer submission.
//
// Allocations loop around the buffer in circles (but are not fragmented at the
// ends of the buffer), where trailing older allocations are freed after use.
class CircularBuffer {
 public:
  CircularBuffer(VulkanDevice* device);
  ~CircularBuffer();

  struct Allocation {
    void* host_ptr;
    VkDeviceMemory gpu_memory;
    VkDeviceSize offset;
    VkDeviceSize length;
    VkDeviceSize aligned_length;

    // Allocation usage fence. This allocation will be deleted when the fence
    // becomes signaled.
    std::shared_ptr<Fence> fence;
  };

  bool Initialize(VkDeviceSize capacity, VkBufferUsageFlags usage,
                  VkDeviceSize alignment = 256);
  void Shutdown();

  VkDeviceSize alignment() const { return alignment_; }
  VkDeviceSize capacity() const { return capacity_; }
  VkBuffer gpu_buffer() const { return gpu_buffer_; }
  VkDeviceMemory gpu_memory() const { return gpu_memory_; }
  uint8_t* host_base() const { return host_base_; }

  bool CanAcquire(VkDeviceSize length);

  // Acquires space to hold memory. This allocation is only freed when the fence
  // reaches the signaled state.
  Allocation* Acquire(VkDeviceSize length, std::shared_ptr<Fence> fence);
  void Flush(Allocation* allocation);

  // Clears all allocations, regardless of whether they've been consumed or not.
  void Clear();

  // Frees any allocations whose fences have been signaled.
  void Scavenge();

 private:
  VkDeviceSize capacity_ = 0;
  VkDeviceSize alignment_ = 0;
  VkDeviceSize write_head_ = 0;
  VkDeviceSize read_head_ = 0;

  VulkanDevice* device_;
  VkBuffer gpu_buffer_ = nullptr;
  VkDeviceMemory gpu_memory_ = nullptr;
  VkDeviceSize gpu_base_ = 0;
  uint8_t* host_base_ = nullptr;

  std::unordered_map<uint64_t, uintptr_t> allocation_cache_;
  std::vector<Allocation*> allocations_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_CIRCULAR_BUFFER_H_
