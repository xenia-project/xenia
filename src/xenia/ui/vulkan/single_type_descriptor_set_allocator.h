/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_SINGLE_TYPE_DESCRIPTOR_SET_ALLOCATOR_H_
#define XENIA_UI_VULKAN_SINGLE_TYPE_DESCRIPTOR_SET_ALLOCATOR_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

// Allocates multiple descriptors of a single type in descriptor set layouts
// consisting of descriptors of only that type. There's no way to free these
// descriptors within the SingleTypeDescriptorSetAllocator, per-layout free
// lists should be used externally.
class SingleTypeDescriptorSetAllocator {
 public:
  explicit SingleTypeDescriptorSetAllocator(
      const ui::vulkan::VulkanProvider& provider,
      VkDescriptorType descriptor_type, uint32_t descriptors_per_page,
      uint32_t descriptor_sets_per_page)
      : provider_(provider),
        descriptor_sets_per_page_(descriptor_sets_per_page) {
    assert_not_zero(descriptor_sets_per_page_);
    descriptor_pool_size_.type = descriptor_type;
    // Not allocating sets with 0 descriptors using the allocator - pointless to
    // have the descriptor count below the set count.
    descriptor_pool_size_.descriptorCount =
        std::max(descriptors_per_page, descriptor_sets_per_page);
  }
  SingleTypeDescriptorSetAllocator(
      const SingleTypeDescriptorSetAllocator& allocator) = delete;
  SingleTypeDescriptorSetAllocator& operator=(
      const SingleTypeDescriptorSetAllocator& allocator) = delete;
  ~SingleTypeDescriptorSetAllocator() { Reset(); }

  void Reset();

  VkDescriptorSet Allocate(VkDescriptorSetLayout descriptor_set_layout,
                           uint32_t descriptor_count);

 private:
  struct Page {
    VkDescriptorPool pool;
    uint32_t descriptors_remaining;
    uint32_t descriptor_sets_remaining;
  };

  const ui::vulkan::VulkanProvider& provider_;

  VkDescriptorPoolSize descriptor_pool_size_;
  uint32_t descriptor_sets_per_page_;

  std::vector<VkDescriptorPool> pages_full_;
  // Because allocations must be contiguous, overflow may happen even if a page
  // still has free descriptors, so multiple pages may have free space.
  // To avoid removing and re-adding the page to the map that keeps them sorted
  // (the key is the number of free descriptors remaining, and it changes at
  // every allocation from a page), instead of always looking for a free space
  // in the map, maintaining one page outside the map, and allocation attempts
  // will be made from that page first.
  std::multimap<uint32_t, Page> pages_usable_;
  // Doesn't exist if page_usable_latest_.pool == VK_NULL_HANDLE.
  Page page_usable_latest_ = {};
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_SINGLE_TYPE_DESCRIPTOR_SET_ALLOCATOR_H_
