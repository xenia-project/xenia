/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_UPLOAD_BUFFER_POOL_H_
#define XENIA_UI_VULKAN_VULKAN_UPLOAD_BUFFER_POOL_H_

#include "xenia/ui/graphics_upload_buffer_pool.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanUploadBufferPool : public GraphicsUploadBufferPool {
 public:
  VulkanUploadBufferPool(const VulkanProvider& provider,
                         VkBufferUsageFlags usage,
                         size_t page_size = kDefaultPageSize);

  uint8_t* Request(uint64_t submission_index, size_t size, size_t alignment,
                   VkBuffer& buffer_out, VkDeviceSize& offset_out);
  uint8_t* RequestPartial(uint64_t submission_index, size_t size,
                          size_t alignment, VkBuffer& buffer_out,
                          VkDeviceSize& offset_out, VkDeviceSize& size_out);

 protected:
  Page* CreatePageImplementation() override;

  void FlushPageWrites(Page* page, size_t offset, size_t size) override;

 private:
  struct VulkanPage : public Page {
    // Takes ownership of the buffer and its memory and mapping.
    VulkanPage(const VulkanProvider& provider, VkBuffer buffer,
               VkDeviceMemory memory, void* mapping)
        : provider_(provider),
          buffer_(buffer),
          memory_(memory),
          mapping_(mapping) {}
    ~VulkanPage() override;
    const VulkanProvider& provider_;
    VkBuffer buffer_;
    VkDeviceMemory memory_;
    void* mapping_;
  };

  const VulkanProvider& provider_;

  VkDeviceSize allocation_size_;
  static constexpr uint32_t kMemoryTypeUnknown = UINT32_MAX;
  static constexpr uint32_t kMemoryTypeUnavailable = kMemoryTypeUnknown - 1;
  uint32_t memory_type_ = UINT32_MAX;

  VkBufferUsageFlags usage_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_UPLOAD_BUFFER_POOL_H_
