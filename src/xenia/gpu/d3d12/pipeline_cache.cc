/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/pipeline_cache.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/gpu_flags.h"

DEFINE_bool(d3d12_dxbc_disasm, false,
            "Disassemble DXBC shaders after generation.");

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/continuous_quad_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/continuous_triangle_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/discrete_quad_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/discrete_triangle_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/primitive_point_list_gs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/primitive_quad_list_gs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/primitive_rectangle_list_gs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/tessellation_quad_vs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/tessellation_triangle_vs.h"

PipelineCache::PipelineCache(D3D12CommandProcessor* command_processor,
                             RegisterFile* register_file, bool edram_rov_used)
    : command_processor_(command_processor),
      register_file_(register_file),
      edram_rov_used_(edram_rov_used) {
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();

  shader_translator_ = std::make_unique<DxbcShaderTranslator>(
      provider->GetAdapterVendorID(), edram_rov_used_);

  if (edram_rov_used_) {
    depth_only_pixel_shader_ =
        std::move(shader_translator_->CreateDepthOnlyPixelShader());
  }

  // Set pipeline state description values we never change.
  // Zero out tessellation, stream output, blend state and formats for render
  // targets 4+, node mask, cached PSO, flags and other things.
  std::memset(&update_desc_, 0, sizeof(update_desc_));
  update_desc_.BlendState.IndependentBlendEnable =
      edram_rov_used_ ? FALSE : TRUE;
  update_desc_.SampleMask = UINT_MAX;
  update_desc_.SampleDesc.Count = 1;
}

PipelineCache::~PipelineCache() { Shutdown(); }

void PipelineCache::Shutdown() { ClearCache(); }

D3D12Shader* PipelineCache::LoadShader(ShaderType shader_type,
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
  D3D12Shader* shader =
      new D3D12Shader(shader_type, data_hash, host_address, dword_count);
  shader_map_.insert({data_hash, shader});

  return shader;
}

bool PipelineCache::EnsureShadersTranslated(D3D12Shader* vertex_shader,
                                            D3D12Shader* pixel_shader,
                                            PrimitiveType primitive_type) {
  auto& regs = *register_file_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(regs[XE_GPU_REG_SQ_VS_CONST].u32 == 0x000FF000 ||
              regs[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(regs[XE_GPU_REG_SQ_PS_CONST].u32 == 0x000FF100 ||
              regs[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  xenos::xe_gpu_program_cntl_t sq_program_cntl;
  sq_program_cntl.dword_0 = regs[XE_GPU_REG_SQ_PROGRAM_CNTL].u32;
  if (!vertex_shader->is_translated() &&
      !TranslateShader(vertex_shader, sq_program_cntl, primitive_type)) {
    XELOGE("Failed to translate the vertex shader!");
    return false;
  }
  if (pixel_shader != nullptr && !pixel_shader->is_translated() &&
      !TranslateShader(pixel_shader, sq_program_cntl, primitive_type)) {
    XELOGE("Failed to translate the pixel shader!");
    return false;
  }
  return true;
}

PipelineCache::UpdateStatus PipelineCache::ConfigurePipeline(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type, IndexFormat index_format,
    const RenderTargetCache::PipelineRenderTarget render_targets[5],
    ID3D12PipelineState** pipeline_out,
    ID3D12RootSignature** root_signature_out) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_not_null(pipeline_out);
  assert_not_null(root_signature_out);

  Pipeline* pipeline = nullptr;
  auto update_status = UpdateState(vertex_shader, pixel_shader, primitive_type,
                                   index_format, render_targets);
  switch (update_status) {
    case UpdateStatus::kCompatible:
      // Requested pipeline is compatible with our previous one, so use that.
      // Note that there still may be dynamic state that needs updating.
      pipeline = current_pipeline_;
      break;
    case UpdateStatus::kMismatch:
      // Pipeline state has changed. We need to either create a new one or find
      // an old one that matches.
      current_pipeline_ = nullptr;
      break;
    case UpdateStatus::kError:
      // Error updating state - bail out.
      // We are in an indeterminate state, so reset things for the next attempt.
      current_pipeline_ = nullptr;
      return update_status;
  }
  if (!pipeline) {
    // Should have a hash key produced by the UpdateState pass.
    uint64_t hash_key = XXH64_digest(&hash_state_);
    pipeline = GetPipeline(hash_key);
    current_pipeline_ = pipeline;
    if (!pipeline) {
      // Unable to create pipeline.
      return UpdateStatus::kError;
    }
  }

  *pipeline_out = pipeline->state;
  *root_signature_out = pipeline->root_signature;
  return update_status;
}

void PipelineCache::ClearCache() {
  // Remove references to the current pipeline.
  current_pipeline_ = nullptr;

  // Destroy all pipelines.
  for (auto it : pipelines_) {
    it.second->state->Release();
    delete it.second;
  }
  pipelines_.clear();
  COUNT_profile_set("gpu/pipeline_cache/pipelines", 0);

  // Destroy all shaders.
  for (auto it : shader_map_) {
    delete it.second;
  }
  shader_map_.clear();
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

bool PipelineCache::TranslateShader(D3D12Shader* shader,
                                    xenos::xe_gpu_program_cntl_t cntl,
                                    PrimitiveType primitive_type) {
  // Set the target for vertex shader translation.
  DxbcShaderTranslator::VertexShaderType vertex_shader_type;
  if (primitive_type == PrimitiveType::kTrianglePatch) {
    vertex_shader_type =
        DxbcShaderTranslator::VertexShaderType::kTriangleDomain;
  } else if (primitive_type == PrimitiveType::kQuadPatch) {
    vertex_shader_type = DxbcShaderTranslator::VertexShaderType::kQuadDomain;
  } else {
    vertex_shader_type = DxbcShaderTranslator::VertexShaderType::kVertex;
  }
  shader_translator_->SetVertexShaderType(vertex_shader_type);

  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_->Translate(shader, cntl)) {
    XELOGE("Shader %.16" PRIX64 "translation failed; marking as ignored",
           shader->ucode_data_hash());
    return false;
  }

  if (vertex_shader_type != DxbcShaderTranslator::VertexShaderType::kVertex) {
    // For checking later for safety (so a vertex shader won't be accidentally
    // used as a domain shader or vice versa).
    shader->SetDomainShaderPrimitiveType(primitive_type);
  }

  uint32_t texture_srv_count;
  const DxbcShaderTranslator::TextureSRV* texture_srvs =
      shader_translator_->GetTextureSRVs(texture_srv_count);
  uint32_t sampler_binding_count;
  const DxbcShaderTranslator::SamplerBinding* sampler_bindings =
      shader_translator_->GetSamplerBindings(sampler_binding_count);
  shader->SetTexturesAndSamplers(texture_srvs, texture_srv_count,
                                 sampler_bindings, sampler_binding_count);

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader (%db) - hash %.16" PRIX64 ":\n%s\n",
             shader->type() == ShaderType::kVertex ? "vertex" : "pixel",
             shader->ucode_dword_count() * 4, shader->ucode_data_hash(),
             shader->ucode_disassembly().c_str());
  }

  // Disassemble the shader for dumping.
  if (FLAGS_d3d12_dxbc_disasm) {
    auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
    if (!shader->DisassembleDxbc(provider)) {
      XELOGE("Failed to disassemble DXBC shader %.16" PRIX64,
             shader->ucode_data_hash());
    }
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "d3d12");
  }

  return shader->is_valid();
}

