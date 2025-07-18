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
#include "third_party/VulkanMemoryAllocator/include/vk_mem_alloc.h"

namespace xe {
namespace ui {
namespace vulkan {

VmaAllocator CreateVmaAllocator(const VulkanProvider& provider,
                                bool externally_synchronized);

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_MEM_ALLOC_H_
