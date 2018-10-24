/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_
#define XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_

#include "third_party/volk/volk.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include "third_party/vulkan/vk_mem_alloc.h"

namespace xe {
namespace ui {
namespace vulkan {

inline void FillVMAVulkanFunctions(VmaVulkanFunctions* vma_funcs) {
  vma_funcs->vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
  vma_funcs->vkGetPhysicalDeviceMemoryProperties =
      vkGetPhysicalDeviceMemoryProperties;
  vma_funcs->vkAllocateMemory = vkAllocateMemory;
  vma_funcs->vkFreeMemory = vkFreeMemory;
  vma_funcs->vkMapMemory = vkMapMemory;
  vma_funcs->vkUnmapMemory = vkUnmapMemory;
  vma_funcs->vkBindBufferMemory = vkBindBufferMemory;
  vma_funcs->vkBindImageMemory = vkBindImageMemory;
  vma_funcs->vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
  vma_funcs->vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
  vma_funcs->vkCreateBuffer = vkCreateBuffer;
  vma_funcs->vkDestroyBuffer = vkDestroyBuffer;
  vma_funcs->vkCreateImage = vkCreateImage;
  vma_funcs->vkDestroyImage = vkDestroyImage;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_