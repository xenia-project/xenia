/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/pipeline_cache.h"

#include "third_party/xxhash/xxhash.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

PipelineCache::PipelineCache(
    RegisterFile* register_file, ui::vulkan::VulkanDevice* device,
    VkDescriptorSetLayout uniform_descriptor_set_layout,
    VkDescriptorSetLayout texture_descriptor_set_layout)
    : register_file_(register_file), device_(*device) {
  // Initialize the shared driver pipeline cache.
  // We'll likely want to serialize this and reuse it, if that proves to be
  // useful. If the shaders are expensive and this helps we could do it per
  // game, otherwise a single shared cache for render state/etc.
  VkPipelineCacheCreateInfo pipeline_cache_info;
  pipeline_cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  pipeline_cache_info.pNext = nullptr;
  pipeline_cache_info.flags = 0;
  pipeline_cache_info.initialDataSize = 0;
  pipeline_cache_info.pInitialData = nullptr;
  auto err = vkCreatePipelineCache(device_, &pipeline_cache_info, nullptr,
                                   &pipeline_cache_);
  CheckResult(err, "vkCreatePipelineCache");

  // Descriptors used by the pipelines.
  // These are the only ones we can ever bind.
  VkDescriptorSetLayout set_layouts[] = {
      // Per-draw constant register uniforms.
      uniform_descriptor_set_layout,
      // All texture bindings.
      texture_descriptor_set_layout,
  };

  // Push constants used for draw parameters.
  // We need to keep these under 128b across all stages.
  VkPushConstantRange push_constant_ranges[2];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_ranges[0].offset = 0;
  push_constant_ranges[0].size = sizeof(float) * 16;
  push_constant_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_constant_ranges[1].offset = sizeof(float) * 16;
  push_constant_ranges[1].size = sizeof(int);

  // Shared pipeline layout.
  VkPipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.pNext = nullptr;
  pipeline_layout_info.flags = 0;
  pipeline_layout_info.setLayoutCount =
      static_cast<uint32_t>(xe::countof(set_layouts));
  pipeline_layout_info.pSetLayouts = set_layouts;
  pipeline_layout_info.pushConstantRangeCount =
      static_cast<uint32_t>(xe::countof(push_constant_ranges));
  pipeline_layout_info.pPushConstantRanges = push_constant_ranges;
  err = vkCreatePipelineLayout(*device, &pipeline_layout_info, nullptr,
                               &pipeline_layout_);
  CheckResult(err, "vkCreatePipelineLayout");
}

PipelineCache::~PipelineCache() {
  vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
  vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);

  // Destroy all shaders.
  for (auto it : shader_map_) {
    delete it.second;
  }
  shader_map_.clear();
}

VulkanShader* PipelineCache::LoadShader(ShaderType shader_type,
                                        uint32_t guest_address,
                                        const uint32_t* host_address,
                                        uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  uint64_t data_hash = XXH64(host_address, dword_count * sizeof(uint32_t), 0);
  auto it = shader_map_.find(data_hash);
  if (it != shader_map_.end()) {
    // Shader has been previously loaded.
    return it->second;
  }

  // Always create the shader and stash it away.
  // We need to track it even if it fails translation so we know not to try
  // again.
  VulkanShader* shader = new VulkanShader(device_, shader_type, data_hash,
                                          host_address, dword_count);
  shader_map_.insert({data_hash, shader});

  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_.Translate(shader)) {
    XELOGE("Shader translation failed; marking shader as ignored");
    return shader;
  }

  // Prepare the shader for use (creates our VkShaderModule).
  // It could still fail at this point.
  if (!shader->Prepare()) {
    XELOGE("Shader preparation failed; marking shader as ignored");
    return shader;
  }

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader at 0x%.8X (%db):\n%s",
             shader_type == ShaderType::kVertex ? "vertex" : "pixel",
             guest_address, dword_count * 4,
             shader->ucode_disassembly().c_str());
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "vk");
  }

  return shader;
}

bool PipelineCache::ConfigurePipeline(VkCommandBuffer command_buffer,
                                      const RenderState* render_state,
                                      VulkanShader* vertex_shader,
                                      VulkanShader* pixel_shader,
                                      PrimitiveType primitive_type) {
  // Uh, yeah. This happened.

  VkPipelineShaderStageCreateInfo pipeline_stages[3];
  uint32_t pipeline_stage_count = 0;
  auto& vertex_pipeline_stage = pipeline_stages[pipeline_stage_count++];
  vertex_pipeline_stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertex_pipeline_stage.pNext = nullptr;
  vertex_pipeline_stage.flags = 0;
  vertex_pipeline_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertex_pipeline_stage.module = vertex_shader->shader_module();
  vertex_pipeline_stage.pName = "main";
  vertex_pipeline_stage.pSpecializationInfo = nullptr;
  auto geometry_shader = GetGeometryShader(primitive_type);
  if (geometry_shader) {
    auto& geometry_pipeline_stage = pipeline_stages[pipeline_stage_count++];
    geometry_pipeline_stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geometry_pipeline_stage.pNext = nullptr;
    geometry_pipeline_stage.flags = 0;
    geometry_pipeline_stage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geometry_pipeline_stage.module = geometry_shader;
    geometry_pipeline_stage.pName = "main";
    geometry_pipeline_stage.pSpecializationInfo = nullptr;
  }
  auto& pixel_pipeline_stage = pipeline_stages[pipeline_stage_count++];
  pixel_pipeline_stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pixel_pipeline_stage.pNext = nullptr;
  pixel_pipeline_stage.flags = 0;
  pixel_pipeline_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  pixel_pipeline_stage.module = pixel_shader->shader_module();
  pixel_pipeline_stage.pName = "main";
  pixel_pipeline_stage.pSpecializationInfo = nullptr;

  VkPipelineVertexInputStateCreateInfo vertex_state_info;
  vertex_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_state_info.pNext = nullptr;
  VkVertexInputBindingDescription vertex_binding_descrs[64];
  uint32_t vertex_binding_count = 0;
  VkVertexInputAttributeDescription vertex_attrib_descrs[64];
  uint32_t vertex_attrib_count = 0;
  for (const auto& vertex_binding : vertex_shader->vertex_bindings()) {
    assert_true(vertex_binding_count < xe::countof(vertex_binding_descrs));
    auto& vertex_binding_descr = vertex_binding_descrs[vertex_binding_count++];
    vertex_binding_descr.binding = vertex_binding.binding_index;
    vertex_binding_descr.stride = vertex_binding.stride_words * 4;
    vertex_binding_descr.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    for (const auto& attrib : vertex_binding.attributes) {
      assert_true(vertex_attrib_count < xe::countof(vertex_attrib_descrs));
      auto& vertex_attrib_descr = vertex_attrib_descrs[vertex_attrib_count++];
      vertex_attrib_descr.location = attrib.attrib_index;
      vertex_attrib_descr.binding = vertex_binding.binding_index;
      vertex_attrib_descr.format = VK_FORMAT_UNDEFINED;
      vertex_attrib_descr.offset = attrib.fetch_instr.attributes.offset * 4;

      bool is_signed = attrib.fetch_instr.attributes.is_signed;
      bool is_integer = attrib.fetch_instr.attributes.is_integer;
      switch (attrib.fetch_instr.attributes.data_format) {
        case VertexFormat::k_8_8_8_8:
          vertex_attrib_descr.format =
              is_signed ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_UNORM;
          break;
        case VertexFormat::k_2_10_10_10:
          vertex_attrib_descr.format = is_signed
                                           ? VK_FORMAT_A2R10G10B10_SNORM_PACK32
                                           : VK_FORMAT_A2R10G10B10_UNORM_PACK32;
          break;
        case VertexFormat::k_10_11_11:
          assert_always("unsupported?");
          vertex_attrib_descr.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
          break;
        case VertexFormat::k_11_11_10:
          assert_true(is_signed);
          vertex_attrib_descr.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
          break;
        case VertexFormat::k_16_16:
          vertex_attrib_descr.format =
              is_signed ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_UNORM;
          break;
        case VertexFormat::k_16_16_FLOAT:
          vertex_attrib_descr.format =
              is_signed ? VK_FORMAT_R16G16_SSCALED : VK_FORMAT_R16G16_USCALED;
          break;
        case VertexFormat::k_16_16_16_16:
          vertex_attrib_descr.format = is_signed ? VK_FORMAT_R16G16B16A16_SNORM
                                                 : VK_FORMAT_R16G16B16A16_UNORM;
          break;
        case VertexFormat::k_16_16_16_16_FLOAT:
          vertex_attrib_descr.format = is_signed
                                           ? VK_FORMAT_R16G16B16A16_SSCALED
                                           : VK_FORMAT_R16G16B16A16_USCALED;
          break;
        case VertexFormat::k_32:
          vertex_attrib_descr.format =
              is_signed ? VK_FORMAT_R32_SINT : VK_FORMAT_R32_UINT;
          break;
        case VertexFormat::k_32_32:
          vertex_attrib_descr.format =
              is_signed ? VK_FORMAT_R32G32_SINT : VK_FORMAT_R32G32_UINT;
          break;
        case VertexFormat::k_32_32_32_32:
          vertex_attrib_descr.format =
              is_signed ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32_UINT;
          break;
        case VertexFormat::k_32_FLOAT:
          assert_true(is_signed);
          vertex_attrib_descr.format = VK_FORMAT_R32_SFLOAT;
          break;
        case VertexFormat::k_32_32_FLOAT:
          assert_true(is_signed);
          vertex_attrib_descr.format = VK_FORMAT_R32G32_SFLOAT;
          break;
        case VertexFormat::k_32_32_32_FLOAT:
          assert_true(is_signed);
          vertex_attrib_descr.format = VK_FORMAT_R32G32B32_SFLOAT;
          break;
        case VertexFormat::k_32_32_32_32_FLOAT:
          assert_true(is_signed);
          vertex_attrib_descr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
          break;
        default:
          assert_unhandled_case(attrib.fetch_instr.attributes.data_format);
          break;
      }
    }
  }
  vertex_state_info.vertexBindingDescriptionCount = vertex_binding_count;
  vertex_state_info.pVertexBindingDescriptions = vertex_binding_descrs;
  vertex_state_info.vertexAttributeDescriptionCount = vertex_attrib_count;
  vertex_state_info.pVertexAttributeDescriptions = vertex_attrib_descrs;

  VkPipelineInputAssemblyStateCreateInfo input_info;
  input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_info.pNext = nullptr;
  input_info.flags = 0;
  input_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_info.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewport_state_info;
  viewport_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state_info.pNext = nullptr;
  viewport_state_info.flags = 0;
  VkViewport viewport;
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = 100;
  viewport.height = 100;
  viewport.minDepth = 0;
  viewport.maxDepth = 1;
  viewport_state_info.viewportCount = 1;
  viewport_state_info.pViewports = &viewport;
  VkRect2D scissor;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = 100;
  scissor.extent.height = 100;
  viewport_state_info.scissorCount = 1;
  viewport_state_info.pScissors = &scissor;

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

  VkPipelineDepthStencilStateCreateInfo depth_stencil_info;
  depth_stencil_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil_info.pNext = nullptr;
  depth_stencil_info.flags = 0;
  depth_stencil_info.depthTestEnable = VK_FALSE;
  depth_stencil_info.depthWriteEnable = VK_FALSE;
  depth_stencil_info.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  depth_stencil_info.depthBoundsTestEnable = VK_FALSE;
  depth_stencil_info.stencilTestEnable = VK_FALSE;
  depth_stencil_info.front.failOp = VK_STENCIL_OP_KEEP;
  depth_stencil_info.front.passOp = VK_STENCIL_OP_KEEP;
  depth_stencil_info.front.depthFailOp = VK_STENCIL_OP_KEEP;
  depth_stencil_info.front.compareOp = VK_COMPARE_OP_ALWAYS;
  depth_stencil_info.front.compareMask = 0;
  depth_stencil_info.front.writeMask = 0;
  depth_stencil_info.front.reference = 0;
  depth_stencil_info.back.failOp = VK_STENCIL_OP_KEEP;
  depth_stencil_info.back.passOp = VK_STENCIL_OP_KEEP;
  depth_stencil_info.back.depthFailOp = VK_STENCIL_OP_KEEP;
  depth_stencil_info.back.compareOp = VK_COMPARE_OP_ALWAYS;
  depth_stencil_info.back.compareMask = 0;
  depth_stencil_info.back.writeMask = 0;
  depth_stencil_info.back.reference = 0;
  depth_stencil_info.minDepthBounds = 0;
  depth_stencil_info.maxDepthBounds = 0;

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

  VkPipelineDynamicStateCreateInfo dynamic_state_info;
  dynamic_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_info.pNext = nullptr;
  dynamic_state_info.flags = 0;
  // VkDynamicState dynamic_states[] = {
  //    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
  //};
  // dynamic_state_info.dynamicStateCount =
  //    static_cast<uint32_t>(xe::countof(dynamic_states));
  // dynamic_state_info.pDynamicStates = dynamic_states;
  dynamic_state_info.dynamicStateCount = 0;
  dynamic_state_info.pDynamicStates = nullptr;

  VkGraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = nullptr;
  pipeline_info.flags = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
  pipeline_info.stageCount = pipeline_stage_count;
  pipeline_info.pStages = pipeline_stages;
  pipeline_info.pVertexInputState = &vertex_state_info;
  pipeline_info.pInputAssemblyState = &input_info;
  pipeline_info.pTessellationState = nullptr;
  pipeline_info.pViewportState = &viewport_state_info;
  pipeline_info.pRasterizationState = &rasterization_info;
  pipeline_info.pMultisampleState = &multisample_info;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pColorBlendState = &blend_info;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_state->render_pass_handle;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = nullptr;
  pipeline_info.basePipelineIndex = 0;

  VkPipeline pipeline = nullptr;
  auto err = vkCreateGraphicsPipelines(device_, nullptr, 1, &pipeline_info,
                                       nullptr, &pipeline);
  CheckResult(err, "vkCreateGraphicsPipelines");

  // TODO(benvanik): don't leak pipelines >_>
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  return true;
}

