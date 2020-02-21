/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vk/vulkan_immediate_drawer.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vk/vulkan_context.h"
#include "xenia/ui/vk/vulkan_util.h"

namespace xe {
namespace ui {
namespace vk {

// Generated with `xb genspirv`.
#include "xenia/ui/vk/shaders/bin/immediate_frag.h"
#include "xenia/ui/vk/shaders/bin/immediate_vert.h"

class VulkanImmediateTexture : public ImmediateTexture {
 public:
  VulkanImmediateTexture(uint32_t width, uint32_t height,
                         ImmediateTextureFilter filter, bool repeat);
  ~VulkanImmediateTexture() override;
};

VulkanImmediateTexture::VulkanImmediateTexture(uint32_t width, uint32_t height,
                                               ImmediateTextureFilter filter,
                                               bool repeat)
    : ImmediateTexture(width, height) {
  handle = reinterpret_cast<uintptr_t>(this);
}

VulkanImmediateTexture::~VulkanImmediateTexture() {}

VulkanImmediateDrawer::VulkanImmediateDrawer(VulkanContext* graphics_context)
    : ImmediateDrawer(graphics_context), context_(graphics_context) {}

VulkanImmediateDrawer::~VulkanImmediateDrawer() { Shutdown(); }

bool VulkanImmediateDrawer::Initialize() {
  auto device = context_->GetVulkanProvider()->GetDevice();

  // Create the pipeline layout.
  // TODO(Triang3l): Texture descriptor set layout.
  VkPushConstantRange push_constant_ranges[2];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_ranges[0].offset = offsetof(PushConstants, vertex);
  push_constant_ranges[0].size = sizeof(PushConstants::vertex);
  push_constant_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_constant_ranges[1].offset = offsetof(PushConstants, fragment);
  push_constant_ranges[1].size = sizeof(PushConstants::fragment);
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
  if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                             &pipeline_layout_) != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan immediate drawer pipeline layout");
    return false;
  }

  // Load the shaders.
  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = sizeof(immediate_vert);
  shader_module_create_info.pCode =
      reinterpret_cast<const uint32_t*>(immediate_vert);
  VkShaderModule shader_module_vertex;
  if (vkCreateShaderModule(device, &shader_module_create_info, nullptr,
                           &shader_module_vertex) != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan immediate drawer vertex shader module");
    return false;
  }
  shader_module_create_info.codeSize = sizeof(immediate_frag);
  shader_module_create_info.pCode =
      reinterpret_cast<const uint32_t*>(immediate_frag);
  VkShaderModule shader_module_fragment;
  if (vkCreateShaderModule(device, &shader_module_create_info, nullptr,
                           &shader_module_fragment) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the Vulkan immediate drawer fragment shader module");
    vkDestroyShaderModule(device, shader_module_vertex, nullptr);
    return false;
  }

