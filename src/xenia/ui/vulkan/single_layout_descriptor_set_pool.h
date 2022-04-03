/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_SINGLE_DESCRIPTOR_SET_POOL_H_
#define XENIA_UI_VULKAN_SINGLE_DESCRIPTOR_SET_POOL_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

class SingleLayoutDescriptorSetPool {
 public:
  // set_layout_descriptor_counts must contain the numbers of descriptors of
  // each type in a single set with the layout (the multiplication by the pool
  // set count will be done internally). The descriptor set layout must not be
  // destroyed until this object is also destroyed.
  SingleLayoutDescriptorSetPool(
      const VulkanProvider& provider, uint32_t pool_set_count,
      uint32_t set_layout_descriptor_counts_count,
      const VkDescriptorPoolSize* set_layout_descriptor_counts,
      VkDescriptorSetLayout set_layout);
  ~SingleLayoutDescriptorSetPool();

  // Returns SIZE_MAX in case of a failure.
  size_t Allocate();
  void Free(size_t index) {
    assert_true(index < descriptor_sets_.size());
    descriptor_sets_free_.push_back(index);
  }
  VkDescriptorSet Get(size_t index) const { return descriptor_sets_[index]; }

 private:
  const VulkanProvider& provider_;
  uint32_t pool_set_count_;
  std::vector<VkDescriptorPoolSize> pool_descriptor_counts_;
  VkDescriptorSetLayout set_layout_;

  std::vector<VkDescriptorPool> full_pools_;
  VkDescriptorPool current_pool_ = VK_NULL_HANDLE;
  uint32_t current_pool_sets_remaining_ = 0;

  std::vector<VkDescriptorSet> descriptor_sets_;
  std::vector<size_t> descriptor_sets_free_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_SINGLE_DESCRIPTOR_SET_POOL_H_
