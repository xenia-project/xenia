/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"

#include <algorithm>
#include <array>
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
      command_processor_.GetVulkanProvider();

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
      command_processor_.GetVulkanProvider();
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
  VulkanShader* shader =
      new VulkanShader(shader_type, data_hash, host_address, dword_count,
                       command_processor_.GetVulkanProvider());
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
    const Shader& shader, uint32_t normalized_color_mask) const {
  assert_true(shader.type() == xenos::ShaderType::kPixel);
  assert_true(shader.is_ucode_analyzed());
  const auto& regs = register_file_;

  auto sq_program_cntl = regs.Get<reg::SQ_PROGRAM_CNTL>();
  SpirvShaderTranslator::Modification modification(
      shader_translator_->GetDefaultPixelShaderModification(
          shader.GetDynamicAddressableRegisterCount(
              sq_program_cntl.ps_num_reg)));

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceFeatures& device_features = provider.device_features();
  if (!device_features.independentBlend) {
    // Since without independent blending, the write mask is common for all
    // attachments, but the render pass may still include the attachments from
    // previous draws (to prevent excessive render pass changes potentially
    // doing stores and loads), disable writing to render targets with a
    // completely empty write mask by removing the output from the shader.
    // Only explicitly excluding render targets that the shader actually writes
    // to, for better pipeline storage compatibility between devices with and
    // without independent blending (so in the usual situation - the shader
    // doesn't write to any render targets disabled via the color mask - no
    // explicit disabling of shader outputs will be needed, and the disabled
    // output mask will be 0).
    uint32_t color_targets_remaining = shader.writes_color_targets();
    uint32_t color_target_index;
    while (xe::bit_scan_forward(color_targets_remaining, &color_target_index)) {
      color_targets_remaining &= ~(uint32_t(1) << color_target_index);
      if (!(normalized_color_mask &
            (uint32_t(0b1111) << (4 * color_target_index)))) {
        modification.pixel.color_outputs_disabled |= uint32_t(1)
                                                     << color_target_index;
      }
    }
  }

  return modification;
}