void PipelineCache::ClearCache() {
  // TODO(benvanik): caching.
}

VkShaderModule PipelineCache::GetGeometryShader(PrimitiveType primitive_type) {
  switch (primitive_type) {
    case PrimitiveType::kLineList:
    case PrimitiveType::kLineStrip:
    case PrimitiveType::kTriangleList:
    case PrimitiveType::kTriangleFan:
    case PrimitiveType::kTriangleStrip:
      // Supported directly - no need to emulate.
      return nullptr;
    case PrimitiveType::kPointList:
      // TODO(benvanik): point list geometry shader.
      return nullptr;
    case PrimitiveType::kUnknown0x07:
      assert_always("Unknown geometry type");
      return nullptr;
    case PrimitiveType::kRectangleList:
      // TODO(benvanik): rectangle list geometry shader.
      return nullptr;
    case PrimitiveType::kLineLoop:
      // TODO(benvanik): line loop geometry shader.
      return nullptr;
    case PrimitiveType::kQuadList:
      // TODO(benvanik): quad list geometry shader.
      return nullptr;
    case PrimitiveType::kQuadStrip:
      // TODO(benvanik): quad strip geometry shader.
      return nullptr;
    default:
      assert_unhandled_case(primitive_type);
      return nullptr;
  }
}

