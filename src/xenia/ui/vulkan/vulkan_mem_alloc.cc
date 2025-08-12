/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Implementing VMA in this translation unit.
#define VMA_IMPLEMENTATION
#include "xenia/ui/vulkan/vulkan_mem_alloc.h"

#include <cstring>

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace vulkan {

VmaAllocator CreateVmaAllocator(const VulkanDevice* const vulkan_device,
                                const bool externally_synchronized) {
  assert_not_null(vulkan_device);

  const VulkanInstance* const vulkan_instance =
      vulkan_device->vulkan_instance();
  const VulkanInstance::Functions& ifn = vulkan_instance->functions();
  const VulkanDevice::Functions& dfn = vulkan_device->functions();

  VmaVulkanFunctions vma_vulkan_functions = {};
  VmaAllocatorCreateInfo allocator_create_info = {};

  vma_vulkan_functions.vkGetInstanceProcAddr = ifn.vkGetInstanceProcAddr;
  vma_vulkan_functions.vkGetDeviceProcAddr = ifn.vkGetDeviceProcAddr;
  vma_vulkan_functions.vkGetPhysicalDeviceProperties =
      ifn.vkGetPhysicalDeviceProperties;
  vma_vulkan_functions.vkGetPhysicalDeviceMemoryProperties =
      ifn.vkGetPhysicalDeviceMemoryProperties;
  vma_vulkan_functions.vkAllocateMemory = dfn.vkAllocateMemory;
  vma_vulkan_functions.vkFreeMemory = dfn.vkFreeMemory;
  vma_vulkan_functions.vkMapMemory = dfn.vkMapMemory;
  vma_vulkan_functions.vkUnmapMemory = dfn.vkUnmapMemory;
  vma_vulkan_functions.vkFlushMappedMemoryRanges =
      dfn.vkFlushMappedMemoryRanges;
  vma_vulkan_functions.vkInvalidateMappedMemoryRanges =
      dfn.vkInvalidateMappedMemoryRanges;
  vma_vulkan_functions.vkBindBufferMemory = dfn.vkBindBufferMemory;
  vma_vulkan_functions.vkBindImageMemory = dfn.vkBindImageMemory;
  vma_vulkan_functions.vkGetBufferMemoryRequirements =
      dfn.vkGetBufferMemoryRequirements;
  vma_vulkan_functions.vkGetImageMemoryRequirements =
      dfn.vkGetImageMemoryRequirements;
  vma_vulkan_functions.vkCreateBuffer = dfn.vkCreateBuffer;
  vma_vulkan_functions.vkDestroyBuffer = dfn.vkDestroyBuffer;
  vma_vulkan_functions.vkCreateImage = dfn.vkCreateImage;
  vma_vulkan_functions.vkDestroyImage = dfn.vkDestroyImage;
  vma_vulkan_functions.vkCmdCopyBuffer = dfn.vkCmdCopyBuffer;
  if (vulkan_device->extensions().ext_1_1_KHR_get_memory_requirements2) {
    vma_vulkan_functions.vkGetBufferMemoryRequirements2KHR =
        dfn.vkGetBufferMemoryRequirements2;
    vma_vulkan_functions.vkGetImageMemoryRequirements2KHR =
        dfn.vkGetImageMemoryRequirements2;
    if (vulkan_device->extensions().ext_1_1_KHR_dedicated_allocation) {
      allocator_create_info.flags |=
          VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
    }
  }
  if (vulkan_device->extensions().ext_1_1_KHR_bind_memory2) {
    vma_vulkan_functions.vkBindBufferMemory2KHR = dfn.vkBindBufferMemory2;
    vma_vulkan_functions.vkBindImageMemory2KHR = dfn.vkBindImageMemory2;
    allocator_create_info.flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
  }
  if (vulkan_instance->extensions()
          .ext_1_1_KHR_get_physical_device_properties2) {
    vma_vulkan_functions.vkGetPhysicalDeviceMemoryProperties2KHR =
        ifn.vkGetPhysicalDeviceMemoryProperties2;
    if (vulkan_device->extensions().ext_EXT_memory_budget) {
      allocator_create_info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
  }
  if (vulkan_device->extensions().ext_1_3_KHR_maintenance4) {
    vma_vulkan_functions.vkGetDeviceBufferMemoryRequirements =
        dfn.vkGetDeviceBufferMemoryRequirements;
    vma_vulkan_functions.vkGetDeviceImageMemoryRequirements =
        dfn.vkGetDeviceImageMemoryRequirements;
  }

  if (externally_synchronized) {
    allocator_create_info.flags |=
        VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
  }
  allocator_create_info.physicalDevice = vulkan_device->physical_device();
  allocator_create_info.device = vulkan_device->device();
  allocator_create_info.pVulkanFunctions = &vma_vulkan_functions;
  allocator_create_info.instance = vulkan_instance->instance();
  allocator_create_info.vulkanApiVersion =
      vulkan_device->properties().apiVersion;
  VmaAllocator allocator;
  if (vmaCreateAllocator(&allocator_create_info, &allocator) != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan Memory Allocator instance");
    return VK_NULL_HANDLE;
  }
  return allocator;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
