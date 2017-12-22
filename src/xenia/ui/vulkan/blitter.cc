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
#include "xenia/ui/vulkan/fenced_pools.h"

namespace xe {
namespace ui {
namespace vulkan {

// Generated with `xenia-build genspirv`.
#include "xenia/ui/vulkan/shaders/bin/blit_color_frag.h"
#include "xenia/ui/vulkan/shaders/bin/blit_depth_frag.h"
#include "xenia/ui/vulkan/shaders/bin/blit_vert.h"

Blitter::Blitter() {}
Blitter::~Blitter() { Shutdown(); }

VkResult Blitter::Initialize(VulkanDevice* device) {
  device_ = device;

  VkResult status = VK_SUCCESS;

  // Shaders
  VkShaderModuleCreateInfo shader_create_info;
  std::memset(&shader_create_info, 0, sizeof(shader_create_info));
  shader_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_create_info.codeSize = sizeof(blit_vert);
  shader_create_info.pCode = reinterpret_cast<const uint32_t*>(blit_vert);
  status = vkCreateShaderModule(*device_, &shader_create_info, nullptr,
                                &blit_vertex_);
  CheckResult(status, "vkCreateShaderModule");
  if (status != VK_SUCCESS) {
    return status;
  }

  shader_create_info.codeSize = sizeof(blit_color_frag);
  shader_create_info.pCode = reinterpret_cast<const uint32_t*>(blit_color_frag);
  status = vkCreateShaderModule(*device_, &shader_create_info, nullptr,
                                &blit_color_);
  CheckResult(status, "vkCreateShaderModule");
  if (status != VK_SUCCESS) {
    return status;
  }

  shader_create_info.codeSize = sizeof(blit_depth_frag);
  shader_create_info.pCode = reinterpret_cast<const uint32_t*>(blit_depth_frag);
  status = vkCreateShaderModule(*device_, &shader_create_info, nullptr,
                                &blit_depth_);
  CheckResult(status, "vkCreateShaderModule");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the descriptor set layout used for our texture sampler.
  // As it changes almost every draw we cache it per texture.
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
  status = vkCreateDescriptorSetLayout(*device_, &texture_set_layout_info,
                                       nullptr, &descriptor_set_layout_);
  CheckResult(status, "vkCreateDescriptorSetLayout");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create a descriptor pool
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].descriptorCount = 4096;
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptor_pool_ = std::make_unique<DescriptorPool>(
      *device_, 4096,
      std::vector<VkDescriptorPoolSize>(pool_sizes, std::end(pool_sizes)));

  // Create the pipeline layout used for our pipeline.
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
  push_constant_ranges[0].size = sizeof(VtxPushConstants);
  push_constant_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_constant_ranges[1].offset = sizeof(VtxPushConstants);
  push_constant_ranges[1].size = sizeof(PixPushConstants);

  pipeline_layout_info.pushConstantRangeCount =
      static_cast<uint32_t>(xe::countof(push_constant_ranges));
  pipeline_layout_info.pPushConstantRanges = push_constant_ranges;
  status = vkCreatePipelineLayout(*device_, &pipeline_layout_info, nullptr,
                                  &pipeline_layout_);
  CheckResult(status, "vkCreatePipelineLayout");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create two samplers.
  VkSamplerCreateInfo sampler_create_info = {
      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      nullptr,
      0,
      VK_FILTER_NEAREST,
      VK_FILTER_NEAREST,
      VK_SAMPLER_MIPMAP_MODE_NEAREST,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      0.f,
      VK_FALSE,
      1.f,
      VK_FALSE,
      VK_COMPARE_OP_NEVER,
      0.f,
      0.f,
      VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
      VK_FALSE,
  };
  status =
      vkCreateSampler(*device_, &sampler_create_info, nullptr, &samp_nearest_);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return status;
  }

  sampler_create_info.minFilter = VK_FILTER_LINEAR;
  sampler_create_info.magFilter = VK_FILTER_LINEAR;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  status =
      vkCreateSampler(*device_, &sampler_create_info, nullptr, &samp_linear_);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return status;
  }

  return VK_SUCCESS;
}