  // Create the pipelines for triangles and lines.
  VkGraphicsPipelineCreateInfo pipeline_create_info;
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = nullptr;
  pipeline_create_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
  VkPipelineShaderStageCreateInfo pipeline_stages[2];
  pipeline_stages[0].sType = pipeline_stages[1].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline_stages[0].pNext = pipeline_stages[1].pNext = nullptr;
  pipeline_stages[0].flags = pipeline_stages[1].flags = 0;
  pipeline_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  pipeline_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  pipeline_stages[0].module = shader_module_vertex;
  pipeline_stages[1].module = shader_module_fragment;
  pipeline_stages[0].pName = pipeline_stages[1].pName = "main";
  pipeline_stages[0].pSpecializationInfo = nullptr;
  pipeline_stages[1].pSpecializationInfo = nullptr;
  pipeline_create_info.stageCount = uint32_t(xe::countof(pipeline_stages));
  pipeline_create_info.pStages = pipeline_stages;
  VkPipelineVertexInputStateCreateInfo pipeline_vertex_input;
  pipeline_vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  pipeline_vertex_input.pNext = nullptr;
  pipeline_vertex_input.flags = 0;
  VkVertexInputBindingDescription pipeline_vertex_bindings[1];
  pipeline_vertex_bindings[0].binding = 0;
  pipeline_vertex_bindings[0].stride = sizeof(ImmediateVertex);
  pipeline_vertex_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  pipeline_vertex_input.vertexBindingDescriptionCount =
      uint32_t(xe::countof(pipeline_vertex_bindings));
  pipeline_vertex_input.pVertexBindingDescriptions = pipeline_vertex_bindings;
  VkVertexInputAttributeDescription pipeline_vertex_attributes[3];
  pipeline_vertex_attributes[0].location = 0;
  pipeline_vertex_attributes[0].binding = 0;
  pipeline_vertex_attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  pipeline_vertex_attributes[0].offset = offsetof(ImmediateVertex, x);
  pipeline_vertex_attributes[1].location = 1;
  pipeline_vertex_attributes[1].binding = 0;
  pipeline_vertex_attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  pipeline_vertex_attributes[1].offset = offsetof(ImmediateVertex, u);
  pipeline_vertex_attributes[2].location = 2;
  pipeline_vertex_attributes[2].binding = 0;
  pipeline_vertex_attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  pipeline_vertex_attributes[2].offset = offsetof(ImmediateVertex, color);
  pipeline_vertex_input.vertexAttributeDescriptionCount =
      uint32_t(xe::countof(pipeline_vertex_attributes));
  pipeline_vertex_input.pVertexAttributeDescriptions =
      pipeline_vertex_attributes;
  pipeline_create_info.pVertexInputState = &pipeline_vertex_input;
  VkPipelineInputAssemblyStateCreateInfo pipeline_input_assembly;
  pipeline_input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  pipeline_input_assembly.pNext = nullptr;
  pipeline_input_assembly.flags = 0;
  pipeline_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipeline_create_info.pInputAssemblyState = &pipeline_input_assembly;
  pipeline_create_info.pTessellationState = nullptr;
  VkPipelineViewportStateCreateInfo pipeline_viewport;
  pipeline_viewport.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  pipeline_viewport.pNext = nullptr;
  pipeline_viewport.flags = 0;
  pipeline_viewport.viewportCount = 1;
  pipeline_viewport.pViewports = nullptr;
  pipeline_viewport.scissorCount = 1;
  pipeline_viewport.pScissors = nullptr;
  pipeline_create_info.pViewportState = &pipeline_viewport;
  VkPipelineRasterizationStateCreateInfo pipeline_rasterization = {};
  pipeline_rasterization.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  pipeline_rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  pipeline_rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
  pipeline_rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
  pipeline_rasterization.lineWidth = 1.0f;
  pipeline_create_info.pRasterizationState = &pipeline_rasterization;
  VkPipelineMultisampleStateCreateInfo pipeline_multisample = {};
  pipeline_multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  pipeline_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  pipeline_create_info.pMultisampleState = &pipeline_multisample;
  pipeline_create_info.pDepthStencilState = nullptr;
  VkPipelineColorBlendStateCreateInfo pipeline_color_blend;
  pipeline_color_blend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  pipeline_color_blend.pNext = nullptr;
  pipeline_color_blend.flags = 0;
  pipeline_color_blend.logicOpEnable = VK_FALSE;
  pipeline_color_blend.logicOp = VK_LOGIC_OP_NO_OP;
  VkPipelineColorBlendAttachmentState pipeline_color_blend_attachment = {};
  pipeline_color_blend_attachment.blendEnable = VK_TRUE;
  pipeline_color_blend_attachment.srcColorBlendFactor =
      VK_BLEND_FACTOR_SRC_ALPHA;
  pipeline_color_blend_attachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline_color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  pipeline_color_blend_attachment.srcAlphaBlendFactor =
      VK_BLEND_FACTOR_SRC_ALPHA;
  pipeline_color_blend_attachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline_color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  pipeline_color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  pipeline_color_blend.attachmentCount = 1;
  pipeline_color_blend.pAttachments = &pipeline_color_blend_attachment;
  pipeline_create_info.pColorBlendState = &pipeline_color_blend;
  static const VkDynamicState pipeline_dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo pipeline_dynamic_state;
  pipeline_dynamic_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  pipeline_dynamic_state.pNext = nullptr;
  pipeline_dynamic_state.flags = 0;
  pipeline_dynamic_state.dynamicStateCount =
      uint32_t(xe::countof(pipeline_dynamic_states));
  pipeline_dynamic_state.pDynamicStates = pipeline_dynamic_states;
  pipeline_create_info.pDynamicState = &pipeline_dynamic_state;
  pipeline_create_info.layout = pipeline_layout_;
  pipeline_create_info.renderPass = context_->GetPresentRenderPass();
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = -1;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                &pipeline_create_info, nullptr,
                                &pipeline_triangle_) != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan immediate drawer triangle pipeline");
    vkDestroyShaderModule(device, shader_module_fragment, nullptr);
    vkDestroyShaderModule(device, shader_module_vertex, nullptr);
    return false;
  }
  pipeline_create_info.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
  pipeline_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipeline_create_info.basePipelineHandle = pipeline_triangle_;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                &pipeline_create_info, nullptr,
                                &pipeline_line_) != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan immediate drawer triangle pipeline");
    vkDestroyShaderModule(device, shader_module_fragment, nullptr);
    vkDestroyShaderModule(device, shader_module_vertex, nullptr);
    return false;
  }

  vkDestroyShaderModule(device, shader_module_fragment, nullptr);
  vkDestroyShaderModule(device, shader_module_vertex, nullptr);

  // Create transient resource pools for draws.
  vertex_buffer_chain_ = std::make_unique<UploadBufferChain>(
      context_, 2 * 1024 * 2014,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  // Reset the current state.
  current_command_buffer_ = VK_NULL_HANDLE;

  return true;
}

