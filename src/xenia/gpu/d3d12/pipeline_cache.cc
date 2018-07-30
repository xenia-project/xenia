/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/pipeline_cache.h"

#include <cinttypes>
#include <cmath>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/hlsl_shader_translator.h"

namespace xe {
namespace gpu {
namespace d3d12 {

PipelineCache::PipelineCache(D3D12CommandProcessor* command_processor,
                             RegisterFile* register_file,
                             ui::d3d12::D3D12Context* context)
    : command_processor_(command_processor),
      register_file_(register_file),
      context_(context) {
  shader_translator_.reset(new HlslShaderTranslator());

  // Set pipeline state description values we never change.
  // Zero out tessellation, stream output, blend state and formats for render
  // targets 4+, node mask, cached PSO, flags and other things.
  std::memset(&update_desc_, 0, sizeof(update_desc_));
  update_desc_.BlendState.IndependentBlendEnable = TRUE;
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

PipelineCache::UpdateStatus PipelineCache::ConfigurePipeline(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type, IndexFormat index_format,
    ID3D12PipelineState** pipeline_out,
    ID3D12RootSignature** root_signature_out) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_not_null(pipeline_out);
  assert_not_null(root_signature_out);

  Pipeline* pipeline = nullptr;
  auto update_status =
      UpdateState(vertex_shader, pixel_shader, primitive_type, index_format);
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
                                    xenos::xe_gpu_program_cntl_t cntl) {
  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_->Translate(shader, cntl)) {
    XELOGE("Shader translation failed; marking shader as ignored");
    return false;
  }

  // Prepare the shader for use (creates the Shader Model bytecode).
  // It could still fail at this point.
  if (!shader->Prepare()) {
    XELOGE("Shader preparation failed; marking shader as ignored");
    return false;
  }

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader (%db) - hash %.16" PRIX64 ":\n%s\n",
             shader->type() == ShaderType::kVertex ? "vertex" : "pixel",
             shader->ucode_dword_count() * 4, shader->ucode_data_hash(),
             shader->ucode_disassembly().c_str());
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "d3d12");
  }

  return shader->is_valid();
}

