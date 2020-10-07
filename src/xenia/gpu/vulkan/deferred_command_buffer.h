/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_DEFERRED_COMMAND_BUFFER_H_
#define XENIA_GPU_VULKAN_DEFERRED_COMMAND_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class DeferredCommandBuffer {
 public:
  DeferredCommandBuffer(const VulkanCommandProcessor& command_processor,
                        size_t initial_size_bytes = 1024 * 1024);

  void Reset();
  void Execute(VkCommandBuffer command_buffer);

  void CmdVkBindIndexBuffer(VkBuffer buffer, VkDeviceSize offset,
                            VkIndexType index_type) {
    auto& args = *reinterpret_cast<ArgsVkBindIndexBuffer*>(WriteCommand(
        Command::kVkBindIndexBuffer, sizeof(ArgsVkBindIndexBuffer)));
    args.buffer = buffer;
    args.offset = offset;
    args.index_type = index_type;
  }

  VkBufferCopy* CmdCopyBufferEmplace(VkBuffer src_buffer, VkBuffer dst_buffer,
                                     uint32_t region_count) {
    static_assert(alignof(VkBufferCopy) <= alignof(uintmax_t));
    const size_t header_size =
        xe::align(sizeof(ArgsVkCopyBuffer), alignof(VkBufferCopy));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkCopyBuffer,
                     header_size + sizeof(VkBufferCopy) * region_count));
    auto& args = *reinterpret_cast<ArgsVkCopyBuffer*>(args_ptr);
    args.src_buffer = src_buffer;
    args.dst_buffer = dst_buffer;
    args.region_count = region_count;
    return reinterpret_cast<VkBufferCopy*>(args_ptr + header_size);
  }
  void CmdVkCopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer,
                       uint32_t region_count, const VkBufferCopy* regions) {
    std::memcpy(CmdCopyBufferEmplace(src_buffer, dst_buffer, region_count),
                regions, sizeof(VkBufferCopy) * region_count);
  }

  // pNext of all barriers must be null.
  void CmdVkPipelineBarrier(VkPipelineStageFlags src_stage_mask,
                            VkPipelineStageFlags dst_stage_mask,
                            VkDependencyFlags dependency_flags,
                            uint32_t memory_barrier_count,
                            const VkMemoryBarrier* memory_barriers,
                            uint32_t buffer_memory_barrier_count,
                            const VkBufferMemoryBarrier* buffer_memory_barriers,
                            uint32_t image_memory_barrier_count,
                            const VkImageMemoryBarrier* image_memory_barriers);

 private:
  enum class Command {
    kVkBindIndexBuffer,
    kVkCopyBuffer,
    kVkPipelineBarrier,
  };

  struct CommandHeader {
    Command command;
    uint32_t arguments_size_elements;
  };
  static constexpr size_t kCommandHeaderSizeElements =
      (sizeof(CommandHeader) + sizeof(uintmax_t) - 1) / sizeof(uintmax_t);

  struct ArgsVkBindIndexBuffer {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkIndexType index_type;
  };

  struct ArgsVkCopyBuffer {
    VkBuffer src_buffer;
    VkBuffer dst_buffer;
    uint32_t region_count;
    // Followed by VkBufferCopy[].
  };

  struct ArgsVkPipelineBarrier {
    VkPipelineStageFlags src_stage_mask;
    VkPipelineStageFlags dst_stage_mask;
    VkDependencyFlags dependency_flags;
    uint32_t memory_barrier_count;
    uint32_t buffer_memory_barrier_count;
    uint32_t image_memory_barrier_count;
    // Followed by aligned VkMemoryBarrier[], VkBufferMemoryBarrier[],
    // VkImageMemoryBarrier[].
  };

  void* WriteCommand(Command command, size_t arguments_size_bytes);

  const VulkanCommandProcessor& command_processor_;

  // uintmax_t to ensure uint64_t and pointer alignment of all structures.
  std::vector<uintmax_t> command_stream_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_DEFERRED_COMMAND_BUFFER_H_