void Blitter::Shutdown() {
  if (samp_nearest_) {
    vkDestroySampler(*device_, samp_nearest_, nullptr);
    samp_nearest_ = nullptr;
  }
  if (samp_linear_) {
    vkDestroySampler(*device_, samp_linear_, nullptr);
    samp_linear_ = nullptr;
  }
  if (blit_vertex_) {
    vkDestroyShaderModule(*device_, blit_vertex_, nullptr);
    blit_vertex_ = nullptr;
  }
  if (blit_color_) {
    vkDestroyShaderModule(*device_, blit_color_, nullptr);
    blit_color_ = nullptr;
  }
  if (blit_depth_) {
    vkDestroyShaderModule(*device_, blit_depth_, nullptr);
    blit_depth_ = nullptr;
  }
  if (pipeline_color_) {
    vkDestroyPipeline(*device_, pipeline_color_, nullptr);
    pipeline_color_ = nullptr;
  }
  if (pipeline_depth_) {
    vkDestroyPipeline(*device_, pipeline_depth_, nullptr);
    pipeline_depth_ = nullptr;
  }
  if (pipeline_layout_) {
    vkDestroyPipelineLayout(*device_, pipeline_layout_, nullptr);
    pipeline_layout_ = nullptr;
  }
  if (descriptor_set_layout_) {
    vkDestroyDescriptorSetLayout(*device_, descriptor_set_layout_, nullptr);
    descriptor_set_layout_ = nullptr;
  }
  for (auto& pipeline : pipelines_) {
    vkDestroyPipeline(*device_, pipeline.second, nullptr);
  }
  pipelines_.clear();

  for (auto& pass : render_passes_) {
    vkDestroyRenderPass(*device_, pass.second, nullptr);
  }
  render_passes_.clear();
}

void Blitter::Scavenge() {
  if (descriptor_pool_->has_open_batch()) {
    descriptor_pool_->EndBatch();
  }

  descriptor_pool_->Scavenge();
}

void Blitter::BlitTexture2D(VkCommandBuffer command_buffer, VkFence fence,
                            VkImageView src_image_view, VkRect2D src_rect,
                            VkExtent2D src_extents, VkFormat dst_image_format,
                            VkRect2D dst_rect, VkExtent2D dst_extents,
                            VkFramebuffer dst_framebuffer, VkViewport viewport,
                            VkRect2D scissor, VkFilter filter,
                            bool color_or_depth, bool swap_channels) {
  // Do we need a full draw, or can we cheap out with a blit command?
  bool full_draw = swap_channels || true;
  if (full_draw) {
    if (!descriptor_pool_->has_open_batch()) {
      descriptor_pool_->BeginBatch(fence);
    }

    // Acquire a render pass.
    auto render_pass = GetRenderPass(dst_image_format, color_or_depth);
    VkRenderPassBeginInfo render_pass_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        nullptr,
        render_pass,
        dst_framebuffer,
        {{0, 0}, dst_extents},
        0,
        nullptr,
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    // Acquire a pipeline.
    auto pipeline =
        GetPipeline(render_pass, color_or_depth ? blit_color_ : blit_depth_,
                    color_or_depth);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);

    // Acquire and update a descriptor set for this image.
    auto set = descriptor_pool_->AcquireEntry(descriptor_set_layout_);
    if (!set) {
      assert_always();
      descriptor_pool_->CancelBatch();
      return;
    }

    VkWriteDescriptorSet write;
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = nullptr;
    write.dstSet = set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorImageInfo image;
    image.sampler = filter == VK_FILTER_NEAREST ? samp_nearest_ : samp_linear_;
    image.imageView = src_image_view;
    image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    write.pImageInfo = &image;
    write.pBufferInfo = nullptr;
    write.pTexelBufferView = nullptr;
    vkUpdateDescriptorSets(*device_, 1, &write, 0, nullptr);

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &set, 0, nullptr);

    VtxPushConstants vtx_constants = {
        {
            float(src_rect.offset.x) / src_extents.width,
            float(src_rect.offset.y) / src_extents.height,
            float(src_rect.extent.width) / src_extents.width,
            float(src_rect.extent.height) / src_extents.height,
        },
        {
            float(dst_rect.offset.x) / dst_extents.width,
            float(dst_rect.offset.y) / dst_extents.height,
            float(dst_rect.extent.width) / dst_extents.width,
            float(dst_rect.extent.height) / dst_extents.height,
        },
    };
    vkCmdPushConstants(command_buffer, pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VtxPushConstants),
                       &vtx_constants);

    PixPushConstants pix_constants = {
        0,
        0,
        0,
        swap_channels ? 1 : 0,
    };
    vkCmdPushConstants(command_buffer, pipeline_layout_,
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(VtxPushConstants),
                       sizeof(PixPushConstants), &pix_constants);

    vkCmdDraw(command_buffer, 4, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
  }
}

void Blitter::CopyColorTexture2D(VkCommandBuffer command_buffer, VkFence fence,
                                 VkImage src_image, VkImageView src_image_view,
                                 VkOffset2D src_offset, VkImage dst_image,
                                 VkImageView dst_image_view, VkExtent2D extents,
                                 VkFilter filter, bool swap_channels) {}

void Blitter::CopyDepthTexture(VkCommandBuffer command_buffer, VkFence fence,
                               VkImage src_image, VkImageView src_image_view,
                               VkOffset2D src_offset, VkImage dst_image,
                               VkImageView dst_image_view, VkExtent2D extents) {
}

VkRenderPass Blitter::GetRenderPass(VkFormat format, bool color_or_depth) {
  auto pass = render_passes_.find(format);
  if (pass != render_passes_.end()) {
    return pass->second;
  }

  // Create and cache the render pass.
  VkRenderPass render_pass = CreateRenderPass(format, color_or_depth);
  if (render_pass) {
    render_passes_[format] = render_pass;
  }

  return render_pass;
}