PipelineCache::UpdateStatus PipelineCache::UpdateState(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type, IndexFormat index_format) {
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
  status = UpdateBlendState(pixel_shader);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update blend state");
  status = UpdateRasterizerState(primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterizer state");
  status = UpdateDepthStencilState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");
  status = UpdateIBStripCutValue(index_format);
  CHECK_UPDATE_STATUS(status, mismatch,
                      "Unable to update index buffer strip cut value");
  status = UpdateRenderTargetFormats();
  CHECK_UPDATE_STATUS(status, mismatch,
                      "Unable to update render target formats");
#undef CHECK_UPDATE_STATUS

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateShaderStages(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type) {
  auto& regs = update_shader_stages_regs_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 ==
                  0x000FF000 ||
              register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 ==
                  0x000FF100 ||
              register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  bool dirty = current_pipeline_ == nullptr;
  dirty |= SetShadowRegister(&regs.sq_program_cntl, XE_GPU_REG_SQ_PROGRAM_CNTL);
  dirty |= regs.vertex_shader != vertex_shader;
  dirty |= regs.pixel_shader != pixel_shader;
  regs.vertex_shader = vertex_shader;
  regs.pixel_shader = pixel_shader;
  // Points are emulated via a geometry shader because Direct3D 10+ doesn't
  // support point sizes other than 1.
  bool primitive_topology_is_line =
      primitive_type == PrimitiveType::kLineList ||
      primitive_type == PrimitiveType::kLineStrip ||
      primitive_type == PrimitiveType::kLineLoop ||
      primitive_type == PrimitiveType::k2DLineStrip;
  dirty |= regs.primitive_topology_is_line != primitive_topology_is_line;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  xenos::xe_gpu_program_cntl_t sq_program_cntl;
  sq_program_cntl.dword_0 = regs.sq_program_cntl;
  if (!vertex_shader->is_translated() &&
      !TranslateShader(vertex_shader, sq_program_cntl)) {
    XELOGE("Failed to translate the vertex shader!");
    return UpdateStatus::kError;
  }
  if (pixel_shader != nullptr && !pixel_shader->is_translated() &&
      !TranslateShader(pixel_shader, sq_program_cntl)) {
    XELOGE("Failed to translate the pixel shader!");
    return UpdateStatus::kError;
  }

  update_desc_.pRootSignature =
      command_processor_->GetRootSignature(vertex_shader, pixel_shader);
  if (update_desc_.pRootSignature == nullptr) {
    return UpdateStatus::kError;
  }
  update_desc_.VS.pShaderBytecode = vertex_shader->GetDXBC();
  update_desc_.VS.BytecodeLength = vertex_shader->GetDXBCSize();
  if (pixel_shader != nullptr) {
    update_desc_.PS.pShaderBytecode = pixel_shader->GetDXBC();
    update_desc_.PS.BytecodeLength = pixel_shader->GetDXBCSize();
  } else {
    update_desc_.PS.pShaderBytecode = nullptr;
    update_desc_.PS.BytecodeLength = 0;
  }
  // TODO(Triang3l): Geometry shaders.
  update_desc_.GS.pShaderBytecode = nullptr;
  update_desc_.GS.BytecodeLength = 0;
  update_desc_.PrimitiveTopologyType =
      primitive_topology_is_line ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                 : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateBlendState(
    D3D12Shader* pixel_shader) {
  auto& regs = update_blend_state_regs_;

  bool dirty = current_pipeline_ == nullptr;
  uint32_t color_mask;
  if (pixel_shader != nullptr) {
    color_mask = register_file_->values[XE_GPU_REG_RB_COLOR_MASK].u32 & 0xFFFF;
    // If the pixel shader doesn't write to a render target, writing to it is
    // disabled in the blend state. Otherwise, in Halo 3, one important render
    // target is destroyed by a shader not writing to one of the outputs.
    for (uint32_t i = 0; i < 4; ++i) {
      if (!pixel_shader->writes_color_target(i)) {
        color_mask &= ~(0xF << (i * 4));
      }
    }
  } else {
    color_mask = 0;
  }
  dirty |= regs.color_mask != color_mask;
  regs.color_mask = color_mask;
  bool blend_enable =
      color_mask != 0 &&
      !(register_file_->values[XE_GPU_REG_RB_COLOR_MASK].u32 & 0x20);
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
  static const D3D12_BLEND_OP kBlendOpMap[] = {
      /*  0 */ D3D12_BLEND_OP_ADD,
      /*  1 */ D3D12_BLEND_OP_SUBTRACT,
      /*  2 */ D3D12_BLEND_OP_MIN,
      /*  3 */ D3D12_BLEND_OP_MAX,
      /*  4 */ D3D12_BLEND_OP_REV_SUBTRACT,
  };
  for (uint32_t i = 0; i < 4; ++i) {
    auto& blend_desc = update_desc_.BlendState.RenderTarget[i];
    if (blend_enable && (color_mask & (0xF << (i * 4)))) {
      uint32_t blend_control = regs.blendcontrol[i];
      // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
      blend_desc.SrcBlend = kBlendFactorMap[(blend_control & 0x0000001F) >> 0];
      // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
      blend_desc.DestBlend = kBlendFactorMap[(blend_control & 0x00001F00) >> 8];
      // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
      blend_desc.BlendOp = kBlendOpMap[(blend_control & 0x000000E0) >> 5];
      // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
      blend_desc.SrcBlendAlpha =
          kBlendFactorMap[(blend_control & 0x001F0000) >> 16];
      // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
      blend_desc.DestBlendAlpha =
          kBlendFactorMap[(blend_control & 0x1F000000) >> 24];
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
    blend_desc.RenderTargetWriteMask = (color_mask >> (i * 4)) & 0xF;
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRasterizerState(
    PrimitiveType primitive_type) {
  auto& regs = update_rasterizer_state_regs_;

  bool dirty = current_pipeline_ == nullptr;
  uint32_t pa_su_sc_mode_cntl =
      register_file_->values[XE_GPU_REG_RB_COLOR_MASK].u32;
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
  if (!(cull_mode & 1)) {
    // Front faces aren't culled.
    uint32_t fill_mode = (pa_su_sc_mode_cntl >> 5) & 0x7;
    if (fill_mode == 0 || fill_mode == 1) {
      fill_mode_wireframe = true;
    }
    if ((pa_su_sc_mode_cntl >> 11) & 0x1) {
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
    // Prefer front depth bias because in general, front faces are the ones that
    // are rendered (except for shadow volumes).
    if (((pa_su_sc_mode_cntl >> 12) & 0x1) && poly_offset == 0.0f &&
        poly_offset_scale == 0.0f) {
      poly_offset =
          register_file_->values[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET].f32;
      poly_offset_scale =
          register_file_->values[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE].f32;
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
  // Conversion based on the calculations in Call of Duty 4 and the values it
  // writes to the registers, and also on:
  // https://github.com/mesa3d/mesa/blob/54ad9b444c8e73da498211870e785239ad3ff1aa/src/gallium/drivers/radeonsi/si_state.c#L943
  // Call of Duty 4 sets the constant bias of 1/32768 and the slope scale of 32.
  // However, it's calculated from a console variable in 2 parts: first it's
  // divided by 65536, and then it's multiplied by 2.
  // TODO(Triang3l): Find the best scale. According to si_state.c, the value in
  // the register should be divided by 2 to get the value suitable for PC
  // graphics APIs if the depth buffer is 24-bit. However, even multiplying by
  // 65536 rather than 32768 still doesn't remove shadow acne in Bomberman Live
  // completely. Maybe 131072 would work the best.
  // Using ceil here just in case a game wants the offset but passes a value
  // that is too small - it's better to apply more offset than to make depth
  // fighting worse or to disable the offset completely (Direct3D 12 takes an
  // integer value).
  update_desc_.RasterizerState.DepthBias =
      int32_t(std::ceil(std::abs(poly_offset) * 131072.0f));
  update_desc_.RasterizerState.DepthBias *= poly_offset < 0.0f ? -1 : 1;
  update_desc_.RasterizerState.DepthBiasClamp = 0.0f;
  update_desc_.RasterizerState.SlopeScaledDepthBias =
      poly_offset_scale * (1.0f / 16.0f);
  update_desc_.RasterizerState.DepthClipEnable =
      !depth_clamp_enable ? TRUE : FALSE;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateDepthStencilState() {
  auto& regs = update_depth_stencil_state_regs_;

  bool dirty = current_pipeline_ == nullptr;
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  update_desc_.DepthStencilState.DepthEnable =
      (regs.rb_depthcontrol & 0x2) ? TRUE : FALSE;
  update_desc_.DepthStencilState.DepthWriteMask =
      (regs.rb_depthcontrol & 0x4) ? D3D12_DEPTH_WRITE_MASK_ALL
                                   : D3D12_DEPTH_WRITE_MASK_ZERO;
  // Comparison functions are the same in Direct3D 12 but plus one (minus one,
  // bit 0 for less, bit 1 for equal, bit 2 for greater).
  update_desc_.DepthStencilState.DepthFunc =
      D3D12_COMPARISON_FUNC(((regs.rb_depthcontrol >> 4) & 0x7) + 1);
  update_desc_.DepthStencilState.StencilEnable =
      (regs.rb_depthcontrol & 0x1) ? TRUE : FALSE;
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

PipelineCache::UpdateStatus PipelineCache::UpdateRenderTargetFormats() {
  bool dirty = current_pipeline_ == nullptr;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  // TODO(Triang3l): Set the formats when RT cache is added.
  update_desc_.NumRenderTargets = 0;
  update_desc_.DSVFormat = DXGI_FORMAT_UNKNOWN;

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

  auto device = context_->GetD3D12Provider()->GetDevice();
  ID3D12PipelineState* state;
  if (FAILED(device->CreateGraphicsPipelineState(&update_desc_,
                                                 IID_PPV_ARGS(&state)))) {
    XELOGE("Failed to create graphics pipeline state");
    return nullptr;
  }
  // TODO(Triang3l): Set the name for the pipeline, with shader hashes.

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