bool PipelineCache::SetShadowRegister(uint32_t* dest, uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

bool PipelineCache::SetShadowRegister(float* dest, uint32_t register_name) {
  float value = register_file_->values[register_name].f32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

PipelineCache::UpdateStatus PipelineCache::UpdateShaders(
    PrimitiveType prim_type) {
  auto& regs = update_shaders_regs_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 ==
                  0x000FF000 ||
              register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 ==
                  0x000FF100 ||
              register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.sq_program_cntl, XE_GPU_REG_SQ_PROGRAM_CNTL);
  dirty |= SetShadowRegister(&regs.sq_context_misc, XE_GPU_REG_SQ_CONTEXT_MISC);
  // dirty |= regs.vertex_shader != active_vertex_shader_;
  // dirty |= regs.pixel_shader != active_pixel_shader_;
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }
  // regs.vertex_shader = static_cast<VulkanShader*>(active_vertex_shader_);
  // regs.pixel_shader = static_cast<VulkanShader*>(active_pixel_shader_);
  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRenderTargets() {
  auto& regs = update_render_targets_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_modecontrol, XE_GPU_REG_RB_MODECONTROL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_info, XE_GPU_REG_RB_COLOR_INFO);
  dirty |= SetShadowRegister(&regs.rb_color1_info, XE_GPU_REG_RB_COLOR1_INFO);
  dirty |= SetShadowRegister(&regs.rb_color2_info, XE_GPU_REG_RB_COLOR2_INFO);
  dirty |= SetShadowRegister(&regs.rb_color3_info, XE_GPU_REG_RB_COLOR3_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_mask, XE_GPU_REG_RB_COLOR_MASK);
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  dirty |= SetShadowRegister(&regs.rb_depth_info, XE_GPU_REG_RB_DEPTH_INFO);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateState(
    PrimitiveType prim_type) {
  bool mismatch = false;

#define CHECK_UPDATE_STATUS(status, mismatch, error_message) \
  {                                                          \
    if (status == UpdateStatus::kError) {                    \
      XELOGE(error_message);                                 \
      return status;                                         \
    } else if (status == UpdateStatus::kMismatch) {          \
      mismatch = true;                                       \
    }                                                        \
  }

  UpdateStatus status;
  status = UpdateViewportState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update viewport state");
  status = UpdateRasterizerState(prim_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterizer state");
  status = UpdateBlendState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update blend state");
  status = UpdateDepthStencilState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateViewportState() {
  auto& regs = update_viewport_state_regs_;

  bool dirty = false;
  // dirty |= SetShadowRegister(&state_regs.pa_cl_clip_cntl,
  //     XE_GPU_REG_PA_CL_CLIP_CNTL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.pa_cl_vte_cntl, XE_GPU_REG_PA_CL_VTE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_offset,
                             XE_GPU_REG_PA_SC_WINDOW_OFFSET);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_tl,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_br,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_xoffset,
                             XE_GPU_REG_PA_CL_VPORT_XOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_yoffset,
                             XE_GPU_REG_PA_CL_VPORT_YOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_zoffset,
                             XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_xscale,
                             XE_GPU_REG_PA_CL_VPORT_XSCALE);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_yscale,
                             XE_GPU_REG_PA_CL_VPORT_YSCALE);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_zscale,
                             XE_GPU_REG_PA_CL_VPORT_ZSCALE);

  // Much of this state machine is extracted from:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf

  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // VTX_XY_FMT = true: the incoming X, Y have already been multiplied by 1/W0.
  //            = false: multiply the X, Y coordinates by 1/W0.
  // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //           = false: multiply the Z coordinate by 1/W0.
  // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
  //                    get 1/W0.
  // draw_batcher_.set_vtx_fmt((regs.pa_cl_vte_cntl >> 8) & 0x1 ? 1.0f : 0.0f,
  //                          (regs.pa_cl_vte_cntl >> 9) & 0x1 ? 1.0f : 0.0f,
  //                          (regs.pa_cl_vte_cntl >> 10) & 0x1 ? 1.0f : 0.0f);

  // Done in VS, no need to flush state.
  // if ((regs.pa_cl_vte_cntl & (1 << 0)) > 0) {
  //  draw_batcher_.set_window_scalar(1.0f, 1.0f);
  //} else {
  //  draw_batcher_.set_window_scalar(1.0f / 2560.0f, -1.0f / 2560.0f);
  //}

  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRasterizerState(
    PrimitiveType prim_type) {
  auto& regs = update_rasterizer_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_tl,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_br,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.multi_prim_ib_reset_index,
                             XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX);
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateBlendState() {
  auto& reg_file = *register_file_;
  auto& regs = update_blend_state_regs_;

  // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
  // Deprecated in GL, implemented in shader.
  // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
  // uint32_t color_control = reg_file[XE_GPU_REG_RB_COLORCONTROL].u32;
  // draw_batcher_.set_alpha_test((color_control & 0x4) != 0,  //
  // ALPAHTESTENABLE
  //                             color_control & 0x7,         // ALPHAFUNC
  //                             reg_file[XE_GPU_REG_RB_ALPHA_REF].f32);

  bool dirty = false;
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[0], XE_GPU_REG_RB_BLENDCONTROL_0);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[1], XE_GPU_REG_RB_BLENDCONTROL_1);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[2], XE_GPU_REG_RB_BLENDCONTROL_2);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[3], XE_GPU_REG_RB_BLENDCONTROL_3);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[0], XE_GPU_REG_RB_BLEND_RED);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[1], XE_GPU_REG_RB_BLEND_GREEN);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[2], XE_GPU_REG_RB_BLEND_BLUE);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[3], XE_GPU_REG_RB_BLEND_ALPHA);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateDepthStencilState() {
  auto& regs = update_depth_stencil_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
