/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_TRANSIENT_DESCRIPTOR_POOL_H_
#define XENIA_UI_VULKAN_TRANSIENT_DESCRIPTOR_POOL_H_

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

// A pool of descriptor pools for single-submission use. For simplicity of
// tracking when overflow happens, only allocating descriptors for sets
// containing descriptors of a single type.
class TransientDescriptorPool {
 public:
  TransientDescriptorPool(const VulkanProvider& provider,
                          VkDescriptorType descriptor_type,
                          uint32_t page_descriptor_set_count,
                          uint32_t page_descriptor_count);
  ~TransientDescriptorPool();

  void Reclaim(uint64_t completed_submission_index);
  void ClearCache();

  // Returns the allocated set, or VK_NULL_HANDLE if failed to allocate.
  VkDescriptorSet Request(uint64_t submission_index,
                          VkDescriptorSetLayout layout,
                          uint32_t layout_descriptor_count);

 private:
  const VulkanProvider& provider_;

  VkDescriptorType descriptor_type_;
  uint32_t page_descriptor_set_count_;
  uint32_t page_descriptor_count_;

  std::vector<VkDescriptorPool> pages_writable_;
  uint64_t page_current_last_submission_ = 0;
  uint32_t page_current_descriptor_sets_used_ = 0;
  uint32_t page_current_descriptors_used_ = 0;
  std::deque<std::pair<VkDescriptorPool, uint64_t>> pages_submitted_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_TRANSIENT_DESCRIPTOR_POOL_H_
