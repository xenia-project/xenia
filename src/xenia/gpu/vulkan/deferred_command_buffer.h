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

#include "xenia/base/assert.h"
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

  // render_pass_begin->pNext of all barriers must be null.
  void CmdVkBeginRenderPass(const VkRenderPassBeginInfo* render_pass_begin,
                            VkSubpassContents contents) {
    assert_null(render_pass_begin->pNext);
    size_t arguments_size = sizeof(ArgsVkBeginRenderPass);
    uint32_t clear_value_count = render_pass_begin->clearValueCount;
    size_t clear_values_offset = 0;
    if (clear_value_count) {
      arguments_size = xe::align(arguments_size, alignof(VkClearValue));
      clear_values_offset = arguments_size;
      arguments_size += sizeof(VkClearValue) * clear_value_count;
    }
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkBeginRenderPass, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkBeginRenderPass*>(args_ptr);
    args.render_pass = render_pass_begin->renderPass;
    args.framebuffer = render_pass_begin->framebuffer;
    args.render_area = render_pass_begin->renderArea;
    args.clear_value_count = clear_value_count;
    args.contents = contents;
    if (clear_value_count) {
      std::memcpy(args_ptr + clear_values_offset,
                  render_pass_begin->pClearValues,
                  sizeof(VkClearValue) * clear_value_count);
    }
  }

  void CmdVkBindDescriptorSets(VkPipelineBindPoint pipeline_bind_point,
                               VkPipelineLayout layout, uint32_t first_set,
                               uint32_t descriptor_set_count,
                               const VkDescriptorSet* descriptor_sets,
                               uint32_t dynamic_offset_count,
                               const uint32_t* dynamic_offsets) {
    size_t arguments_size =
        xe::align(sizeof(ArgsVkBindDescriptorSets), alignof(VkDescriptorSet));
    size_t descriptor_sets_offset = arguments_size;
    arguments_size += sizeof(VkDescriptorSet) * descriptor_set_count;
    size_t dynamic_offsets_offset = 0;
    if (dynamic_offset_count) {
      arguments_size = xe::align(arguments_size, alignof(uint32_t));
      dynamic_offsets_offset = arguments_size;
      arguments_size += sizeof(uint32_t) * dynamic_offset_count;
    }
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkBindDescriptorSets, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkBindDescriptorSets*>(args_ptr);
    args.pipeline_bind_point = pipeline_bind_point;
    args.layout = layout;
    args.first_set = first_set;
    args.descriptor_set_count = descriptor_set_count;
    args.dynamic_offset_count = dynamic_offset_count;
    std::memcpy(args_ptr + descriptor_sets_offset, descriptor_sets,
                sizeof(VkDescriptorSet) * descriptor_set_count);
    if (dynamic_offset_count) {
      std::memcpy(args_ptr + dynamic_offsets_offset, dynamic_offsets,
                  sizeof(uint32_t) * dynamic_offset_count);
    }
  }

  void CmdVkBindIndexBuffer(VkBuffer buffer, VkDeviceSize offset,
                            VkIndexType index_type) {
    auto& args = *reinterpret_cast<ArgsVkBindIndexBuffer*>(WriteCommand(
        Command::kVkBindIndexBuffer, sizeof(ArgsVkBindIndexBuffer)));
    args.buffer = buffer;
    args.offset = offset;
    args.index_type = index_type;
  }

  void CmdVkBindPipeline(VkPipelineBindPoint pipeline_bind_point,
                         VkPipeline pipeline) {
    auto& args = *reinterpret_cast<ArgsVkBindPipeline*>(
        WriteCommand(Command::kVkBindPipeline, sizeof(ArgsVkBindPipeline)));
    args.pipeline_bind_point = pipeline_bind_point;
    args.pipeline = pipeline;
  }

  void CmdVkBindVertexBuffers(uint32_t first_binding, uint32_t binding_count,
                              const VkBuffer* buffers,
                              const VkDeviceSize* offsets) {
    size_t arguments_size =
        xe::align(sizeof(ArgsVkBindVertexBuffers), alignof(VkBuffer));
    size_t buffers_offset = arguments_size;
    arguments_size =
        xe::align(arguments_size + sizeof(VkBuffer) * binding_count,
                  alignof(VkDeviceSize));
    size_t offsets_offset = arguments_size;
    arguments_size += sizeof(VkDeviceSize) * binding_count;
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkBindVertexBuffers, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkBindVertexBuffers*>(args_ptr);
    args.first_binding = first_binding;
    args.binding_count = binding_count;
    std::memcpy(args_ptr + buffers_offset, buffers,
                sizeof(VkBuffer) * binding_count);
    std::memcpy(args_ptr + offsets_offset, offsets,
                sizeof(VkDeviceSize) * binding_count);
  }

  void CmdClearAttachmentsEmplace(uint32_t attachment_count,
                                  VkClearAttachment*& attachments_out,
                                  uint32_t rect_count,
                                  VkClearRect*& rects_out) {
    size_t arguments_size =
        xe::align(sizeof(ArgsVkClearAttachments), alignof(VkClearAttachment));
    size_t attachments_offset = arguments_size;
    arguments_size =
        xe::align(arguments_size + sizeof(VkClearAttachment) * attachment_count,
                  alignof(VkClearRect));
    size_t rects_offset = arguments_size;
    arguments_size += sizeof(VkClearRect) * rect_count;
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkClearAttachments, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkClearAttachments*>(args_ptr);
    args.attachment_count = attachment_count;
    args.rect_count = rect_count;
    attachments_out =
        reinterpret_cast<VkClearAttachment*>(args_ptr + attachments_offset);
    rects_out = reinterpret_cast<VkClearRect*>(args_ptr + rects_offset);
  }
  void CmdVkClearAttachments(uint32_t attachment_count,
                             const VkClearAttachment* attachments,
                             uint32_t rect_count, const VkClearRect* rects) {
    VkClearAttachment* attachments_arg;
    VkClearRect* rects_arg;
    CmdClearAttachmentsEmplace(attachment_count, attachments_arg, rect_count,
                               rects_arg);
    std::memcpy(attachments_arg, attachments,
                sizeof(VkClearAttachment) * attachment_count);
    std::memcpy(rects_arg, rects, sizeof(VkClearRect) * rect_count);
  }

  VkBufferCopy* CmdCopyBufferEmplace(VkBuffer src_buffer, VkBuffer dst_buffer,
                                     uint32_t region_count) {
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

  void CmdVkDispatch(uint32_t group_count_x, uint32_t group_count_y,
                     uint32_t group_count_z) {
    auto& args = *reinterpret_cast<ArgsVkDispatch*>(
        WriteCommand(Command::kVkDispatch, sizeof(ArgsVkDispatch)));
    args.group_count_x = group_count_x;
    args.group_count_y = group_count_y;
    args.group_count_z = group_count_z;
  }

  void CmdVkDraw(uint32_t vertex_count, uint32_t instance_count,
                 uint32_t first_vertex, uint32_t first_instance) {
    auto& args = *reinterpret_cast<ArgsVkDraw*>(
        WriteCommand(Command::kVkDraw, sizeof(ArgsVkDraw)));
    args.vertex_count = vertex_count;
    args.instance_count = instance_count;
    args.first_vertex = first_vertex;
    args.first_instance = first_instance;
  }

  void CmdVkDrawIndexed(uint32_t index_count, uint32_t instance_count,
                        uint32_t first_index, int32_t vertex_offset,
                        uint32_t first_instance) {
    auto& args = *reinterpret_cast<ArgsVkDrawIndexed*>(
        WriteCommand(Command::kVkDrawIndexed, sizeof(ArgsVkDrawIndexed)));
    args.index_count = index_count;
    args.instance_count = instance_count;
    args.first_index = first_index;
    args.vertex_offset = vertex_offset;
    args.first_instance = first_instance;
  }

  void CmdVkEndRenderPass() { WriteCommand(Command::kVkEndRenderPass, 0); }

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

  void CmdVkPushConstants(VkPipelineLayout layout,
                          VkShaderStageFlags stage_flags, uint32_t offset,
                          uint32_t size, const void* values) {
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(WriteCommand(
        Command::kVkPushConstants, sizeof(ArgsVkPushConstants) + size));
    auto& args = *reinterpret_cast<ArgsVkPushConstants*>(args_ptr);
    args.layout = layout;
    args.stage_flags = stage_flags;
    args.offset = offset;
    args.size = size;
    std::memcpy(args_ptr + sizeof(ArgsVkPushConstants), values, size);
  }

  void CmdVkSetBlendConstants(const float* blend_constants) {
    auto& args = *reinterpret_cast<ArgsVkSetBlendConstants*>(WriteCommand(
        Command::kVkSetBlendConstants, sizeof(ArgsVkSetBlendConstants)));
    std::memcpy(args.blend_constants, blend_constants, sizeof(float) * 4);
  }

  void CmdVkSetDepthBias(float depth_bias_constant_factor,
                         float depth_bias_clamp,
                         float depth_bias_slope_factor) {
    auto& args = *reinterpret_cast<ArgsVkSetDepthBias*>(
        WriteCommand(Command::kVkSetDepthBias, sizeof(ArgsVkSetDepthBias)));
    args.depth_bias_constant_factor = depth_bias_constant_factor;
    args.depth_bias_clamp = depth_bias_clamp;
    args.depth_bias_slope_factor = depth_bias_slope_factor;
  }

  void CmdVkSetScissor(uint32_t first_scissor, uint32_t scissor_count,
                       const VkRect2D* scissors) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkSetScissor), alignof(VkRect2D));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkSetScissor,
                     header_size + sizeof(VkRect2D) * scissor_count));
    auto& args = *reinterpret_cast<ArgsVkSetScissor*>(args_ptr);
    args.first_scissor = first_scissor;
    args.scissor_count = scissor_count;
    std::memcpy(args_ptr + header_size, scissors,
                sizeof(VkRect2D) * scissor_count);
  }

  void CmdVkSetStencilCompareMask(VkStencilFaceFlags face_mask,
                                  uint32_t compare_mask) {
    auto& args = *reinterpret_cast<ArgsSetStencilMaskReference*>(
        WriteCommand(Command::kVkSetStencilCompareMask,
                     sizeof(ArgsSetStencilMaskReference)));
    args.face_mask = face_mask;
    args.mask_reference = compare_mask;
  }

  void CmdVkSetStencilReference(VkStencilFaceFlags face_mask,
                                uint32_t reference) {
    auto& args = *reinterpret_cast<ArgsSetStencilMaskReference*>(WriteCommand(
        Command::kVkSetStencilReference, sizeof(ArgsSetStencilMaskReference)));
    args.face_mask = face_mask;
    args.mask_reference = reference;
  }

  void CmdVkSetStencilWriteMask(VkStencilFaceFlags face_mask,
                                uint32_t write_mask) {
    auto& args = *reinterpret_cast<ArgsSetStencilMaskReference*>(WriteCommand(
        Command::kVkSetStencilWriteMask, sizeof(ArgsSetStencilMaskReference)));
    args.face_mask = face_mask;
    args.mask_reference = write_mask;
  }

  void CmdVkSetViewport(uint32_t first_viewport, uint32_t viewport_count,
                        const VkViewport* viewports) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkSetViewport), alignof(VkViewport));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkSetViewport,
                     header_size + sizeof(VkViewport) * viewport_count));
    auto& args = *reinterpret_cast<ArgsVkSetViewport*>(args_ptr);
    args.first_viewport = first_viewport;
    args.viewport_count = viewport_count;
    std::memcpy(args_ptr + header_size, viewports,
                sizeof(VkViewport) * viewport_count);
  }

 private:
  enum class Command {
    kVkBeginRenderPass,
    kVkBindDescriptorSets,
    kVkBindIndexBuffer,
    kVkBindPipeline,
    kVkBindVertexBuffers,
    kVkClearAttachments,
    kVkCopyBuffer,
    kVkDispatch,
    kVkDraw,
    kVkDrawIndexed,
    kVkEndRenderPass,
    kVkPipelineBarrier,
    kVkPushConstants,
    kVkSetBlendConstants,
    kVkSetDepthBias,
    kVkSetScissor,
    kVkSetStencilCompareMask,
    kVkSetStencilReference,
    kVkSetStencilWriteMask,
    kVkSetViewport,
  };

  struct CommandHeader {
    Command command;
    uint32_t arguments_size_elements;
  };
  static constexpr size_t kCommandHeaderSizeElements =
      (sizeof(CommandHeader) + sizeof(uintmax_t) - 1) / sizeof(uintmax_t);

  struct ArgsVkBeginRenderPass {
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
    VkRect2D render_area;
    uint32_t clear_value_count;
    VkSubpassContents contents;
    // Followed by aligned optional VkClearValue[].
    static_assert(alignof(VkClearValue) <= alignof(uintmax_t));
  };

  struct ArgsVkBindDescriptorSets {
    VkPipelineBindPoint pipeline_bind_point;
    VkPipelineLayout layout;
    uint32_t first_set;
    uint32_t descriptor_set_count;
    uint32_t dynamic_offset_count;
    // Followed by aligned VkDescriptorSet[], optional uint32_t[].
    static_assert(alignof(VkDescriptorSet) <= alignof(uintmax_t));
  };

  struct ArgsVkBindIndexBuffer {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkIndexType index_type;
  };

  struct ArgsVkBindPipeline {
    VkPipelineBindPoint pipeline_bind_point;
    VkPipeline pipeline;
  };

  struct ArgsVkBindVertexBuffers {
    uint32_t first_binding;
    uint32_t binding_count;
    // Followed by aligned VkBuffer[], VkDeviceSize[].
    static_assert(alignof(VkBuffer) <= alignof(uintmax_t));
    static_assert(alignof(VkDeviceSize) <= alignof(uintmax_t));
  };

  struct ArgsVkClearAttachments {
    uint32_t attachment_count;
    uint32_t rect_count;
    // Followed by aligned VkClearAttachment[], VkClearRect[].
    static_assert(alignof(VkClearAttachment) <= alignof(uintmax_t));
    static_assert(alignof(VkClearRect) <= alignof(uintmax_t));
  };

  struct ArgsVkCopyBuffer {
    VkBuffer src_buffer;
    VkBuffer dst_buffer;
    uint32_t region_count;
    // Followed by aligned VkBufferCopy[].
    static_assert(alignof(VkBufferCopy) <= alignof(uintmax_t));
  };

  struct ArgsVkDispatch {
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
  };

  struct ArgsVkDraw {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
  };

  struct ArgsVkDrawIndexed {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
  };

  struct ArgsVkPipelineBarrier {
    VkPipelineStageFlags src_stage_mask;
    VkPipelineStageFlags dst_stage_mask;
    VkDependencyFlags dependency_flags;
    uint32_t memory_barrier_count;
    uint32_t buffer_memory_barrier_count;
    uint32_t image_memory_barrier_count;
    // Followed by aligned optional VkMemoryBarrier[],
    // optional VkBufferMemoryBarrier[], optional VkImageMemoryBarrier[].
    static_assert(alignof(VkMemoryBarrier) <= alignof(uintmax_t));
    static_assert(alignof(VkBufferMemoryBarrier) <= alignof(uintmax_t));
    static_assert(alignof(VkImageMemoryBarrier) <= alignof(uintmax_t));
  };

  struct ArgsVkPushConstants {
    VkPipelineLayout layout;
    VkShaderStageFlags stage_flags;
    uint32_t offset;
    uint32_t size;
    // Followed by `size` bytes of values.
  };

  struct ArgsVkSetBlendConstants {
    float blend_constants[4];
  };

  struct ArgsVkSetDepthBias {
    float depth_bias_constant_factor;
    float depth_bias_clamp;
    float depth_bias_slope_factor;
  };

  struct ArgsVkSetScissor {
    uint32_t first_scissor;
    uint32_t scissor_count;
    // Followed by aligned VkRect2D[].
    static_assert(alignof(VkRect2D) <= alignof(uintmax_t));
  };

  struct ArgsSetStencilMaskReference {
    VkStencilFaceFlags face_mask;
    uint32_t mask_reference;
  };

  struct ArgsVkSetViewport {
    uint32_t first_viewport;
    uint32_t viewport_count;
    // Followed by aligned VkViewport[].
    static_assert(alignof(VkViewport) <= alignof(uintmax_t));
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