void VulkanImmediateDrawer::Shutdown() {
  auto device = context_->GetVulkanProvider()->GetDevice();

  vertex_buffer_chain_.reset();

  util::DestroyAndNullHandle(vkDestroyPipeline, device, pipeline_line_);
  util::DestroyAndNullHandle(vkDestroyPipeline, device, pipeline_triangle_);

  util::DestroyAndNullHandle(vkDestroyPipelineLayout, device, pipeline_layout_);
}

std::unique_ptr<ImmediateTexture> VulkanImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  auto texture =
      std::make_unique<VulkanImmediateTexture>(width, height, filter, repeat);
  if (data) {
    UpdateTexture(texture.get(), data);
  }
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

void VulkanImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                          const uint8_t* data) {}

void VulkanImmediateDrawer::Begin(int render_target_width,
                                  int render_target_height) {
  current_command_buffer_ = context_->GetPresentCommandBuffer();

  current_render_target_extent_.width = render_target_width;
  current_render_target_extent_.height = render_target_height;

  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = float(render_target_width);
  viewport.height = float(render_target_height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 0.0f;
  vkCmdSetViewport(current_command_buffer_, 0, 1, &viewport);

  float viewport_inv_size[2];
  viewport_inv_size[0] = 1.0f / viewport.width;
  viewport_inv_size[1] = 1.0f / viewport.height;
  vkCmdPushConstants(current_command_buffer_, pipeline_layout_,
                     VK_SHADER_STAGE_VERTEX_BIT,
                     offsetof(PushConstants, vertex.viewport_inv_size),
                     sizeof(viewport_inv_size), viewport_inv_size);

  current_pipeline_ = VK_NULL_HANDLE;
}

void VulkanImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_true(current_command_buffer_ != VK_NULL_HANDLE);
  if (current_command_buffer_ == VK_NULL_HANDLE) {
    return;
  }

  batch_open_ = false;

  // Bind the vertices.
  size_t vertex_buffer_size = batch.vertex_count * sizeof(ImmediateVertex);
  VkBuffer vertex_buffer;
  VkDeviceSize vertex_buffer_offset;
  void* vertex_buffer_mapping = vertex_buffer_chain_->RequestFull(
      vertex_buffer_size, vertex_buffer, vertex_buffer_offset);
  if (!vertex_buffer_mapping) {
    XELOGE(
        "Failed to get a Vulkan buffer for %u vertices in the immediate drawer",
        batch.vertex_count);
    return;
  }
  std::memcpy(vertex_buffer_mapping, batch.vertices, vertex_buffer_size);
  vkCmdBindVertexBuffers(current_command_buffer_, 0, 1, &vertex_buffer,
                         &vertex_buffer_offset);

  // Bind the indices.
  batch_has_index_buffer_ = batch.indices != nullptr;
  if (batch_has_index_buffer_) {
    size_t index_buffer_size = batch.index_count * sizeof(uint16_t);
    VkBuffer index_buffer;
    VkDeviceSize index_buffer_offset;
    void* index_buffer_mapping = vertex_buffer_chain_->RequestFull(
        xe::align(index_buffer_size, VkDeviceSize(sizeof(uint32_t))),
        index_buffer, index_buffer_offset);
    if (!index_buffer_mapping) {
      XELOGE(
          "Failed to get a Vulkan buffer for %u indices in the immediate "
          "drawer",
          batch.index_count);
      return;
    }
    std::memcpy(index_buffer_mapping, batch.indices, index_buffer_size);
    vkCmdBindIndexBuffer(current_command_buffer_, index_buffer,
                         index_buffer_offset, VK_INDEX_TYPE_UINT16);
  }

  batch_open_ = true;
}

