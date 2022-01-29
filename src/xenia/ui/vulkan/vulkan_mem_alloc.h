/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_
#define XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_

// Make sure vulkan.h is included from third_party (rather than from the system
// include directory) before vk_mem_alloc.h.

#include "xenia/ui/vulkan/vulkan_provider.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
// Work around the pointer nullability completeness warnings on Clang.
#ifndef VMA_NULLABLE
#define VMA_NULLABLE
#endif
#ifndef VMA_NOT_NULL
#define VMA_NOT_NULL
#endif
#include "third_party/vulkan/vk_mem_alloc.h"

namespace xe {
namespace ui {
namespace vulkan {

inline void FillVMAVulkanFunctions(VmaVulkanFunctions* vma_funcs,
                                   const VulkanProvider& provider) {
  const VulkanProvider::LibraryFunctions& lfn = provider.lfn();
  const VulkanProvider::InstanceFunctions& ifn = provider.ifn();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  vma_funcs->vkGetInstanceProcAddr = lfn.vkGetInstanceProcAddr;
  vma_funcs->vkGetDeviceProcAddr = ifn.vkGetDeviceProcAddr;
  vma_funcs->vkGetPhysicalDeviceProperties = ifn.vkGetPhysicalDeviceProperties;
  vma_funcs->vkGetPhysicalDeviceMemoryProperties =
      ifn.vkGetPhysicalDeviceMemoryProperties;
  vma_funcs->vkAllocateMemory = dfn.vkAllocateMemory;
  vma_funcs->vkFreeMemory = dfn.vkFreeMemory;
  vma_funcs->vkMapMemory = dfn.vkMapMemory;
  vma_funcs->vkUnmapMemory = dfn.vkUnmapMemory;
  vma_funcs->vkFlushMappedMemoryRanges = dfn.vkFlushMappedMemoryRanges;
  vma_funcs->vkInvalidateMappedMemoryRanges =
      dfn.vkInvalidateMappedMemoryRanges;
  vma_funcs->vkBindBufferMemory = dfn.vkBindBufferMemory;
  vma_funcs->vkBindImageMemory = dfn.vkBindImageMemory;
  vma_funcs->vkGetBufferMemoryRequirements = dfn.vkGetBufferMemoryRequirements;
  vma_funcs->vkGetImageMemoryRequirements = dfn.vkGetImageMemoryRequirements;
  vma_funcs->vkCreateBuffer = dfn.vkCreateBuffer;
  vma_funcs->vkDestroyBuffer = dfn.vkDestroyBuffer;
  vma_funcs->vkCreateImage = dfn.vkCreateImage;
  vma_funcs->vkDestroyImage = dfn.vkDestroyImage;
  vma_funcs->vkCmdCopyBuffer = dfn.vkCmdCopyBuffer;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_