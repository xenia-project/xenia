/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/blitter.h"
#include "xenia/base/math.h"

namespace xe {
namespace ui {
namespace vulkan {

// Generated with `xenia-build genspirv`.
#include "xenia/ui/vulkan/shaders/bin/blit_color_frag.h"
#include "xenia/ui/vulkan/shaders/bin/blit_depth_frag.h"
#include "xenia/ui/vulkan/shaders/bin/blit_vert.h"

Blitter::Blitter() {}
Blitter::~Blitter() {}

bool Blitter::Initialize(VulkanDevice* device) {
  device_ = device;

  // Shaders
  VkShaderModuleCreateInfo shader_create_info;
  std::memset(&shader_create_info, 0, sizeof(shader_create_info));
  shader_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_create_info.codeSize = sizeof(blit_vert);
  shader_create_info.pCode = reinterpret_cast<const uint32_t*>(blit_vert);
  auto result = vkCreateShaderModule(*device_, &shader_create_info, nullptr,
                                     &blit_vertex_);
  CheckResult(result, "vkCreateShaderModule");

  shader_create_info.codeSize = sizeof(blit_color_frag);
  shader_create_info.pCode = reinterpret_cast<const uint32_t*>(blit_color_frag);
  result = vkCreateShaderModule(*device_, &shader_create_info, nullptr,
                                &blit_color_);
  CheckResult(result, "vkCreateShaderModule");

  shader_create_info.codeSize = sizeof(blit_depth_frag);
  shader_create_info.pCode = reinterpret_cast<const uint32_t*>(blit_depth_frag);
  result = vkCreateShaderModule(*device_, &shader_create_info, nullptr,
                                &blit_depth_);
  CheckResult(result, "vkCreateShaderModule");

  // Create the descriptor set layout used for our texture sampler.
  // As it changes almost every draw we keep it separate from the uniform buffer
  // and cache it on the textures.
  VkDescriptorSetLayoutCreateInfo texture_set_layout_info;
  texture_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  texture_set_layout_info.pNext = nullptr;
  texture_set_layout_info.flags = 0;
  texture_set_layout_info.bindingCount = 1;
  VkDescriptorSetLayoutBinding texture_binding;
  texture_binding.binding = 0;
  texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  texture_binding.descriptorCount = 1;
  texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  texture_binding.pImmutableSamplers = nullptr;
  texture_set_layout_info.pBindings = &texture_binding;
  result = vkCreateDescriptorSetLayout(*device_, &texture_set_layout_info,
                                       nullptr, &descriptor_set_layout_);
  CheckResult(result, "vkCreateDescriptorSetLayout");

  // Create the pipeline layout used for our pipeline.
  // If we had multiple pipelines they would share this.
  VkPipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.pNext = nullptr;
  pipeline_layout_info.flags = 0;
  VkDescriptorSetLayout set_layouts[] = {descriptor_set_layout_};
  pipeline_layout_info.setLayoutCount =
      static_cast<uint32_t>(xe::countof(set_layouts));
  pipeline_layout_info.pSetLayouts = set_layouts;
  VkPushConstantRange push_constant_ranges[2];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_ranges[0].offset = 0;
  push_constant_ranges[0].size = sizeof(float) * 16;
  push_constant_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_constant_ranges[1].offset = sizeof(float) * 16;
  push_constant_ranges[1].size = sizeof(int);
  pipeline_layout_info.pushConstantRangeCount =
      static_cast<uint32_t>(xe::countof(push_constant_ranges));
  pipeline_layout_info.pPushConstantRanges = push_constant_ranges;
  result = vkCreatePipelineLayout(*device_, &pipeline_layout_info, nullptr,
                                  &pipeline_layout_);
  CheckResult(result, "vkCreatePipelineLayout");

  return true;
}

void Blitter::Shutdown() {
  vkDestroyShaderModule(*device_, blit_vertex_, nullptr);
  vkDestroyShaderModule(*device_, blit_color_, nullptr);
  vkDestroyShaderModule(*device_, blit_depth_, nullptr);
  vkDestroyPipeline(*device_, pipeline_color_, nullptr);
  vkDestroyPipeline(*device_, pipeline_depth_, nullptr);
  vkDestroyPipelineLayout(*device_, pipeline_layout_, nullptr);
  vkDestroyDescriptorSetLayout(*device_, descriptor_set_layout_, nullptr);
  vkDestroyRenderPass(*device_, render_pass_, nullptr);
}

void Blitter::BlitTexture2D(VkCommandBuffer command_buffer, VkImage src_image,
                            VkImageView src_image_view, VkOffset2D src_offset,
                            VkExtent2D src_extents, VkImage dst_image,
                            VkImageView dst_image_view, VkFilter filter,
                            bool swap_channels) {}

void Blitter::CopyColorTexture2D(VkCommandBuffer command_buffer,
                                 VkImage src_image, VkImageView src_image_view,
                                 VkOffset2D src_offset, VkImage dst_image,
                                 VkImageView dst_image_view, VkExtent2D extents,
                                 VkFilter filter, bool swap_channels) {}

void Blitter::CopyDepthTexture(VkCommandBuffer command_buffer,
                               VkImage src_image, VkImageView src_image_view,
                               VkOffset2D src_offset, VkImage dst_image,
                               VkImageView dst_image_view, VkExtent2D extents) {
}

VkRenderPass Blitter::CreateRenderPass(VkFormat output_format) {
  VkAttachmentDescription attachments[1];
  std::memset(attachments, 0, sizeof(attachments));

  // Output attachment
  // TODO(DrChat): Figure out a way to leave the format undefined here.
  attachments[0].format = output_format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkSubpassDescription subpass;
  std::memset(&subpass, 0, sizeof(subpass));
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

  VkAttachmentReference attach_refs[1];
  attach_refs[0].attachment = 0;
  attach_refs[0].layout = VK_IMAGE_LAYOUT_GENERAL;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = attach_refs;

  VkRenderPassCreateInfo renderpass;
  std::memset(&renderpass, 0, sizeof(renderpass));
  renderpass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderpass.attachmentCount = 1;
  renderpass.pAttachments = attachments;
  renderpass.subpassCount = 1;
  renderpass.pSubpasses = &subpass;

  return nullptr;
}

VkPipeline Blitter::CreatePipeline(VkRenderPass render_pass) {
  // Pipeline
  VkGraphicsPipelineCreateInfo pipeline_info;
  std::memset(&pipeline_info, 0, sizeof(VkGraphicsPipelineCreateInfo));
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

  // Shaders
  pipeline_info.stageCount = 2;
  VkPipelineShaderStageCreateInfo stages[2];
  std::memset(stages, 0, sizeof(stages));
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = blit_vertex_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[1].module = blit_color_;
  stages[1].pName = "main";

  // Vertex input
  VkPipelineVertexInputStateCreateInfo vtx_state;
  std::memset(&vtx_state, 0, sizeof(vtx_state));
  vtx_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkVertexInputBindingDescription bindings[1];
  VkVertexInputAttributeDescription attribs[1];
  bindings[0].binding = 0;
  bindings[0].stride = 8;
  bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  attribs[0].location = 0;
  attribs[0].binding = 0;
  attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
  attribs[0].offset = 0;

  pipeline_info.pVertexInputState = &vtx_state;

  // Input Assembly
  VkPipelineInputAssemblyStateCreateInfo input_info;
  input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_info.pNext = nullptr;
  input_info.flags = 0;
  input_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_info.primitiveRestartEnable = VK_FALSE;
  pipeline_info.pInputAssemblyState = &input_info;
  pipeline_info.pTessellationState = nullptr;
  VkPipelineViewportStateCreateInfo viewport_state_info;
  viewport_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state_info.pNext = nullptr;
  viewport_state_info.flags = 0;
  viewport_state_info.viewportCount = 1;
  viewport_state_info.pViewports = nullptr;
  viewport_state_info.scissorCount = 1;
  viewport_state_info.pScissors = nullptr;
  pipeline_info.pViewportState = &viewport_state_info;
  VkPipelineRasterizationStateCreateInfo rasterization_info;
  rasterization_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_info.pNext = nullptr;
  rasterization_info.flags = 0;
  rasterization_info.depthClampEnable = VK_FALSE;
  rasterization_info.rasterizerDiscardEnable = VK_FALSE;
  rasterization_info.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_info.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterization_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterization_info.depthBiasEnable = VK_FALSE;
  rasterization_info.depthBiasConstantFactor = 0;
  rasterization_info.depthBiasClamp = 0;
  rasterization_info.depthBiasSlopeFactor = 0;
  rasterization_info.lineWidth = 1.0f;
  pipeline_info.pRasterizationState = &rasterization_info;
  VkPipelineMultisampleStateCreateInfo multisample_info;
  multisample_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_info.pNext = nullptr;
  multisample_info.flags = 0;
  multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisample_info.sampleShadingEnable = VK_FALSE;
  multisample_info.minSampleShading = 0;
  multisample_info.pSampleMask = nullptr;
  multisample_info.alphaToCoverageEnable = VK_FALSE;
  multisample_info.alphaToOneEnable = VK_FALSE;
  pipeline_info.pMultisampleState = &multisample_info;
  pipeline_info.pDepthStencilState = nullptr;
  VkPipelineColorBlendStateCreateInfo blend_info;
  blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend_info.pNext = nullptr;
  blend_info.flags = 0;
  blend_info.logicOpEnable = VK_FALSE;
  blend_info.logicOp = VK_LOGIC_OP_NO_OP;
  VkPipelineColorBlendAttachmentState blend_attachments[1];
  blend_attachments[0].blendEnable = VK_TRUE;
  blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachments[0].dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachments[0].dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
  blend_attachments[0].colorWriteMask = 0xF;
  blend_info.attachmentCount =
      static_cast<uint32_t>(xe::countof(blend_attachments));
  blend_info.pAttachments = blend_attachments;
  std::memset(blend_info.blendConstants, 0, sizeof(blend_info.blendConstants));
  pipeline_info.pColorBlendState = &blend_info;
  VkPipelineDynamicStateCreateInfo dynamic_state_info;
  dynamic_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_info.pNext = nullptr;
  dynamic_state_info.flags = 0;
  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
  };
  dynamic_state_info.dynamicStateCount =
      static_cast<uint32_t>(xe::countof(dynamic_states));
  dynamic_state_info.pDynamicStates = dynamic_states;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = nullptr;
  pipeline_info.basePipelineIndex = -1;

  return nullptr;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe