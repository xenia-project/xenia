/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_util.h"

#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {
namespace util {

void FlushMappedMemoryRange(const VulkanProvider& provider,
                            VkDeviceMemory memory, uint32_t memory_type,
                            VkDeviceSize offset, VkDeviceSize size) {
  if (!size ||
      (provider.memory_types_host_coherent() & (uint32_t(1) << memory_type))) {
    return;
  }
  VkMappedMemoryRange range;
  range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range.pNext = nullptr;
  range.memory = memory;
  range.offset = offset;
  range.size = size;
  VkDeviceSize non_coherent_atom_size =
      provider.device_properties().limits.nonCoherentAtomSize;
  // On some Android implementations, nonCoherentAtomSize is 0, not 1.
  if (non_coherent_atom_size > 1) {
    range.offset = offset / non_coherent_atom_size * non_coherent_atom_size;
    if (size != VK_WHOLE_SIZE) {
      range.size =
          xe::round_up(offset + size, non_coherent_atom_size) - range.offset;
    }
  }
  provider.dfn().vkFlushMappedMemoryRanges(provider.device(), 1, &range);
}

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe
