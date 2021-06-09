/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanPipelineCache::VulkanPipelineCache(
    VulkanCommandProcessor& command_processor,
    const RegisterFile& register_file,
    VulkanRenderTargetCache& render_target_cache)
    : command_processor_(command_processor),
      register_file_(register_file),
      render_target_cache_(render_target_cache) {}

VulkanPipelineCache::~VulkanPipelineCache() { Shutdown(); }

bool VulkanPipelineCache::Initialize() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();

  device_pipeline_features_.features = 0;
  // TODO(Triang3l): Support the portability subset.
  device_pipeline_features_.point_polygons = 1;
  device_pipeline_features_.triangle_fans = 1;

  shader_translator_ = std::make_unique<SpirvShaderTranslator>(
      SpirvShaderTranslator::Features(provider));

  return true;
}

void VulkanPipelineCache::Shutdown() {
  ClearCache();

  shader_translator_.reset();
}

void VulkanPipelineCache::ClearCache() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  last_pipeline_ = nullptr;
  for (const auto& pipeline_pair : pipelines_) {
    if (pipeline_pair.second.pipeline != VK_NULL_HANDLE) {
      dfn.vkDestroyPipeline(device, pipeline_pair.second.pipeline, nullptr);
    }
  }
  pipelines_.clear();

  for (auto it : shaders_) {
    delete it.second;
  }
  shaders_.clear();
}

VulkanShader* VulkanPipelineCache::LoadShader(xenos::ShaderType shader_type,
                                              const uint32_t* host_address,
                                              uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  uint64_t data_hash =
      XXH3_64bits(host_address, dword_count * sizeof(uint32_t));
  auto it = shaders_.find(data_hash);
  if (it != shaders_.end()) {
    // Shader has been previously loaded.
    return it->second;
  }
  // Always create the shader and stash it away.
  // We need to track it even if it fails translation so we know not to try
  // again.
  VulkanShader* shader = new VulkanShader(
      shader_type, data_hash, host_address, dword_count,
      command_processor_.GetVulkanContext().GetVulkanProvider());
  shaders_.emplace(data_hash, shader);
  return shader;
}

SpirvShaderTranslator::Modification
VulkanPipelineCache::GetCurrentVertexShaderModification(
    const Shader& shader,
    Shader::HostVertexShaderType host_vertex_shader_type) const {
  assert_true(shader.type() == xenos::ShaderType::kVertex);
  assert_true(shader.is_ucode_analyzed());
  const auto& regs = register_file_;
  auto sq_program_cntl = regs.Get<reg::SQ_PROGRAM_CNTL>();
  return SpirvShaderTranslator::Modification(
      shader_translator_->GetDefaultVertexShaderModification(
          shader.GetDynamicAddressableRegisterCount(sq_program_cntl.vs_num_reg),
          host_vertex_shader_type));
}

SpirvShaderTranslator::Modification
VulkanPipelineCache::GetCurrentPixelShaderModification(
    const Shader& shader) const {
  assert_true(shader.type() == xenos::ShaderType::kPixel);
  assert_true(shader.is_ucode_analyzed());
  const auto& regs = register_file_;
  auto sq_program_cntl = regs.Get<reg::SQ_PROGRAM_CNTL>();
  return SpirvShaderTranslator::Modification(
      shader_translator_->GetDefaultPixelShaderModification(
          shader.GetDynamicAddressableRegisterCount(
              sq_program_cntl.ps_num_reg)));
}

bool VulkanPipelineCache::ConfigurePipeline(
    VulkanShader::VulkanTranslation* vertex_shader,
    VulkanShader::VulkanTranslation* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    VulkanRenderTargetCache::RenderPassKey render_pass_key,
    VkPipeline& pipeline_out,
    const PipelineLayoutProvider*& pipeline_layout_out) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  // Ensure shaders are translated - needed now for GetCurrentStateDescription.
  // Edge flags are not supported yet (because polygon primitives are not).
  assert_true(register_file_.Get<reg::SQ_PROGRAM_CNTL>().vs_export_mode !=
                  xenos::VertexShaderExportMode::kPosition2VectorsEdge &&
              register_file_.Get<reg::SQ_PROGRAM_CNTL>().vs_export_mode !=
                  xenos::VertexShaderExportMode::kPosition2VectorsEdgeKill);
  assert_false(register_file_.Get<reg::SQ_PROGRAM_CNTL>().gen_index_vtx);
  if (!vertex_shader->is_translated()) {
    vertex_shader->shader().AnalyzeUcode(ucode_disasm_buffer_);
    if (!TranslateAnalyzedShader(*shader_translator_, *vertex_shader)) {
      XELOGE("Failed to translate the vertex shader!");
      return false;
    }
  }
  if (!vertex_shader->is_valid()) {
    // Translation attempted previously, but not valid.
    return false;
  }
  if (pixel_shader != nullptr) {
    if (!pixel_shader->is_translated()) {
      pixel_shader->shader().AnalyzeUcode(ucode_disasm_buffer_);
      if (!TranslateAnalyzedShader(*shader_translator_, *pixel_shader)) {
        XELOGE("Failed to translate the pixel shader!");
        return false;
      }
    }
    if (!pixel_shader->is_valid()) {
      // Translation attempted previously, but not valid.
      return false;
    }
  }

  PipelineDescription description;
  if (!GetCurrentStateDescription(vertex_shader, pixel_shader,
                                  primitive_processing_result, render_pass_key,
                                  description)) {
    return false;
  }
  if (last_pipeline_ && last_pipeline_->first == description) {
    pipeline_out = last_pipeline_->second.pipeline;
    pipeline_layout_out = last_pipeline_->second.pipeline_layout;
    return true;
  }
  auto it = pipelines_.find(description);
  if (it != pipelines_.end()) {
    last_pipeline_ = &*it;
    pipeline_out = it->second.pipeline;
    pipeline_layout_out = it->second.pipeline_layout;
    return true;
  }

  // Create the pipeline if not the latest and not already existing.
  const PipelineLayoutProvider* pipeline_layout =
      command_processor_.GetPipelineLayout(0, 0);
  if (!pipeline_layout) {
    return false;
  }
  VkRenderPass render_pass =
      render_target_cache_.GetRenderPass(render_pass_key);
  if (render_pass == VK_NULL_HANDLE) {
    return false;
  }
  PipelineCreationArguments creation_arguments;
  auto& pipeline =
      *pipelines_.emplace(description, Pipeline(pipeline_layout)).first;
  creation_arguments.pipeline = &pipeline;
  creation_arguments.vertex_shader = vertex_shader;
  creation_arguments.pixel_shader = pixel_shader;
  creation_arguments.render_pass = render_pass;
  if (!EnsurePipelineCreated(creation_arguments)) {
    return false;
  }
  pipeline_out = pipeline.second.pipeline;
  pipeline_layout_out = pipeline_layout;
  return true;
}

bool VulkanPipelineCache::TranslateAnalyzedShader(
    SpirvShaderTranslator& translator,
    VulkanShader::VulkanTranslation& translation) {
  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!translator.TranslateAnalyzedShader(translation)) {
    XELOGE("Shader {:016X} translation failed; marking as ignored",
           translation.shader().ucode_data_hash());
    return false;
  }
  return translation.GetOrCreateShaderModule() != VK_NULL_HANDLE;
}

bool VulkanPipelineCache::GetCurrentStateDescription(
    const VulkanShader::VulkanTranslation* vertex_shader,
    const VulkanShader::VulkanTranslation* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    VulkanRenderTargetCache::RenderPassKey render_pass_key,
    PipelineDescription& description_out) const {
  description_out.Reset();

  const RegisterFile& regs = register_file_;
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();

  description_out.vertex_shader_hash =
      vertex_shader->shader().ucode_data_hash();
  description_out.vertex_shader_modification = vertex_shader->modification();
  if (pixel_shader) {
    description_out.pixel_shader_hash =
        pixel_shader->shader().ucode_data_hash();
    description_out.pixel_shader_modification = pixel_shader->modification();
  }
  description_out.render_pass_key = render_pass_key;

  PipelinePrimitiveTopology primitive_topology;
  switch (primitive_processing_result.host_primitive_type) {
    case xenos::PrimitiveType::kPointList:
      primitive_topology = PipelinePrimitiveTopology::kPointList;
      break;
    case xenos::PrimitiveType::kLineList:
      primitive_topology = PipelinePrimitiveTopology::kLineList;
      break;
    case xenos::PrimitiveType::kLineStrip:
      primitive_topology = PipelinePrimitiveTopology::kLineStrip;
      break;
    case xenos::PrimitiveType::kTriangleList:
    case xenos::PrimitiveType::kRectangleList:
      primitive_topology = PipelinePrimitiveTopology::kTriangleList;
      break;
    case xenos::PrimitiveType::kTriangleFan:
      primitive_topology = PipelinePrimitiveTopology::kTriangleFan;
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      primitive_topology = PipelinePrimitiveTopology::kTriangleStrip;
      break;
    case xenos::PrimitiveType::kQuadList:
      primitive_topology = PipelinePrimitiveTopology::kLineListWithAdjacency;
      break;
    default:
      // TODO(Triang3l): All primitive types and tessellation.
      return false;
  }
  description_out.primitive_topology = primitive_topology;
  description_out.primitive_restart =
      primitive_processing_result.host_primitive_reset_enabled;

  // TODO(Triang3l): Tessellation.
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  if (primitive_polygonal) {
    // Vulkan only allows the polygon mode to be set for both faces - pick the
    // most special one (more likely to represent the developer's deliberate
    // intentions - fill is very generic, wireframe is common in debug, points
    // are for pretty unusual things, but closer to debug purposes too - on the
    // Xenos, points have the lowest register value and triangles have the
    // highest) based on which faces are not culled.
    bool cull_front = pa_su_sc_mode_cntl.cull_front;
    bool cull_back = pa_su_sc_mode_cntl.cull_back;
    description_out.cull_front = cull_front;
    description_out.cull_back = cull_back;
    xenos::PolygonType polygon_type = xenos::PolygonType::kTriangles;
    if (!cull_front) {
      polygon_type =
          std::min(polygon_type, pa_su_sc_mode_cntl.polymode_front_ptype);
    }
    if (!cull_back) {
      polygon_type =
          std::min(polygon_type, pa_su_sc_mode_cntl.polymode_back_ptype);
    }
    if (pa_su_sc_mode_cntl.poly_mode != xenos::PolygonModeEnable::kDualMode) {
      polygon_type = xenos::PolygonType::kTriangles;
    }
    switch (polygon_type) {
      case xenos::PolygonType::kPoints:
        // When points are not supported, use lines instead, preserving
        // debug-like purpose.
        description_out.polygon_mode = device_pipeline_features_.point_polygons
                                           ? PipelinePolygonMode::kPoint
                                           : PipelinePolygonMode::kLine;
        break;
      case xenos::PolygonType::kLines:
        description_out.polygon_mode = PipelinePolygonMode::kLine;
        break;
      case xenos::PolygonType::kTriangles:
        description_out.polygon_mode = PipelinePolygonMode::kFill;
        break;
      default:
        assert_unhandled_case(polygon_type);
        return false;
    }
    description_out.front_face_clockwise = pa_su_sc_mode_cntl.face != 0;
  } else {
    description_out.polygon_mode = PipelinePolygonMode::kFill;
  }

  return true;
}