bool VulkanPipelineCache::ConfigurePipeline(
    VulkanShader::VulkanTranslation* vertex_shader,
    VulkanShader::VulkanTranslation* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    uint32_t normalized_color_mask,
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
  if (!GetCurrentStateDescription(
          vertex_shader, pixel_shader, primitive_processing_result,
          normalized_color_mask, render_pass_key, description)) {
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

void VulkanPipelineCache::WritePipelineRenderTargetDescription(
    reg::RB_BLENDCONTROL blend_control, uint32_t write_mask,
    PipelineRenderTarget& render_target_out) const {
  if (write_mask) {
    assert_zero(write_mask & ~uint32_t(0b1111));
    // 32 because of 0x1F mask, for safety (all unknown to zero).
    static const PipelineBlendFactor kBlendFactorMap[32] = {
        /*  0 */ PipelineBlendFactor::kZero,
        /*  1 */ PipelineBlendFactor::kOne,
        /*  2 */ PipelineBlendFactor::kZero,  // ?
        /*  3 */ PipelineBlendFactor::kZero,  // ?
        /*  4 */ PipelineBlendFactor::kSrcColor,
        /*  5 */ PipelineBlendFactor::kOneMinusSrcColor,
        /*  6 */ PipelineBlendFactor::kSrcAlpha,
        /*  7 */ PipelineBlendFactor::kOneMinusSrcAlpha,
        /*  8 */ PipelineBlendFactor::kDstColor,
        /*  9 */ PipelineBlendFactor::kOneMinusDstColor,
        /* 10 */ PipelineBlendFactor::kDstAlpha,
        /* 11 */ PipelineBlendFactor::kOneMinusDstAlpha,
        /* 12 */ PipelineBlendFactor::kConstantColor,
        /* 13 */ PipelineBlendFactor::kOneMinusConstantColor,
        /* 14 */ PipelineBlendFactor::kConstantAlpha,
        /* 15 */ PipelineBlendFactor::kOneMinusConstantAlpha,
        /* 16 */ PipelineBlendFactor::kSrcAlphaSaturate,
    };
    render_target_out.src_color_blend_factor =
        kBlendFactorMap[uint32_t(blend_control.color_srcblend)];
    render_target_out.dst_color_blend_factor =
        kBlendFactorMap[uint32_t(blend_control.color_destblend)];
    render_target_out.color_blend_op = blend_control.color_comb_fcn;
    render_target_out.src_alpha_blend_factor =
        kBlendFactorMap[uint32_t(blend_control.alpha_srcblend)];
    render_target_out.dst_alpha_blend_factor =
        kBlendFactorMap[uint32_t(blend_control.alpha_destblend)];
    render_target_out.alpha_blend_op = blend_control.alpha_comb_fcn;
    const ui::vulkan::VulkanProvider& provider =
        command_processor_.GetVulkanProvider();
    const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
        device_portability_subset_features =
            provider.device_portability_subset_features();
    if (device_portability_subset_features &&
        !device_portability_subset_features->constantAlphaColorBlendFactors) {
      if (blend_control.color_srcblend == xenos::BlendFactor::kConstantAlpha) {
        render_target_out.src_color_blend_factor =
            PipelineBlendFactor::kConstantColor;
      } else if (blend_control.color_srcblend ==
                 xenos::BlendFactor::kOneMinusConstantAlpha) {
        render_target_out.src_color_blend_factor =
            PipelineBlendFactor::kOneMinusConstantColor;
      }
      if (blend_control.color_destblend == xenos::BlendFactor::kConstantAlpha) {
        render_target_out.dst_color_blend_factor =
            PipelineBlendFactor::kConstantColor;
      } else if (blend_control.color_destblend ==
                 xenos::BlendFactor::kOneMinusConstantAlpha) {
        render_target_out.dst_color_blend_factor =
            PipelineBlendFactor::kOneMinusConstantColor;
      }
    }
  } else {
    render_target_out.src_color_blend_factor = PipelineBlendFactor::kOne;
    render_target_out.dst_color_blend_factor = PipelineBlendFactor::kZero;
    render_target_out.color_blend_op = xenos::BlendOp::kAdd;
    render_target_out.src_alpha_blend_factor = PipelineBlendFactor::kOne;
    render_target_out.dst_alpha_blend_factor = PipelineBlendFactor::kZero;
    render_target_out.alpha_blend_op = xenos::BlendOp::kAdd;
  }
  render_target_out.color_write_mask = write_mask;
}

bool VulkanPipelineCache::GetCurrentStateDescription(
    const VulkanShader::VulkanTranslation* vertex_shader,
    const VulkanShader::VulkanTranslation* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    uint32_t normalized_color_mask,
    VulkanRenderTargetCache::RenderPassKey render_pass_key,
    PipelineDescription& description_out) const {
  description_out.Reset();

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceFeatures& device_features = provider.device_features();
  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
      device_portability_subset_features =
          provider.device_portability_subset_features();

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
      // The check should be performed at primitive processing time.
      assert_true(!device_portability_subset_features ||
                  device_portability_subset_features->triangleFans);
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

  description_out.depth_clamp_enable =
      regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable;

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
        description_out.polygon_mode =
            (!device_portability_subset_features ||
             device_portability_subset_features->pointPolygons)
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

  // TODO(Triang3l): Skip depth / stencil and color state for the fragment
  // shader interlock RB implementation.

  if (render_pass_key.depth_and_color_used & 1) {
    auto rb_depthcontrol = draw_util::GetDepthControlForCurrentEdramMode(regs);
    if (rb_depthcontrol.z_enable) {
      description_out.depth_write_enable = rb_depthcontrol.z_write_enable;
      description_out.depth_compare_op = rb_depthcontrol.zfunc;
    } else {
      description_out.depth_compare_op = xenos::CompareFunction::kAlways;
    }
    if (rb_depthcontrol.stencil_enable) {
      description_out.stencil_test_enable = 1;
      description_out.stencil_front_fail_op = rb_depthcontrol.stencilfail;
      description_out.stencil_front_pass_op = rb_depthcontrol.stencilzpass;
      description_out.stencil_front_depth_fail_op =
          rb_depthcontrol.stencilzfail;
      description_out.stencil_front_compare_op = rb_depthcontrol.stencilfunc;
      if (primitive_polygonal && rb_depthcontrol.backface_enable) {
        description_out.stencil_back_fail_op = rb_depthcontrol.stencilfail_bf;
        description_out.stencil_back_pass_op = rb_depthcontrol.stencilzpass_bf;
        description_out.stencil_back_depth_fail_op =
            rb_depthcontrol.stencilzfail_bf;
        description_out.stencil_back_compare_op =
            rb_depthcontrol.stencilfunc_bf;
      } else {
        description_out.stencil_back_fail_op =
            description_out.stencil_front_fail_op;
        description_out.stencil_back_pass_op =
            description_out.stencil_front_pass_op;
        description_out.stencil_back_depth_fail_op =
            description_out.stencil_front_depth_fail_op;
        description_out.stencil_back_compare_op =
            description_out.stencil_front_compare_op;
      }
    }
  }

  // Color blending and write masks (filled only for the attachments present in
  // the render pass object).
  uint32_t render_pass_color_rts = render_pass_key.depth_and_color_used >> 1;
  if (device_features.independentBlend) {
    uint32_t render_pass_color_rts_remaining = render_pass_color_rts;
    uint32_t color_rt_index;
    while (xe::bit_scan_forward(render_pass_color_rts_remaining,
                                &color_rt_index)) {
      render_pass_color_rts_remaining &= ~(uint32_t(1) << color_rt_index);
      WritePipelineRenderTargetDescription(
          regs.Get<reg::RB_BLENDCONTROL>(
              reg::RB_BLENDCONTROL::rt_register_indices[color_rt_index]),
          (normalized_color_mask >> (color_rt_index * 4)) & 0b1111,
          description_out.render_targets[color_rt_index]);
    }
  } else {
    // Take the blend control for the first render target that the guest wants
    // to write to (consider it the most important) and use it for all render
    // targets, if any.
    // TODO(Triang3l): Implement an option for independent blending via multiple
    // draw calls with different pipelines maybe? Though independent blending
    // support is pretty wide, with a quite prominent exception of Adreno 4xx
    // apparently.
    uint32_t render_pass_color_rts_remaining = render_pass_color_rts;
    uint32_t render_pass_first_color_rt_index;
    if (xe::bit_scan_forward(render_pass_color_rts_remaining,
                             &render_pass_first_color_rt_index)) {
      render_pass_color_rts_remaining &=
          ~(uint32_t(1) << render_pass_first_color_rt_index);
      PipelineRenderTarget& render_pass_first_color_rt =
          description_out.render_targets[render_pass_first_color_rt_index];
      uint32_t common_blend_rt_index;
      if (xe::bit_scan_forward(normalized_color_mask, &common_blend_rt_index)) {
        common_blend_rt_index >>= 2;
        // If a common write mask will be used for multiple render targets, use
        // the original RB_COLOR_MASK instead of the normalized color mask as
        // the normalized color mask has non-existent components forced to
        // written (don't need reading to be preserved), while the number of
        // components may vary between render targets. The attachments in the
        // pass that must not be written to at all will be excluded via a shader
        // modification.
        WritePipelineRenderTargetDescription(
            regs.Get<reg::RB_BLENDCONTROL>(
                reg::RB_BLENDCONTROL::rt_register_indices
                    [common_blend_rt_index]),
            (((normalized_color_mask &
               ~(uint32_t(0b1111) << (4 * common_blend_rt_index)))
                  ? regs[XE_GPU_REG_RB_COLOR_MASK].u32
                  : normalized_color_mask) >>
             (4 * common_blend_rt_index)) &
                0b1111,
            render_pass_first_color_rt);
      } else {
        // No render targets are written to, though the render pass still may
        // contain color attachments - set them to not written and not blending.
        render_pass_first_color_rt.src_color_blend_factor =
            PipelineBlendFactor::kOne;
        render_pass_first_color_rt.dst_color_blend_factor =
            PipelineBlendFactor::kZero;
        render_pass_first_color_rt.color_blend_op = xenos::BlendOp::kAdd;
        render_pass_first_color_rt.src_alpha_blend_factor =
            PipelineBlendFactor::kOne;
        render_pass_first_color_rt.dst_alpha_blend_factor =
            PipelineBlendFactor::kZero;
        render_pass_first_color_rt.alpha_blend_op = xenos::BlendOp::kAdd;
      }
      // Reuse the same blending settings for all render targets in the pass,
      // for description consistency.
      uint32_t color_rt_index;
      while (xe::bit_scan_forward(render_pass_color_rts_remaining,
                                  &color_rt_index)) {
        render_pass_color_rts_remaining &= ~(uint32_t(1) << color_rt_index);
        description_out.render_targets[color_rt_index] =
            render_pass_first_color_rt;
      }
    }
  }

  return true;
}

bool VulkanPipelineCache::ArePipelineRequirementsMet(
    const PipelineDescription& description) const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceFeatures& device_features = provider.device_features();

  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
      device_portability_subset_features =
          provider.device_portability_subset_features();
  if (device_portability_subset_features) {
    if (description.primitive_topology ==
            PipelinePrimitiveTopology::kTriangleFan &&
        device_portability_subset_features->triangleFans) {
      return false;
    }
    if (description.polygon_mode == PipelinePolygonMode::kPoint &&
        device_portability_subset_features->pointPolygons) {
      return false;
    }
    if (!device_portability_subset_features->constantAlphaColorBlendFactors) {
      uint32_t color_rts_remaining =
          description.render_pass_key.depth_and_color_used >> 1;
      uint32_t color_rt_index;
      while (xe::bit_scan_forward(color_rts_remaining, &color_rt_index)) {
        color_rts_remaining &= ~(uint32_t(1) << color_rt_index);
        const PipelineRenderTarget& color_rt =
            description.render_targets[color_rt_index];
        if (color_rt.src_color_blend_factor ==
                PipelineBlendFactor::kConstantAlpha ||
            color_rt.src_color_blend_factor ==
                PipelineBlendFactor::kOneMinusConstantAlpha ||
            color_rt.dst_color_blend_factor ==
                PipelineBlendFactor::kConstantAlpha ||
            color_rt.dst_color_blend_factor ==
                PipelineBlendFactor::kOneMinusConstantAlpha) {
          return false;
        }
      }
    }
  }

  if (!device_features.independentBlend) {
    uint32_t color_rts_remaining =
        description.render_pass_key.depth_and_color_used >> 1;
    uint32_t first_color_rt_index;
    if (xe::bit_scan_forward(color_rts_remaining, &first_color_rt_index)) {
      color_rts_remaining &= ~(uint32_t(1) << first_color_rt_index);
      const PipelineRenderTarget& first_color_rt =
          description.render_targets[first_color_rt_index];
      uint32_t color_rt_index;
      while (xe::bit_scan_forward(color_rts_remaining, &color_rt_index)) {
        color_rts_remaining &= ~(uint32_t(1) << color_rt_index);
        const PipelineRenderTarget& color_rt =
            description.render_targets[color_rt_index];
        if (color_rt.src_color_blend_factor !=
                first_color_rt.src_color_blend_factor ||
            color_rt.dst_color_blend_factor !=
                first_color_rt.dst_color_blend_factor ||
            color_rt.color_blend_op != first_color_rt.color_blend_op ||
            color_rt.src_alpha_blend_factor !=
                first_color_rt.src_alpha_blend_factor ||
            color_rt.dst_alpha_blend_factor !=
                first_color_rt.dst_alpha_blend_factor ||
            color_rt.alpha_blend_op != first_color_rt.alpha_blend_op ||
            color_rt.color_write_mask != first_color_rt.color_write_mask) {
          return false;
        }
      }
    }
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
  if (!ArePipelineRequirementsMet(description)) {
    assert_always(
        "When creating a new pipeline, the description must not require "
        "unsupported features, and when loading the pipeline storage, "
        "unsupported supported must be filtered out");
    return false;
  }

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceFeatures& device_features = provider.device_features();

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

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
  vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
  rasterization_state.depthClampEnable =
      description.depth_clamp_enable ? VK_TRUE : VK_FALSE;
  switch (description.polygon_mode) {
    case PipelinePolygonMode::kFill:
      rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
      break;
    case PipelinePolygonMode::kLine:
      rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
      break;
    case PipelinePolygonMode::kPoint:
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
  // Depth bias is dynamic (even toggling - pipeline creation is expensive).
  // "If no depth attachment is present, r is undefined" in the depth bias
  // formula, though Z has no effect on anything if a depth attachment is not
  // used (the guest shader can't access Z), enabling only when there's a
  // depth / stencil attachment for correctness.
  // TODO(Triang3l): Disable the depth bias for the fragment shader interlock RB
  // implementation.
  rasterization_state.depthBiasEnable =
      (description.render_pass_key.depth_and_color_used & 0b1) ? VK_TRUE
                                                               : VK_FALSE;
  // TODO(Triang3l): Wide lines.
  rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample_state = {};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples = VkSampleCountFlagBits(
      uint32_t(1) << uint32_t(description.render_pass_key.msaa_samples));

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
  depth_stencil_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil_state.pNext = nullptr;
  if (description.depth_write_enable ||
      description.depth_compare_op != xenos::CompareFunction::kAlways) {
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthWriteEnable =
        description.depth_write_enable ? VK_TRUE : VK_FALSE;
    depth_stencil_state.depthCompareOp = VkCompareOp(
        uint32_t(VK_COMPARE_OP_NEVER) + uint32_t(description.depth_compare_op));
  }
  if (description.stencil_test_enable) {
    depth_stencil_state.stencilTestEnable = VK_TRUE;
    depth_stencil_state.front.failOp =
        VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                    uint32_t(description.stencil_front_fail_op));
    depth_stencil_state.front.passOp =
        VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                    uint32_t(description.stencil_front_pass_op));
    depth_stencil_state.front.depthFailOp =
        VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                    uint32_t(description.stencil_front_depth_fail_op));
    depth_stencil_state.front.compareOp =
        VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +
                    uint32_t(description.stencil_front_compare_op));
    depth_stencil_state.back.failOp =
        VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                    uint32_t(description.stencil_back_fail_op));
    depth_stencil_state.back.passOp =
        VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                    uint32_t(description.stencil_back_pass_op));
    depth_stencil_state.back.depthFailOp =
        VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                    uint32_t(description.stencil_back_depth_fail_op));
    depth_stencil_state.back.compareOp =
        VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +
                    uint32_t(description.stencil_back_compare_op));
  }

  VkPipelineColorBlendAttachmentState
      color_blend_attachments[xenos::kMaxColorRenderTargets] = {};
  uint32_t color_rts_used =
      description.render_pass_key.depth_and_color_used >> 1;
  {
    static const VkBlendFactor kBlendFactorMap[] = {
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_SRC_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_DST_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_DST_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VK_BLEND_FACTOR_CONSTANT_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
        VK_BLEND_FACTOR_CONSTANT_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
        VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    };
    // 8 entries for safety since 3 bits from the guest are passed directly.
    static const VkBlendOp kBlendOpMap[] = {VK_BLEND_OP_ADD,
                                            VK_BLEND_OP_SUBTRACT,
                                            VK_BLEND_OP_MIN,
                                            VK_BLEND_OP_MAX,
                                            VK_BLEND_OP_REVERSE_SUBTRACT,
                                            VK_BLEND_OP_ADD,
                                            VK_BLEND_OP_ADD,
                                            VK_BLEND_OP_ADD};
    uint32_t color_rts_remaining = color_rts_used;
    uint32_t color_rt_index;
    while (xe::bit_scan_forward(color_rts_remaining, &color_rt_index)) {
      color_rts_remaining &= ~(uint32_t(1) << color_rt_index);
      VkPipelineColorBlendAttachmentState& color_blend_attachment =
          color_blend_attachments[color_rt_index];
      const PipelineRenderTarget& color_rt =
          description.render_targets[color_rt_index];
      if (color_rt.src_color_blend_factor != PipelineBlendFactor::kOne ||
          color_rt.dst_color_blend_factor != PipelineBlendFactor::kZero ||
          color_rt.color_blend_op != xenos::BlendOp::kAdd ||
          color_rt.src_alpha_blend_factor != PipelineBlendFactor::kOne ||
          color_rt.dst_alpha_blend_factor != PipelineBlendFactor::kZero ||
          color_rt.alpha_blend_op != xenos::BlendOp::kAdd) {
        color_blend_attachment.blendEnable = VK_TRUE;
        color_blend_attachment.srcColorBlendFactor =
            kBlendFactorMap[uint32_t(color_rt.src_color_blend_factor)];
        color_blend_attachment.dstColorBlendFactor =
            kBlendFactorMap[uint32_t(color_rt.dst_color_blend_factor)];
        color_blend_attachment.colorBlendOp =
            kBlendOpMap[uint32_t(color_rt.color_blend_op)];
        color_blend_attachment.srcAlphaBlendFactor =
            kBlendFactorMap[uint32_t(color_rt.src_alpha_blend_factor)];
        color_blend_attachment.dstAlphaBlendFactor =
            kBlendFactorMap[uint32_t(color_rt.dst_alpha_blend_factor)];
        color_blend_attachment.alphaBlendOp =
            kBlendOpMap[uint32_t(color_rt.alpha_blend_op)];
      }
      color_blend_attachment.colorWriteMask =
          VkColorComponentFlags(color_rt.color_write_mask);
      if (!device_features.independentBlend) {
        // For non-independent blend, the pAttachments element for the first
        // actually used color will be replicated into all.
        break;
      }
    }
  }
  VkPipelineColorBlendStateCreateInfo color_blend_state = {};
  color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.attachmentCount = 32 - xe::lzcnt(color_rts_used);
  color_blend_state.pAttachments = color_blend_attachments;
  if (color_rts_used && !device_features.independentBlend) {
    // "If the independent blending feature is not enabled, all elements of
    //  pAttachments must be identical."
    uint32_t first_color_rt_index;
    xe::bit_scan_forward(color_rts_used, &first_color_rt_index);
    for (uint32_t i = 0; i < color_blend_state.attachmentCount; ++i) {
      if (i == first_color_rt_index) {
        continue;
      }
      color_blend_attachments[i] =
          color_blend_attachments[first_color_rt_index];
    }
  }

  std::array<VkDynamicState, 7> dynamic_states;
  VkPipelineDynamicStateCreateInfo dynamic_state;
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.pNext = nullptr;
  dynamic_state.flags = 0;
  dynamic_state.dynamicStateCount = 0;
  dynamic_state.pDynamicStates = dynamic_states.data();
  // Regardless of whether some of this state actually has any effect on the
  // pipeline, marking all as dynamic because otherwise, binding any pipeline
  // with such state not marked as dynamic will cause the dynamic state to be
  // invalidated (again, even if it has no effect).
  dynamic_states[dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
  dynamic_states[dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
  dynamic_states[dynamic_state.dynamicStateCount++] =
      VK_DYNAMIC_STATE_DEPTH_BIAS;
  dynamic_states[dynamic_state.dynamicStateCount++] =
      VK_DYNAMIC_STATE_BLEND_CONSTANTS;
  dynamic_states[dynamic_state.dynamicStateCount++] =
      VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
  dynamic_states[dynamic_state.dynamicStateCount++] =
      VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
  dynamic_states[dynamic_state.dynamicStateCount++] =
      VK_DYNAMIC_STATE_STENCIL_REFERENCE;

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
  pipeline_create_info.pDepthStencilState = &depth_stencil_state;
  pipeline_create_info.pColorBlendState = &color_blend_state;
  pipeline_create_info.pDynamicState = &dynamic_state;
  pipeline_create_info.layout =
      creation_arguments.pipeline->second.pipeline_layout->GetPipelineLayout();
  pipeline_create_info.renderPass = creation_arguments.render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = UINT32_MAX;

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
