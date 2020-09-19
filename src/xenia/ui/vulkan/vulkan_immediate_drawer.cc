/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

// Generated with `xb genspirv`.
#include "xenia/ui/shaders/bytecode/vulkan_spirv/immediate_frag.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/immediate_vert.h"

class VulkanImmediateTexture : public ImmediateTexture {
 public:
  VulkanImmediateTexture(uint32_t width, uint32_t height)
      : ImmediateTexture(width, height) {}
};

VulkanImmediateDrawer::VulkanImmediateDrawer(VulkanContext& graphics_context)
    : ImmediateDrawer(&graphics_context), context_(graphics_context) {}

VulkanImmediateDrawer::~VulkanImmediateDrawer() { Shutdown(); }

bool VulkanImmediateDrawer::Initialize() {
  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkPushConstantRange push_constant_ranges[1];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_ranges[0].offset = offsetof(PushConstants, vertex);
  push_constant_ranges[0].size = sizeof(PushConstants::Vertex);
  VkPipelineLayoutCreateInfo pipeline_layout_create_info;
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = nullptr;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount = 0;
  pipeline_layout_create_info.pSetLayouts = nullptr;
  pipeline_layout_create_info.pushConstantRangeCount =
      uint32_t(xe::countof(push_constant_ranges));
  pipeline_layout_create_info.pPushConstantRanges = push_constant_ranges;
  if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                 &pipeline_layout_) != VK_SUCCESS) {
    XELOGE("Failed to create the immediate drawer Vulkan pipeline layout");
    Shutdown();
    return false;
  }

  vertex_buffer_pool_ = std::make_unique<VulkanUploadBufferPool>(
      provider,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  // Reset the current state.
  current_command_buffer_ = VK_NULL_HANDLE;
  batch_open_ = false;

  return true;
}

void VulkanImmediateDrawer::Shutdown() {
  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_line_);
  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_triangle_);

  vertex_buffer_pool_.reset();

  util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                             pipeline_layout_);
}

std::unique_ptr<ImmediateTexture> VulkanImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  auto texture = std::make_unique<VulkanImmediateTexture>(width, height);
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

void VulkanImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                          const uint8_t* data) {}

void VulkanImmediateDrawer::Begin(int render_target_width,
                                  int render_target_height) {
  assert_true(current_command_buffer_ == VK_NULL_HANDLE);
  assert_false(batch_open_);

  if (!EnsurePipelinesCreated()) {
    return;
  }

  current_command_buffer_ = context_.GetSwapCommandBuffer();

  uint64_t submission_completed = context_.swap_submission_completed();
  vertex_buffer_pool_->Reclaim(submission_completed);

  const VulkanProvider::DeviceFunctions& dfn =
      context_.GetVulkanProvider().dfn();

  current_render_target_extent_.width = uint32_t(render_target_width);
  current_render_target_extent_.height = uint32_t(render_target_height);
  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = float(render_target_width);
  viewport.height = float(render_target_height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  dfn.vkCmdSetViewport(current_command_buffer_, 0, 1, &viewport);
  PushConstants::Vertex push_constants_vertex;
  push_constants_vertex.viewport_size_inv[0] = 1.0f / viewport.width;
  push_constants_vertex.viewport_size_inv[1] = 1.0f / viewport.height;
  dfn.vkCmdPushConstants(current_command_buffer_, pipeline_layout_,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         offsetof(PushConstants, vertex),
                         sizeof(PushConstants::Vertex), &push_constants_vertex);

  current_pipeline_ = VK_NULL_HANDLE;
}

void VulkanImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_false(batch_open_);
  if (current_command_buffer_ == VK_NULL_HANDLE) {
    // No surface, or failed to create the pipelines.
    return;
  }

  uint64_t submission_current = context_.swap_submission_current();
  const VulkanProvider::DeviceFunctions& dfn =
      context_.GetVulkanProvider().dfn();

  // Bind the vertices.
  size_t vertex_buffer_size = sizeof(ImmediateVertex) * batch.vertex_count;
  VkBuffer vertex_buffer;
  VkDeviceSize vertex_buffer_offset;
  void* vertex_buffer_mapping = vertex_buffer_pool_->Request(
      submission_current, vertex_buffer_size, sizeof(float), vertex_buffer,
      vertex_buffer_offset);
  if (!vertex_buffer_mapping) {
    XELOGE("Failed to get a buffer for {} vertices in the immediate drawer",
           batch.vertex_count);
    return;
  }
  std::memcpy(vertex_buffer_mapping, batch.vertices, vertex_buffer_size);
  dfn.vkCmdBindVertexBuffers(current_command_buffer_, 0, 1, &vertex_buffer,
                             &vertex_buffer_offset);

  // Bind the indices.
  batch_has_index_buffer_ = batch.indices != nullptr;
  if (batch_has_index_buffer_) {
    size_t index_buffer_size = sizeof(uint16_t) * batch.index_count;
    VkBuffer index_buffer;
    VkDeviceSize index_buffer_offset;
    void* index_buffer_mapping = vertex_buffer_pool_->Request(
        submission_current, index_buffer_size, sizeof(uint16_t), index_buffer,
        index_buffer_offset);
    if (!index_buffer_mapping) {
      XELOGE("Failed to get a buffer for {} indices in the immediate drawer",
             batch.index_count);
      return;
    }
    std::memcpy(index_buffer_mapping, batch.indices, index_buffer_size);
    dfn.vkCmdBindIndexBuffer(current_command_buffer_, index_buffer,
                             index_buffer_offset, VK_INDEX_TYPE_UINT16);
  }

  batch_open_ = true;
}

