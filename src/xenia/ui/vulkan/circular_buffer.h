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

#include <queue>

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
  CircularBuffer(VulkanDevice* device, VkBufferUsageFlags usage,
                 VkDeviceSize capacity, VkDeviceSize alignment = 256);
  ~CircularBuffer();

  struct Allocation {
    void* host_ptr;
    VkDeviceMemory gpu_memory;
    VkDeviceSize offset;
    VkDeviceSize length;
    VkDeviceSize aligned_length;

    // Allocation usage fence. This allocation will be deleted when the fence
    // becomes signaled.
    VkFence fence;
  };

  VkResult Initialize(VkDeviceMemory memory, VkDeviceSize offset);
  VkResult Initialize();
  void Shutdown();

  void GetBufferMemoryRequirements(VkMemoryRequirements* reqs);

  VkDeviceSize alignment() const { return alignment_; }
  VkDeviceSize capacity() const { return capacity_; }
  VkBuffer gpu_buffer() const { return gpu_buffer_; }
  VkDeviceMemory gpu_memory() const { return gpu_memory_; }
  uint8_t* host_base() const { return host_base_; }

  bool CanAcquire(VkDeviceSize length);

  // Acquires space to hold memory. This allocation is only freed when the fence
  // reaches the signaled state.
  Allocation* Acquire(VkDeviceSize length, VkFence fence);
  void Flush(Allocation* allocation);
  void Flush(VkDeviceSize offset, VkDeviceSize length);

  // Clears all allocations, regardless of whether they've been consumed or not.
  void Clear();

  // Frees any allocations whose fences have been signaled.
  void Scavenge();

 private:
  // All of these variables are relative to gpu_base
  VkDeviceSize capacity_ = 0;
  VkDeviceSize alignment_ = 0;
  VkDeviceSize write_head_ = 0;
  VkDeviceSize read_head_ = 0;

  VulkanDevice* device_;
  bool owns_gpu_memory_ = false;
  VkBuffer gpu_buffer_ = nullptr;
  VkDeviceMemory gpu_memory_ = nullptr;
  VkDeviceSize gpu_base_ = 0;
  uint8_t* host_base_ = nullptr;

  std::queue<Allocation> allocations_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_CIRCULAR_BUFFER_H_