bool VulkanPipelineCache::EnsurePipelineCreated(
    const PipelineCreationArguments& creation_arguments) {
  if (creation_arguments.pipeline->second.pipeline != VK_NULL_HANDLE) {
    return true;
  }

  // This function preferably should validate the description to prevent
  // unsupported behavior that may be dangerous/crashing because pipelines can
  // be created from the disk storage.

  if (creation_arguments.pixel_shader) {
    XELOGGPU("Creating graphics pipeline state with VS {:016X}, PS {:016X}",
             creation_arguments.vertex_shader->shader().ucode_data_hash(),
             creation_arguments.pixel_shader->shader().ucode_data_hash());
  } else {
    XELOGGPU("Creating graphics pipeline state with VS {:016X}",
             creation_arguments.vertex_shader->shader().ucode_data_hash());
  }

  const PipelineDescription& description = creation_arguments.pipeline->first;

  VkPipelineShaderStageCreateInfo shader_stages[2];
  uint32_t shader_stage_count = 0;

  assert_true(creation_arguments.vertex_shader->is_translated());
  if (!creation_arguments.vertex_shader->is_valid()) {
    return false;
  }
  assert_true(shader_stage_count < xe::countof(shader_stages));
  VkPipelineShaderStageCreateInfo& shader_stage_vertex =
      shader_stages[shader_stage_count++];
  shader_stage_vertex.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stage_vertex.pNext = nullptr;
  shader_stage_vertex.flags = 0;
  shader_stage_vertex.stage = VK_SHADER_STAGE_VERTEX_BIT;
  shader_stage_vertex.module =
      creation_arguments.vertex_shader->shader_module();
  assert_true(shader_stage_vertex.module != VK_NULL_HANDLE);
  shader_stage_vertex.pName = "main";
  shader_stage_vertex.pSpecializationInfo = nullptr;
  if (creation_arguments.pixel_shader) {
    assert_true(creation_arguments.pixel_shader->is_translated());
    if (!creation_arguments.pixel_shader->is_valid()) {
      return false;
    }
    assert_true(shader_stage_count < xe::countof(shader_stages));
    VkPipelineShaderStageCreateInfo& shader_stage_fragment =
        shader_stages[shader_stage_count++];
    shader_stage_fragment.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_fragment.pNext = nullptr;
    shader_stage_fragment.flags = 0;
    shader_stage_fragment.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stage_fragment.module =
        creation_arguments.pixel_shader->shader_module();
    assert_true(shader_stage_fragment.module != VK_NULL_HANDLE);
    shader_stage_fragment.pName = "main";
    shader_stage_fragment.pSpecializationInfo = nullptr;
  }

  VkPipelineVertexInputStateCreateInfo vertex_input_state;
  vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_state.pNext = nullptr;
  vertex_input_state.flags = 0;
  vertex_input_state.vertexBindingDescriptionCount = 0;
  vertex_input_state.pVertexBindingDescriptions = nullptr;
  vertex_input_state.vertexAttributeDescriptionCount = 0;
  vertex_input_state.pVertexAttributeDescriptions = nullptr;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
  input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.pNext = nullptr;
  input_assembly_state.flags = 0;
  switch (description.primitive_topology) {
    case PipelinePrimitiveTopology::kPointList:
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      assert_false(description.primitive_restart);
      if (description.primitive_restart) {
        return false;
      }
      break;
    case PipelinePrimitiveTopology::kLineList:
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      assert_false(description.primitive_restart);
      if (description.primitive_restart) {
        return false;
      }
      break;
    case PipelinePrimitiveTopology::kLineStrip:
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      break;
    case PipelinePrimitiveTopology::kTriangleList:
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      assert_false(description.primitive_restart);
      if (description.primitive_restart) {
        return false;
      }
      break;
    case PipelinePrimitiveTopology::kTriangleStrip:
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      break;
    case PipelinePrimitiveTopology::kTriangleFan:
      assert_true(device_pipeline_features_.triangle_fans);
      if (!device_pipeline_features_.triangle_fans) {
        return false;
      }
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
      break;
    case PipelinePrimitiveTopology::kLineListWithAdjacency:
      input_assembly_state.topology =
          VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
      assert_false(description.primitive_restart);
      if (description.primitive_restart) {
        return false;
      }
      break;
    case PipelinePrimitiveTopology::kPatchList:
      input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
      assert_false(description.primitive_restart);
      if (description.primitive_restart) {
        return false;
      }
      break;
    default:
      assert_unhandled_case(description.primitive_topology);
      return false;
  }
  input_assembly_state.primitiveRestartEnable =
      description.primitive_restart ? VK_TRUE : VK_FALSE;

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
  switch (description.polygon_mode) {
    case PipelinePolygonMode::kFill:
      rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
      break;
    case PipelinePolygonMode::kLine:
      rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
      break;
    case PipelinePolygonMode::kPoint:
      assert_true(device_pipeline_features_.point_polygons);
      if (!device_pipeline_features_.point_polygons) {
        return false;
      }
      rasterization_state.polygonMode = VK_POLYGON_MODE_POINT;
      break;
    default:
      assert_unhandled_case(description.polygon_mode);
      return false;
  }
  rasterization_state.cullMode = VK_CULL_MODE_NONE;
  if (description.cull_front) {
    rasterization_state.cullMode |= VK_CULL_MODE_FRONT_BIT;
  }
  if (description.cull_back) {
    rasterization_state.cullMode |= VK_CULL_MODE_BACK_BIT;
  }
  rasterization_state.frontFace = description.front_face_clockwise
                                      ? VK_FRONT_FACE_CLOCKWISE
                                      : VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample_state = {};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
  pipeline_create_info.flags = 0;
  pipeline_create_info.stageCount = shader_stage_count;
  pipeline_create_info.pStages = shader_stages;
  pipeline_create_info.pVertexInputState = &vertex_input_state;
  pipeline_create_info.pInputAssemblyState = &input_assembly_state;
  pipeline_create_info.pTessellationState = nullptr;
  pipeline_create_info.pViewportState = &viewport_state;
  pipeline_create_info.pRasterizationState = &rasterization_state;
  pipeline_create_info.pMultisampleState = &multisample_state;
  pipeline_create_info.pDepthStencilState = nullptr;
  pipeline_create_info.pColorBlendState = nullptr;
  pipeline_create_info.pDynamicState = &dynamic_state;
  pipeline_create_info.layout =
      creation_arguments.pipeline->second.pipeline_layout->GetPipelineLayout();
  pipeline_create_info.renderPass = creation_arguments.render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = 0;

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VkPipeline pipeline;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                    &pipeline_create_info, nullptr,
                                    &pipeline) != VK_SUCCESS) {
    // TODO(Triang3l): Move these error messages outside.
    /* if (creation_arguments.pixel_shader) {
      XELOGE(
          "Failed to create graphics pipeline with VS {:016X}, PS {:016X}",
          creation_arguments.vertex_shader->shader().ucode_data_hash(),
          creation_arguments.pixel_shader->shader().ucode_data_hash());
    } else {
      XELOGE("Failed to create graphics pipeline with VS {:016X}",
             creation_arguments.vertex_shader->shader().ucode_data_hash());
    } */
    return false;
  }
  creation_arguments.pipeline->second.pipeline = pipeline;
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
