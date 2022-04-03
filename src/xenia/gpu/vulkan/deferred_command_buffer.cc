/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/deferred_command_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"

namespace xe {
namespace gpu {
namespace vulkan {

DeferredCommandBuffer::DeferredCommandBuffer(
    const VulkanCommandProcessor& command_processor, size_t initial_size)
    : command_processor_(command_processor) {
  command_stream_.reserve(initial_size / sizeof(uintmax_t));
}

void DeferredCommandBuffer::Reset() { command_stream_.clear(); }

void DeferredCommandBuffer::Execute(VkCommandBuffer command_buffer) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn =
      command_processor_.GetVulkanProvider().dfn();
  const uintmax_t* stream = command_stream_.data();
  size_t stream_remaining = command_stream_.size();
  while (stream_remaining) {
    const CommandHeader& header =
        *reinterpret_cast<const CommandHeader*>(stream);
    stream += kCommandHeaderSizeElements;
    stream_remaining -= kCommandHeaderSizeElements;

    switch (header.command) {
      case Command::kVkBeginRenderPass: {
        auto& args = *reinterpret_cast<const ArgsVkBeginRenderPass*>(stream);
        size_t offset_bytes = sizeof(ArgsVkBeginRenderPass);
        VkRenderPassBeginInfo render_pass_begin_info;
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.pNext = nullptr;
        render_pass_begin_info.renderPass = args.render_pass;
        render_pass_begin_info.framebuffer = args.framebuffer;
        render_pass_begin_info.renderArea = args.render_area;
        render_pass_begin_info.clearValueCount = args.clear_value_count;
        if (render_pass_begin_info.clearValueCount) {
          offset_bytes = xe::align(offset_bytes, alignof(VkClearValue));
          render_pass_begin_info.pClearValues =
              reinterpret_cast<const VkClearValue*>(
                  reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
          offset_bytes +=
              sizeof(VkClearValue) * render_pass_begin_info.clearValueCount;
        } else {
          render_pass_begin_info.pClearValues = nullptr;
        }
        dfn.vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info,
                                 args.contents);
      } break;

      case Command::kVkBindDescriptorSets: {
        auto& args = *reinterpret_cast<const ArgsVkBindDescriptorSets*>(stream);
        size_t offset_bytes = xe::align(sizeof(ArgsVkBindDescriptorSets),
                                        alignof(VkDescriptorSet));
        const VkDescriptorSet* descriptor_sets =
            reinterpret_cast<const VkDescriptorSet*>(
                reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
        offset_bytes += sizeof(VkDescriptorSet) * args.descriptor_set_count;
        const uint32_t* dynamic_offsets = nullptr;
        if (args.dynamic_offset_count) {
          offset_bytes = xe::align(offset_bytes, alignof(uint32_t));
          dynamic_offsets = reinterpret_cast<const uint32_t*>(
              reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
          offset_bytes += sizeof(uint32_t) * args.dynamic_offset_count;
        }
        dfn.vkCmdBindDescriptorSets(command_buffer, args.pipeline_bind_point,
                                    args.layout, args.first_set,
                                    args.descriptor_set_count, descriptor_sets,
                                    args.dynamic_offset_count, dynamic_offsets);
      } break;

      case Command::kVkBindIndexBuffer: {
        auto& args = *reinterpret_cast<const ArgsVkBindIndexBuffer*>(stream);
        dfn.vkCmdBindIndexBuffer(command_buffer, args.buffer, args.offset,
                                 args.index_type);
      } break;

      case Command::kVkBindPipeline: {
        auto& args = *reinterpret_cast<const ArgsVkBindPipeline*>(stream);
        dfn.vkCmdBindPipeline(command_buffer, args.pipeline_bind_point,
                              args.pipeline);
      } break;

      case Command::kVkBindVertexBuffers: {
        auto& args = *reinterpret_cast<const ArgsVkBindVertexBuffers*>(stream);
        size_t offset_bytes =
            xe::align(sizeof(ArgsVkBindVertexBuffers), alignof(VkBuffer));
        const VkBuffer* buffers = reinterpret_cast<const VkBuffer*>(
            reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
        offset_bytes =
            xe::align(offset_bytes + sizeof(VkBuffer) * args.binding_count,
                      alignof(VkDeviceSize));
        const VkDeviceSize* offsets = reinterpret_cast<const VkDeviceSize*>(
            reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
        dfn.vkCmdBindVertexBuffers(command_buffer, args.first_binding,
                                   args.binding_count, buffers, offsets);
      } break;

      case Command::kVkClearAttachments: {
        auto& args = *reinterpret_cast<const ArgsVkClearAttachments*>(stream);
        size_t offset_bytes = xe::align(sizeof(ArgsVkClearAttachments),
                                        alignof(VkClearAttachment));
        const VkClearAttachment* attachments =
            reinterpret_cast<const VkClearAttachment*>(
                reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
        offset_bytes = xe::align(
            offset_bytes + sizeof(VkClearAttachment) * args.attachment_count,
            alignof(VkClearRect));
        const VkClearRect* rects = reinterpret_cast<const VkClearRect*>(
            reinterpret_cast<const uint8_t*>(stream) + offset_bytes);
        dfn.vkCmdClearAttachments(command_buffer, args.attachment_count,
                                  attachments, args.rect_count, rects);
      } break;

      case Command::kVkCopyBuffer: {
        auto& args = *reinterpret_cast<const ArgsVkCopyBuffer*>(stream);
        dfn.vkCmdCopyBuffer(
            command_buffer, args.src_buffer, args.dst_buffer, args.region_count,
            reinterpret_cast<const VkBufferCopy*>(
                reinterpret_cast<const uint8_t*>(stream) +
                xe::align(sizeof(ArgsVkCopyBuffer), alignof(VkBufferCopy))));
      } break;

      case Command::kVkDispatch: {
        auto& args = *reinterpret_cast<const ArgsVkDispatch*>(stream);
        dfn.vkCmdDispatch(command_buffer, args.group_count_x,
                          args.group_count_y, args.group_count_z);
      } break;

      case Command::kVkDraw: {
        auto& args = *reinterpret_cast<const ArgsVkDraw*>(stream);
        dfn.vkCmdDraw(command_buffer, args.vertex_count, args.instance_count,
                      args.first_vertex, args.first_instance);
      } break;

      case Command::kVkDrawIndexed: {
        auto& args = *reinterpret_cast<const ArgsVkDrawIndexed*>(stream);
        dfn.vkCmdDrawIndexed(command_buffer, args.index_count,
                             args.instance_count, args.first_index,
                             args.vertex_offset, args.first_instance);
      } break;

      case Command::kVkEndRenderPass:
        dfn.vkCmdEndRenderPass(command_buffer);
        break;

      case Command::kVkPipelineBarrier: {
        auto& args = *reinterpret_cast<const ArgsVkPipelineBarrier*>(stream);
        size_t barrier_offset_bytes = sizeof(ArgsVkPipelineBarrier);
        const VkMemoryBarrier* memory_barriers = nullptr;
        if (args.memory_barrier_count) {
          barrier_offset_bytes =
              xe::align(barrier_offset_bytes, alignof(VkMemoryBarrier));
          memory_barriers = reinterpret_cast<const VkMemoryBarrier*>(
              reinterpret_cast<const uint8_t*>(stream) + barrier_offset_bytes);
          barrier_offset_bytes +=
              sizeof(VkMemoryBarrier) * args.memory_barrier_count;
        }
        const VkBufferMemoryBarrier* buffer_memory_barriers = nullptr;
        if (args.buffer_memory_barrier_count) {
          barrier_offset_bytes =
              xe::align(barrier_offset_bytes, alignof(VkBufferMemoryBarrier));
          buffer_memory_barriers =
              reinterpret_cast<const VkBufferMemoryBarrier*>(
                  reinterpret_cast<const uint8_t*>(stream) +
                  barrier_offset_bytes);
          barrier_offset_bytes +=
              sizeof(VkBufferMemoryBarrier) * args.buffer_memory_barrier_count;
        }
        const VkImageMemoryBarrier* image_memory_barriers = nullptr;
        if (args.image_memory_barrier_count) {
          barrier_offset_bytes =
              xe::align(barrier_offset_bytes, alignof(VkImageMemoryBarrier));
          image_memory_barriers = reinterpret_cast<const VkImageMemoryBarrier*>(
              reinterpret_cast<const uint8_t*>(stream) + barrier_offset_bytes);
          barrier_offset_bytes +=
              sizeof(VkImageMemoryBarrier) * args.image_memory_barrier_count;
        }
        dfn.vkCmdPipelineBarrier(
            command_buffer, args.src_stage_mask, args.dst_stage_mask,
            args.dependency_flags, args.memory_barrier_count, memory_barriers,
            args.buffer_memory_barrier_count, buffer_memory_barriers,
            args.image_memory_barrier_count, image_memory_barriers);
      } break;

      case Command::kVkPushConstants: {
        auto& args = *reinterpret_cast<const ArgsVkPushConstants*>(stream);
        dfn.vkCmdPushConstants(command_buffer, args.layout, args.stage_flags,
                               args.offset, args.size,
                               reinterpret_cast<const uint8_t*>(stream) +
                                   sizeof(ArgsVkPushConstants));
      } break;

      case Command::kVkSetBlendConstants: {
        auto& args = *reinterpret_cast<const ArgsVkSetBlendConstants*>(stream);
        dfn.vkCmdSetBlendConstants(command_buffer, args.blend_constants);
      } break;

      case Command::kVkSetDepthBias: {
        auto& args = *reinterpret_cast<const ArgsVkSetDepthBias*>(stream);
        dfn.vkCmdSetDepthBias(command_buffer, args.depth_bias_constant_factor,
                              args.depth_bias_clamp,
                              args.depth_bias_slope_factor);
      } break;

      case Command::kVkSetScissor: {
        auto& args = *reinterpret_cast<const ArgsVkSetScissor*>(stream);
        dfn.vkCmdSetScissor(
            command_buffer, args.first_scissor, args.scissor_count,
            reinterpret_cast<const VkRect2D*>(
                reinterpret_cast<const uint8_t*>(stream) +
                xe::align(sizeof(ArgsVkSetScissor), alignof(VkRect2D))));
      } break;

      case Command::kVkSetStencilCompareMask: {
        auto& args =
            *reinterpret_cast<const ArgsSetStencilMaskReference*>(stream);
        dfn.vkCmdSetStencilCompareMask(command_buffer, args.face_mask,
                                       args.mask_reference);
      } break;

      case Command::kVkSetStencilReference: {
        auto& args =
            *reinterpret_cast<const ArgsSetStencilMaskReference*>(stream);
        dfn.vkCmdSetStencilReference(command_buffer, args.face_mask,
                                     args.mask_reference);
      } break;

      case Command::kVkSetStencilWriteMask: {
        auto& args =
            *reinterpret_cast<const ArgsSetStencilMaskReference*>(stream);
        dfn.vkCmdSetStencilWriteMask(command_buffer, args.face_mask,
                                     args.mask_reference);
      } break;

      case Command::kVkSetViewport: {
        auto& args = *reinterpret_cast<const ArgsVkSetViewport*>(stream);
        dfn.vkCmdSetViewport(
            command_buffer, args.first_viewport, args.viewport_count,
            reinterpret_cast<const VkViewport*>(
                reinterpret_cast<const uint8_t*>(stream) +
                xe::align(sizeof(ArgsVkSetViewport), alignof(VkViewport))));
      } break;

      default:
        assert_unhandled_case(header.command);
        break;
    }

    stream += header.arguments_size_elements;
    stream_remaining -= header.arguments_size_elements;
  }
}

void DeferredCommandBuffer::CmdVkPipelineBarrier(
    VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
    VkDependencyFlags dependency_flags, uint32_t memory_barrier_count,
    const VkMemoryBarrier* memory_barriers,
    uint32_t buffer_memory_barrier_count,
    const VkBufferMemoryBarrier* buffer_memory_barriers,
    uint32_t image_memory_barrier_count,
    const VkImageMemoryBarrier* image_memory_barriers) {
  size_t arguments_size = sizeof(ArgsVkPipelineBarrier);
  size_t memory_barriers_offset = 0;
  if (memory_barrier_count) {
    arguments_size = xe::align(arguments_size, alignof(VkMemoryBarrier));
    memory_barriers_offset = arguments_size;
    arguments_size += sizeof(VkMemoryBarrier) * memory_barrier_count;
  }
  size_t buffer_memory_barriers_offset = 0;
  if (buffer_memory_barrier_count) {
    arguments_size = xe::align(arguments_size, alignof(VkBufferMemoryBarrier));
    buffer_memory_barriers_offset = arguments_size;
    arguments_size +=
        sizeof(VkBufferMemoryBarrier) * buffer_memory_barrier_count;
  }
  size_t image_memory_barriers_offset = 0;
  if (image_memory_barrier_count) {
    arguments_size = xe::align(arguments_size, alignof(VkImageMemoryBarrier));
    image_memory_barriers_offset = arguments_size;
    arguments_size += sizeof(VkImageMemoryBarrier) * image_memory_barrier_count;
  }
  uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
      WriteCommand(Command::kVkPipelineBarrier, arguments_size));
  auto& args = *reinterpret_cast<ArgsVkPipelineBarrier*>(args_ptr);
  args.src_stage_mask = src_stage_mask;
  args.dst_stage_mask = dst_stage_mask;
  args.dependency_flags = dependency_flags;
  args.memory_barrier_count = memory_barrier_count;
  args.buffer_memory_barrier_count = buffer_memory_barrier_count;
  args.image_memory_barrier_count = image_memory_barrier_count;
  if (memory_barrier_count) {
    std::memcpy(args_ptr + memory_barriers_offset, memory_barriers,
                sizeof(VkMemoryBarrier) * memory_barrier_count);
  }
  if (buffer_memory_barrier_count) {
    std::memcpy(args_ptr + buffer_memory_barriers_offset,
                buffer_memory_barriers,
                sizeof(VkBufferMemoryBarrier) * buffer_memory_barrier_count);
  }
  if (image_memory_barrier_count) {
    std::memcpy(args_ptr + image_memory_barriers_offset, image_memory_barriers,
                sizeof(VkImageMemoryBarrier) * image_memory_barrier_count);
  }
}

void* DeferredCommandBuffer::WriteCommand(Command command,
                                          size_t arguments_size_bytes) {
  size_t arguments_size_elements =
      (arguments_size_bytes + sizeof(uintmax_t) - 1) / sizeof(uintmax_t);
  size_t offset = command_stream_.size();
  command_stream_.resize(offset + kCommandHeaderSizeElements +
                         arguments_size_elements);
  CommandHeader& header =
      *reinterpret_cast<CommandHeader*>(command_stream_.data() + offset);
  header.command = command;
  header.arguments_size_elements = uint32_t(arguments_size_elements);
  return command_stream_.data() + (offset + kCommandHeaderSizeElements);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
