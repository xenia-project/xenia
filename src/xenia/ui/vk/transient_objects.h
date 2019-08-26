/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VK_TRANSIENT_OBJECTS_H_
#define XENIA_UI_VK_TRANSIENT_OBJECTS_H_

#include <vector>

#include "xenia/ui/vk/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vk {

class VulkanContext;

class UploadBufferChain {
 public:
  UploadBufferChain(VulkanContext* context, VkDeviceSize frame_page_size,
                    VkBufferUsageFlags usage_flags);
  ~UploadBufferChain();

  void EndFrame();
  void ClearCache();

  // Request to write data in a single piece, creating a new page if the current
  // one doesn't have enough free space.
  uint8_t* RequestFull(VkDeviceSize size, VkBuffer& buffer_out,
                       VkDeviceSize& offset_out);
  // Request to write data in multiple parts, filling the buffer entirely.
  uint8_t* RequestPartial(VkDeviceSize size, VkBuffer& buffer_out,
                          VkDeviceSize& offset_out, VkDeviceSize& size_out);

 private:
  VulkanContext* context_;
  VkBufferUsageFlags usage_flags_;
  VkDeviceSize frame_page_size_;

  void EndPage();
  bool EnsureCurrentBufferAllocated();

  VkDeviceSize memory_page_size_ = 0;
  uint32_t memory_type_ = UINT32_MAX;
  bool memory_host_coherent_ = false;

  struct UploadBuffer {
    // frame_page_size_ * VulkanContext::kQueuedFrames bytes.
    // Single allocation for VulkanContext::kQueuedFrames pages so there are
    // less different memory objects.
    VkDeviceMemory memory;
    void* mapping;
    VkBuffer buffer;
  };
  std::vector<UploadBuffer> upload_buffers_;

  size_t current_frame_buffer_ = 0;
  VkDeviceSize current_frame_buffer_bytes_ = 0;

  bool buffer_creation_failed_ = false;
};

}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_TRANSIENT_OBJECTS_H_