PipelineCache::UpdateStatus PipelineCache::UpdateState(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type, IndexFormat index_format,
    const RenderTargetCache::PipelineRenderTarget render_targets[5]) {
  bool mismatch = false;

  // Reset hash so we can build it up.
  XXH64_reset(&hash_state_, 0);

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
  status = UpdateShaderStages(vertex_shader, pixel_shader, primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update shader stages");
  status = UpdateBlendStateAndRenderTargets(pixel_shader, render_targets);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update blend state");
  status = UpdateRasterizerState(primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterizer state");
  status = UpdateDepthStencilState(render_targets[4].format);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");
  status = UpdateIBStripCutValue(index_format);
  CHECK_UPDATE_STATUS(status, mismatch,
                      "Unable to update index buffer strip cut value");
#undef CHECK_UPDATE_STATUS

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateShaderStages(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type) {
  auto& regs = update_shader_stages_regs_;

  bool dirty = current_pipeline_ == nullptr;
  dirty |= regs.vertex_shader != vertex_shader;
  dirty |= regs.pixel_shader != pixel_shader;
  regs.vertex_shader = vertex_shader;
  regs.pixel_shader = pixel_shader;
  // This topology type is before the geometry shader stage.
  D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
  switch (primitive_type) {
    case PrimitiveType::kPointList:
      primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      break;
    case PrimitiveType::kLineList:
    case PrimitiveType::kLineStrip:
    case PrimitiveType::kLineLoop:
    // Quads are emulated as line lists with adjacency.
    case PrimitiveType::kQuadList:
    case PrimitiveType::k2DLineStrip:
      primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    case PrimitiveType::kTrianglePatch:
    case PrimitiveType::kQuadPatch:
      primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
      break;
    default:
      primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  };
  dirty |= regs.primitive_topology_type != primitive_topology_type;
  regs.primitive_topology_type = primitive_topology_type;
  // Zero out the tessellation mode by default for a stable hash.
  TessellationMode tessellation_mode = TessellationMode(0);
  if (primitive_type == PrimitiveType::kPointList ||
      primitive_type == PrimitiveType::kRectangleList ||
      primitive_type == PrimitiveType::kQuadList ||
      primitive_type == PrimitiveType::kTrianglePatch ||
      primitive_type == PrimitiveType::kQuadPatch) {
    dirty |= regs.hs_gs_ds_primitive_type != primitive_type;
    regs.hs_gs_ds_primitive_type = primitive_type;
    if (primitive_type == PrimitiveType::kTrianglePatch ||
        primitive_type == PrimitiveType::kQuadPatch) {
      tessellation_mode = TessellationMode(
          register_file_->values[XE_GPU_REG_VGT_HOS_CNTL].u32 & 0x3);
    }
  } else {
    dirty |= regs.hs_gs_ds_primitive_type != PrimitiveType::kNone;
    regs.hs_gs_ds_primitive_type = PrimitiveType::kNone;
  }
  dirty |= regs.tessellation_mode != tessellation_mode;
  regs.tessellation_mode = tessellation_mode;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  if (!EnsureShadersTranslated(vertex_shader, pixel_shader, primitive_type)) {
    return UpdateStatus::kError;
  }

  update_desc_.pRootSignature = command_processor_->GetRootSignature(
      vertex_shader, pixel_shader, primitive_type);
  if (update_desc_.pRootSignature == nullptr) {
    return UpdateStatus::kError;
  }
  if (primitive_type == PrimitiveType::kTrianglePatch ||
      primitive_type == PrimitiveType::kQuadPatch) {
    if (vertex_shader->GetDomainShaderPrimitiveType() != primitive_type) {
      XELOGE("Tried to use vertex shader %.16" PRIX64
             " for tessellation, but it's not a tessellation domain shader or "
             "has the wrong domain",
             vertex_shader->ucode_data_hash());
      assert_always();
      return UpdateStatus::kError;
    }
    if (primitive_type == PrimitiveType::kTrianglePatch) {
      update_desc_.VS.pShaderBytecode = tessellation_triangle_vs;
      update_desc_.VS.BytecodeLength = sizeof(tessellation_triangle_vs);
    } else if (primitive_type == PrimitiveType::kQuadPatch) {
      update_desc_.VS.pShaderBytecode = tessellation_quad_vs;
      update_desc_.VS.BytecodeLength = sizeof(tessellation_quad_vs);
    }
    // The Xenos vertex shader works like a domain shader with tessellation.
    update_desc_.DS.pShaderBytecode = vertex_shader->translated_binary().data();
    update_desc_.DS.BytecodeLength = vertex_shader->translated_binary().size();
  } else {
    if (vertex_shader->GetDomainShaderPrimitiveType() != PrimitiveType::kNone) {
      XELOGE("Tried to use vertex shader %.16" PRIX64
             " without tessellation, but it's a tessellation domain shader",
             vertex_shader->ucode_data_hash());
      assert_always();
      return UpdateStatus::kError;
    }
    update_desc_.VS.pShaderBytecode = vertex_shader->translated_binary().data();
    update_desc_.VS.BytecodeLength = vertex_shader->translated_binary().size();
    update_desc_.DS.pShaderBytecode = nullptr;
    update_desc_.DS.BytecodeLength = 0;
  }
  if (pixel_shader != nullptr) {
    update_desc_.PS.pShaderBytecode = pixel_shader->translated_binary().data();
    update_desc_.PS.BytecodeLength = pixel_shader->translated_binary().size();
  } else {
    if (edram_rov_used_) {
      update_desc_.PS.pShaderBytecode = depth_only_pixel_shader_.data();
      update_desc_.PS.BytecodeLength = depth_only_pixel_shader_.size();
    } else {
      update_desc_.PS.pShaderBytecode = nullptr;
      update_desc_.PS.BytecodeLength = 0;
    }
  }
  switch (primitive_type) {
    case PrimitiveType::kTrianglePatch:
      if (tessellation_mode == TessellationMode::kDiscrete) {
        update_desc_.HS.pShaderBytecode = discrete_triangle_hs;
        update_desc_.HS.BytecodeLength = sizeof(discrete_triangle_hs);
      } else {
        update_desc_.HS.pShaderBytecode = continuous_triangle_hs;
        update_desc_.HS.BytecodeLength = sizeof(continuous_triangle_hs);
        // TODO(Triang3l): True adaptive tessellation when memexport is added.
      }
      break;
    case PrimitiveType::kQuadPatch:
      if (tessellation_mode == TessellationMode::kDiscrete) {
        update_desc_.HS.pShaderBytecode = discrete_quad_hs;
        update_desc_.HS.BytecodeLength = sizeof(discrete_quad_hs);
      } else {
        update_desc_.HS.pShaderBytecode = continuous_quad_hs;
        update_desc_.HS.BytecodeLength = sizeof(continuous_quad_hs);
        // TODO(Triang3l): True adaptive tessellation when memexport is added.
      }
      break;
    default:
      update_desc_.HS.pShaderBytecode = nullptr;
      update_desc_.HS.BytecodeLength = 0;
      break;
  }
  switch (primitive_type) {
    case PrimitiveType::kPointList:
      update_desc_.GS.pShaderBytecode = primitive_point_list_gs;
      update_desc_.GS.BytecodeLength = sizeof(primitive_point_list_gs);
      break;
    case PrimitiveType::kRectangleList:
      update_desc_.GS.pShaderBytecode = primitive_rectangle_list_gs;
      update_desc_.GS.BytecodeLength = sizeof(primitive_rectangle_list_gs);
      break;
    case PrimitiveType::kQuadList:
      update_desc_.GS.pShaderBytecode = primitive_quad_list_gs;
      update_desc_.GS.BytecodeLength = sizeof(primitive_quad_list_gs);
      break;
    default:
      // TODO(Triang3l): More geometry shaders for various primitive types.
      update_desc_.GS.pShaderBytecode = nullptr;
      update_desc_.GS.BytecodeLength = 0;
  }
  update_desc_.PrimitiveTopologyType = primitive_topology_type;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateBlendStateAndRenderTargets(
    D3D12Shader* pixel_shader,
    const RenderTargetCache::PipelineRenderTarget render_targets[4]) {
  if (edram_rov_used_) {
    return current_pipeline_ == nullptr ? UpdateStatus::kMismatch
                                        : UpdateStatus::kCompatible;
  }

  auto& regs = update_blend_state_and_render_targets_regs_;

  bool dirty = current_pipeline_ == nullptr;
  for (uint32_t i = 0; i < 4; ++i) {
    dirty |= regs.render_targets[i].guest_render_target !=
             render_targets[i].guest_render_target;
    regs.render_targets[i].guest_render_target =
        render_targets[i].guest_render_target;
    dirty |= regs.render_targets[i].format != render_targets[i].format;
    regs.render_targets[i].format = render_targets[i].format;
  }
  uint32_t color_mask = command_processor_->GetCurrentColorMask(pixel_shader);
  dirty |= regs.color_mask != color_mask;
  regs.color_mask = color_mask;
  bool blend_enable =
      color_mask != 0 &&
      !(register_file_->values[XE_GPU_REG_RB_COLORCONTROL].u32 & 0x20);
  dirty |= regs.colorcontrol_blend_enable != blend_enable;
  regs.colorcontrol_blend_enable = blend_enable;
  static const Register kBlendControlRegs[] = {
      XE_GPU_REG_RB_BLENDCONTROL_0, XE_GPU_REG_RB_BLENDCONTROL_1,
      XE_GPU_REG_RB_BLENDCONTROL_2, XE_GPU_REG_RB_BLENDCONTROL_3};
  for (uint32_t i = 0; i < 4; ++i) {
    if (blend_enable && (color_mask & (0xF << (i * 4)))) {
      dirty |= SetShadowRegister(&regs.blendcontrol[i], kBlendControlRegs[i]);
    } else {
      // Zero out blend color for unused render targets and when not blending
      // for a stable hash.
      regs.blendcontrol[i] = 0;
    }
  }
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  static const D3D12_BLEND kBlendFactorMap[] = {
      /*  0 */ D3D12_BLEND_ZERO,
      /*  1 */ D3D12_BLEND_ONE,
      /*  2 */ D3D12_BLEND_ZERO,  // ?
      /*  3 */ D3D12_BLEND_ZERO,  // ?
      /*  4 */ D3D12_BLEND_SRC_COLOR,
      /*  5 */ D3D12_BLEND_INV_SRC_COLOR,
      /*  6 */ D3D12_BLEND_SRC_ALPHA,
      /*  7 */ D3D12_BLEND_INV_SRC_ALPHA,
      /*  8 */ D3D12_BLEND_DEST_COLOR,
      /*  9 */ D3D12_BLEND_INV_DEST_COLOR,
      /* 10 */ D3D12_BLEND_DEST_ALPHA,
      /* 11 */ D3D12_BLEND_INV_DEST_ALPHA,
      /* 12 */ D3D12_BLEND_BLEND_FACTOR,      // CONSTANT_COLOR
      /* 13 */ D3D12_BLEND_INV_BLEND_FACTOR,  // ONE_MINUS_CONSTANT_COLOR
      /* 14 */ D3D12_BLEND_BLEND_FACTOR,      // CONSTANT_ALPHA
      /* 15 */ D3D12_BLEND_INV_BLEND_FACTOR,  // ONE_MINUS_CONSTANT_ALPHA
      /* 16 */ D3D12_BLEND_SRC_ALPHA_SAT,
  };
  // Like kBlendFactorMap, but with _COLOR modes changed to _ALPHA. Some
  // pipelines aren't created in Prey because a color mode is used for alpha.
  static const D3D12_BLEND kBlendFactorAlphaMap[] = {
      /*  0 */ D3D12_BLEND_ZERO,
      /*  1 */ D3D12_BLEND_ONE,
      /*  2 */ D3D12_BLEND_ZERO,  // ?
      /*  3 */ D3D12_BLEND_ZERO,  // ?
      /*  4 */ D3D12_BLEND_SRC_ALPHA,
      /*  5 */ D3D12_BLEND_INV_SRC_ALPHA,
      /*  6 */ D3D12_BLEND_SRC_ALPHA,
      /*  7 */ D3D12_BLEND_INV_SRC_ALPHA,
      /*  8 */ D3D12_BLEND_DEST_ALPHA,
      /*  9 */ D3D12_BLEND_INV_DEST_ALPHA,
      /* 10 */ D3D12_BLEND_DEST_ALPHA,
      /* 11 */ D3D12_BLEND_INV_DEST_ALPHA,
      /* 12 */ D3D12_BLEND_BLEND_FACTOR,      // CONSTANT_COLOR
      /* 13 */ D3D12_BLEND_INV_BLEND_FACTOR,  // ONE_MINUS_CONSTANT_COLOR
      /* 14 */ D3D12_BLEND_BLEND_FACTOR,      // CONSTANT_ALPHA
      /* 15 */ D3D12_BLEND_INV_BLEND_FACTOR,  // ONE_MINUS_CONSTANT_ALPHA
      /* 16 */ D3D12_BLEND_SRC_ALPHA_SAT,
  };
  static const D3D12_BLEND_OP kBlendOpMap[] = {
      /*  0 */ D3D12_BLEND_OP_ADD,
      /*  1 */ D3D12_BLEND_OP_SUBTRACT,
      /*  2 */ D3D12_BLEND_OP_MIN,
      /*  3 */ D3D12_BLEND_OP_MAX,
      /*  4 */ D3D12_BLEND_OP_REV_SUBTRACT,
  };
  update_desc_.NumRenderTargets = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    auto& blend_desc = update_desc_.BlendState.RenderTarget[i];
    uint32_t guest_render_target = render_targets[i].guest_render_target;
    uint32_t blend_control = regs.blendcontrol[guest_render_target];
    DXGI_FORMAT format = render_targets[i].format;
    // Also treat 1 * src + 0 * dest as disabled blending (there are opaque
    // surfaces drawn with blending enabled, but it's 1 * src + 0 * dest, in
    // Call of Duty 4 - GPU performance is better when not blending.
    if (blend_enable && (blend_control & 0x1FFF1FFF) != 0x00010001 &&
        format != DXGI_FORMAT_UNKNOWN &&
        (color_mask & (0xF << (guest_render_target * 4)))) {
      blend_desc.BlendEnable = TRUE;
      // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
      blend_desc.SrcBlend = kBlendFactorMap[(blend_control & 0x0000001F) >> 0];
      // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
      blend_desc.DestBlend = kBlendFactorMap[(blend_control & 0x00001F00) >> 8];
      // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
      blend_desc.BlendOp = kBlendOpMap[(blend_control & 0x000000E0) >> 5];
      // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
      blend_desc.SrcBlendAlpha =
          kBlendFactorAlphaMap[(blend_control & 0x001F0000) >> 16];
      // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
      blend_desc.DestBlendAlpha =
          kBlendFactorAlphaMap[(blend_control & 0x1F000000) >> 24];
      // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
      blend_desc.BlendOpAlpha = kBlendOpMap[(blend_control & 0x00E00000) >> 21];
    } else {
      blend_desc.BlendEnable = FALSE;
      blend_desc.SrcBlend = D3D12_BLEND_ONE;
      blend_desc.DestBlend = D3D12_BLEND_ZERO;
      blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
      blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
      blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
      blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    blend_desc.RenderTargetWriteMask =
        (color_mask >> (guest_render_target * 4)) & 0xF;
    update_desc_.RTVFormats[i] = format;
    if (format != DXGI_FORMAT_UNKNOWN) {
      update_desc_.NumRenderTargets = i + 1;
    }
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRasterizerState(
    PrimitiveType primitive_type) {
  auto& regs = update_rasterizer_state_regs_;

  bool dirty = current_pipeline_ == nullptr;
  uint32_t pa_su_sc_mode_cntl =
      register_file_->values[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;
  uint32_t cull_mode = pa_su_sc_mode_cntl & 0x3;
  if (primitive_type == PrimitiveType::kPointList ||
      primitive_type == PrimitiveType::kRectangleList) {
    cull_mode = 0;
  }
  dirty |= regs.cull_mode != cull_mode;
  regs.cull_mode = cull_mode;
  // Because Direct3D 12 doesn't support per-side fill mode and depth bias, the
  // values to use depends on the current culling state.
  // If front faces are culled, use the ones for back faces.
  // If back faces are culled, it's the other way around.
  // If culling is not enabled, assume the developer wanted to draw things in a
  // more special way - so if one side is wireframe or has a depth bias, then
  // that's intentional (if both sides have a depth bias, the one for the front
  // faces is used, though it's unlikely that they will ever be different -
  // SetRenderState sets the same offset for both sides).
  // Points fill mode (0) also isn't supported in Direct3D 12, but assume the
  // developer didn't want to fill the whole primitive and use wireframe (like
  // Xenos fill mode 1).
  // Here we also assume that only one side is culled - if two sides are culled,
  // the D3D12 command processor will drop such draw early.
  bool fill_mode_wireframe = false;
  float poly_offset = 0.0f, poly_offset_scale = 0.0f;
  // With ROV, the depth bias is applied in the pixel shader because per-sample
  // depth is needed for MSAA.
  if (!(cull_mode & 1)) {
    // Front faces aren't culled.
    uint32_t fill_mode = (pa_su_sc_mode_cntl >> 5) & 0x7;
    if (fill_mode == 0 || fill_mode == 1) {
      fill_mode_wireframe = true;
    }
    if (!edram_rov_used_ && ((pa_su_sc_mode_cntl >> 11) & 0x1)) {
      poly_offset =
          register_file_->values[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET].f32;
      poly_offset_scale =
          register_file_->values[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE].f32;
    }
  }
  if (!(cull_mode & 2)) {
    // Back faces aren't culled.
    uint32_t fill_mode = (pa_su_sc_mode_cntl >> 8) & 0x7;
    if (fill_mode == 0 || fill_mode == 1) {
      fill_mode_wireframe = true;
    }
    // Prefer front depth bias because in general, front faces are the ones
    // that are rendered (except for shadow volumes).
    if (!edram_rov_used_ && ((pa_su_sc_mode_cntl >> 12) & 0x1) &&
        poly_offset == 0.0f && poly_offset_scale == 0.0f) {
      poly_offset =
          register_file_->values[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET].f32;
      poly_offset_scale =
          register_file_->values[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE].f32;
    }
  }
  if (!edram_rov_used_) {
    // Conversion based on the calculations in Call of Duty 4 and the values it
    // writes to the registers, and also on:
    // https://github.com/mesa3d/mesa/blob/54ad9b444c8e73da498211870e785239ad3ff1aa/src/gallium/drivers/radeonsi/si_state.c#L943
    // Dividing the scale by 2 - Call of Duty 4 sets the constant bias of
    // 1/32768 for decals, however, it's done in two steps in separate places:
    // first it's divided by 65536, and then it's multiplied by 2 (which is
    // consistent with what si_create_rs_state does, which multiplies the offset
    // by 2 if it comes from a non-D3D9 API for 24-bit depth buffers) - and
    // multiplying by 2 to the number of significand bits. Tested mostly in Call
    // of Duty 4 (vehicledamage map explosion decals) and Red Dead Redemption
    // (shadows - 2^17 is not enough, 2^18 hasn't been tested, but 2^19
    // eliminates the acne).
    if (((register_file_->values[XE_GPU_REG_RB_DEPTH_INFO].u32 >> 16) & 0x1) ==
        uint32_t(DepthRenderTargetFormat::kD24FS8)) {
      poly_offset *= float(1 << 19);
    } else {
      poly_offset *= float(1 << 23);
    }
    // Reversed depth is emulated in vertex shaders because MinDepth > MaxDepth
    // in viewports doesn't seem to work on Nvidia.
    if ((register_file_->values[XE_GPU_REG_PA_CL_VTE_CNTL].u32 & (1 << 4)) &&
        register_file_->values[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32 < 0.0f) {
      poly_offset = -poly_offset;
      poly_offset_scale = -poly_offset_scale;
    }
  }
  if (((pa_su_sc_mode_cntl >> 3) & 0x3) == 0) {
    // Fill mode is disabled.
    fill_mode_wireframe = false;
  }
  dirty |= regs.fill_mode_wireframe != fill_mode_wireframe;
  regs.fill_mode_wireframe = fill_mode_wireframe;
  dirty |= regs.poly_offset != poly_offset;
  regs.poly_offset = poly_offset;
  dirty |= regs.poly_offset_scale != poly_offset_scale;
  regs.poly_offset_scale = poly_offset_scale;
  bool front_counter_clockwise = !(pa_su_sc_mode_cntl & 0x4);
  dirty |= regs.front_counter_clockwise != front_counter_clockwise;
  regs.front_counter_clockwise = front_counter_clockwise;
  uint32_t pa_cl_clip_cntl =
      register_file_->values[XE_GPU_REG_PA_CL_CLIP_CNTL].u32;
  // CLIP_DISABLE
  bool depth_clamp_enable = !!(pa_cl_clip_cntl & (1 << 16));
  // TODO(DrChat): This seem to differ. Need to examine this.
  // https://github.com/decaf-emu/decaf-emu/blob/c017a9ff8128852fb9a5da19466778a171cea6e1/src/libdecaf/src/gpu/latte_registers_pa.h#L11
  // ZCLIP_NEAR_DISABLE
  // bool depth_clamp_enable = !(pa_cl_clip_cntl & (1 << 26));
  // RASTERIZER_DISABLE
  // Disable rendering in command processor if regs.pa_cl_clip_cntl & (1 << 22)?
  dirty |= regs.depth_clamp_enable != depth_clamp_enable;
  regs.depth_clamp_enable = depth_clamp_enable;
  bool msaa_rov = false;
  if (edram_rov_used_) {
    if ((register_file_->values[XE_GPU_REG_RB_SURFACE_INFO].u32 >> 16) & 0x3) {
      msaa_rov = true;
    }
    dirty |= regs.msaa_rov != msaa_rov;
    regs.msaa_rov = msaa_rov;
  }
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  update_desc_.RasterizerState.FillMode =
      fill_mode_wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  if (cull_mode & 1) {
    update_desc_.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
  } else if (cull_mode & 2) {
    update_desc_.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  } else {
    update_desc_.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  }
  update_desc_.RasterizerState.FrontCounterClockwise =
      front_counter_clockwise ? TRUE : FALSE;
  // Using ceil here just in case a game wants the offset but passes a value
  // that is too small - it's better to apply more offset than to make depth
  // fighting worse or to disable the offset completely (Direct3D 12 takes an
  // integer value).
  update_desc_.RasterizerState.DepthBias =
      int32_t(std::ceil(std::abs(poly_offset))) * (poly_offset < 0.0f ? -1 : 1);
  update_desc_.RasterizerState.DepthBiasClamp = 0.0f;
  update_desc_.RasterizerState.SlopeScaledDepthBias =
      poly_offset_scale * (1.0f / 16.0f);
  update_desc_.RasterizerState.DepthClipEnable =
      !depth_clamp_enable ? TRUE : FALSE;
  if (edram_rov_used_) {
    // Only 1, 4, 8 and (not on all GPUs) 16 are allowed, using sample 0 as 0
    // and 3 as 1 for 2x instead (not exactly the same sample positions, but
    // still top-left and bottom-right - however, this can be adjusted with
    // programmable sample positions).
    update_desc_.RasterizerState.ForcedSampleCount = msaa_rov ? 4 : 1;
  } else {
    update_desc_.RasterizerState.ForcedSampleCount = 0;
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateDepthStencilState(
    DXGI_FORMAT format) {
  if (edram_rov_used_) {
    return current_pipeline_ == nullptr ? UpdateStatus::kMismatch
                                        : UpdateStatus::kCompatible;
  }

  auto& regs = update_depth_stencil_state_regs_;

  bool dirty = current_pipeline_ == nullptr;
  dirty |= regs.format != format;
  regs.format = format;
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  bool dsv_bound = format != DXGI_FORMAT_UNKNOWN;
  update_desc_.DepthStencilState.DepthEnable =
      (dsv_bound && (regs.rb_depthcontrol & 0x2)) ? TRUE : FALSE;
  update_desc_.DepthStencilState.DepthWriteMask =
      (dsv_bound && (regs.rb_depthcontrol & 0x4)) ? D3D12_DEPTH_WRITE_MASK_ALL
                                                  : D3D12_DEPTH_WRITE_MASK_ZERO;
  // Comparison functions are the same in Direct3D 12 but plus one (minus one,
  // bit 0 for less, bit 1 for equal, bit 2 for greater).
  update_desc_.DepthStencilState.DepthFunc =
      D3D12_COMPARISON_FUNC(((regs.rb_depthcontrol >> 4) & 0x7) + 1);
  update_desc_.DepthStencilState.StencilEnable =
      (dsv_bound && (regs.rb_depthcontrol & 0x1)) ? TRUE : FALSE;
  update_desc_.DepthStencilState.StencilReadMask =
      (regs.rb_stencilrefmask >> 8) & 0xFF;
  update_desc_.DepthStencilState.StencilWriteMask =
      (regs.rb_stencilrefmask >> 16) & 0xFF;
  // Stencil operations are the same in Direct3D 12 too but plus one.
  update_desc_.DepthStencilState.FrontFace.StencilFailOp =
      D3D12_STENCIL_OP(((regs.rb_depthcontrol >> 11) & 0x7) + 1);
  update_desc_.DepthStencilState.FrontFace.StencilDepthFailOp =
      D3D12_STENCIL_OP(((regs.rb_depthcontrol >> 17) & 0x7) + 1);
  update_desc_.DepthStencilState.FrontFace.StencilPassOp =
      D3D12_STENCIL_OP(((regs.rb_depthcontrol >> 14) & 0x7) + 1);
  update_desc_.DepthStencilState.FrontFace.StencilFunc =
      D3D12_COMPARISON_FUNC(((regs.rb_depthcontrol >> 8) & 0x7) + 1);
  // BACKFACE_ENABLE.
  if (regs.rb_depthcontrol & 0x80) {
    update_desc_.DepthStencilState.BackFace.StencilFailOp =
        D3D12_STENCIL_OP(((regs.rb_depthcontrol >> 23) & 0x7) + 1);
    update_desc_.DepthStencilState.BackFace.StencilDepthFailOp =
        D3D12_STENCIL_OP(((regs.rb_depthcontrol >> 29) & 0x7) + 1);
    update_desc_.DepthStencilState.BackFace.StencilPassOp =
        D3D12_STENCIL_OP(((regs.rb_depthcontrol >> 26) & 0x7) + 1);
    update_desc_.DepthStencilState.BackFace.StencilFunc =
        D3D12_COMPARISON_FUNC(((regs.rb_depthcontrol >> 20) & 0x7) + 1);
  } else {
    // Back state is identical to front state.
    update_desc_.DepthStencilState.BackFace =
        update_desc_.DepthStencilState.FrontFace;
  }
  // TODO(Triang3l): EARLY_Z_ENABLE (needs to be enabled in shaders, but alpha
  // test is dynamic - should be enabled anyway if there's no alpha test,
  // discarding and depth output).

  update_desc_.DSVFormat = format;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateIBStripCutValue(
    IndexFormat index_format) {
  auto& regs = update_ib_strip_cut_value_regs_;

  bool dirty = current_pipeline_ == nullptr;
  D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ib_strip_cut_value =
      D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  if (register_file_->values[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & (1 << 21)) {
    ib_strip_cut_value = index_format == IndexFormat::kInt32
                             ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF
                             : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
  }
  dirty |= regs.ib_strip_cut_value != ib_strip_cut_value;
  regs.ib_strip_cut_value = ib_strip_cut_value;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  update_desc_.IBStripCutValue = ib_strip_cut_value;

  // TODO(Triang3l): Geometry shaders for non-0xFFFF values if they are used.

  return UpdateStatus::kMismatch;
}

PipelineCache::Pipeline* PipelineCache::GetPipeline(uint64_t hash_key) {
  // Lookup the pipeline in the cache.
  auto it = pipelines_.find(hash_key);
  if (it != pipelines_.end()) {
    // Found existing pipeline.
    return it->second;
  }

  // TODO(Triang3l): Cache create pipelines using CachedPSO.

  if (update_shader_stages_regs_.pixel_shader != nullptr) {
    XELOGGPU(
        "Creating pipeline %.16" PRIX64 ", VS %.16" PRIX64 ", PS %.16" PRIX64,
        hash_key, update_shader_stages_regs_.vertex_shader->ucode_data_hash(),
        update_shader_stages_regs_.pixel_shader->ucode_data_hash());
  } else {
    XELOGGPU("Creating pipeline %.16" PRIX64 ", VS %.16" PRIX64, hash_key,
             update_shader_stages_regs_.vertex_shader->ucode_data_hash());
  }

  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  ID3D12PipelineState* state;
  if (FAILED(device->CreateGraphicsPipelineState(&update_desc_,
                                                 IID_PPV_ARGS(&state)))) {
    XELOGE("Failed to create graphics pipeline state");
    return nullptr;
  }

  std::wstring name;
  if (update_shader_stages_regs_.pixel_shader != nullptr) {
    name = xe::format_string(
        L"VS %.16I64X, PS %.16I64X, PL %.16I64X",
        update_shader_stages_regs_.vertex_shader->ucode_data_hash(),
        update_shader_stages_regs_.pixel_shader->ucode_data_hash(), hash_key);
  } else {
    name = xe::format_string(
        L"VS %.16I64X, PL %.16I64X",
        update_shader_stages_regs_.vertex_shader->ucode_data_hash(), hash_key);
  }
  state->SetName(name.c_str());

  // Add to cache with the hash key for reuse.
  Pipeline* pipeline = new Pipeline;
  pipeline->state = state;
  pipeline->root_signature = update_desc_.pRootSignature;
  pipelines_.insert({hash_key, pipeline});
  return pipeline;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
