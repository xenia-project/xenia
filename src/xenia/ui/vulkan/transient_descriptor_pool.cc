/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/transient_descriptor_pool.h"

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace vulkan {

TransientDescriptorPool::TransientDescriptorPool(
    const VulkanProvider& provider, VkDescriptorType descriptor_type,
    uint32_t page_descriptor_set_count, uint32_t page_descriptor_count)
    : provider_(provider),
      descriptor_type_(descriptor_type),
      page_descriptor_set_count_(page_descriptor_set_count),
      page_descriptor_count_(page_descriptor_count) {
  assert_not_zero(page_descriptor_set_count);
  assert_true(page_descriptor_set_count <= page_descriptor_count);
}

TransientDescriptorPool::~TransientDescriptorPool() { ClearCache(); }

void TransientDescriptorPool::Reclaim(uint64_t completed_submission_index) {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  while (!pages_submitted_.empty()) {
    const auto& descriptor_pool_pair = pages_submitted_.front();
    if (descriptor_pool_pair.second > completed_submission_index) {
      break;
    }
    dfn.vkResetDescriptorPool(device, descriptor_pool_pair.first, 0);
    pages_writable_.push_back(descriptor_pool_pair.first);
    pages_submitted_.pop_front();
  }
}

void TransientDescriptorPool::ClearCache() {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  for (const auto& descriptor_pool_pair : pages_submitted_) {
    dfn.vkDestroyDescriptorPool(device, descriptor_pool_pair.first, nullptr);
  }
  pages_submitted_.clear();
  page_current_descriptors_used_ = 0;
  page_current_descriptor_sets_used_ = 0;
  page_current_last_submission_ = 0;
  for (VkDescriptorPool descriptor_pool : pages_writable_) {
    dfn.vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  }
  pages_writable_.clear();
}

VkDescriptorSet TransientDescriptorPool::Request(
    uint64_t submission_index, VkDescriptorSetLayout layout,
    uint32_t layout_descriptor_count) {
  assert_true(submission_index >= page_current_last_submission_);
  assert_not_zero(layout_descriptor_count);
  assert_true(layout_descriptor_count <= page_descriptor_count_);

  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = nullptr;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts = &layout;
  VkDescriptorSet descriptor_set;

  // Try to allocate as normal.
  // TODO(Triang3l): Investigate the possibility of reuse of descriptor sets, as
  // vkAllocateDescriptorSets may be implemented suboptimally.
  if (!pages_writable_.empty()) {
    if (page_current_descriptor_sets_used_ < page_descriptor_set_count_ &&
        page_current_descriptors_used_ + layout_descriptor_count <=
            page_descriptor_count_) {
      descriptor_set_allocate_info.descriptorPool = pages_writable_.front();
      switch (dfn.vkAllocateDescriptorSets(
          device, &descriptor_set_allocate_info, &descriptor_set)) {
        case VK_SUCCESS:
          page_current_last_submission_ = submission_index;
          ++page_current_descriptor_sets_used_;
          page_current_descriptors_used_ += layout_descriptor_count;
          return descriptor_set;
        case VK_ERROR_FRAGMENTED_POOL:
        case VK_ERROR_OUT_OF_POOL_MEMORY:
          // Need to create a new pool.
          break;
        default:
          XELOGE(
              "Failed to allocate a transient Vulkan descriptor set with {} "
              "descriptors of type {}",
              layout_descriptor_count, uint32_t(descriptor_type_));
          return VK_NULL_HANDLE;
      }
    }

    // Overflow - go to the next pool.
    pages_submitted_.emplace_back(pages_writable_.front(),
                                  page_current_last_submission_);
    pages_writable_.front() = pages_writable_.back();
    pages_writable_.pop_back();
    page_current_descriptor_sets_used_ = 0;
    page_current_descriptors_used_ = 0;
  }

  if (pages_writable_.empty()) {
    VkDescriptorPoolSize descriptor_pool_size;
    descriptor_pool_size.type = descriptor_type_;
    descriptor_pool_size.descriptorCount = page_descriptor_count_;
    VkDescriptorPoolCreateInfo descriptor_pool_create_info;
    descriptor_pool_create_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_create_info.pNext = nullptr;
    descriptor_pool_create_info.flags = 0;
    descriptor_pool_create_info.maxSets = page_descriptor_set_count_;
    descriptor_pool_create_info.poolSizeCount = 1;
    descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size;
    VkDescriptorPool descriptor_pool;
    if (dfn.vkCreateDescriptorPool(device, &descriptor_pool_create_info,
                                   nullptr, &descriptor_pool) != VK_SUCCESS) {
      XELOGE(
          "Failed to create a transient Vulkan descriptor pool for {} sets of "
          "up to {} descriptors of type {}",
          page_descriptor_set_count_, page_descriptor_count_,
          uint32_t(descriptor_type_));
      return VK_NULL_HANDLE;
    }
    pages_writable_.push_back(descriptor_pool);
  }

  // Try to allocate after handling overflow.
  descriptor_set_allocate_info.descriptorPool = pages_writable_.front();
  if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                   &descriptor_set) != VK_SUCCESS) {
    XELOGE(
        "Failed to allocate a transient Vulkan descriptor set with {} "
        "descriptors of type {}",
        layout_descriptor_count, uint32_t(descriptor_type_));
    return VK_NULL_HANDLE;
  }
  page_current_last_submission_ = submission_index;
  ++page_current_descriptor_sets_used_;
  page_current_descriptors_used_ += layout_descriptor_count;
  return descriptor_set;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