void VulkanImmediateDrawer::Draw(const ImmediateDraw& draw) {
  if (!batch_open_) {
    // No surface, or failed to create the pipelines, or could be an error while
    // obtaining the vertex and index buffers.
    return;
  }

  const VulkanProvider::DeviceFunctions& dfn =
      context_.GetVulkanProvider().dfn();

  // Bind the pipeline for the current primitive count.
  VkPipeline pipeline;
  switch (draw.primitive_type) {
    case ImmediatePrimitiveType::kLines:
      pipeline = pipeline_line_;
      break;
    case ImmediatePrimitiveType::kTriangles:
      pipeline = pipeline_triangle_;
      break;
    default:
      assert_unhandled_case(draw.primitive_type);
      return;
  }
  if (current_pipeline_ != pipeline) {
    current_pipeline_ = pipeline;
    dfn.vkCmdBindPipeline(current_command_buffer_,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  }

  // Set the scissor rectangle if enabled.
  VkRect2D scissor;
  if (draw.scissor) {
    scissor.offset.x = draw.scissor_rect[0];
    scissor.offset.y = current_render_target_extent_.height -
                       (draw.scissor_rect[1] + draw.scissor_rect[3]);
    scissor.extent.width = draw.scissor_rect[2];
    scissor.extent.height = draw.scissor_rect[3];
  } else {
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = current_render_target_extent_;
  }
  dfn.vkCmdSetScissor(current_command_buffer_, 0, 1, &scissor);

  // Draw.
  if (batch_has_index_buffer_) {
    dfn.vkCmdDrawIndexed(current_command_buffer_, draw.count, 1,
                         draw.index_offset, draw.base_vertex, 0);
  } else {
    dfn.vkCmdDraw(current_command_buffer_, draw.count, 1, draw.base_vertex, 0);
  }
}

void VulkanImmediateDrawer::EndDrawBatch() { batch_open_ = false; }

void VulkanImmediateDrawer::End() {
  assert_false(batch_open_);
  if (current_command_buffer_ == VK_NULL_HANDLE) {
    // Didn't draw anything because the of some issue or surface not being
    // available.
    return;
  }
  vertex_buffer_pool_->FlushWrites();
  current_command_buffer_ = VK_NULL_HANDLE;
}

bool VulkanImmediateDrawer::EnsurePipelinesCreated() {
  VkFormat swap_surface_format = context_.swap_surface_format().format;
  if (swap_surface_format == pipeline_framebuffer_format_) {
    // Either created, or failed to create once (don't try to create every
    // frame).
    return pipeline_triangle_ != VK_NULL_HANDLE &&
           pipeline_line_ != VK_NULL_HANDLE;
  }
  VkRenderPass swap_render_pass = context_.swap_render_pass();
  if (swap_surface_format == VK_FORMAT_UNDEFINED ||
      swap_render_pass == VK_NULL_HANDLE) {
    // Not ready yet.
    return false;
  }

  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Safe to destroy the pipelines now - if the render pass was recreated,
  // completion of its usage has already been awaited.
  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_line_);
  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_triangle_);
  // If creation fails now, don't try to create every frame.
  pipeline_framebuffer_format_ = swap_surface_format;

  // Triangle pipeline.

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = util::CreateShaderModule(provider, immediate_vert,
                                              sizeof(immediate_vert));
  if (stages[0].module == VK_NULL_HANDLE) {
    XELOGE("Failed to create the immediate drawer Vulkan vertex shader module");
    return false;
  }
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = util::CreateShaderModule(provider, immediate_frag,
                                              sizeof(immediate_frag));
  if (stages[1].module == VK_NULL_HANDLE) {
    XELOGE(
        "Failed to create the immediate drawer Vulkan fragment shader module");
    dfn.vkDestroyShaderModule(device, stages[0].module, nullptr);
    return false;
  }
  stages[1].pName = "main";

  VkVertexInputBindingDescription vertex_input_binding;
  vertex_input_binding.binding = 0;
  vertex_input_binding.stride = sizeof(ImmediateVertex);
  vertex_input_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription vertex_input_attributes[3];
  vertex_input_attributes[0].location = 0;
  vertex_input_attributes[0].binding = 0;
  vertex_input_attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_input_attributes[0].offset = offsetof(ImmediateVertex, x);
  vertex_input_attributes[1].location = 1;
  vertex_input_attributes[1].binding = 0;
  vertex_input_attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_input_attributes[1].offset = offsetof(ImmediateVertex, u);
  vertex_input_attributes[2].location = 2;
  vertex_input_attributes[2].binding = 0;
  vertex_input_attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  vertex_input_attributes[2].offset = offsetof(ImmediateVertex, color);
  VkPipelineVertexInputStateCreateInfo vertex_input_state;
  vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_state.pNext = nullptr;
  vertex_input_state.flags = 0;
  vertex_input_state.vertexBindingDescriptionCount = 1;
  vertex_input_state.pVertexBindingDescriptions = &vertex_input_binding;
  vertex_input_state.vertexAttributeDescriptionCount =
      uint32_t(xe::countof(vertex_input_attributes));
  vertex_input_state.pVertexAttributeDescriptions = vertex_input_attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
  input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.pNext = nullptr;
  input_assembly_state.flags = 0;
  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly_state.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewport_state;
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = nullptr;
  viewport_state.flags = 0;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = nullptr;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterization_state = {};
  rasterization_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_state.cullMode = VK_CULL_MODE_NONE;
  rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample_state = {};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachment_state;
  color_blend_attachment_state.blendEnable = VK_TRUE;
  color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment_state.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
  // Don't change alpha (always 1).
  color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend_state;
  color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.pNext = nullptr;
  color_blend_state.flags = 0;
  color_blend_state.logicOpEnable = VK_FALSE;
  color_blend_state.logicOp = VK_LOGIC_OP_NO_OP;
  color_blend_state.attachmentCount = 1;
  color_blend_state.pAttachments = &color_blend_attachment_state;
  color_blend_state.blendConstants[0] = 1.0f;
  color_blend_state.blendConstants[1] = 1.0f;
  color_blend_state.blendConstants[2] = 1.0f;
  color_blend_state.blendConstants[3] = 1.0f;

  static const VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo dynamic_state;
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.pNext = nullptr;
  dynamic_state.flags = 0;
  dynamic_state.dynamicStateCount = uint32_t(xe::countof(dynamic_states));
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_create_info;
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = nullptr;
  pipeline_create_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
  pipeline_create_info.stageCount = uint32_t(xe::countof(stages));
  pipeline_create_info.pStages = stages;
  pipeline_create_info.pVertexInputState = &vertex_input_state;
  pipeline_create_info.pInputAssemblyState = &input_assembly_state;
  pipeline_create_info.pTessellationState = nullptr;
  pipeline_create_info.pViewportState = &viewport_state;
  pipeline_create_info.pRasterizationState = &rasterization_state;
  pipeline_create_info.pMultisampleState = &multisample_state;
  pipeline_create_info.pDepthStencilState = nullptr;
  pipeline_create_info.pColorBlendState = &color_blend_state;
  pipeline_create_info.pDynamicState = &dynamic_state;
  pipeline_create_info.layout = pipeline_layout_;
  pipeline_create_info.renderPass = swap_render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = -1;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                    &pipeline_create_info, nullptr,
                                    &pipeline_triangle_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the immediate drawer triangle list Vulkan pipeline");
    dfn.vkDestroyShaderModule(device, stages[1].module, nullptr);
    dfn.vkDestroyShaderModule(device, stages[0].module, nullptr);
    return false;
  }

  // Line pipeline.

  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipeline_create_info.flags =
      (pipeline_create_info.flags & ~VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT) |
      VK_PIPELINE_CREATE_DERIVATIVE_BIT;
  pipeline_create_info.basePipelineHandle = pipeline_triangle_;
  VkResult pipeline_line_create_result = dfn.vkCreateGraphicsPipelines(
      device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
      &pipeline_line_);
  dfn.vkDestroyShaderModule(device, stages[1].module, nullptr);
  dfn.vkDestroyShaderModule(device, stages[0].module, nullptr);
  if (pipeline_line_create_result != VK_SUCCESS) {
    XELOGE("Failed to create the immediate drawer line list Vulkan pipeline");
    dfn.vkDestroyPipeline(device, pipeline_triangle_, nullptr);
    pipeline_triangle_ = VK_NULL_HANDLE;
    return false;
  }

  return true;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
