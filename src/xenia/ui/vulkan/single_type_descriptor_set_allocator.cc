/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/single_type_descriptor_set_allocator.h"

#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

void SingleTypeDescriptorSetAllocator::Reset() {
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                                         page_usable_latest_.pool);
  for (const std::pair<uint32_t, Page>& page_pair : pages_usable_) {
    dfn.vkDestroyDescriptorPool(device, page_pair.second.pool, nullptr);
  }
  pages_usable_.clear();
  for (VkDescriptorPool pool : pages_full_) {
    dfn.vkDestroyDescriptorPool(device, pool, nullptr);
  }
  pages_full_.clear();
}

VkDescriptorSet SingleTypeDescriptorSetAllocator::Allocate(
    VkDescriptorSetLayout descriptor_set_layout, uint32_t descriptor_count) {
  assert_not_zero(descriptor_count);
  if (descriptor_count == 0) {
    return VK_NULL_HANDLE;
  }

  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = nullptr;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts = &descriptor_set_layout;
  VkDescriptorSet descriptor_set;

  if (descriptor_count > descriptor_pool_size_.descriptorCount) {
    // Can't allocate in the pool, need a dedicated allocation.
    VkDescriptorPoolSize dedicated_descriptor_pool_size;
    dedicated_descriptor_pool_size.type = descriptor_pool_size_.type;
    dedicated_descriptor_pool_size.descriptorCount = descriptor_count;
    VkDescriptorPoolCreateInfo dedicated_descriptor_pool_create_info;
    dedicated_descriptor_pool_create_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dedicated_descriptor_pool_create_info.pNext = nullptr;
    dedicated_descriptor_pool_create_info.flags = 0;
    dedicated_descriptor_pool_create_info.maxSets = 1;
    dedicated_descriptor_pool_create_info.poolSizeCount = 1;
    dedicated_descriptor_pool_create_info.pPoolSizes =
        &dedicated_descriptor_pool_size;
    VkDescriptorPool dedicated_descriptor_pool;
    if (dfn.vkCreateDescriptorPool(
            device, &dedicated_descriptor_pool_create_info, nullptr,
            &dedicated_descriptor_pool) != VK_SUCCESS) {
      XELOGE(
          "SingleTypeDescriptorSetAllocator: Failed to create a dedicated pool "
          "for {} descriptors",
          dedicated_descriptor_pool_size.descriptorCount);
      return VK_NULL_HANDLE;
    }
    descriptor_set_allocate_info.descriptorPool = dedicated_descriptor_pool;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      XELOGE(
          "SingleTypeDescriptorSetAllocator: Failed to allocate {} descriptors "
          "in a dedicated pool",
          descriptor_count);
      dfn.vkDestroyDescriptorPool(device, dedicated_descriptor_pool, nullptr);
      return VK_NULL_HANDLE;
    }
    pages_full_.push_back(dedicated_descriptor_pool);
    return descriptor_set;
  }

  // Try allocating from the latest page an allocation has happened from, to
  // avoid detaching from the map and re-attaching for every allocation.
  if (page_usable_latest_.pool != VK_NULL_HANDLE) {
    assert_not_zero(page_usable_latest_.descriptors_remaining);
    assert_not_zero(page_usable_latest_.descriptor_sets_remaining);
    if (page_usable_latest_.descriptors_remaining >= descriptor_count) {
      descriptor_set_allocate_info.descriptorPool = page_usable_latest_.pool;
      if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                       &descriptor_set) == VK_SUCCESS) {
        page_usable_latest_.descriptors_remaining -= descriptor_count;
        --page_usable_latest_.descriptor_sets_remaining;
        if (!page_usable_latest_.descriptors_remaining ||
            !page_usable_latest_.descriptor_sets_remaining) {
          pages_full_.push_back(page_usable_latest_.pool);
          page_usable_latest_.pool = VK_NULL_HANDLE;
        }
        return descriptor_set;
      }
      // Failed to allocate internally even though there should be enough space,
      // don't try to allocate from this pool again at all.
      pages_full_.push_back(page_usable_latest_.pool);
      page_usable_latest_.pool = VK_NULL_HANDLE;
    }
  }

  // If allocating from the latest pool wasn't possible, pick any that has free
  // space. Prefer filling pages that have the most free space as they can more
  // likely be used for more allocations later.
  while (!pages_usable_.empty()) {
    auto page_usable_last_it = std::prev(pages_usable_.cend());
    if (page_usable_last_it->second.descriptors_remaining < descriptor_count) {
      // All other pages_usable_ entries have fewer free descriptors too (the
      // remaining count is the map key).
      break;
    }
    // Remove the page from the map unconditionally - in case of a successful
    // allocation, it will have a different number of free descriptors, thus a
    // new map key (but it will also become page_usable_latest_ instead even),
    // or will become full, and in case of a failure to allocate internally even
    // though there still should be enough space, it should never be allocated
    // from again.
    Page map_page = pages_usable_.crend()->second;
    pages_usable_.erase(page_usable_last_it);
    descriptor_set_allocate_info.descriptorPool = map_page.pool;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      pages_full_.push_back(map_page.pool);
      continue;
    }
    map_page.descriptors_remaining -= descriptor_count;
    --map_page.descriptor_sets_remaining;
    if (!map_page.descriptors_remaining ||
        !map_page.descriptor_sets_remaining) {
      pages_full_.push_back(map_page.pool);
    } else {
      if (page_usable_latest_.pool != VK_NULL_HANDLE) {
        // Make the page with more free descriptors the next to allocate from.
        if (map_page.descriptors_remaining >
            page_usable_latest_.descriptors_remaining) {
          pages_usable_.emplace(page_usable_latest_.descriptors_remaining,
                                page_usable_latest_);
          page_usable_latest_ = map_page;
        } else {
          pages_usable_.emplace(map_page.descriptors_remaining, map_page);
        }
      } else {
        page_usable_latest_ = map_page;
      }
    }
    return descriptor_set;
  }

  // Try allocating from a new page.
  VkDescriptorPoolCreateInfo new_descriptor_pool_create_info;
  new_descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  new_descriptor_pool_create_info.pNext = nullptr;
  new_descriptor_pool_create_info.flags = 0;
  new_descriptor_pool_create_info.maxSets = descriptor_sets_per_page_;
  new_descriptor_pool_create_info.poolSizeCount = 1;
  new_descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size_;
  VkDescriptorPool new_descriptor_pool;
  if (dfn.vkCreateDescriptorPool(device, &new_descriptor_pool_create_info,
                                 nullptr, &new_descriptor_pool) != VK_SUCCESS) {
    XELOGE(
        "SingleTypeDescriptorSetAllocator: Failed to create a pool for {} sets "
        "with {} descriptors",
        descriptor_sets_per_page_, descriptor_pool_size_.descriptorCount);
    return VK_NULL_HANDLE;
  }
  descriptor_set_allocate_info.descriptorPool = new_descriptor_pool;
  if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                   &descriptor_set) != VK_SUCCESS) {
    XELOGE(
        "SingleTypeDescriptorSetAllocator: Failed to allocate {} descriptors",
        descriptor_count);
    dfn.vkDestroyDescriptorPool(device, new_descriptor_pool, nullptr);
    return VK_NULL_HANDLE;
  }
  Page new_page;
  new_page.pool = new_descriptor_pool;
  new_page.descriptors_remaining =
      descriptor_pool_size_.descriptorCount - descriptor_count;
  new_page.descriptor_sets_remaining = descriptor_sets_per_page_ - 1;
  if (!new_page.descriptors_remaining || !new_page.descriptor_sets_remaining) {
    pages_full_.push_back(new_page.pool);
  } else {
    if (page_usable_latest_.pool != VK_NULL_HANDLE) {
      // Make the page with more free descriptors the next to allocate from.
      if (new_page.descriptors_remaining >
          page_usable_latest_.descriptors_remaining) {
        pages_usable_.emplace(page_usable_latest_.descriptors_remaining,
                              page_usable_latest_);
        page_usable_latest_ = new_page;
      } else {
        pages_usable_.emplace(new_page.descriptors_remaining, new_page);
      }
    } else {
      page_usable_latest_ = new_page;
    }
  }
  return descriptor_set;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
