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

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include "third_party/vulkan/vk_mem_alloc.h"

#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

namespace xe {
namespace ui {
namespace vulkan {

inline void FillVMAVulkanFunctions(VmaVulkanFunctions* vma_funcs,
                                   const VulkanDevice& device) {
  const VulkanInstance::InstanceFunctions& ifn = device.instance()->ifn();
  const VulkanDevice::DeviceFunctions& dfn = device.dfn();
  vma_funcs->vkGetPhysicalDeviceProperties = ifn.vkGetPhysicalDeviceProperties;
  vma_funcs->vkGetPhysicalDeviceMemoryProperties =
      ifn.vkGetPhysicalDeviceMemoryProperties;
  vma_funcs->vkAllocateMemory = dfn.vkAllocateMemory;
  vma_funcs->vkFreeMemory = dfn.vkFreeMemory;
  vma_funcs->vkMapMemory = dfn.vkMapMemory;
  vma_funcs->vkUnmapMemory = dfn.vkUnmapMemory;
  vma_funcs->vkBindBufferMemory = dfn.vkBindBufferMemory;
  vma_funcs->vkBindImageMemory = dfn.vkBindImageMemory;
  vma_funcs->vkGetBufferMemoryRequirements = dfn.vkGetBufferMemoryRequirements;
  vma_funcs->vkGetImageMemoryRequirements = dfn.vkGetImageMemoryRequirements;
  vma_funcs->vkCreateBuffer = dfn.vkCreateBuffer;
  vma_funcs->vkDestroyBuffer = dfn.vkDestroyBuffer;
  vma_funcs->vkCreateImage = dfn.vkCreateImage;
  vma_funcs->vkDestroyImage = dfn.vkDestroyImage;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_