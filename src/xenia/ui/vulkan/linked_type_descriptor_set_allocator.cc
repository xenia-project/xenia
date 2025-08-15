/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/linked_type_descriptor_set_allocator.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

void LinkedTypeDescriptorSetAllocator::Reset() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                                         page_usable_latest_.pool);
  page_usable_latest_.descriptors_remaining.reset();
  for (const std::pair<const uint32_t, Page>& page_pair : pages_usable_) {
    dfn.vkDestroyDescriptorPool(device, page_pair.second.pool, nullptr);
  }
  pages_usable_.clear();
  for (VkDescriptorPool pool : pages_full_) {
    dfn.vkDestroyDescriptorPool(device, pool, nullptr);
  }
  pages_full_.clear();
}

VkDescriptorSet LinkedTypeDescriptorSetAllocator::Allocate(
    VkDescriptorSetLayout descriptor_set_layout,
    const VkDescriptorPoolSize* descriptor_counts,
    uint32_t descriptor_type_count) {
  assert_not_zero(descriptor_type_count);
#ifndef NDEBUG
  for (uint32_t i = 0; i < descriptor_type_count; ++i) {
    const VkDescriptorPoolSize& descriptor_count_for_type =
        descriptor_counts[i];
    assert_not_zero(descriptor_count_for_type.descriptorCount);
    for (uint32_t j = 0; j < i; ++j) {
      assert_true(descriptor_counts[j].type != descriptor_count_for_type.type);
    }
  }
#endif

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = nullptr;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts = &descriptor_set_layout;
  VkDescriptorSet descriptor_set;

  // Check if more descriptors have been requested than a page can hold, or
  // descriptors of types not provided by this allocator, and if that's the
  // case, create a dedicated pool for this allocation.
  bool dedicated_descriptor_pool_needed = false;
  for (uint32_t i = 0; i < descriptor_type_count; ++i) {
    const VkDescriptorPoolSize& descriptor_count_for_type =
        descriptor_counts[i];
    // If the type is one that's not supported by the allocator, a dedicated
    // pool is required. If it's supported, and the allocator has large enough
    // pools to hold the requested number of descriptors,
    // dedicated_descriptor_pool_needed will be set to false for this iteration,
    // and the loop will continue. Otherwise, if that doesn't happen, a
    // dedicated pool is required.
    dedicated_descriptor_pool_needed = true;
    for (uint32_t j = 0; j < descriptor_pool_size_count_; ++j) {
      const VkDescriptorPoolSize& descriptor_pool_size =
          descriptor_pool_sizes_[j];
      if (descriptor_count_for_type.type != descriptor_pool_size.type) {
        continue;
      }
      if (descriptor_count_for_type.descriptorCount <=
          descriptor_pool_size.descriptorCount) {
        // For this type, pages can hold enough descriptors.
        dedicated_descriptor_pool_needed = false;
      }
      break;
    }
    if (dedicated_descriptor_pool_needed) {
      // For at least one requested type, pages can't hold enough descriptors.
      break;
    }
  }
  if (dedicated_descriptor_pool_needed) {
    VkDescriptorPoolCreateInfo dedicated_descriptor_pool_create_info;
    dedicated_descriptor_pool_create_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dedicated_descriptor_pool_create_info.pNext = nullptr;
    dedicated_descriptor_pool_create_info.flags = 0;
    dedicated_descriptor_pool_create_info.maxSets = 1;
    dedicated_descriptor_pool_create_info.poolSizeCount = descriptor_type_count;
    dedicated_descriptor_pool_create_info.pPoolSizes = descriptor_counts;
    VkDescriptorPool dedicated_descriptor_pool;
    if (dfn.vkCreateDescriptorPool(
            device, &dedicated_descriptor_pool_create_info, nullptr,
            &dedicated_descriptor_pool) != VK_SUCCESS) {
      XELOGE(
          "LinkedTypeDescriptorSetAllocator: Failed to create a dedicated "
          "descriptor pool for a descriptor set that is too large for a pool "
          "page");
      return VK_NULL_HANDLE;
    }
    descriptor_set_allocate_info.descriptorPool = dedicated_descriptor_pool;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      XELOGE(
          "LinkedTypeDescriptorSetAllocator: Failed to allocate descriptors in "
          "a dedicated pool");
      dfn.vkDestroyDescriptorPool(device, dedicated_descriptor_pool, nullptr);
      return VK_NULL_HANDLE;
    }
    pages_full_.push_back(dedicated_descriptor_pool);
    return descriptor_set;
  }

  // Try allocating from the latest page an allocation has happened from, to
  // avoid detaching from the map and re-attaching for every allocation.
  if (page_usable_latest_.pool != VK_NULL_HANDLE) {
    assert_not_zero(page_usable_latest_.descriptor_sets_remaining);
    bool allocate_from_latest_page = true;
    bool latest_page_becomes_full =
        page_usable_latest_.descriptor_sets_remaining == 1;
    for (uint32_t i = 0; i < descriptor_type_count; ++i) {
      const VkDescriptorPoolSize& descriptor_count_for_type =
          descriptor_counts[i];
      for (uint32_t j = 0; j < descriptor_pool_size_count_; ++j) {
        const VkDescriptorPoolSize& descriptors_remaining_for_type =
            page_usable_latest_.descriptors_remaining[j];
        if (descriptor_count_for_type.type !=
            descriptors_remaining_for_type.type) {
          continue;
        }
        if (descriptor_count_for_type.descriptorCount >=
            descriptors_remaining_for_type.descriptorCount) {
          if (descriptor_count_for_type.descriptorCount >
              descriptors_remaining_for_type.descriptorCount) {
            allocate_from_latest_page = false;
            break;
          }
          latest_page_becomes_full = true;
        }
      }
      if (!allocate_from_latest_page) {
        break;
      }
    }
    if (allocate_from_latest_page) {
      descriptor_set_allocate_info.descriptorPool = page_usable_latest_.pool;
      if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                       &descriptor_set) != VK_SUCCESS) {
        descriptor_set = VK_NULL_HANDLE;
        // Failed to allocate internally even though there should be enough
        // space, don't try to allocate from this pool again at all.
        latest_page_becomes_full = true;
      }
      if (latest_page_becomes_full) {
        pages_full_.push_back(page_usable_latest_.pool);
        page_usable_latest_.pool = VK_NULL_HANDLE;
        page_usable_latest_.descriptors_remaining.reset();
      } else {
        --page_usable_latest_.descriptor_sets_remaining;
        for (uint32_t i = 0; i < descriptor_type_count; ++i) {
          const VkDescriptorPoolSize& descriptor_count_for_type =
              descriptor_counts[i];
          for (uint32_t j = 0; j < descriptor_pool_size_count_; ++j) {
            VkDescriptorPoolSize& descriptors_remaining_for_type =
                page_usable_latest_.descriptors_remaining[j];
            if (descriptor_count_for_type.type !=
                descriptors_remaining_for_type.type) {
              continue;
            }
            descriptors_remaining_for_type.descriptorCount -=
                descriptor_count_for_type.descriptorCount;
          }
        }
      }
      if (descriptor_set != VK_NULL_HANDLE) {
        return descriptor_set;
      }
    }
  }

  // Count the maximum number of descriptors requested for any type to stop
  // searching for pages once they can't satisfy this requirement.
  uint32_t max_descriptors_per_type = descriptor_counts[0].descriptorCount;
  for (uint32_t i = 1; i < descriptor_type_count; ++i) {
    max_descriptors_per_type = std::max(max_descriptors_per_type,
                                        descriptor_counts[i].descriptorCount);
  }

  // If allocating from the latest pool wasn't possible, pick any that has
  // enough free space. Prefer filling pages that have the most free space as
  // they can more likely be used for more allocations later.
  auto page_usable_it_next = pages_usable_.rbegin();
  while (page_usable_it_next != pages_usable_.rend()) {
    auto page_usable_it = page_usable_it_next;
    ++page_usable_it_next;
    if (page_usable_it->first < max_descriptors_per_type) {
      // All other pages_usable_ entries have smaller maximum number of free
      // descriptor for any type (it's the map key).
      break;
    }
    // Check if the page has enough free descriptors for all requested types,
    // and whether allocating the requested number of descriptors in it will
    // result in the page becoming full.
    bool map_page_has_sufficient_space = true;
    bool map_page_becomes_full =
        page_usable_it->second.descriptor_sets_remaining == 1;
    for (uint32_t i = 0; i < descriptor_type_count; ++i) {
      const VkDescriptorPoolSize& descriptor_count_for_type =
          descriptor_counts[i];
      for (uint32_t j = 0; j < descriptor_pool_size_count_; ++j) {
        const VkDescriptorPoolSize& descriptors_remaining_for_type =
            page_usable_it->second.descriptors_remaining[j];
        if (descriptor_count_for_type.type !=
            descriptors_remaining_for_type.type) {
          continue;
        }
        if (descriptor_count_for_type.descriptorCount >=
            descriptors_remaining_for_type.descriptorCount) {
          if (descriptor_count_for_type.descriptorCount >
              descriptors_remaining_for_type.descriptorCount) {
            map_page_has_sufficient_space = false;
            break;
          }
          map_page_becomes_full = true;
        }
      }
      if (!map_page_has_sufficient_space) {
        break;
      }
    }
    if (!map_page_has_sufficient_space) {
      // Even though the coarse (maximum number of descriptors for any type)
      // check has passed, for the exact types requested this page doesn't have
      // sufficient space - try another one.
      continue;
    }
    // Remove the page from the map unconditionally - in case of a successful
    // allocation, it will have a different number of free descriptors for
    // different types, thus potentially a new map key (but it will also become
    // page_usable_latest_ instead even), or will become full, and in case of a
    // failure to allocate internally even though there still should be enough
    // space, it should never be allocated from again.
    Page map_page = std::move(page_usable_it->second);
    // Convert the reverse iterator to a forward iterator for erasing.
    pages_usable_.erase(std::next(page_usable_it).base());
    descriptor_set_allocate_info.descriptorPool = map_page.pool;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      descriptor_set = VK_NULL_HANDLE;
      // Failed to allocate internally even though there should be enough space,
      // don't try to allocate from this pool again at all.
      map_page_becomes_full = true;
    }
    if (map_page_becomes_full) {
      map_page.descriptors_remaining.reset();
      pages_full_.push_back(map_page.pool);
    } else {
      --map_page.descriptor_sets_remaining;
      for (uint32_t i = 0; i < descriptor_type_count; ++i) {
        const VkDescriptorPoolSize& descriptor_count_for_type =
            descriptor_counts[i];
        for (uint32_t j = 0; j < descriptor_pool_size_count_; ++j) {
          VkDescriptorPoolSize& descriptors_remaining_for_type =
              map_page.descriptors_remaining[j];
          if (descriptor_count_for_type.type !=
              descriptors_remaining_for_type.type) {
            continue;
          }
          descriptors_remaining_for_type.descriptorCount -=
              descriptor_count_for_type.descriptorCount;
        }
      }
      // Move the latest page that allocation couldn't be done in to the usable
      // pages to replace it with the new one.
      if (page_usable_latest_.pool != VK_NULL_HANDLE) {
        // Calculate the map key (the maximum number of remaining descriptors of
        // any type).
        uint32_t latest_page_max_descriptors_remaining =
            page_usable_latest_.descriptors_remaining[0].descriptorCount;
        for (uint32_t i = 1; i < descriptor_pool_size_count_; ++i) {
          latest_page_max_descriptors_remaining = std::max(
              latest_page_max_descriptors_remaining,
              page_usable_latest_.descriptors_remaining[i].descriptorCount);
        }
        assert_not_zero(latest_page_max_descriptors_remaining);
        pages_usable_.emplace(latest_page_max_descriptors_remaining,
                              std::move(page_usable_latest_));
      }
      page_usable_latest_ = std::move(map_page);
    }
    if (descriptor_set != VK_NULL_HANDLE) {
      return descriptor_set;
    }
  }

  // Try allocating from a new page.
  // See if the new page has instantly become full.
  bool new_page_becomes_full = descriptor_sets_per_page_ == 1;
  for (uint32_t i = 0; !new_page_becomes_full && i < descriptor_type_count;
       ++i) {
    const VkDescriptorPoolSize& descriptor_count_for_type =
        descriptor_counts[i];
    for (uint32_t j = 0; j < descriptor_pool_size_count_; ++j) {
      const VkDescriptorPoolSize& descriptors_remaining_for_type =
          descriptor_pool_sizes_[j];
      if (descriptor_count_for_type.type !=
          descriptors_remaining_for_type.type) {
        continue;
      }
      assert_true(descriptor_count_for_type.descriptorCount <=
                  descriptors_remaining_for_type.descriptorCount);
      if (descriptor_count_for_type.descriptorCount >=
          descriptors_remaining_for_type.descriptorCount) {
        new_page_becomes_full = true;
        break;
      }
    }
  }
  // Allocate from a new page. However, if the new page becomes full
  // immediately, create a dedicated pool instead for the exact number of
  // descriptors not to leave any unused space in the pool.
  VkDescriptorPoolCreateInfo new_descriptor_pool_create_info;
  new_descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  new_descriptor_pool_create_info.pNext = nullptr;
  new_descriptor_pool_create_info.flags = 0;
  if (new_page_becomes_full) {
    new_descriptor_pool_create_info.maxSets = 1;
    new_descriptor_pool_create_info.poolSizeCount = descriptor_type_count;
    new_descriptor_pool_create_info.pPoolSizes = descriptor_counts;
  } else {
    new_descriptor_pool_create_info.maxSets = descriptor_sets_per_page_;
    new_descriptor_pool_create_info.poolSizeCount = descriptor_pool_size_count_;
    new_descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes_.get();
  }
  VkDescriptorPool new_descriptor_pool;
  if (dfn.vkCreateDescriptorPool(device, &new_descriptor_pool_create_info,
                                 nullptr, &new_descriptor_pool) != VK_SUCCESS) {
    XELOGE(
        "LinkedTypeDescriptorSetAllocator: Failed to create a descriptor pool");
    return VK_NULL_HANDLE;
  }
  descriptor_set_allocate_info.descriptorPool = new_descriptor_pool;
  if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                   &descriptor_set) != VK_SUCCESS) {
    XELOGE("LinkedTypeDescriptorSetAllocator: Failed to allocate descriptors");
    dfn.vkDestroyDescriptorPool(device, new_descriptor_pool, nullptr);
    return VK_NULL_HANDLE;
  }
  if (new_page_becomes_full) {
    pages_full_.push_back(new_descriptor_pool);
  } else {
    // Move the latest page that allocation couldn't be done in to the usable
    // pages to replace it with the new one.
    if (page_usable_latest_.pool != VK_NULL_HANDLE) {
      // Calculate the map key (the maximum number of remaining descriptors of
      // any type).
      uint32_t latest_page_max_descriptors_remaining =
          page_usable_latest_.descriptors_remaining[0].descriptorCount;
      for (uint32_t i = 1; i < descriptor_pool_size_count_; ++i) {
        latest_page_max_descriptors_remaining = std::max(
            latest_page_max_descriptors_remaining,
            page_usable_latest_.descriptors_remaining[i].descriptorCount);
      }
      assert_not_zero(latest_page_max_descriptors_remaining);
      pages_usable_.emplace(latest_page_max_descriptors_remaining,
                            std::move(page_usable_latest_));
    }
    page_usable_latest_.pool = new_descriptor_pool;
    page_usable_latest_.descriptors_remaining =
        std::unique_ptr<VkDescriptorPoolSize[]>(
            new VkDescriptorPoolSize[descriptor_pool_size_count_]);
    for (uint32_t i = 0; i < descriptor_pool_size_count_; ++i) {
      const VkDescriptorPoolSize& descriptor_pool_size_for_type =
          descriptor_pool_sizes_[i];
      page_usable_latest_.descriptors_remaining[i] =
          descriptor_pool_size_for_type;
      for (uint32_t j = 0; j < descriptor_type_count; ++j) {
        const VkDescriptorPoolSize& descriptor_count_for_type =
            descriptor_counts[j];
        if (descriptor_count_for_type.type !=
            descriptor_pool_size_for_type.type) {
          continue;
        }
        page_usable_latest_.descriptors_remaining[i].descriptorCount -=
            descriptor_count_for_type.descriptorCount;
        break;
      }
    }
    page_usable_latest_.descriptor_sets_remaining =
        descriptor_sets_per_page_ - 1;
  }
  return descriptor_set;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
