/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/single_layout_descriptor_set_pool.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace vulkan {

SingleLayoutDescriptorSetPool::SingleLayoutDescriptorSetPool(
    const VulkanProvider& provider, uint32_t pool_set_count,
    uint32_t set_layout_descriptor_counts_count,
    const VkDescriptorPoolSize* set_layout_descriptor_counts,
    VkDescriptorSetLayout set_layout)
    : provider_(provider),
      pool_set_count_(pool_set_count),
      set_layout_(set_layout) {
  assert_not_zero(pool_set_count);
  pool_descriptor_counts_.resize(set_layout_descriptor_counts_count);
  for (uint32_t i = 0; i < set_layout_descriptor_counts_count; ++i) {
    VkDescriptorPoolSize& pool_descriptor_type_count =
        pool_descriptor_counts_[i];
    const VkDescriptorPoolSize& set_layout_descriptor_type_count =
        set_layout_descriptor_counts[i];
    pool_descriptor_type_count.type = set_layout_descriptor_type_count.type;
    pool_descriptor_type_count.descriptorCount =
        set_layout_descriptor_type_count.descriptorCount * pool_set_count;
  }
}

SingleLayoutDescriptorSetPool::~SingleLayoutDescriptorSetPool() {
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  if (current_pool_ != VK_NULL_HANDLE) {
    dfn.vkDestroyDescriptorPool(device, current_pool_, nullptr);
  }
  for (VkDescriptorPool pool : full_pools_) {
    dfn.vkDestroyDescriptorPool(device, pool, nullptr);
  }
}

size_t SingleLayoutDescriptorSetPool::Allocate() {
  if (!descriptor_sets_free_.empty()) {
    size_t free_index = descriptor_sets_free_.back();
    descriptor_sets_free_.pop_back();
    return free_index;
  }

  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  // Two iterations so if vkAllocateDescriptorSets fails even with a non-zero
  // current_pool_sets_remaining_, another attempt will be made in a new pool.
  for (uint32_t i = 0; i < 2; ++i) {
    if (current_pool_ != VK_NULL_HANDLE && !current_pool_sets_remaining_) {
      full_pools_.push_back(current_pool_);
      current_pool_ = VK_NULL_HANDLE;
    }
    if (current_pool_ == VK_NULL_HANDLE) {
      VkDescriptorPoolCreateInfo pool_create_info;
      pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      pool_create_info.pNext = nullptr;
      pool_create_info.flags = 0;
      pool_create_info.maxSets = pool_set_count_;
      pool_create_info.poolSizeCount = uint32_t(pool_descriptor_counts_.size());
      pool_create_info.pPoolSizes = pool_descriptor_counts_.data();
      if (dfn.vkCreateDescriptorPool(device, &pool_create_info, nullptr,
                                     &current_pool_) != VK_SUCCESS) {
        XELOGE(
            "SingleLayoutDescriptorSetPool: Failed to create a descriptor "
            "pool");
        return SIZE_MAX;
      }
      current_pool_sets_remaining_ = pool_set_count_;
    }

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
    descriptor_set_allocate_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_allocate_info.pNext = nullptr;
    descriptor_set_allocate_info.descriptorPool = current_pool_;
    descriptor_set_allocate_info.descriptorSetCount = 1;
    descriptor_set_allocate_info.pSetLayouts = &set_layout_;
    VkDescriptorSet descriptor_set;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      XELOGE(
          "SingleLayoutDescriptorSetPool: Failed to allocate a descriptor "
          "layout");
      if (current_pool_sets_remaining_ >= pool_set_count_) {
        // Failed to allocate in a new pool - something completely wrong, don't
        // store empty pools as full.
        dfn.vkDestroyDescriptorPool(device, current_pool_, nullptr);
        current_pool_ = VK_NULL_HANDLE;
        return SIZE_MAX;
      }
      full_pools_.push_back(current_pool_);
      current_pool_ = VK_NULL_HANDLE;
    }
    --current_pool_sets_remaining_;
    descriptor_sets_.push_back(descriptor_set);
    return descriptor_sets_.size() - 1;
  }

  // Both attempts have failed.
  return SIZE_MAX;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