void VulkanImmediateDrawer::Draw(const ImmediateDraw& draw) {
  assert_true(current_command_buffer_ != VK_NULL_HANDLE);
  if (current_command_buffer_ == VK_NULL_HANDLE) {
    return;
  }

  if (!batch_open_) {
    // Could be an error while obtaining the vertex and index buffers.
    return;
  }

  // TODO(Triang3l): Bind the texture.

  // Set whether texture coordinates need to be restricted.
  uint32_t restrict_texture_samples = draw.restrict_texture_samples ? 1 : 0;
  vkCmdPushConstants(current_command_buffer_, pipeline_layout_,
                     VK_SHADER_STAGE_FRAGMENT_BIT,
                     offsetof(PushConstants, fragment.restrict_texture_samples),
                     sizeof(uint32_t), &restrict_texture_samples);

  // Bind the pipeline for the primitive type.
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
    vkCmdBindPipeline(current_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
    current_pipeline_ = pipeline;
  }

  // Set the scissor rectangle if enabled.
  VkRect2D scissor;
  if (draw.scissor) {
    scissor.offset.x = draw.scissor_rect[0];
    scissor.offset.y = draw.scissor_rect[1];
    scissor.extent.width = draw.scissor_rect[2];
    scissor.extent.height = draw.scissor_rect[3];
  } else {
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent = current_render_target_extent_;
  }
  vkCmdSetScissor(current_command_buffer_, 0, 1, &scissor);

  // Draw.
  if (batch_has_index_buffer_) {
    vkCmdDrawIndexed(current_command_buffer_, draw.count, 1, draw.index_offset,
                     draw.base_vertex, 0);
  } else {
    vkCmdDraw(current_command_buffer_, draw.count, 1, draw.base_vertex, 0);
  }
}

void VulkanImmediateDrawer::EndDrawBatch() { batch_open_ = false; }

void VulkanImmediateDrawer::End() {
  vertex_buffer_chain_->EndFrame();
  current_command_buffer_ = VK_NULL_HANDLE;
}

}  // namespace vk
}  // namespace ui
}  // namespace xe