VkPipeline Blitter::GetPipeline(VkRenderPass render_pass,
                                VkShaderModule frag_shader,
                                bool color_or_depth) {
  auto it = pipelines_.find(std::make_pair(render_pass, frag_shader));
  if (it != pipelines_.end()) {
    return it->second;
  }

  // Create and cache the pipeline.
  VkPipeline pipeline =
      CreatePipeline(render_pass, frag_shader, color_or_depth);
  if (pipeline) {
    pipelines_[std::make_pair(render_pass, frag_shader)] = pipeline;
  }

  return pipeline;
}

VkRenderPass Blitter::CreateRenderPass(VkFormat output_format,
                                       bool color_or_depth) {
  VkAttachmentDescription attachments[1];
  std::memset(attachments, 0, sizeof(attachments));

  // Output attachment
  attachments[0].flags = 0;
  attachments[0].format = output_format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].initialLayout =
      color_or_depth ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                     : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachments[0].finalLayout = attachments[0].initialLayout;

  VkAttachmentReference attach_refs[1];
  attach_refs[0].attachment = 0;
  attach_refs[0].layout =
      color_or_depth ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                     : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {
      0,       VK_PIPELINE_BIND_POINT_GRAPHICS,
      0,       nullptr,
      0,       nullptr,
      nullptr, nullptr,
      0,       nullptr,
  };

  if (color_or_depth) {
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = attach_refs;
  } else {
    subpass.pDepthStencilAttachment = attach_refs;
  }

  VkRenderPassCreateInfo renderpass_info = {
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      nullptr,
      0,
      1,
      attachments,
      1,
      &subpass,
      0,
      nullptr,
  };
  VkRenderPass renderpass = nullptr;
  VkResult result =
      vkCreateRenderPass(*device_, &renderpass_info, nullptr, &renderpass);
  CheckResult(result, "vkCreateRenderPass");

  return renderpass;
}

VkPipeline Blitter::CreatePipeline(VkRenderPass render_pass,
                                   VkShaderModule frag_shader,
                                   bool color_or_depth) {
  VkResult result = VK_SUCCESS;

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
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag_shader;
  stages[1].pName = "main";

  pipeline_info.pStages = stages;

  // Vertex input
  VkPipelineVertexInputStateCreateInfo vtx_state;
  std::memset(&vtx_state, 0, sizeof(vtx_state));
  vtx_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vtx_state.flags = 0;
  vtx_state.vertexAttributeDescriptionCount = 0;
  vtx_state.pVertexAttributeDescriptions = nullptr;
  vtx_state.vertexBindingDescriptionCount = 0;
  vtx_state.pVertexBindingDescriptions = nullptr;

  pipeline_info.pVertexInputState = &vtx_state;

  // Input Assembly
  VkPipelineInputAssemblyStateCreateInfo input_info;
  input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_info.pNext = nullptr;
  input_info.flags = 0;
  input_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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
  rasterization_info.cullMode = VK_CULL_MODE_NONE;
  rasterization_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
  VkPipelineDepthStencilStateCreateInfo depth_info = {
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      nullptr,
      0,
      VK_TRUE,
      VK_TRUE,
      VK_COMPARE_OP_ALWAYS,
      VK_FALSE,
      VK_FALSE,
      {},
      {},
      0.f,
      1.f,
  };
  pipeline_info.pDepthStencilState = &depth_info;
  VkPipelineColorBlendStateCreateInfo blend_info;
  blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend_info.pNext = nullptr;
  blend_info.flags = 0;
  blend_info.logicOpEnable = VK_FALSE;
  blend_info.logicOp = VK_LOGIC_OP_NO_OP;

  VkPipelineColorBlendAttachmentState blend_attachments[1];
  if (color_or_depth) {
    blend_attachments[0].blendEnable = VK_FALSE;
    blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachments[0].colorWriteMask = 0xF;

    blend_info.attachmentCount =
        static_cast<uint32_t>(xe::countof(blend_attachments));
    blend_info.pAttachments = blend_attachments;
  } else {
    blend_info.attachmentCount = 0;
    blend_info.pAttachments = nullptr;
  }

  std::memset(blend_info.blendConstants, 0, sizeof(blend_info.blendConstants));
  pipeline_info.pColorBlendState = &blend_info;
  VkPipelineDynamicStateCreateInfo dynamic_state_info;
  dynamic_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_info.pNext = nullptr;
  dynamic_state_info.flags = 0;
  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  dynamic_state_info.dynamicStateCount =
      static_cast<uint32_t>(xe::countof(dynamic_states));
  dynamic_state_info.pDynamicStates = dynamic_states;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = nullptr;
  pipeline_info.basePipelineIndex = -1;

  VkPipeline pipeline = nullptr;
  result = vkCreateGraphicsPipelines(*device_, nullptr, 1, &pipeline_info,
                                     nullptr, &pipeline);
  CheckResult(result, "vkCreateGraphicsPipelines");

  return pipeline;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe