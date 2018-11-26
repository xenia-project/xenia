/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

#include "third_party/dxbc/DXBCChecksum.h"
#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"

DEFINE_bool(dxbc_indexable_temps, true,
            "Use indexable temporary registers in translated DXBC shaders for "
            "relative addressing of general-purpose registers - shaders rarely "
            "do that, but when they do, this may improve performance on AMD, "
            "but may cause unknown issues on Nvidia.");
DEFINE_bool(dxbc_switch, true,
            "Use switch rather than if for flow control. Turning this off or "
            "on may improve stability, though this heavily depends on the "
            "driver - on AMD, it's recommended to have this set to true, as "
            "Halo 3 appears to crash when if is used for flow control "
            "(possibly the shader compiler tries to flatten them).");
DEFINE_bool(dxbc_source_map, false,
            "Disassemble Xenos instructions as comments in the resulting DXBC "
            "for debugging.");

namespace xe {
namespace gpu {
using namespace ucode;

// Notes about operands:
//
// Reading and writing:
// - Writes to 4-component registers must be masked.
// - Reads from 4-component registers can be swizzled, or 1 component can be
//   selected.
// - r# (temporary registers) are 4-component and can be used anywhere.
// - v# (inputs) are 4-component and read-only.
// - o# (outputs) are 4-component and write-only.
// - oDepth (pixel shader depth output) is 1-component and write-only.
// - x# (indexable temporary registers) are 4-component (though not sure what
//   happens if you dcl them as 1-component) and can be accessed either via
//   a mov load or a mov store (and those movs are counted as ArrayInstructions
//   in STAT, not as MovInstructions).
//
// Indexing:
// - Constant buffers use 3D indices in CBx[y][z] format, where x is the ID of
//   the binding (CB#), y is the register to access within its space, z is the
//   4-component vector to access within the register binding.
//   For example, if the requested vector is located in the beginning of the
//   second buffer in the descriptor array at b2, which is assigned to CB1, the
//   index would be CB1[3][0].
// - Resources and samplers use 2D indices, where the first dimension is the
//   S#/T#/U# binding index, and the second is the s#/t#/u# register index
//   within its space.

constexpr uint32_t DxbcShaderTranslator::kMaxTextureSRVIndexBits;
constexpr uint32_t DxbcShaderTranslator::kMaxTextureSRVs;
constexpr uint32_t DxbcShaderTranslator::kMaxSamplerBindingIndexBits;
constexpr uint32_t DxbcShaderTranslator::kMaxSamplerBindings;
constexpr uint32_t DxbcShaderTranslator::kInterpolatorCount;
constexpr uint32_t DxbcShaderTranslator::kPointParametersTexCoord;
constexpr uint32_t DxbcShaderTranslator::kClipSpaceZWTexCoord;
constexpr uint32_t DxbcShaderTranslator::kSwizzleXYZW;
constexpr uint32_t DxbcShaderTranslator::kSwizzleXXXX;
constexpr uint32_t DxbcShaderTranslator::kSwizzleYYYY;
constexpr uint32_t DxbcShaderTranslator::kSwizzleZZZZ;
constexpr uint32_t DxbcShaderTranslator::kSwizzleWWWW;
constexpr uint32_t
    DxbcShaderTranslator::DxbcSourceOperand::kIntermediateRegisterNone;
constexpr uint32_t DxbcShaderTranslator::kCbufferIndexUnallocated;
constexpr uint32_t DxbcShaderTranslator::kCfExecBoolConstantNone;

DxbcShaderTranslator::DxbcShaderTranslator(bool edram_rov_used)
    : edram_rov_used_(edram_rov_used) {
  // Don't allocate again and again for the first shader.
  shader_code_.reserve(8192);
  shader_object_.reserve(16384);
  float_constant_index_offsets_.reserve(512);
}
DxbcShaderTranslator::~DxbcShaderTranslator() = default;

uint32_t DxbcShaderTranslator::GetColorFormatRTFlags(
    ColorRenderTargetFormat format) {
  static const uint32_t kRTFormatFlags[16] = {
      // k_8_8_8_8
      kRTFlag_FormatFixed,
      // k_8_8_8_8_GAMMA
      kRTFlag_FormatFixed,
      // k_2_10_10_10
      kRTFlag_FormatFixed,
      // k_2_10_10_10_FLOAT
      kRTFlag_FormatFloat10,
      // k_16_16
      kRTFlag_FormatFixed | kRTFlag_FormatUnusedB | kRTFlag_FormatUnusedA,
      // k_16_16_16_16
      kRTFlag_FormatFixed,
      // k_16_16_FLOAT
      kRTFlag_FormatFloat16 | kRTFlag_FormatUnusedB | kRTFlag_FormatUnusedA,
      // k_16_16_16_16_FLOAT
      kRTFlag_FormatFloat16,
      // Unused
      kRTFlag_FormatUnusedR | kRTFlag_FormatUnusedG | kRTFlag_FormatUnusedB |
          kRTFlag_FormatUnusedA,
      // Unused
      kRTFlag_FormatUnusedR | kRTFlag_FormatUnusedG | kRTFlag_FormatUnusedB |
          kRTFlag_FormatUnusedA,
      // k_2_10_10_10_AS_16_16_16_16
      kRTFlag_FormatFixed,
      // Unused.
      kRTFlag_FormatUnusedR | kRTFlag_FormatUnusedG | kRTFlag_FormatUnusedB |
          kRTFlag_FormatUnusedA,
      // k_2_10_10_10_FLOAT_AS_16_16_16_16
      kRTFlag_FormatFloat10,
      // Unused.
      kRTFlag_FormatUnusedR | kRTFlag_FormatUnusedG | kRTFlag_FormatUnusedB |
          kRTFlag_FormatUnusedA,
      // k_32_FLOAT
      kRTFlag_FormatUnusedG | kRTFlag_FormatUnusedB | kRTFlag_FormatUnusedA,
      // k_32_32_FLOAT
      kRTFlag_FormatUnusedB | kRTFlag_FormatUnusedA,
  };
  return kRTFormatFlags[uint32_t(format)];
}

void DxbcShaderTranslator::SetColorFormatSystemConstants(
    SystemConstants& constants, uint32_t rt_index,
    ColorRenderTargetFormat format) {
  constants.edram_rt_pack_width_high[rt_index] = 0;
  constants.edram_rt_pack_offset_high[rt_index] = 0;
  uint32_t color_mask = UINT32_MAX, alpha_mask = UINT32_MAX;
  uint32_t color_min = 0, alpha_min = 0;
  uint32_t color_max = 0x3F800000, alpha_max = 0x3F800000;
  float color_load_scale = 1.0f, alpha_load_scale = 1.0f;
  float color_store_scale = 1.0f, alpha_store_scale = 1.0f;
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      constants.edram_rt_pack_width_low[rt_index] =
          8 | (8 << 8) | (8 << 16) | (8 << 24);
      constants.edram_rt_pack_offset_low[rt_index] =
          (8 << 8) | (16 << 16) | (24 << 24);
      color_mask = alpha_mask = 255;
      color_load_scale = alpha_load_scale = 1.0f / 255.0f;
      color_store_scale = alpha_store_scale = 255.0f;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_AS_16_16_16_16:
      constants.edram_rt_pack_width_low[rt_index] =
          10 | (10 << 8) | (10 << 16) | (2 << 24);
      constants.edram_rt_pack_offset_low[rt_index] =
          (10 << 8) | (20 << 16) | (30 << 24);
      color_mask = 1023;
      alpha_mask = 3;
      color_load_scale = 1.0f / 1023.0f;
      alpha_load_scale = 1.0f / 3.0f;
      color_store_scale = 1023.0f;
      alpha_store_scale = 3.0f;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      constants.edram_rt_pack_width_low[rt_index] =
          10 | (10 << 8) | (10 << 16) | (2 << 24);
      constants.edram_rt_pack_offset_low[rt_index] =
          (10 << 8) | (20 << 16) | (30 << 24);
      color_mask = 1023;
      alpha_mask = 3;
      // 31.875.
      color_max = 0x41FF0000;
      alpha_load_scale = 1.0f / 3.0f;
      alpha_store_scale = 3.0f;
      break;
    case ColorRenderTargetFormat::k_16_16:
    case ColorRenderTargetFormat::k_16_16_16_16:
      constants.edram_rt_pack_width_low[rt_index] = 16 | (16 << 8);
      constants.edram_rt_pack_offset_low[rt_index] = 16 << 8;
      if (format == ColorRenderTargetFormat::k_16_16_16_16) {
        constants.edram_rt_pack_width_high[rt_index] = (16 << 16) | (16 << 24);
        constants.edram_rt_pack_offset_high[rt_index] = 16 << 24;
      }
      // -32.0.
      color_min = alpha_min = 0xC2000000u;
      // 32.0.
      color_max = alpha_max = 0x42000000u;
      color_load_scale = alpha_load_scale = 32.0f / 32767.0f;
      color_store_scale = alpha_store_scale = 32767.0f / 32.0f;
      break;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      constants.edram_rt_pack_width_low[rt_index] = 16 | (16 << 8);
      constants.edram_rt_pack_offset_low[rt_index] = 16 << 8;
      if (format == ColorRenderTargetFormat::k_16_16_16_16_FLOAT) {
        constants.edram_rt_pack_width_high[rt_index] = (16 << 16) | (16 << 24);
        constants.edram_rt_pack_offset_high[rt_index] = 16 << 24;
      }
      color_mask = alpha_mask = 0xFFFF;
      // -65504.0 to 65504.0 - the Xbox 360 doesn't have Infinity or NaN in
      // float16, instead it has the range expanded to 131008.0, however,
      // supporting it correctly would require making changes to texture
      // formats (float32 would be required for emulating textures, which is
      // pretty big, resolves also will require conversion; vertex fetch, vpkd3d
      // CPU instruction). The precision in the 65504-131008 range is very low
      // anyway, let's hope games don't really rely on it. So let's only clamp
      // to a finite value to remove specials from blending.
      // https://blogs.msdn.microsoft.com/chuckw/2013/03/05/known-issues-directxmath-3-03/
      // TODO(Triang3l): Maybe handle float16 correctly everywhere.
      color_min = alpha_min = 0xC77FE000u;
      color_max = alpha_max = 0x477FE000u;
      break;
    case ColorRenderTargetFormat::k_32_FLOAT:
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      constants.edram_rt_pack_width_low[rt_index] = 32;
      constants.edram_rt_pack_offset_low[rt_index] = 0;
      if (format == ColorRenderTargetFormat::k_32_32_FLOAT) {
        constants.edram_rt_pack_width_high[rt_index] = 32;
      }
      // -Infinity.
      color_min = alpha_min = 0xFF800000u;
      // Infinity.
      color_max = alpha_max = 0x7F800000u;
      break;
    default:
      assert_always();
      break;
  }
  uint32_t rt_pair_index = rt_index >> 1;
  uint32_t rt_pair_comp = (rt_index & 1) << 1;
  constants.edram_load_mask_rt01_rt23[rt_pair_index][rt_pair_comp] = color_mask;
  constants.edram_load_mask_rt01_rt23[rt_pair_index][rt_pair_comp + 1] =
      alpha_mask;
  constants.edram_load_scale_rt01_rt23[rt_pair_index][rt_pair_comp] =
      color_load_scale;
  constants.edram_load_scale_rt01_rt23[rt_pair_index][rt_pair_comp + 1] =
      alpha_load_scale;
  constants.edram_store_min_rt01_rt23[rt_pair_index][rt_pair_comp] = color_min;
  constants.edram_store_min_rt01_rt23[rt_pair_index][rt_pair_comp + 1] =
      alpha_min;
  constants.edram_store_max_rt01_rt23[rt_pair_index][rt_pair_comp] = color_max;
  constants.edram_store_max_rt01_rt23[rt_pair_index][rt_pair_comp + 1] =
      alpha_max;
  constants.edram_store_scale_rt01_rt23[rt_pair_index][rt_pair_comp] =
      color_store_scale;
  constants.edram_store_scale_rt01_rt23[rt_pair_index][rt_pair_comp + 1] =
      alpha_store_scale;
}

bool DxbcShaderTranslator::GetBlendConstants(uint32_t blend_control,
                                             uint32_t& blend_x_out,
                                             uint32_t& blend_y_out) {
  static const uint32_t kBlendXSrcFactorMap[32] = {
      0,
      kBlendX_Src_One,
      0,
      0,
      kBlendX_Src_SrcColor_Pos,
      kBlendX_Src_One | kBlendX_Src_SrcColor_Neg,
      kBlendX_Src_SrcAlpha_Pos,
      kBlendX_Src_One | kBlendX_Src_SrcAlpha_Neg,
      kBlendX_Src_DestColor_Pos,
      kBlendX_Src_One | kBlendX_Src_DestColor_Neg,
      kBlendX_Src_DestAlpha_Pos,
      kBlendX_Src_One | kBlendX_Src_DestAlpha_Neg,
      0,
      kBlendX_Src_One,
      0,
      kBlendX_Src_One,
      kBlendX_Src_SrcAlphaSaturate,
  };
  static const uint32_t kBlendYSrcFactorMap[32] = {
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      kBlendY_Src_ConstantColor_Pos,
      kBlendY_Src_ConstantColor_Neg,
      kBlendY_Src_ConstantAlpha_Pos,
      kBlendY_Src_ConstantAlpha_Neg,
      0,
  };
  static const uint32_t kBlendXSrcAlphaFactorMap[32] = {
      0,
      kBlendX_SrcAlpha_One,
      0,
      0,
      kBlendX_SrcAlpha_SrcAlpha_Pos,
      kBlendX_SrcAlpha_One | kBlendX_SrcAlpha_SrcAlpha_Neg,
      kBlendX_SrcAlpha_SrcAlpha_Pos,
      kBlendX_SrcAlpha_One | kBlendX_SrcAlpha_SrcAlpha_Neg,
      kBlendX_SrcAlpha_DestAlpha_Pos,
      kBlendX_SrcAlpha_One | kBlendX_SrcAlpha_DestAlpha_Neg,
      kBlendX_SrcAlpha_DestAlpha_Pos,
      kBlendX_SrcAlpha_One | kBlendX_SrcAlpha_DestAlpha_Neg,
      0,
      kBlendX_SrcAlpha_One,
      0,
      kBlendX_SrcAlpha_One,
      kBlendX_SrcAlpha_One,
  };
  static const uint32_t kBlendYSrcAlphaFactorMap[32] = {
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      kBlendY_SrcAlpha_ConstantAlpha_Pos,
      kBlendY_SrcAlpha_ConstantAlpha_Neg,
      kBlendY_SrcAlpha_ConstantAlpha_Pos,
      kBlendY_SrcAlpha_ConstantAlpha_Neg,
      0,
  };
  static const uint32_t kBlendXDestFactorMap[32] = {
      0,
      kBlendX_Dest_One,
      0,
      0,
      kBlendX_Dest_SrcColor_Pos,
      kBlendX_Dest_One | kBlendX_Dest_SrcColor_Neg,
      kBlendX_Dest_SrcAlpha_Pos,
      kBlendX_Dest_One | kBlendX_Dest_SrcAlpha_Neg,
      kBlendX_Dest_DestColor_Pos,
      kBlendX_Dest_One | kBlendX_Dest_DestColor_Neg,
      kBlendX_Dest_DestAlpha_Pos,
      kBlendX_Dest_One | kBlendX_Dest_DestAlpha_Neg,
      0,
      kBlendX_Dest_One,
      0,
      kBlendX_Dest_One,
      kBlendX_Dest_SrcAlphaSaturate,
  };
  static const uint32_t kBlendYDestFactorMap[32] = {
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      kBlendY_Dest_ConstantColor_Pos,
      kBlendY_Dest_ConstantColor_Neg,
      kBlendY_Dest_ConstantAlpha_Pos,
      kBlendY_Dest_ConstantAlpha_Neg,
      0,
  };
  static const uint32_t kBlendXDestAlphaFactorMap[32] = {
      0,
      kBlendX_DestAlpha_One,
      0,
      0,
      kBlendX_DestAlpha_SrcAlpha_Pos,
      kBlendX_DestAlpha_One | kBlendX_DestAlpha_SrcAlpha_Neg,
      kBlendX_DestAlpha_SrcAlpha_Pos,
      kBlendX_DestAlpha_One | kBlendX_DestAlpha_SrcAlpha_Neg,
      kBlendX_DestAlpha_DestAlpha_Pos,
      kBlendX_DestAlpha_One | kBlendX_DestAlpha_DestAlpha_Neg,
      kBlendX_DestAlpha_DestAlpha_Pos,
      kBlendX_DestAlpha_One | kBlendX_DestAlpha_DestAlpha_Neg,
      0,
      kBlendX_DestAlpha_One,
      0,
      kBlendX_DestAlpha_One,
      kBlendX_DestAlpha_One,
  };
  static const uint32_t kBlendYDestAlphaFactorMap[32] = {
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      kBlendY_DestAlpha_ConstantAlpha_Pos,
      kBlendY_DestAlpha_ConstantAlpha_Neg,
      kBlendY_DestAlpha_ConstantAlpha_Pos,
      kBlendY_DestAlpha_ConstantAlpha_Neg,
      0,
  };

  uint32_t blend_x = 0, blend_y = 0;

  // Min and max don't use the factors.

  BlendOp op_color = BlendOp((blend_control >> 5) & 0x7);
  if (op_color == BlendOp::kMin) {
    blend_y |= kBlendY_Color_OpMin;
  } else if (op_color == BlendOp::kMax) {
    blend_y |= kBlendY_Color_OpMax;
  } else {
    uint32_t src_factor = blend_control & 0x1F;
    uint32_t dest_factor = (blend_control >> 8) & 0x1F;
    blend_x |=
        kBlendXSrcFactorMap[src_factor] | kBlendXDestFactorMap[dest_factor];
    blend_y |=
        kBlendYSrcFactorMap[src_factor] | kBlendYDestFactorMap[dest_factor];
    switch (op_color) {
      case BlendOp::kAdd:
        blend_y |= kBlendY_Src_OpSign_Pos | kBlendY_Dest_OpSign_Pos;
        break;
      case BlendOp::kSubtract:
        blend_y |= kBlendY_Src_OpSign_Pos | kBlendY_Dest_OpSign_Neg;
        break;
      case BlendOp::kRevSubtract:
        blend_y |= kBlendY_Src_OpSign_Neg | kBlendY_Dest_OpSign_Pos;
        break;
      default:
        assert_always();
    }
  }

  BlendOp op_alpha = BlendOp((blend_control >> 21) & 0x7);
  if (op_alpha == BlendOp::kMin) {
    blend_y |= kBlendY_Alpha_OpMin;
  } else if (op_alpha == BlendOp::kMax) {
    blend_y |= kBlendY_Alpha_OpMax;
  } else {
    uint32_t src_alpha_factor = (blend_control >> 16) & 0x1F;
    uint32_t dest_alpha_factor = (blend_control >> 24) & 0x1F;
    blend_x |= kBlendXSrcAlphaFactorMap[src_alpha_factor] |
               kBlendXDestAlphaFactorMap[dest_alpha_factor];
    blend_y |= kBlendYSrcAlphaFactorMap[src_alpha_factor] |
               kBlendYDestAlphaFactorMap[dest_alpha_factor];
    switch (op_alpha) {
      case BlendOp::kAdd:
        blend_y |= kBlendY_SrcAlpha_OpSign_Pos | kBlendY_DestAlpha_OpSign_Pos;
        break;
      case BlendOp::kSubtract:
        blend_y |= kBlendY_SrcAlpha_OpSign_Pos | kBlendY_DestAlpha_OpSign_Neg;
        break;
      case BlendOp::kRevSubtract:
        blend_y |= kBlendY_SrcAlpha_OpSign_Neg | kBlendY_DestAlpha_OpSign_Pos;
        break;
      default:
        assert_always();
    }
  }

  blend_x_out = blend_x;
  blend_y_out = blend_y;

  // 1 * src + 0 * dest is nop, don't waste GPU time.
  return (blend_control & 0x1FFF1FFF) != 0x00010001;
}

std::vector<uint8_t> DxbcShaderTranslator::CreateDepthOnlyPixelShader() {
  Reset();
  is_depth_only_pixel_shader_ = true;
  StartTranslation();
  return std::move(CompleteTranslation());
}

void DxbcShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  shader_code_.clear();

  is_depth_only_pixel_shader_ = false;

  cbuffer_count_ = 0;
  // System constants always used in prologues/epilogues.
  cbuffer_index_system_constants_ = cbuffer_count_++;
  cbuffer_index_float_constants_ = kCbufferIndexUnallocated;
  cbuffer_index_bool_loop_constants_ = kCbufferIndexUnallocated;
  cbuffer_index_fetch_constants_ = kCbufferIndexUnallocated;

  system_constants_used_ = 0;
  float_constants_dynamic_indexed_ = false;
  bool_loop_constants_dynamic_indexed_ = false;
  float_constant_index_offsets_.clear();

  system_temp_count_current_ = 0;
  system_temp_count_max_ = 0;

  cf_exec_bool_constant_ = kCfExecBoolConstantNone;
  cf_exec_predicated_ = false;
  cf_instruction_predicate_if_open_ = false;
  cf_exec_predicate_written_ = false;

  texture_srvs_.clear();
  sampler_bindings_.clear();

  std::memset(&stat_, 0, sizeof(stat_));
}

uint32_t DxbcShaderTranslator::PushSystemTemp(bool zero) {
  uint32_t register_index = system_temp_count_current_;
  if (!IndexableGPRsUsed() && !is_depth_only_pixel_shader_) {
    // Guest shader registers first if they're not in x0. Depth-only pixel
    // shader is a special case of the DXBC translator usage, where there are no
    // GPRs because there's no shader to translate, and a guest shader is not
    // loaded.
    register_index += register_count();
  }
  ++system_temp_count_current_;
  system_temp_count_max_ =
      std::max(system_temp_count_max_, system_temp_count_current_);

  if (zero) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(register_index);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  return register_index;
}

void DxbcShaderTranslator::PopSystemTemp(uint32_t count) {
  assert_true(count <= system_temp_count_current_);
  system_temp_count_current_ -= std::min(count, system_temp_count_current_);
}

bool DxbcShaderTranslator::IndexableGPRsUsed() const {
  return FLAGS_dxbc_indexable_temps && uses_register_dynamic_addressing();
}

void DxbcShaderTranslator::StartVertexShader_LoadVertexIndex() {
  // Vertex index is in an input bound to SV_VertexID, byte swapped according to
  // xe_vertex_index_endian system constant and written to GPR 0 (which is
  // always present because register_count includes +1).
  // TODO(Triang3l): Check if there's vs_param_gen.

  // xe_vertex_index_endian is:
  // - 00 for no swap.
  // - 01 for 8-in-16.
  // - 10 for 8-in-32 (8-in-16 and 16-in-32).
  // - 11 for 16-in-32.

  // Write to GPR 0 - either directly if not using indexable registers, or via a
  // system temporary register.
  uint32_t reg;
  if (IndexableGPRsUsed()) {
    reg = PushSystemTemp();
  } else {
    reg = 0;
  }

  // 8-in-16: Create target for A and C insertion in Y and sources in X and Z.
  // ushr reg.xyz, input, l(0, 8, 16, 0)
  // ABCD | BCD0 | CD00 | unused
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kVSInVertexIndex));
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(8);
  shader_code_.push_back(16);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 8-in-16: Insert A in Y.
  // bfi reg.y, l(8), l(8), reg.x, reg.y
  // ABCD | BAD0 | CD00 | unused
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 8-in-16: Insert C in W.
  // bfi reg.y, l(8), l(24), reg.z, reg.y
  // ABCD | BADC | CD00 | unused
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get bits indicating what swaps should be done.
  // ubfe reg.zw, l(0, 0, 1, 1).zw, l(0, 0, 0, 1).zw, xe_vertex_index_endian.xx
  // ABCD | BADC | 8in16/16in32? | 8in32/16in32?
  system_constants_used_ |= 1ull << kSysConst_VertexIndexEndian_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_VertexIndexEndian_Comp, 3));
  shader_code_.push_back(uint32_t(cbuffer_index_system_constants_));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_VertexIndexEndian_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 16-in-32 is used as intermediate swapping step here rather than 8-in-32.
  // Thus 8-in-16 needs to be done for 8-in-16 (01) and 8-in-32 (10).
  // And 16-in-32 needs to be done for 8-in-32 (10) and 16-in-32 (11).
  // xor reg.z, reg.z, reg.w
  // ABCD | BADC | 8in16/8in32? | 8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_XOR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Write the 8-in-16 value to X if needed.
  // movc reg.x, reg.z, reg.y, reg.x
  // ABCD/BADC | unused | unused | 8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // 16-in-32: Write the low 16 bits.
  // ushr reg.y, reg.x, l(16)
  // ABCD/BADC | CD00/DC00 | unused | 8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(16);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 16-in-32: Write the high 16 bits.
  // bfi reg.y, l(16), l(16), reg.x, reg.y
  // ABCD/BADC | CDAB/DCBA | unused | 8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Apply the 16-in-32 swap if needed.
  // movc reg.x, reg.w, reg.y, reg.x
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Add the base vertex index.
  system_constants_used_ |= 1ull << kSysConst_VertexBaseIndex_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_VertexBaseIndex_Comp, 3));
  shader_code_.push_back(uint32_t(cbuffer_index_system_constants_));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_VertexBaseIndex_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Convert to float and replicate the swapped value in the destination
  // register (what should be in YZW is unknown, but just to make it a bit
  // cleaner).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  if (IndexableGPRsUsed()) {
    // Store to indexed GPR 0 in x0[0].
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b1111, 2));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(reg);
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::StartVertexShader() {
  // Zero the interpolators.
  for (uint32_t i = 0; i < kInterpolatorCount; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kVSOutInterpolators) + i);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  // Zero the point coordinate (will be set in the geometry shader if needed)
  // and set the point size to a negative value to tell the geometry shader that
  // it should use the global point size - the vertex shader may overwrite it
  // later.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0111, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kVSOutPointParameters));
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  // -1.0f
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Write the vertex index to GPR 0.
  StartVertexShader_LoadVertexIndex();
}

void DxbcShaderTranslator::StartPixelShader() {
  if (edram_rov_used_ && !writes_depth()) {
    // Load depth at the center to system_temp_depth_.x.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 1, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Unconditionally calculate depth derivatives to system_temp_depth.yz for
    // applying the polygon offset.
    for (uint32_t i = 0; i < 2; ++i) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(i ? D3D11_SB_OPCODE_DERIV_RTY_COARSE
                                        : D3D11_SB_OPCODE_DERIV_RTX_COARSE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b0010 << i, 1));
      shader_code_.push_back(system_temp_depth_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_depth_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    }
  }

  // If not translating anything, we only need the depth.
  if (is_depth_only_pixel_shader_) {
    return;
  }

  // Copy interpolants to GPRs.
  uint32_t interpolator_count = std::min(kInterpolatorCount, register_count());
  if (IndexableGPRsUsed()) {
    // Copy through r# to x0[#].
    uint32_t interpolator_temp_register = PushSystemTemp();
    for (uint32_t i = 0; i < interpolator_count; ++i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(interpolator_temp_register);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
      shader_code_.push_back(uint32_t(InOutRegister::kPSInInterpolators) + i);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b1111, 2));
      shader_code_.push_back(0);
      shader_code_.push_back(i);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(interpolator_temp_register);
      ++stat_.instruction_count;
      ++stat_.array_instruction_count;
    }
    PopSystemTemp();
  } else {
    // Copy directly to r#.
    for (uint32_t i = 0; i < interpolator_count; ++i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(i);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
      shader_code_.push_back(uint32_t(InOutRegister::kPSInInterpolators) + i);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
    }
  }

  // Write screen and point coordinates to the specified interpolator register
  // (ps_param_gen).
  uint32_t param_gen_select_temp = PushSystemTemp();
  uint32_t param_gen_value_temp = PushSystemTemp();
  // Check if they need to be written.
  system_constants_used_ |= 1ull << kSysConst_PixelPosReg_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(param_gen_select_temp);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_PixelPosReg_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_PixelPosReg_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(interpolator_count);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(param_gen_select_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;
  // Write VPOS (without supersampling because SSAA is used to fake MSAA with
  // RTV/DSV), at integer coordinates rather than half-pixel if needed, to XY.
  if (!edram_rov_used_) {
    // Get inverse of the sample size.
    system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(param_gen_value_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        kSysConst_SampleCountLog2_Comp |
            ((kSysConst_SampleCountLog2_Comp + 1) << 2),
        3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(uint32_t(-(1 << 23)));
    shader_code_.push_back(uint32_t(-(1 << 23)));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Convert from samples to pixels.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(param_gen_value_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(param_gen_value_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
  // Floor VPOS so with AA, the resulting pixel corner/center is written, not
  // the sample position (games likely don't expect it to be fractional).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(param_gen_value_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(edram_rov_used_ ? D3D10_SB_OPERAND_TYPE_INPUT
                                                  : D3D10_SB_OPERAND_TYPE_TEMP,
                                  kSwizzleXYZW, 1));
  shader_code_.push_back(edram_rov_used_
                             ? uint32_t(InOutRegister::kPSInPosition)
                             : param_gen_value_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // If in OpenGL half-pixel offset mode, write the center of the pixel.
  system_constants_used_ |= 1ull << kSysConst_PixelHalfPixelOffset_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(param_gen_value_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(param_gen_value_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_PixelHalfPixelOffset_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_PixelHalfPixelOffset_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // Write point sprite coordinates to ZW.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(param_gen_value_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b01000000, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInPointParameters));
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
  if (IndexableGPRsUsed()) {
    // Copy the register index to an r# so it can be used for indexable temp
    // addressing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(param_gen_select_temp);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_PixelPosReg_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_PixelPosReg_Vec);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
    // Store the value to an x0[xe_pixel_pos_reg].
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b1111, 2,
        D3D10_SB_OPERAND_INDEX_IMMEDIATE32, D3D10_SB_OPERAND_INDEX_RELATIVE));
    shader_code_.push_back(0);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(param_gen_select_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(param_gen_value_temp);
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
  } else {
    // Store to the needed register using movc.
    for (uint32_t i = 0; i < interpolator_count; ++i) {
      if ((i & 3) == 0) {
        // Get a mask of whether the current register index is the target one.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(param_gen_select_temp);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                          kSysConst_PixelPosReg_Comp, 3));
        shader_code_.push_back(cbuffer_index_system_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
        shader_code_.push_back(kSysConst_PixelPosReg_Vec);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(i);
        shader_code_.push_back(i + 1);
        shader_code_.push_back(i + 2);
        shader_code_.push_back(i + 3);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
      }
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(i);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i & 3, 1));
      shader_code_.push_back(param_gen_select_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(param_gen_value_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(i);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }
  }
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  // Release param_gen_select_temp and param_gen_value_temp.
  PopSystemTemp(2);
}

void DxbcShaderTranslator::StartTranslation() {
  // Allocate global system temporary registers that may also be used in the
  // epilogue.
  if (IsDXBCVertexShader()) {
    system_temp_position_ = PushSystemTemp(true);
  } else if (IsDXBCPixelShader()) {
    if (!is_depth_only_pixel_shader_) {
      for (uint32_t i = 0; i < 4; ++i) {
        // In the ROV path, no need to initialize the colors because original
        // values will be kept for the unwritten components.
        system_temp_color_[i] = PushSystemTemp(!edram_rov_used_);
      }
    }
    if (edram_rov_used_) {
      if (!is_depth_only_pixel_shader_) {
        system_temp_color_written_ = PushSystemTemp(true);
      }
      // If the shader doesn't write to depth, StartPixelShader will load the
      // depth and its derivatives, so no need to initialize. If it does,
      // initialize it to something consistent - depth must be written in every
      // shader execution path (at least in PC ps_3_0 and later shader models).
      system_temp_depth_ = PushSystemTemp(writes_depth());
    }
  }

  if (!is_depth_only_pixel_shader_) {
    // Allocate system temporary variables for the translated code.
    system_temp_pv_ = PushSystemTemp(true);
    system_temp_ps_pc_p0_a0_ = PushSystemTemp(true);
    system_temp_aL_ = PushSystemTemp(true);
    system_temp_loop_count_ = PushSystemTemp(true);
    system_temp_grad_h_lod_ = PushSystemTemp(true);
    system_temp_grad_v_ = PushSystemTemp(true);
  }

  // Write stage-specific prologue.
  if (IsDXBCVertexShader()) {
    StartVertexShader();
  } else if (IsDXBCPixelShader()) {
    StartPixelShader();
  }

  // If not translating anything, don't start the main loop.
  if (is_depth_only_pixel_shader_) {
    return;
  }

  // Start the main loop (for jumping to labels by setting pc and continuing).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LOOP) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;
  // Switch and the first label (pc == 0).
  if (FLAGS_dxbc_switch) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;
  } else {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
  }
}

void DxbcShaderTranslator::CompleteVertexShader() {
  // Get what we need to do with the position.
  uint32_t ndc_control_temp = PushSystemTemp();
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(ndc_control_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kSysFlag_XYDividedByW);
  shader_code_.push_back(kSysFlag_ZDividedByW);
  shader_code_.push_back(kSysFlag_WNotReciprocal);
  shader_code_.push_back(kSysFlag_ReverseZ);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Revert getting the reciprocal of W and dividing XY by W if needed.
  // TODO(Triang3l): Check if having XY or Z pre-divided by W should enable
  // affine interpolation.
  uint32_t w_format_temp = PushSystemTemp();
  // If the shader has returned 1/W, restore W. First take the reciprocal, which
  // may be either W (what we need) or 1/W, depending on the vertex W format.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_RCP) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(w_format_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // Then, if the shader returns 1/W (vtx_w0_fmt is 0), write 1/(1/W) to the
  // position.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(ndc_control_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(w_format_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Multiply XYZ by W in case the shader returns XYZ/W and we'll need to
  // restore XYZ.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(w_format_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // If vtx_xy_fmt and/or vtx_z_fmt are 1, XY and/or Z are pre-divided by W.
  // Restore them in this case.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00010000, 1));
  shader_code_.push_back(ndc_control_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(w_format_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Release w_format_temp.
  PopSystemTemp();

  // Apply scale for drawing without a viewport, and also remap from OpenGL
  // Z clip space to Direct3D if needed.
  system_constants_used_ |= 1ull << kSysConst_NDCScale_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_NDCScale_Comp | ((kSysConst_NDCScale_Comp + 1) << 2) |
          ((kSysConst_NDCScale_Comp + 2) << 4),
      3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_NDCScale_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Reverse Z (Z = W - Z) if the viewport depth is inverted.
  uint32_t reverse_z_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(reverse_z_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(ndc_control_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reverse_z_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Release reverse_z_temp.
  PopSystemTemp();

  // Release ndc_control_temp.
  PopSystemTemp();

  // Apply offset (multiplied by W) for drawing without a viewport and for half
  // pixel offset.
  system_constants_used_ |= 1ull << kSysConst_NDCOffset_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_NDCOffset_Comp | ((kSysConst_NDCOffset_Comp + 1) << 2) |
          ((kSysConst_NDCOffset_Comp + 2) << 4),
      3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_NDCOffset_Vec);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Write Z and W of the position to a separate attribute so ROV output can get
  // per-sample depth.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0011, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kVSOutClipSpaceZW));
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11111110, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Write the position to the output.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kVSOutPosition));
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_DepthTo24Bit(
    uint32_t depths_temp) {
  // Allocate temporary registers for conversion.
  uint32_t temp1 = PushSystemTemp(), temp2 = PushSystemTemp();

  // Unpack the depth format.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_DepthFloat24);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert according to the format.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // ***************************************************************************
  // 20e4 conversion begins here.
  // CFloat24 from d3dref9.dll.
  // ***************************************************************************

  // Assuming the depth is already clamped to [0, 2) (in all places, the depth
  // is written with the saturate flag set).

  // Calculate the denormalized value if the number is too small to be
  // represented as normalized 20e4 into Y.

  // t1 = f32 & 0x7FFFFF
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x7FFFFF);
  shader_code_.push_back(0x7FFFFF);
  shader_code_.push_back(0x7FFFFF);
  shader_code_.push_back(0x7FFFFF);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t1 = (f32 & 0x7FFFFF) | 0x800000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x800000);
  shader_code_.push_back(0x800000);
  shader_code_.push_back(0x800000);
  shader_code_.push_back(0x800000);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t2 = f32 >> 23
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t2 = 113 - (f32 >> 23)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(113);
  shader_code_.push_back(113);
  shader_code_.push_back(113);
  shader_code_.push_back(113);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(temp2);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // t1 = ((f32 & 0x7FFFFF) | 0x800000) >> (113 - (f32 >> 23))
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp2);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the number is too small to be represented as normalized 20e4.
  // t2 = f32 < 0x38800000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x38800000);
  shader_code_.push_back(0x38800000);
  shader_code_.push_back(0x38800000);
  shader_code_.push_back(0x38800000);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Bias the exponent.
  // f32 += 0xC8000000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0xC8000000u);
  shader_code_.push_back(0xC8000000u);
  shader_code_.push_back(0xC8000000u);
  shader_code_.push_back(0xC8000000u);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Replace the number in f32 with a denormalized one if needed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Build the 20e4 number.
  // t1 = f32 >> 3
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t1 = (f32 >> 3) & 1
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // f24 = f32 + 3
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // f24 = f32 + 3 + ((f32 >> 3) & 1)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // f24 = (f32 + 3 + ((f32 >> 3) & 1)) >> 3
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  shader_code_.push_back(3);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // f24 = ((f32 + 3 + ((f32 >> 3) & 1)) >> 3) & 0xFFFFFF
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0xFFFFFF);
  shader_code_.push_back(0xFFFFFF);
  shader_code_.push_back(0xFFFFFF);
  shader_code_.push_back(0xFFFFFF);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // ***************************************************************************
  // 20e4 conversion ends here.
  // ***************************************************************************

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // Unorm24 conversion begins here.
  // ***************************************************************************

  // Multiply by float(0xFFFFFF).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x4B7FFFFF);
  shader_code_.push_back(0x4B7FFFFF);
  shader_code_.push_back(0x4B7FFFFF);
  shader_code_.push_back(0x4B7FFFFF);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Round to the nearest integer. This is the correct way of rounding, rounding
  // towards zero gives 0xFF instead of 0x100 in clear shaders in, for instance,
  // Halo 3.
  // https://docs.microsoft.com/en-us/windows/desktop/direct3d10/d3d10-graphics-programming-guide-resources-data-conversion
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Convert to fixed-point.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depths_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depths_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // ***************************************************************************
  // Unorm24 conversion ends here.
  // ***************************************************************************

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Release temp1 and temp2.
  PopSystemTemp(2);
}

void DxbcShaderTranslator::CompletePixelShader_GammaCorrect(uint32_t color_temp,
                                                            bool to_gamma) {
  uint32_t pieces_temp = PushSystemTemp();
  for (uint32_t j = 0; j < 3; ++j) {
    // Calculate how far we are on each piece of the curve. Multiply by 1/width
    // of each piece, subtract start/width of it and saturate.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(pieces_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    if (to_gamma) {
      // 1.0 / 0.0625
      shader_code_.push_back(0x41800000u);
      // 1.0 / 0.0625
      shader_code_.push_back(0x41800000u);
      // 1.0 / 0.375
      shader_code_.push_back(0x402AAAABu);
      // 1.0 / 0.5
      shader_code_.push_back(0x40000000u);
    } else {
      // 1.0 / 0.25
      shader_code_.push_back(0x40800000u);
      // 1.0 / 0.125
      shader_code_.push_back(0x41000000u);
      // 1.0 / 0.375
      shader_code_.push_back(0x402AAAABu);
      // 1.0 / 0.25
      shader_code_.push_back(0x40800000u);
    }
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    if (to_gamma) {
      // -0.0 / 0.0625
      shader_code_.push_back(0);
      // -0.0625 / 0.0625
      shader_code_.push_back(0xBF800000u);
      // -0.125 / 0.375
      shader_code_.push_back(0xBEAAAAABu);
      // -0.5 / 0.5
      shader_code_.push_back(0xBF800000u);
    } else {
      // -0.0 / 0.25
      shader_code_.push_back(0);
      // -0.25 / 0.125
      shader_code_.push_back(0xC0000000u);
      // -0.375 / 0.375
      shader_code_.push_back(0xBF800000u);
      // -0.75 / 0.25
      shader_code_.push_back(0xC0400000u);
    }
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // Combine the contribution of all pieces to the resulting value - multiply
    // each piece by slope*width and sum them.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP4) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << j, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(pieces_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    if (to_gamma) {
      // 4.0 * 0.0625
      shader_code_.push_back(0x3E800000u);
      // 2.0 * 0.0625
      shader_code_.push_back(0x3E000000u);
      // 1.0 * 0.375
      shader_code_.push_back(0x3EC00000u);
      // 0.5 * 0.5
      shader_code_.push_back(0x3E800000u);
    } else {
      // 0.25 * 0.25
      shader_code_.push_back(0x3D800000u);
      // 0.5 * 0.125
      shader_code_.push_back(0x3D800000u);
      // 1.0 * 0.375
      shader_code_.push_back(0x3EC00000u);
      // 2.0 * 0.25
      shader_code_.push_back(0x3F000000u);
    }
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
  // Release pieces_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs() {
  // Convert to gamma space - this is incorrect, since it must be done after
  // blending on the Xbox 360, but this is just one of many blending issues in
  // the RTV path.
  uint32_t gamma_temp = PushSystemTemp();
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(gamma_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kSysFlag_Color0Gamma);
  shader_code_.push_back(kSysFlag_Color1Gamma);
  shader_code_.push_back(kSysFlag_Color2Gamma);
  shader_code_.push_back(kSysFlag_Color3Gamma);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(gamma_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    CompletePixelShader_GammaCorrect(system_temp_color_[i], true);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }
  // Release gamma_temp.
  PopSystemTemp();

  // Remap guest render target indices to host since because on the host, the
  // indices of the bound render targets are consecutive. This is done using 16
  // movc instructions because indexable temps are known to be causing
  // performance issues on some Nvidia GPUs. In the map, the components are host
  // render target indices, and the values are the guest ones.
  uint32_t remap_movc_mask_temp = PushSystemTemp();
  uint32_t remap_movc_target_temp = PushSystemTemp();
  system_constants_used_ |= 1ull << kSysConst_ColorOutputMap_Index;
  // Host RT i, guest RT j.
  for (uint32_t i = 0; i < 4; ++i) {
    // mask = map.iiii == (0, 1, 2, 3)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(remap_movc_mask_temp);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_ColorOutputMap_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(1);
    shader_code_.push_back(2);
    shader_code_.push_back(3);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    for (uint32_t j = 0; j < 4; ++j) {
      // If map.i == j, move guest color j to the temporary host color.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(remap_movc_target_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
      shader_code_.push_back(remap_movc_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_color_[j]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(remap_movc_target_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }
    // Write the remapped color to host render target i.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
    shader_code_.push_back(i);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(remap_movc_target_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }
  // Release remap_movc_mask_temp and remap_movc_target_temp.
  PopSystemTemp(2);
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_DepthStencil(
    uint32_t edram_dword_offset_temp, uint32_t coverage_out_temp) {
  // Load the coverage before the depth/stencil test - if depth/stencil is not
  // needed, this is still needed to determine which samples to write color for.
  // For 2x AA, use samples 0 and 3 (top-left and bottom-right), for 4x, use
  // all, because ForcedSampleCount can't be 2.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_SampleCountLog2_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1 << 0);
  shader_code_.push_back(1 << 1);
  shader_code_.push_back(1 << 2);
  shader_code_.push_back(1 << 3);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1 << 0);
  shader_code_.push_back(1 << 3);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK, 0, 0));
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(coverage_out_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  uint32_t flags_temp = PushSystemTemp();

  // Check if anything related to depth/stencil needs to be done at all, and get
  // the conditions of passing the depth test - as 0 or 0xFFFFFFFF - into
  // flags_temp.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(flags_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kSysFlag_DepthStencil_Shift);
  shader_code_.push_back(kSysFlag_DepthPassIfLess_Shift);
  shader_code_.push_back(kSysFlag_DepthPassIfEqual_Shift);
  shader_code_.push_back(kSysFlag_DepthPassIfGreater_Shift);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Prevent going out of EDRAM bounds - disable depth/stencil testing if
  // outside of the EDRAM.
  uint32_t edram_bound_check_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_bound_check_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_dword_offset_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1280 * 2048);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(flags_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(flags_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_bound_check_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
  // Release edram_bound_check_temp.
  PopSystemTemp();

  // Enter the depth/stencil test if needed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(flags_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Allocate a temporary register for new depth values (but if the shader
  // writes depth, reuse system_temp_depth_, which already contains the pixel
  // depth for all samples in X) and calculate the depth values for all samples
  // into it.
  uint32_t depth_new_values_temp;
  if (writes_depth()) {
    depth_new_values_temp = system_temp_depth_;

    // Replicate pixel depth into all samples.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1110, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(depth_new_values_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  } else {
    depth_new_values_temp = PushSystemTemp();

    // Replicate pixel depth into all samples if using only a single sample.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_depth_);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // If multisampling, calculate depth at every sample. Check if using 2x MSAA
    // at least.
    system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_SampleCountLog2_Comp + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Load Z and W at sample 0 to depth_new_values_temp.xy and at sample 3 to
    // depth_new_values_temp.zw for 2x MSAA.
    for (uint32_t i = 0; i < 2; ++i) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, i ? 0b1100 : 0b0011, 1));
      shader_code_.push_back(depth_new_values_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_INPUT, i ? 0b01000000 : 0b00000100, 1));
      shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(i ? 3 : 0);
      ++stat_.instruction_count;
    }

    // Calculate Z/W at samples 0 and 3 to depth_new_values_temp.xy for 2x MSAA.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00001000, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00001101, 1));
    shader_code_.push_back(depth_new_values_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Check if using 4x MSAA.
    system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_SampleCountLog2_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Sample 3 is used as 3 with 4x MSAA, not as 1.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(depth_new_values_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Load Z and W at sample 1 to clip_space_zw_01_temp.xy and at sample 2 to
    // clip_space_zw_01_temp.zw for 4x MSAA.
    uint32_t clip_space_zw_01_temp = PushSystemTemp();
    for (uint32_t i = 0; i < 2; ++i) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, i ? 0b1100 : 0b0011, 1));
      shader_code_.push_back(clip_space_zw_01_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_INPUT, i ? 0b01000000 : 0b00000100, 1));
      shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(i ? 2 : 1);
      ++stat_.instruction_count;
    }

    // Calculate Z/W at samples 1 and 2 to depth_new_values_temp.yz for 4x MSAA.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00100000, 1));
    shader_code_.push_back(clip_space_zw_01_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00110100, 1));
    shader_code_.push_back(clip_space_zw_01_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Release clip_space_zw_01_temp.
    PopSystemTemp();

    // 4x MSAA sample loading done.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // 2x MSAA sample loading done.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Get the maximum depth slope for polygon offset to system_temp_depth_.y.
    // https://docs.microsoft.com/en-us/windows/desktop/direct3d9/depth-bias
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
        ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_ABS));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1) |
        ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_ABS));
    shader_code_.push_back(system_temp_depth_);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Copy the needed polygon offset values to system_temp_depth_.zw.
    system_constants_used_ |= (1ull << kSysConst_EDRAMPolyOffsetFront_Index) |
                              (1ull << kSysConst_EDRAMPolyOffsetBack_Index);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        (kSysConst_EDRAMPolyOffsetFrontScale_Comp << 4) |
            (kSysConst_EDRAMPolyOffsetFrontOffset_Comp << 6),
        3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMPolyOffsetFront_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        (kSysConst_EDRAMPolyOffsetBackScale_Comp << 4) |
            (kSysConst_EDRAMPolyOffsetBackOffset_Comp << 6),
        3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMPolyOffsetBack_Vec);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Calculate total polygon offset to system_temp_depth_.z.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_depth_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temp_depth_);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Apply polygon offset.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_depth_);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Apply viewport Z range.
    system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(depth_new_values_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      kSysConst_EDRAMDepthRangeScale_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      kSysConst_EDRAMDepthRangeOffset_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Convert the depth to the target format.
  CompletePixelShader_DepthTo24Bit(depth_new_values_temp);

  // Get EDRAM offsets for each sample.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1110, 1));
  shader_code_.push_back(edram_dword_offset_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_dword_offset_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(80);
  shader_code_.push_back(1);
  shader_code_.push_back(81);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Load the previous depth/stencil values.
  uint32_t depth_values_temp = PushSystemTemp();
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(coverage_out_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_UAV_TYPED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(depth_values_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(edram_dword_offset_temp);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0, 2));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.texture_load_instructions;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Take the stencil part of the original values.
  uint32_t stencil_values_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_values_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0xFF);
  shader_code_.push_back(0xFF);
  shader_code_.push_back(0xFF);
  shader_code_.push_back(0xFF);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Take the depth part of the original values.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depth_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_values_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Do the depth test.
  uint32_t depth_test_results_temp = PushSystemTemp(true);
  uint32_t depth_test_op_results_temp = PushSystemTemp();
  for (uint32_t i = 0; i < 3; ++i) {
    // Check if this operation giving true should result in passing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 + i, 1));
    shader_code_.push_back(flags_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Get the result of the operation: less, equal or greater.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(i == 1 ? D3D10_SB_OPCODE_IEQ
                                           : D3D10_SB_OPCODE_ULT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(depth_test_op_results_temp);
    if (i != 0) {
      // For 1, old == new. For 2, new > old, but with ult, old < new.
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(depth_values_temp);
    }
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(depth_new_values_temp);
    if (i == 0) {
      // New < old.
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(depth_values_temp);
    }
    ++stat_.instruction_count;
    if (i == 1) {
      ++stat_.int_instruction_count;
    } else {
      ++stat_.uint_instruction_count;
    }

    // Merge the result.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(depth_test_results_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(depth_test_results_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(depth_test_op_results_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }
  // Release depth_test_op_results_temp.
  PopSystemTemp();

  // Get bits containing whether stencil testing needs to be done, depth/stencil
  // needs to be written, and the depth write mask.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(flags_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kSysFlag_StencilTest);
  shader_code_.push_back(kSysFlag_DepthStencilWrite);
  shader_code_.push_back(kSysFlag_DepthWriteMask);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if stencil test needs to be done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(flags_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // ***************************************************************************
  // Stencil test begins here. Will replace the values in stencil_values_temp.
  // ***************************************************************************

  uint32_t stencil_control_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp

  // Load the comparison bits to stencil_control_temp.x.
  system_constants_used_ |= (1ull << kSysConst_EDRAMStencilFront_Index) |
                            (1ull << kSysConst_EDRAMStencilBack_Index);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencilSide_Comparison_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilFront_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencilSide_Comparison_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilBack_Vec);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Load the masked reference value to stencil_control_temp.w.
  system_constants_used_ |= (1ull << kSysConst_EDRAMStencilReference_Index) |
                            (1ull << kSysConst_EDRAMStencilReadMask_Vec);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencilReference_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilReference_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencilReadMask_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilReadMask_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Expand the comparison bits - less, equal, greater - into
  // stencil_control_temp.xyz.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1 << 0);
  shader_code_.push_back(1 << 1);
  shader_code_.push_back(1 << 2);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Allocate the stencil test results register.
  uint32_t stencil_test_results_temp = PushSystemTemp(true);
  // Stencil temps: stencil_control_temp | stencil_test_results_temp

  // Mask the current stencil values into stencil_values_read_masked_temp.
  uint32_t stencil_values_read_masked_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_test_results_temp |
  //                stencil_values_read_masked_temp
  system_constants_used_ |= 1ull << kSysConst_EDRAMStencilReadMask_Vec;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_values_read_masked_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_EDRAMStencilReadMask_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilReadMask_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Do the stencil test.
  uint32_t stencil_test_op_results_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_test_results_temp |
  //                stencil_values_read_masked_temp |
  //                stencil_test_op_results_temp
  for (uint32_t i = 0; i < 3; ++i) {
    // Check if this operation giving true should result in passing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(stencil_control_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Get the result of the operation: less, equal or greater.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(i == 1 ? D3D10_SB_OPCODE_IEQ
                                           : D3D10_SB_OPCODE_ULT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(stencil_test_op_results_temp);
    if (i != 0) {
      // For 1, old == new. For 2, new > old, but with ult, old < new.
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(stencil_values_read_masked_temp);
    }
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(stencil_control_temp);
    if (i == 0) {
      // New < old.
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(stencil_values_read_masked_temp);
    }
    ++stat_.instruction_count;
    if (i == 1) {
      ++stat_.int_instruction_count;
    } else {
      ++stat_.uint_instruction_count;
    }

    // Merge the result.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(stencil_test_results_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(stencil_test_results_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(stencil_test_op_results_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Release stencil_values_read_masked_temp and stencil_test_op_results_temp.
  PopSystemTemp(2);
  // Stencil temps: stencil_control_temp | stencil_test_results_temp

  // Get the operations for the current face into stencil_control_temp.xyz.
  system_constants_used_ |= (1ull << kSysConst_EDRAMStencilFront_Index) |
                            (1ull << kSysConst_EDRAMStencilBack_Index);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilFront_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilBack_Vec);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Select the operations for each sample, part 1 for stencil pass case - both
  // depth/stencil passed or depth failed into stencil_pass_op_temp.
  uint32_t stencil_pass_op_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_test_results_temp |
  //                stencil_pass_op_temp
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_pass_op_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_test_results_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kSysConst_EDRAMStencilSide_Pass_Comp, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kSysConst_EDRAMStencilSide_DepthFail_Comp,
      1));
  shader_code_.push_back(stencil_control_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Select the operations for each sample, part 2 for stencil fail case, into
  // stencil_control_temp, so stencil_pass_op_temp can be released.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_test_results_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_pass_op_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kSysConst_EDRAMStencilSide_Fail_Comp, 1));
  shader_code_.push_back(stencil_control_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Release stencil_pass_op_temp.
  PopSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_test_results_temp

  // We don't need separate depth and stencil test results anymore, so now we
  // can mark the samples to be discarded if the stencil test has failed - by
  // setting that whole depth/stencil test has failed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depth_test_results_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_test_results_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_test_results_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release stencil_test_results_temp.
  PopSystemTemp();
  // Stencil temps: stencil_control_temp

  // Allocate the register for combining sub-operation results into the new
  // value to write.
  uint32_t stencil_new_values_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_new_values_temp

  // Allocate the register for sub-operation factors.
  uint32_t stencil_subop_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_new_values_temp |
  //                stencil_subop_temp

  // 1) Apply the current value mask (keep/increment/decrement/invert vs.
  //    zero/replace) - expand to 0xFFFFFFFF or 0, then AND.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kStencilOp_Flag_CurrentMask_Shift);
  shader_code_.push_back(kStencilOp_Flag_CurrentMask_Shift);
  shader_code_.push_back(kStencilOp_Flag_CurrentMask_Shift);
  shader_code_.push_back(kStencilOp_Flag_CurrentMask_Shift);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_control_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_subop_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 2) Increment/decrement stencil - expand 2 bits to 0, 1 or 0xFFFFFFFF (-1)
  //    and add.
  // Not caring about & 0xFF now - applying the write mask will drop the unused
  // bits.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(2);
  shader_code_.push_back(2);
  shader_code_.push_back(2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kStencilOp_Flag_Add_Shift);
  shader_code_.push_back(kStencilOp_Flag_Add_Shift);
  shader_code_.push_back(kStencilOp_Flag_Add_Shift);
  shader_code_.push_back(kStencilOp_Flag_Add_Shift);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_control_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_subop_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // 3) Saturate to 0-255 after adding (INCRSAT/DECRSAT), then conditionally
  //    move if needed.

  uint32_t stencil_saturate_temp = PushSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_new_values_temp |
  //                stencil_subop_temp | stencil_saturate_temp

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAX) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_saturate_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_saturate_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_saturate_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(255);
  shader_code_.push_back(255);
  shader_code_.push_back(255);
  shader_code_.push_back(255);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kStencilOp_Flag_Saturate);
  shader_code_.push_back(kStencilOp_Flag_Saturate);
  shader_code_.push_back(kStencilOp_Flag_Saturate);
  shader_code_.push_back(kStencilOp_Flag_Saturate);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_saturate_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release stencil_saturate_temp.
  PopSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_new_values_temp |
  //                stencil_subop_temp

  // 4) Invert - XOR 0xFFFFFFFF or 0.
  // Not caring about & 0xFF now - applying the write mask will drop the unused
  // bits.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kStencilOp_Flag_Invert_Shift);
  shader_code_.push_back(kStencilOp_Flag_Invert_Shift);
  shader_code_.push_back(kStencilOp_Flag_Invert_Shift);
  shader_code_.push_back(kStencilOp_Flag_Invert_Shift);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_control_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_XOR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_subop_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 5) Replace with the reference value if needed.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kStencilOp_Flag_NewMask);
  shader_code_.push_back(kStencilOp_Flag_NewMask);
  shader_code_.push_back(kStencilOp_Flag_NewMask);
  shader_code_.push_back(kStencilOp_Flag_NewMask);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  system_constants_used_ |= 1ull << kSysConst_EDRAMStencilReference_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_subop_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_EDRAMStencilReference_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilReference_Vec);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Release stencil_subop_temp.
  PopSystemTemp();
  // Stencil temps: stencil_control_temp | stencil_new_values_temp

  // Apply the write mask to the new value - this will also reduce it to 8 bits.
  system_constants_used_ |= 1ull << kSysConst_EDRAMStencilWriteMask_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_EDRAMStencilWriteMask_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilWriteMask_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Invert the write mask into stencil_control_temp.x to keep the unmodified
  // bits of the old value.
  system_constants_used_ |= 1ull << kSysConst_EDRAMStencilWriteMask_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(stencil_control_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencilWriteMask_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencilWriteMask_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Mask the old value.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(stencil_control_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Combine the old and new stencil values.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_new_values_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release stencil_control_temp and stencil_new_values_temp.
  PopSystemTemp();

  // ***************************************************************************
  // Stencil test ends here.
  // ***************************************************************************

  // If not doing stencil test, it's safe to update the coverage a bit earlier -
  // no need to modify the stencil, no need to write the new depth/stencil to
  // the ROV.
  // Check if stencil test is not done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Update the coverage according to the depth test result (0 or 0xFFFFFFFF)
  // earlier if stencil is disabled.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_test_results_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Stencil test done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Check if depth/stencil needs to be written.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(flags_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Check the depth write mask.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(flags_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // If depth must be written, replace the old depth with the new one for the
  // samples for which the test has passed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depth_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_test_results_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_new_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_values_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Close the depth write mask conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Combine depth and stencil into depth_values_temp.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(depth_values_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_values_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(256);
  shader_code_.push_back(256);
  shader_code_.push_back(256);
  shader_code_.push_back(256);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(stencil_values_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Write new depth/stencil for the covered samples.
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(coverage_out_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(edram_dword_offset_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(depth_values_temp);
    ++stat_.instruction_count;
    ++stat_.c_texture_store_instructions;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Close the depth/stencil write conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Update the coverage according to the depth/stencil test result (0 or
  // 0xFFFFFFFF) after writing the new depth/stencil if stencil is enabled.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(depth_test_results_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release depth_new_values_temp (if allocated), depth_values_temp,
  // stencil_values_temp and depth_test_results_temp.
  PopSystemTemp(writes_depth() ? 3 : 4);

  // Depth/stencil operations done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Release flags_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_ExtractPackLayout(
    uint32_t rt_index, bool high, uint32_t width_temp, uint32_t offset_temp) {
  if (high) {
    system_constants_used_ |= (1ull << kSysConst_EDRAMRTPackWidthHigh_Index) |
                              (1ull << kSysConst_EDRAMRTPackOffsetHigh_Index);
  } else {
    system_constants_used_ |= (1ull << kSysConst_EDRAMRTPackWidthLow_Index) |
                              (1ull << kSysConst_EDRAMRTPackOffsetLow_Index);
  }
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(i ? offset_temp : width_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(8);
    shader_code_.push_back(8);
    shader_code_.push_back(8);
    shader_code_.push_back(8);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(8);
    shader_code_.push_back(16);
    shader_code_.push_back(24);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    if (i) {
      shader_code_.push_back(high ? kSysConst_EDRAMRTPackOffsetHigh_Vec
                                  : kSysConst_EDRAMRTPackOffsetLow_Vec);
    } else {
      shader_code_.push_back(high ? kSysConst_EDRAMRTPackWidthHigh_Vec
                                  : kSysConst_EDRAMRTPackWidthLow_Vec);
    }
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_UnpackColor(
    uint32_t data_low_temp, uint32_t data_high_temp, uint32_t data_component,
    uint32_t rt_index, uint32_t rt_format_flags_temp, uint32_t target_temp) {
  // For indexing of the format constants.
  uint32_t rt_pair_index = rt_index >> 1;
  uint32_t rt_pair_swizzle = rt_index & 1 ? 0b11101010 : 0b01000000;

  // Allocate temporary registers for unpacking pixels.
  uint32_t pack_width_temp = PushSystemTemp();
  uint32_t pack_offset_temp = PushSystemTemp();

  // Unpack the bits from the lower 32 bits, as signed because of k_16_16 and
  // k_16_16_16_16 (will be masked later if needed).
  CompletePixelShader_WriteToROV_ExtractPackLayout(
      rt_index, false, pack_width_temp, pack_offset_temp);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(pack_width_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(pack_offset_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, data_component, 1));
  shader_code_.push_back(data_low_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Allocate a register for the components from the upper 32 bits (will be
  // combined with the lower using OR).
  uint32_t high_temp = PushSystemTemp();

  // Unpack the bits from the upper 32 bits.
  CompletePixelShader_WriteToROV_ExtractPackLayout(
      rt_index, true, pack_width_temp, pack_offset_temp);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(high_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(pack_width_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(pack_offset_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, data_component, 1));
  shader_code_.push_back(data_high_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Combine the components from the lower and the upper 32 bits. In ibfe, if
  // width is 0, the result is 0 (not 0xFFFFFFFF), so it's fine to do this
  // without pre-masking.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(high_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release pack_width_temp, pack_offset_temp and high_temp.
  PopSystemTemp(3);

  // Mask the components to differentiate between signed and unsigned.
  system_constants_used_ |= (1ull << kSysConst_EDRAMLoadMaskRT01_Index)
                            << rt_pair_index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_pair_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMLoadMaskRT01_Vec + rt_pair_index);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert from fixed-point.
  uint32_t fixed_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(fixed_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Fixed_Swizzle, 1));
  shader_code_.push_back(rt_format_flags_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(fixed_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Release fixed_temp.
  PopSystemTemp();

  // ***************************************************************************
  // 7e3 conversion begins here.
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************

  // Check if the target format is 7e3 and the conversion is needed (this is
  // pretty long, better to branch here).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Float10, 1));
  shader_code_.push_back(rt_format_flags_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  uint32_t f10_mantissa_temp = PushSystemTemp();
  uint32_t f10_exponent_temp = PushSystemTemp();
  uint32_t f10_denormalized_temp = PushSystemTemp();

  // Extract the mantissa.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x7F);
  shader_code_.push_back(0x7F);
  shader_code_.push_back(0x7F);
  shader_code_.push_back(0x7F);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Extract the exponent.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(7);
  shader_code_.push_back(7);
  shader_code_.push_back(7);
  shader_code_.push_back(7);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Normalize the mantissa for denormalized numbers (with zero exponent -
  // exponent can be used for selection in movc).
  // Note that HLSL firstbithigh(x) is compiled to DXBC like:
  // `x ? 31 - firstbit_hi(x) : -1`
  // (it returns the index from the LSB, not the MSB, but -1 for zero as well).

  // denormalized_temp = firstbit_hi(mantissa)
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_FIRSTBIT_HI) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_mantissa_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // denormalized_temp = 7 - (31 - firstbit_hi(mantissa))
  // Or, if expanded:
  // denormalized_temp = firstbit_hi(mantissa) - 24
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(uint32_t(-24));
  shader_code_.push_back(uint32_t(-24));
  shader_code_.push_back(uint32_t(-24));
  shader_code_.push_back(uint32_t(-24));
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // If mantissa is zero, then:
  // denormalized_temp = 7 - (-1) = 8
  // After this, it works like the following HLSL:
  // denormalized_temp = 7 - firstbithigh(mantissa)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // If the number is not denormalized, make
  // `(mantissa << (7 - firstbithigh(mantissa))) & 0x7F`
  // a no-op - zero 7 - firstbithigh(mantissa).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_denormalized_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Normalize the mantissa - step 1.
  // mantissa = mantissa << (7 - firstbithigh(mantissa))
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_denormalized_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Normalize the mantissa - step 2.
  // mantissa = (mantissa << (7 - firstbithigh(mantissa))) & 0x7F
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x7F);
  shader_code_.push_back(0x7F);
  shader_code_.push_back(0x7F);
  shader_code_.push_back(0x7F);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get the normalized exponent.
  // denormalized_temp = 1 - (7 - firstbithigh(mantissa))
  // If the number is normal, the result will be ignored anyway, so zeroing
  // 7 - firstbithigh(mantissa) will have no effect on this.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_denormalized_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(f10_denormalized_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Overwrite the exponent with the normalized one if needed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_denormalized_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Bias the exponent.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(124);
  shader_code_.push_back(124);
  shader_code_.push_back(124);
  shader_code_.push_back(124);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // If the original number is zero, make the exponent zero (mantissa is already
  // zero in this case).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Shift the mantissa into its float32 position.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Shift the exponent into its float32 position.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Combine mantissa and exponent into float32 numbers.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_mantissa_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_exponent_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release f10_mantissa_temp, f10_exponent_temp and f10_denormalized_temp.
  PopSystemTemp(3);

  // 7e3 conversion done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // 7e3 conversion ends here.
  // ***************************************************************************

  // Convert from 16-bit float.
  uint32_t f16_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_F16TOF32) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(f16_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Float16, 1));
  shader_code_.push_back(rt_format_flags_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f16_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Release f16_temp.
  PopSystemTemp();

  // Scale by the fixed-point conversion factor.
  system_constants_used_ |= (1ull << kSysConst_EDRAMLoadScaleRT01_Index)
                            << rt_pair_index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_pair_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMLoadScaleRT01_Vec + rt_pair_index);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_ExtractBlendScales(
    uint32_t rt_index, uint32_t constant_swizzle, bool is_signed,
    uint32_t shift_x, uint32_t shift_y, uint32_t shift_z, uint32_t shift_w,
    uint32_t target_temp, uint32_t write_mask) {
  uint32_t rt_pair_index = rt_index >> 1;
  if (rt_index & 1) {
    constant_swizzle |= 0b10101010;
  }

  // Sign-extend 2 bits for signed, extract 1 bit for unsigned.
  system_constants_used_ |= (1ull << kSysConst_EDRAMBlendRT01_Index)
                            << rt_pair_index;
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(is_signed ? D3D11_SB_OPCODE_IBFE
                                            : D3D11_SB_OPCODE_UBFE) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  uint32_t width = is_signed ? 2 : 1;
  shader_code_.push_back(width);
  shader_code_.push_back(width);
  shader_code_.push_back(width);
  shader_code_.push_back(width);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(shift_x);
  shader_code_.push_back(shift_y);
  shader_code_.push_back(shift_z);
  shader_code_.push_back(shift_w);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, constant_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendRT01_Vec + rt_pair_index);
  ++stat_.instruction_count;
  if (is_signed) {
    ++stat_.int_instruction_count;
  } else {
    ++stat_.uint_instruction_count;
  }

  // Convert -1, 0 or 1 integer to float.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(is_signed ? D3D10_SB_OPCODE_ITOF
                                            : D3D10_SB_OPCODE_UTOF) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(target_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(target_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_ClampColor(
    uint32_t rt_index, uint32_t color_in_temp, uint32_t color_out_temp) {
  uint32_t rt_pair_index = rt_index >> 1;
  uint32_t rt_pair_swizzle = rt_index & 1 ? 0b11101010 : 0b01000000;

  system_constants_used_ |= (1ull << kSysConst_EDRAMStoreMinRT01_Index)
                            << rt_pair_index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(color_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(color_in_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_pair_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStoreMinRT01_Vec + rt_pair_index);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  system_constants_used_ |= (1ull << kSysConst_EDRAMStoreMaxRT01_Index)
                            << rt_pair_index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(color_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(color_out_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_pair_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStoreMaxRT01_Vec + rt_pair_index);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_ApplyZeroBlendScale(
    uint32_t scale_temp, uint32_t scale_swizzle, uint32_t factor_in_temp,
    uint32_t factor_swizzle, uint32_t factor_out_temp, uint32_t write_mask) {
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(factor_out_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     scale_swizzle, 1));
  shader_code_.push_back(scale_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     factor_swizzle, 1));
  shader_code_.push_back(factor_in_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_Blend(
    uint32_t rt_index, uint32_t rt_format_flags_temp,
    uint32_t src_color_and_output_temp, uint32_t dest_color_temp) {
  // Temporary register for scales of things that contribute to the blending,
  // usually -1.0, 0.0 or 1.0.
  uint32_t scale_temp = PushSystemTemp();
  // Temporary register for making 0 * Infinity result in 0 rather than NaN,
  // for clamping of the source color and the factors, and for applying alpha
  // saturate factor.
  uint32_t factor_calculation_temp = PushSystemTemp();
  uint32_t src_factor_and_result_temp = PushSystemTemp();
  uint32_t dest_factor_and_minmax_temp = PushSystemTemp();

  // Clamp the source color if needed. For fixed-point formats, clamping must
  // always be done, for floating-point, it must not be, however,
  // k_2_10_10_10_FLOAT has fixed-point alpha.
  // https://docs.microsoft.com/en-us/windows/desktop/direct3d11/d3d10-graphics-programming-guide-output-merger-stage
  CompletePixelShader_WriteToROV_ClampColor(rt_index, src_color_and_output_temp,
                                            factor_calculation_temp);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(src_color_and_output_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Fixed_Swizzle, 1));
  shader_code_.push_back(rt_format_flags_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(factor_calculation_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(src_color_and_output_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Interleaving source and destination writes when possible to reduce
  // write-read dependencies.

  // Constant one for factors, reusing dest_factor_and_minmax_temp (since it's
  // the last to be modified).
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b00000000, false, kBlendX_Src_One_Shift,
      kBlendX_SrcAlpha_One_Shift, kBlendX_Dest_One_Shift,
      kBlendX_DestAlpha_One_Shift, dest_factor_and_minmax_temp);

  // Source color for color factors, source alpha for alpha factors, plus ones.
  // This will initialize src_factor_and_result_temp and
  // dest_factor_and_minmax_temp.
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b00000000, true, kBlendX_Src_SrcColor_Shift,
      kBlendX_SrcAlpha_SrcAlpha_Shift, kBlendX_Dest_SrcColor_Shift,
      kBlendX_DestAlpha_SrcAlpha_Shift, scale_temp);
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t swizzle = i ? 0b11101010 : 0b01000000;
    CompletePixelShader_WriteToROV_ApplyZeroBlendScale(
        scale_temp, swizzle, src_color_and_output_temp, kSwizzleXYZW,
        factor_calculation_temp);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, swizzle, 1));
    shader_code_.push_back(scale_temp);
    // dest_factor_and_minmax_temp is the last one to be modified, so it stores
    // the ones (not to allocate an additional temporary register).
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, swizzle, 1));
    shader_code_.push_back(dest_factor_and_minmax_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Destination color for color factors, destination alpha for alpha factors.
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b00000000, true, kBlendX_Src_DestColor_Shift,
      kBlendX_SrcAlpha_DestAlpha_Shift, kBlendX_Dest_DestColor_Shift,
      kBlendX_DestAlpha_DestAlpha_Shift, scale_temp);
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t swizzle = i ? 0b11101010 : 0b01000000;
    CompletePixelShader_WriteToROV_ApplyZeroBlendScale(
        scale_temp, swizzle, dest_color_temp, kSwizzleXYZW,
        factor_calculation_temp);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, swizzle, 1));
    shader_code_.push_back(scale_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Source and destination alphas for color factors.
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b00000000, true, kBlendX_Src_SrcAlpha_Shift,
      kBlendX_Dest_SrcAlpha_Shift, kBlendX_Src_DestAlpha_Shift,
      kBlendX_Dest_DestAlpha_Shift, scale_temp);
  CompletePixelShader_WriteToROV_ApplyZeroBlendScale(
      scale_temp, kSwizzleXYZW, src_color_and_output_temp, kSwizzleWWWW,
      factor_calculation_temp, 0b0011);
  CompletePixelShader_WriteToROV_ApplyZeroBlendScale(
      scale_temp, kSwizzleXYZW, dest_color_temp, kSwizzleWWWW,
      factor_calculation_temp, 0b1100);
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(i & 1 ? dest_factor_and_minmax_temp
                                 : src_factor_and_result_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(scale_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(i & 1 ? dest_factor_and_minmax_temp
                                 : src_factor_and_result_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Constant color for color factors, constant alpha for alpha factors.
  system_constants_used_ |= 1ull << kSysConst_EDRAMBlendConstant_Index;
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b01010101, true, kBlendY_Src_ConstantColor_Shift,
      kBlendY_SrcAlpha_ConstantAlpha_Shift, kBlendY_Dest_ConstantColor_Shift,
      kBlendY_DestAlpha_ConstantAlpha_Shift, scale_temp);
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t swizzle = i ? 0b11101010 : 0b01000000;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(14));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, swizzle, 1));
    shader_code_.push_back(scale_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, swizzle, 1));
    shader_code_.push_back(scale_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Constant alpha for color factors.
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b01010101, true, kBlendY_Src_ConstantAlpha_Shift,
      kBlendY_Dest_ConstantAlpha_Shift, 0, 0, scale_temp, 0b0011);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(14));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(factor_calculation_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(scale_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(scale_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Alpha saturate mode.

  // 1) Clamp the alphas to 1 or less.
  // https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/ne-d3d12-d3d12_blend
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(i ? dest_color_temp : src_color_and_output_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0x3F800000);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // 2) Subtract the destination alpha from 1.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(factor_calculation_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(factor_calculation_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // 3) Min(source alpha, 1 - destination alpha).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(factor_calculation_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(factor_calculation_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(factor_calculation_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // 4) Extract whether the source and the destination color factors are
  // saturate (for alphas, One should be used in this case).
  system_constants_used_ |= (1ull << kSysConst_EDRAMBlendRT01_Index)
                            << (rt_index >> 1);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
  shader_code_.push_back(factor_calculation_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (rt_index & 1) * 2, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendRT01_Vec + (rt_index >> 1));
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(kBlendX_Src_SrcAlphaSaturate);
  shader_code_.push_back(kBlendX_Dest_SrcAlphaSaturate);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 5) Replace the color factors with the saturated alpha.
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 + i, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(i ? dest_factor_and_minmax_temp
                             : src_factor_and_result_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;
  }

  // Multiply the colors by the factors.
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t factor_temp =
        i ? dest_factor_and_minmax_temp : src_factor_and_result_temp;
    uint32_t color_temp = i ? dest_color_temp : src_color_and_output_temp;

    // Check if the factor is zero to zero the result later.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Multiply.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Zero the result if the factor was zero (and the color could be Infinity).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Check if the color is zero.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Zero the result if the color was zero (and the factor could be Infinity).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Clamp the color if the components aren't floating-point.
    // https://docs.microsoft.com/en-us/windows/desktop/direct3d11/d3d10-graphics-programming-guide-output-merger-stage
    CompletePixelShader_WriteToROV_ClampColor(rt_index, factor_temp,
                                              factor_calculation_temp);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Fixed_Swizzle, 1));
    shader_code_.push_back(rt_format_flags_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;
  }

  // Apply the signs for addition/subtraction/inverse subtraction and
  // add/subtract/inverse subtract (for min/max, this will be overwritten
  // later).
  CompletePixelShader_WriteToROV_ExtractBlendScales(
      rt_index, 0b01010101, true, kBlendY_Src_OpSign_Shift,
      kBlendY_SrcAlpha_OpSign_Shift, kBlendY_Dest_OpSign_Shift,
      kBlendY_DestAlpha_OpSign_Shift, scale_temp);

  // 1) Apply the source signs (zero is not used, so no need to check).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(src_factor_and_result_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(src_factor_and_result_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01000000, 1));
  shader_code_.push_back(scale_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // 2) Apply the destination signs and combine. dest_factor_and_minmax_temp
  // may be reused for min/max from now on.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(src_factor_and_result_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(dest_factor_and_minmax_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11101010, 1));
  shader_code_.push_back(scale_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(src_factor_and_result_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Extract whether min/max should be done.
  system_constants_used_ |= (1ull << kSysConst_EDRAMBlendRT01_Index)
                            << (rt_index >> 1);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(scale_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  (rt_index & 1) ? 0b11111111 : 0b01010101, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendRT01_Vec + (rt_index >> 1));
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(kBlendY_Color_OpMin);
  shader_code_.push_back(kBlendY_Alpha_OpMin);
  shader_code_.push_back(kBlendY_Color_OpMax);
  shader_code_.push_back(kBlendY_Alpha_OpMax);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Replace the result with the minimum or the maximum of the source and the
  // destination because min/max don't use factors (also not using anything
  // involving multiplication for this so 0 * Infinity may not affect this).
  // Final output to src_color_and_output_temp happens here.
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MAX : D3D10_SB_OPCODE_MIN) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(dest_factor_and_minmax_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(src_color_and_output_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(dest_color_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    // In min, writing to the intermediate result register because max still
    // needs the original source color.
    // In max, doing the final output.
    shader_code_.push_back(i ? src_color_and_output_temp
                             : src_factor_and_result_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, i ? 0b11101010 : 0b01000000, 1));
    shader_code_.push_back(scale_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(dest_factor_and_minmax_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(src_factor_and_result_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;
  }

  // Release scale_temp, factor_calculation_temp, src_factor_and_result_temp
  // and dest_factor_and_minmax_temp.
  PopSystemTemp(4);
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_PackColor(
    uint32_t data_low_temp, uint32_t data_high_temp, uint32_t data_component,
    uint32_t rt_index, uint32_t rt_format_flags_temp,
    uint32_t source_and_scratch_temp) {
  // For indexing of the format constants.
  uint32_t rt_pair_index = rt_index >> 1;
  uint32_t rt_pair_swizzle = rt_index & 1 ? 0b11101010 : 0b01000000;

  // Scale by the fixed-point conversion factor.
  system_constants_used_ |= (1ull << kSysConst_EDRAMStoreScaleRT01_Index)
                            << rt_pair_index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_pair_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStoreScaleRT01_Vec + rt_pair_index);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Convert to fixed-point, rounding to the nearest integer.
  // https://docs.microsoft.com/en-us/windows/desktop/direct3d10/d3d10-graphics-programming-guide-resources-data-conversion
  uint32_t fixed_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(fixed_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(fixed_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(fixed_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Fixed_Swizzle, 1));
  shader_code_.push_back(rt_format_flags_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(fixed_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Release fixed_temp.
  PopSystemTemp();

  // ***************************************************************************
  // 7e3 conversion begins here.
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************

  // Check if the target format is 7e3 and the conversion is needed (this is
  // pretty long, better to branch here).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Float10, 1));
  shader_code_.push_back(rt_format_flags_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  uint32_t f10_temp1 = PushSystemTemp(), f10_temp2 = PushSystemTemp();

  // Assuming RGB is already clamped to [0.0, 31.875], and alpha is a float and
  // already clamped and multiplied by 3 to get [0.0, 3.0].

  // Calculate the denormalized value if the numbers are too small to be
  // represented as normalized 7e3 into f10_temp1.

  // t1 = f32 & 0x7FFFFF
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x7FFFFF);
  shader_code_.push_back(0x7FFFFF);
  shader_code_.push_back(0x7FFFFF);
  shader_code_.push_back(0x7FFFFF);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t1 = (f32 & 0x7FFFFF) | 0x800000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x800000);
  shader_code_.push_back(0x800000);
  shader_code_.push_back(0x800000);
  shader_code_.push_back(0x800000);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t2 = f32 >> 23
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  shader_code_.push_back(23);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t2 = 125 - (f32 >> 23)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(125);
  shader_code_.push_back(125);
  shader_code_.push_back(125);
  shader_code_.push_back(125);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(f10_temp2);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // t1 = ((f32 & 0x7FFFFF) | 0x800000) >> (125 - (f32 >> 23))
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp2);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the numbers are too small to be represented as normalized 7e3.
  // t2 = f32 < 0x3E800000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3E800000);
  shader_code_.push_back(0x3E800000);
  shader_code_.push_back(0x3E800000);
  shader_code_.push_back(0x3E800000);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Bias the exponent.
  // f32 += 0xC2000000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0xC2000000u);
  shader_code_.push_back(0xC2000000u);
  shader_code_.push_back(0xC2000000u);
  shader_code_.push_back(0xC2000000u);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Replace the number in f32 with a denormalized one if needed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Build the 7e3 numbers.
  // t1 = f32 >> 16
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t1 = (f32 >> 16) & 1
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // f10 = f32 + 0x7FFF
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x7FFF);
  shader_code_.push_back(0x7FFF);
  shader_code_.push_back(0x7FFF);
  shader_code_.push_back(0x7FFF);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // f10 = f32 + 0x7FFF + ((f32 >> 16) & 1)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp1);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // f10 = (f32 + 0x7FFF + ((f32 >> 16) & 1)) >> 16
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // f10 = ((f32 + 0x7FFF + ((f32 >> 16) & 1)) >> 16) & 0x3FF
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3FF);
  shader_code_.push_back(0x3FF);
  shader_code_.push_back(0x3FF);
  shader_code_.push_back(0x3FF);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release f10_temp1 and f10_temp2.
  PopSystemTemp(2);

  // 7e3 conversion done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // 7e3 conversion ends here.
  // ***************************************************************************

  // Convert to 16-bit float.
  uint32_t f16_temp = PushSystemTemp();
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_F32TOF16) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(f16_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(source_and_scratch_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, kROVRTFormatFlagTemp_Float16, 1));
  shader_code_.push_back(rt_format_flags_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f16_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(source_and_scratch_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Release f16_temp.
  PopSystemTemp();

  // Pack and store the lower and the upper 32 bits.
  uint32_t pack_temp = PushSystemTemp();
  uint32_t pack_width_temp = PushSystemTemp();
  uint32_t pack_offset_temp = PushSystemTemp();

  for (uint32_t i = 0; i < 2; ++i) {
    if (i != 0) {
      // Check if need to store the upper 32 bits.
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTPackWidthHigh_Index;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMRTPackWidthHigh_Vec);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;
    }

    // Insert color components into different vector components.
    CompletePixelShader_WriteToROV_ExtractPackLayout(
        rt_index, i != 0, pack_width_temp, pack_offset_temp);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(14));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(pack_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(pack_width_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(pack_offset_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(source_and_scratch_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // bfi doesn't work with width 32 - handle it specially.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(pack_width_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(pack_width_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(5);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(pack_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(pack_width_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(source_and_scratch_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(pack_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Merge XY and ZW.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(pack_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(pack_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
    shader_code_.push_back(pack_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Merge X and Y and into the data register.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     1 << data_component, 1));
    shader_code_.push_back(i ? data_high_temp : data_low_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(pack_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(pack_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    if (i != 0) {
      // Upper 32 bits stored.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }
  }

  // Release pack_temp, pack_width_temp, pack_offset_temp.
  PopSystemTemp(3);
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV() {
  bool color_targets_written;
  if (is_depth_only_pixel_shader_) {
    color_targets_written = false;
  } else {
    color_targets_written = writes_color_target(0) || writes_color_target(1) ||
                            writes_color_target(2) || writes_color_target(3);
  }

  // ***************************************************************************
  // Calculate the offsets for the first sample in the EDRAM.
  // ***************************************************************************

  uint32_t edram_coord_pixel_temp = PushSystemTemp();
  uint32_t edram_coord_pixel_depth_temp = PushSystemTemp();

  // Load SV_Position in edram_coord_pixel_temp.xy as an integer.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Convert the position from pixels to samples.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_SampleCountLog2_Comp |
          ((kSysConst_SampleCountLog2_Comp + 1) << 2),
      3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Load X tile index to edram_coord_pixel_temp.z, part 1 of the division by
  // 80 - get the high 32 bits of the result of the multiplication by
  // 0xCCCCCCCD.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_0_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_NULL) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xCCCCCCCDu);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Load tile index to edram_coord_pixel_temp.zw. Part 2 of the division by
  // 80 - right shift the high bits of x*0xCCCCCCCD by 6. And divide by 16 by
  // right shifting by 4.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01100100, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(6);
  shader_code_.push_back(4);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Write tile-relative offset to XY. Subtract the tile index * 80x16 from the
  // position.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11101110, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(uint32_t(-80));
  shader_code_.push_back(uint32_t(-16));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Multiply tile Y index by the pitch and add X tile index to it to
  // edram_coord_pixel_temp.z.
  system_constants_used_ |= 1ull << kSysConst_EDRAMPitchTiles_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMPitchTiles_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMPitchTiles_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Swap 40 sample columns within the tile for the depth buffer into
  // edram_coord_pixel_depth_temp.x - shaders uploading depth to the EDRAM by
  // aliasing a color render target expect this.

  // 1) Check in which half of the tile the sample is.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(40);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 2) Get the value to add to the tile-relative X sample index.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(40);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(-40));
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // 3) Actually swap the 40 sample columns.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Calculate the address in the EDRAM buffer.

  if (color_targets_written) {
    // 1a) Get dword offset within the tile to edram_coord_pixel_temp.x.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(80);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }

  // 1b) Do the same for depth/stencil to edram_coord_pixel_depth_temp.x.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(80);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  if (color_targets_written) {
    // 2a) Combine the tile offset and the offset within the tile to
    // edram_coord_pixel_temp.x.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1280);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }

  // 2b) Do the same for depth/stencil to edram_coord_pixel_depth_temp.x.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1280);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Adjust the offsets for 64 bits per pixel, and add EDRAM bases of color
  // render targets.

  uint32_t rt_64bpp_temp = 0;

  if (color_targets_written) {
    rt_64bpp_temp = PushSystemTemp();

    // Get which render targets are 64bpp, as log2 of dword count per pixel.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTPackWidthHigh_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_64bpp_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTPackWidthHigh_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Multiply the offsets by 1 or 2 depending on the number of bits per pixel.
    // It's okay to do this here because everything in the equation (at least
    // for Xenia's representation of the EDRAM - may not be true on the real
    // console) needs to be multiplied by 2 - Y tile index (the same as
    // multipying the pitch by 2), X tile index (it addresses pairs of tiles in
    // this case), and the offset within a pair of tiles.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_64bpp_temp);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Add the EDRAM bases for each render target.
    system_constants_used_ |= 1ull << kSysConst_EDRAMBaseDwords_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMBaseDwords_Vec);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
  }

  // Add the EDRAM base for depth.
  system_constants_used_ |= 1ull << kSysConst_EDRAMDepthBaseDwords_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(edram_coord_pixel_depth_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMDepthBaseDwords_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMDepthBaseDwords_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Perform all the depth/stencil-related operations, and get the samples that
  // have passed the depth test.
  uint32_t coverage_temp = PushSystemTemp();
  CompletePixelShader_WriteToROV_DepthStencil(edram_coord_pixel_depth_temp,
                                              coverage_temp);

  // ***************************************************************************
  // Write to color render targets.
  // ***************************************************************************

  if (color_targets_written) {
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTFlags_Index;

    // Get if any sample is covered to exit earlier if all have failed the depth
    // test: samples 02 and 13.
    uint32_t coverage_any_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(coverage_any_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(coverage_temp);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
    shader_code_.push_back(coverage_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Get if any sample is covered.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(coverage_any_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(coverage_any_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(coverage_any_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Discard the pixel if it's not covered.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RETC) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(coverage_any_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Release coverage_any_temp.
    PopSystemTemp();

    // Mask disabled color writes.
    uint32_t rt_write_masks_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_write_masks_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTFlags_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(kRTFlag_WriteR_Shift);
    shader_code_.push_back(kRTFlag_WriteR_Shift);
    shader_code_.push_back(kRTFlag_WriteR_Shift);
    shader_code_.push_back(kRTFlag_WriteR_Shift);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_write_masks_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Prevent going out of EDRAM bounds.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_write_masks_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(1280 * 2048);
    shader_code_.push_back(1280 * 2048);
    shader_code_.push_back(1280 * 2048);
    shader_code_.push_back(1280 * 2048);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_write_masks_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Release rt_write_masks_temp.
    PopSystemTemp();

    // Get what render targets need gamma conversion.
    uint32_t rt_gamma_temp = PushSystemTemp();
    system_constants_used_ |= 1ull << kSysConst_Flags_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_gamma_temp);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_Flags_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(kSysFlag_Color0Gamma);
    shader_code_.push_back(kSysFlag_Color1Gamma);
    shader_code_.push_back(kSysFlag_Color2Gamma);
    shader_code_.push_back(kSysFlag_Color3Gamma);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Get what render targets need blending (if only write mask is used and no
    // blending, skip blending).
    uint32_t rt_blend_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_blend_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTFlags_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(kRTFlag_Blend);
    shader_code_.push_back(kRTFlag_Blend);
    shader_code_.push_back(kRTFlag_Blend);
    shader_code_.push_back(kRTFlag_Blend);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Get what render targets need to be read (for write mask and blending).
    uint32_t rt_overwritten_temp = PushSystemTemp();
    // First, ignore components that don't exist in the render target at all -
    // treat them as overwritten.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_overwritten_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(4);
    shader_code_.push_back(4);
    shader_code_.push_back(4);
    shader_code_.push_back(4);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(kRTFlag_FormatUnusedR_Shift);
    shader_code_.push_back(kRTFlag_FormatUnusedR_Shift);
    shader_code_.push_back(kRTFlag_FormatUnusedR_Shift);
    shader_code_.push_back(kRTFlag_FormatUnusedR_Shift);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTFlags_Vec);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_overwritten_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_overwritten_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // Then, check if the write mask + unused components is 1111 - if yes (and
    // not blending), the pixel will be totally overwritten and no need to load
    // the old pixel value.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_overwritten_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_overwritten_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0b1111);
    shader_code_.push_back(0b1111);
    shader_code_.push_back(0b1111);
    shader_code_.push_back(0b1111);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Force load the previous pixel if blending.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(rt_overwritten_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_blend_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_overwritten_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    for (uint32_t i = 0; i < 4; ++i) {
      if (!writes_color_target(i)) {
        continue;
      }

      // Check if the render target needs to be written to.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(system_temp_color_written_);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Load the format flags:
      // X - color is fixed-point (kROVRTFormatFlagTemp_ColorFixed).
      // Y - alpha is fixed-point (kROVRTFormatFlagTemp_AlphaFixed).
      // Z - format is 2:10:10:10 floating-point (kROVRTFormatFlagTemp_Float10).
      // W - format is 16-bit floating-point (kROVRTFormatFlagTemp_Float16).
      uint32_t format_flags_temp = PushSystemTemp();
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTFlags_Index;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(format_flags_temp);
      shader_code_.push_back(EncodeVectorReplicatedOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMRTFlags_Vec);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(kRTFlag_FormatFixed);
      shader_code_.push_back(kRTFlag_FormatFixed | kRTFlag_FormatFloat10);
      shader_code_.push_back(kRTFlag_FormatFloat10);
      shader_code_.push_back(kRTFlag_FormatFloat16);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Get per-sample EDRAM addresses offsets. First, multiply the relative
      // sample offset by sample size.
      uint32_t edram_coord_sample_temp = PushSystemTemp();
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(edram_coord_sample_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(80);
      shader_code_.push_back(1);
      shader_code_.push_back(81);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_64bpp_temp);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;

      // Add the EDRAM base for the render target to the sample EDRAM addresses.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(edram_coord_sample_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(edram_coord_pixel_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(edram_coord_sample_temp);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;

      // Allocate registers for raw pixel data (lower 32 bits and, if needed,
      // upper 32 bits) for reading and writing pixel data (can't really access
      // ROV in a loop, it seems, at least on Nvidia as of November 13, 2018 -
      // generating an access violation in pipeline creation).
      uint32_t data_low_temp = PushSystemTemp();
      uint32_t data_high_temp = PushSystemTemp();

      // Check if need to load the previous values in the render target.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_ZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_overwritten_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Lower/upper bits loop of render target loading.
      for (uint32_t j = 0; j < 2; ++j) {
        // Only load the upper 32 bits if the format is 64bpp, and adjust the
        // addresses to the upper 32 bits.
        if (j != 0) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
              ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                  D3D10_SB_INSTRUCTION_TEST_NONZERO) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(rt_64bpp_temp);
          ++stat_.instruction_count;
          ++stat_.dynamic_flow_control_count;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          shader_code_.push_back(1);
          shader_code_.push_back(1);
          shader_code_.push_back(1);
          shader_code_.push_back(1);
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;
        }

        // Sample loop.
        for (uint32_t k = 0; k < 4; ++k) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
              ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                  D3D10_SB_INSTRUCTION_TEST_NONZERO) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
          shader_code_.push_back(coverage_temp);
          ++stat_.instruction_count;
          ++stat_.dynamic_flow_control_count;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_UAV_TYPED) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << k, 1));
          shader_code_.push_back(j ? data_high_temp : data_low_temp);
          shader_code_.push_back(
              EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorReplicatedOperand(
              D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0, 2));
          shader_code_.push_back(0);
          shader_code_.push_back(0);
          ++stat_.instruction_count;
          ++stat_.texture_load_instructions;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }

        // Restore the addresses for the lower 32 bits, since they're needed for
        // storing, and close the 64bpp conditional.
        if (j != 0) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          shader_code_.push_back(uint32_t(-1));
          shader_code_.push_back(uint32_t(-1));
          shader_code_.push_back(uint32_t(-1));
          shader_code_.push_back(uint32_t(-1));
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }
      }

      // Done loading the previous values as raw.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Begin the coverage loop.
      uint32_t samples_remaining_temp = PushSystemTemp();
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(samples_remaining_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(4);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LOOP) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Check if the sample is covered.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(coverage_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Allocate temporary registers for the new color (so it can be used as
      // scratch with blending, which may give different results for different
      // samples), for loading the previous color and for the write mask. This
      // is done because some operations - clamping, gamma correction - should
      // be done only for the source color. If no need to get the previous
      // color, will just assume use the 1111 write mask for the movc.
      uint32_t src_color_temp = PushSystemTemp();
      uint32_t dest_color_temp = PushSystemTemp();
      uint32_t write_mask_temp = PushSystemTemp();

      // Copy the pixel color to the per-sample scratch.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(src_color_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_color_[i]);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;

      // Check if need to process the previous value in the render target.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_ZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_overwritten_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Unpack the previous value in the render target to blend and to apply
      // the write mask.
      CompletePixelShader_WriteToROV_UnpackColor(data_low_temp, data_high_temp,
                                                 0, i, format_flags_temp,
                                                 dest_color_temp);

      // Blend if needed.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_blend_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;
      // Convert the destination to linear before blending - to an intermediate
      // register because write masking will use dest_color_temp too.
      // https://steamcdn-a.akamaihd.net/apps/valve/2008/GDC2008_PostProcessingInTheOrangeBox.pdf
      uint32_t dest_color_linear_temp = PushSystemTemp();
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(dest_color_linear_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(dest_color_temp);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_gamma_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;
      CompletePixelShader_GammaCorrect(dest_color_linear_temp, false);
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
      CompletePixelShader_WriteToROV_Blend(i, format_flags_temp, src_color_temp,
                                           dest_color_linear_temp);

      // Release dest_color_linear_temp.
      PopSystemTemp();
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Mask the components to overwrite.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(write_mask_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(system_temp_color_written_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(1 << 0);
      shader_code_.push_back(1 << 1);
      shader_code_.push_back(1 << 2);
      shader_code_.push_back(1 << 3);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // If not using the previous color, set the write mask to 1111 to ignore
      // the uninitialized register with the previous color.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(write_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(1);
      shader_code_.push_back(1);
      shader_code_.push_back(1);
      shader_code_.push_back(1);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;

      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Clamp to the representable range after blending (for float10 and
      // float16, clamping must not be done during blending) and before storing.
      CompletePixelShader_WriteToROV_ClampColor(i, src_color_temp,
                                                src_color_temp);

      // Convert to gamma space after blending.
      // https://steamcdn-a.akamaihd.net/apps/valve/2008/GDC2008_PostProcessingInTheOrangeBox.pdf
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_gamma_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;
      CompletePixelShader_GammaCorrect(src_color_temp, true);
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Keep previous values of the components where needed.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(src_color_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(write_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(src_color_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(dest_color_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // Write the new color, which may have been modified by blending.
      CompletePixelShader_WriteToROV_PackColor(data_low_temp, data_high_temp, 0,
                                               i, format_flags_temp,
                                               src_color_temp);

      // Release src_color_temp, dest_color_temp and write_mask_temp.
      PopSystemTemp(3);

      // Close the conditional for whether the sample is covered.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Go to the next coverage loop iteration - rotate coverage and packed
      // color values (after 4 iterations they will be back to normal).
      uint32_t rotate_temps[] = {coverage_temp, data_low_temp, data_high_temp};
      for (uint32_t j = 0; j < xe::countof(rotate_temps); ++j) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(rotate_temps[j]);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, 0b00111001, 1));
        shader_code_.push_back(rotate_temps[j]);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
      }

      // Check if this is the last sample to process and break.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(samples_remaining_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(samples_remaining_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(uint32_t(-1));
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAKC) |
          ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
              D3D10_SB_INSTRUCTION_TEST_ZERO) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(samples_remaining_temp);
      ++stat_.instruction_count;

      // Close the coverage loop.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDLOOP) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Store the new color values. Lower/upper bits loop.
      for (uint32_t j = 0; j < 2; ++j) {
        // Only store the upper 32 bits if the format is 64bpp, and adjust the
        // addresses to the upper 32 bits.
        if (j != 0) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
              ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                  D3D10_SB_INSTRUCTION_TEST_NONZERO) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(rt_64bpp_temp);
          ++stat_.instruction_count;
          ++stat_.dynamic_flow_control_count;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          shader_code_.push_back(1);
          shader_code_.push_back(1);
          shader_code_.push_back(1);
          shader_code_.push_back(1);
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;
        }

        // Sample loop.
        for (uint32_t k = 0; k < 4; ++k) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
              ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                  D3D10_SB_INSTRUCTION_TEST_NONZERO) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
          shader_code_.push_back(coverage_temp);
          ++stat_.instruction_count;
          ++stat_.dynamic_flow_control_count;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
          shader_code_.push_back(0);
          shader_code_.push_back(0);
          shader_code_.push_back(
              EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
          shader_code_.push_back(edram_coord_sample_temp);
          shader_code_.push_back(
              EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
          shader_code_.push_back(j ? data_high_temp : data_low_temp);
          ++stat_.instruction_count;
          ++stat_.c_texture_store_instructions;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }

        // Close the 64bpp conditional. No need to subtract 1 from the sample
        // EDRAM addresses since we don't need them anymore for the current
        // render target.
        if (j != 0) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }
      }

      // Release format_flags_temp, edram_coord_sample_temp, data_low_temp,
      // data_high_temp and samples_remaining_temp.
      PopSystemTemp(5);

      // Close the check whether the RT is used.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }

    // Release rt_gamma_temp, rt_blend_temp and rt_overwritten_temp.
    PopSystemTemp(3);
  }

  // Release edram_coord_pixel_temp, edram_coord_pixel_depth_temp,
  // coverage_temp, and, if used, rt_64bpp_temp.
  PopSystemTemp(color_targets_written ? 4 : 3);
}

void DxbcShaderTranslator::CompletePixelShader() {
  if (is_depth_only_pixel_shader_) {
    // The depth-only shader only needs to do the depth test and to write the
    // depth to the ROV.
    if (edram_rov_used_) {
      CompletePixelShader_WriteToROV();
    }
    return;
  }

  // Alpha test.
  // Check if alpha test is enabled (if the constant is not 0).
  system_constants_used_ |= (1ull << kSysConst_AlphaTest_Index) |
                            (1ull << kSysConst_AlphaTestRange_Index);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_AlphaTest_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_AlphaTest_Vec);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;
  // Allocate a register for the test result.
  uint32_t alpha_test_reg = PushSystemTemp();
  // Check the alpha against the lower bound (inclusively).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_color_[0]);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_AlphaTestRange_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_AlphaTestRange_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // Check the alpha against the upper bound (inclusively).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_AlphaTestRange_Comp + 1, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_AlphaTestRange_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_color_[0]);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // Check if both tests have passed and the alpha is in the range.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(alpha_test_reg);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
  // xe_alpha_test of 1 means alpha test passes in the range, -1 means it fails.
  // Compare xe_alpha_test to 0 and see what action should be performed.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ILT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_AlphaTest_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_AlphaTest_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;
  // Flip the test result if alpha being in the range means passing.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_XOR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(alpha_test_reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(alpha_test_reg);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
  // Discard the texel if failed the test.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DISCARD) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(alpha_test_reg);
  ++stat_.instruction_count;
  // Release alpha_test_reg.
  PopSystemTemp();
  // Close the alpha test conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Apply color exponent bias (the constant contains 2.0^bias).
  // Not sure if this should be done before alpha testing or after, but this is
  // render target state, and alpha test works with values obtained mainly from
  // textures (so conceptually closer to the shader rather than the
  // output-merger in the pipeline).
  // TODO(Triang3l): Verify whether the order of alpha testing and exponent bias
  // is correct.
  system_constants_used_ |= 1ull << kSysConst_ColorExpBias_Index;
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_color_[i]);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_color_[i]);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_ColorExpBias_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Write the values to the render targets.
  if (edram_rov_used_) {
    CompletePixelShader_WriteToROV();
  } else {
    CompletePixelShader_WriteToRTVs();
  }
}

void DxbcShaderTranslator::CompleteShaderCode() {
  if (!is_depth_only_pixel_shader_) {
    // Close the last exec, there's nothing to merge it with anymore, and we're
    // closing upper-level flow control blocks.
    CloseExecConditionals();
    // Close the last label and the switch.
    if (FLAGS_dxbc_switch) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    } else {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }
    // End the main loop.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDLOOP) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Release the following system temporary values so epilogue can reuse them:
    // - system_temp_pv_.
    // - system_temp_ps_pc_p0_a0_.
    // - system_temp_aL_.
    // - system_temp_loop_count_.
    // - system_temp_grad_h_lod_.
    // - system_temp_grad_v_.
    PopSystemTemp(6);
  }

  // Write stage-specific epilogue.
  if (IsDXBCVertexShader()) {
    CompleteVertexShader();
  } else if (IsDXBCPixelShader()) {
    CompletePixelShader();
  }

  if (IsDXBCVertexShader()) {
    // Release system_temp_position_.
    PopSystemTemp();
  } else if (IsDXBCPixelShader()) {
    if (edram_rov_used_) {
      // Release system_temp_depth_.
      PopSystemTemp();
      if (!is_depth_only_pixel_shader_) {
        // Release system_temp_color_written_.
        PopSystemTemp();
      }
    }
    if (!is_depth_only_pixel_shader_) {
      // Release system_temp_color_.
      PopSystemTemp(4);
    }
  }

  // Return from `main`.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  // Remap float constant indices if not indexed dynamically.
  if (!float_constants_dynamic_indexed_ &&
      !float_constant_index_offsets_.empty()) {
    uint8_t float_constant_map[256] = {};
    uint32_t float_constant_count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t float_constants_used = constant_register_map().float_bitmap[i];
      uint32_t float_constant_index;
      while (
          xe::bit_scan_forward(float_constants_used, &float_constant_index)) {
        float_constants_used &= ~(1ull << float_constant_index);
        float_constant_map[i * 64 + float_constant_index] =
            float_constant_count++;
      }
    }
    size_t index_count = float_constant_index_offsets_.size();
    for (size_t i = 0; i < index_count; ++i) {
      uint32_t index_offset = float_constant_index_offsets_[i];
      shader_code_[index_offset] =
          float_constant_map[shader_code_[index_offset] & 255];
    }
  }
}

std::vector<uint8_t> DxbcShaderTranslator::CompleteTranslation() {
  // Write the code epilogue.
  CompleteShaderCode();

  shader_object_.clear();

  // Write the shader object header.
  shader_object_.push_back('CBXD');
  // Checksum (set later).
  for (uint32_t i = 0; i < 4; ++i) {
    shader_object_.push_back(0);
  }
  shader_object_.push_back(1);
  // Size (set later).
  shader_object_.push_back(0);
  // 5 chunks - RDEF, ISGN, OSGN, SHEX, STAT.
  shader_object_.push_back(5);
  // Chunk offsets (set later).
  for (uint32_t i = 0; i < shader_object_[7]; ++i) {
    shader_object_.push_back(0);
  }

  uint32_t chunk_position_dwords;

  // Write Resource DEFinitions.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[8] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('FEDR');
  shader_object_.push_back(0);
  WriteResourceDefinitions();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write Input SiGNature.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[9] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('NGSI');
  shader_object_.push_back(0);
  WriteInputSignature();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write Output SiGNature.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[10] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('NGSO');
  shader_object_.push_back(0);
  WriteOutputSignature();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write SHader EXtended.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[11] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('XEHS');
  shader_object_.push_back(0);
  WriteShaderCode();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write STATistics.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[12] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('TATS');
  shader_object_.push_back(sizeof(stat_));
  shader_object_.resize(shader_object_.size() +
                        sizeof(stat_) / sizeof(uint32_t));
  std::memcpy(&shader_object_[chunk_position_dwords + 2], &stat_,
              sizeof(stat_));

  // Fill the remaining fields of the header and copy bytes out.
  uint32_t shader_object_size =
      uint32_t(shader_object_.size() * sizeof(uint32_t));
  shader_object_[6] = shader_object_size;
  // The checksum includes the size field, so it must be the last.
  CalculateDXBCChecksum(reinterpret_cast<unsigned char*>(shader_object_.data()),
                        shader_object_size,
                        reinterpret_cast<unsigned int*>(&shader_object_[1]));
  // TODO(Triang3l): Avoid copy?
  std::vector<uint8_t> shader_object_bytes;
  shader_object_bytes.resize(shader_object_size);
  std::memcpy(shader_object_bytes.data(), shader_object_.data(),
              shader_object_size);
  return shader_object_bytes;
}

void DxbcShaderTranslator::EmitInstructionDisassembly() {
  if (!FLAGS_dxbc_source_map) {
    return;
  }

  const char* source = instruction_disassembly_buffer_.GetString();
  uint32_t length = uint32_t(instruction_disassembly_buffer_.length());
  // Trim leading spaces and trailing new line.
  while (length != 0 && source[0] == ' ') {
    ++source;
    --length;
  }
  while (length != 0 && source[length - 1] == '\n') {
    --length;
  }
  if (length == 0) {
    return;
  }

  uint32_t length_dwords =
      (length + 1 + (sizeof(uint32_t) - 1)) / sizeof(uint32_t);
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CUSTOMDATA) |
      ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D10_SB_CUSTOMDATA_COMMENT));
  shader_code_.push_back(2 + length_dwords);
  size_t offset_dwords = shader_code_.size();
  shader_code_.resize(offset_dwords + length_dwords);
  char* target = reinterpret_cast<char*>(&shader_code_[offset_dwords]);
  std::memcpy(target, source, length);
  target[length] = '\0';
  // Don't leave uninitialized data, and make sure multiple invocations of the
  // translator for the same Xenos shader give the same DXBC.
  std::memset(target + length + 1, 0xAB,
              length_dwords * sizeof(uint32_t) - length - 1);
}

void DxbcShaderTranslator::LoadDxbcSourceOperand(
    const InstructionOperand& operand, DxbcSourceOperand& dxbc_operand) {
  // Initialize the values to their defaults.
  dxbc_operand.type = DxbcSourceOperand::Type::kZerosOnes;
  dxbc_operand.index = 0;
  dxbc_operand.addressing_mode = InstructionStorageAddressingMode::kStatic;
  dxbc_operand.swizzle = kSwizzleXYZW;
  dxbc_operand.is_negated = operand.is_negated;
  dxbc_operand.is_absolute_value = operand.is_absolute_value;
  dxbc_operand.intermediate_register =
      DxbcSourceOperand::kIntermediateRegisterNone;

  if (operand.component_count == 0) {
    // No components requested, probably totally invalid - give something more
    // or less safe (zeros) and exit.
    assert_always();
    return;
  }

  // Make the DXBC swizzle, and also check whether there are any components with
  // constant zero or one values (in this case, the operand will have to be
  // loaded into the intermediate register) and if there are any real components
  // at all (if there aren't, a literal can just be loaded).
  uint32_t swizzle = 0;
  uint32_t constant_components = 0;
  uint32_t constant_component_values = 0;
  for (uint32_t i = 0; i < uint32_t(operand.component_count); ++i) {
    if (operand.components[i] <= SwizzleSource::kW) {
      swizzle |= uint32_t(operand.components[i]) << (2 * i);
    } else {
      constant_components |= 1 << i;
      if (operand.components[i] == SwizzleSource::k1) {
        constant_component_values |= 1 << i;
      }
    }
  }
  // Replicate the last component's swizzle into all unused components.
  uint32_t component_last = uint32_t(operand.component_count) - 1;
  for (uint32_t i = uint32_t(operand.component_count); i < 4; ++i) {
    swizzle |= ((swizzle >> (2 * component_last)) & 0x3) << (2 * i);
    constant_components |= ((constant_components >> component_last) & 0x1) << i;
    constant_component_values |=
        ((constant_component_values >> component_last) & 0x1) << i;
  }
  // If all components are constant, just write a literal.
  if (constant_components == 0xF) {
    dxbc_operand.index = constant_component_values;
    return;
  }
  dxbc_operand.swizzle = swizzle;

  // If the index is dynamic, choose where it's taken from.
  uint32_t dynamic_address_register, dynamic_address_component;
  if (operand.storage_addressing_mode ==
      InstructionStorageAddressingMode::kAddressRelative) {
    // Addressed by aL.x.
    dynamic_address_register = system_temp_aL_;
    dynamic_address_component = 0;
  } else {
    // Addressed by a0.
    dynamic_address_register = system_temp_ps_pc_p0_a0_;
    dynamic_address_component = 3;
  }

  // Actually load the operand.
  switch (operand.storage_source) {
    case InstructionStorageSource::kRegister:
      // ***********************************************************************
      // General-purpose register
      // ***********************************************************************
      if (IndexableGPRsUsed()) {
        // GPRs are in x0 - need to load to the intermediate register (indexable
        // temps are only accessible via mov load/store).
        if (dxbc_operand.intermediate_register ==
            DxbcSourceOperand::kIntermediateRegisterNone) {
          dxbc_operand.intermediate_register = PushSystemTemp();
        }
        dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
        if (operand.storage_addressing_mode ==
            InstructionStorageAddressingMode::kStatic) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(dxbc_operand.intermediate_register);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, kSwizzleXYZW, 2));
          shader_code_.push_back(0);
          shader_code_.push_back(uint32_t(operand.storage_index));
        } else {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(dxbc_operand.intermediate_register);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, kSwizzleXYZW, 2,
              D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
              D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE));
          shader_code_.push_back(0);
          shader_code_.push_back(uint32_t(operand.storage_index));
          shader_code_.push_back(EncodeVectorSelectOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
          shader_code_.push_back(dynamic_address_register);
        }
        ++stat_.instruction_count;
        ++stat_.array_instruction_count;
      } else {
        // GPRs are in r# - can access directly if addressed statically, load
        // by checking every register whether it's the needed one if addressed
        // dynamically.
        if (operand.storage_addressing_mode ==
            InstructionStorageAddressingMode::kStatic) {
          dxbc_operand.type = DxbcSourceOperand::Type::kRegister;
          dxbc_operand.index = uint32_t(operand.storage_index);
        } else {
          if (dxbc_operand.intermediate_register ==
              DxbcSourceOperand::kIntermediateRegisterNone) {
            dxbc_operand.intermediate_register = PushSystemTemp();
          }
          dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
          uint32_t gpr_movc_mask_register = PushSystemTemp();
          for (uint32_t i = 0; i < register_count(); ++i) {
            if ((i & 3) == 0) {
              // Compare the dynamic address to each register number to check if
              // it's the one that's needed.
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
              shader_code_.push_back(EncodeVectorMaskedOperand(
                  D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
              shader_code_.push_back(gpr_movc_mask_register);
              shader_code_.push_back(EncodeVectorReplicatedOperand(
                  D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
              shader_code_.push_back(dynamic_address_register);
              shader_code_.push_back(EncodeVectorSwizzledOperand(
                  D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
              for (uint32_t j = 0; j < 4; ++j) {
                shader_code_.push_back(i + j - uint32_t(operand.storage_index));
              }
              ++stat_.instruction_count;
              ++stat_.int_instruction_count;
            }
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
            shader_code_.push_back(dxbc_operand.intermediate_register);
            shader_code_.push_back(EncodeVectorReplicatedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, i & 3, 1));
            shader_code_.push_back(gpr_movc_mask_register);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(i);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(dxbc_operand.intermediate_register);
            ++stat_.instruction_count;
            ++stat_.movc_instruction_count;
          }
          // Release gpr_movc_mask_register.
          PopSystemTemp();
        }
      }
      break;

    case InstructionStorageSource::kConstantFloat:
      // ***********************************************************************
      // Float constant
      // ***********************************************************************
      if (cbuffer_index_float_constants_ == kCbufferIndexUnallocated) {
        cbuffer_index_float_constants_ = cbuffer_count_++;
      }
      dxbc_operand.type = DxbcSourceOperand::Type::kConstantFloat;
      dxbc_operand.index = uint32_t(operand.storage_index);
      dxbc_operand.addressing_mode = operand.storage_addressing_mode;
      if (operand.storage_addressing_mode !=
          InstructionStorageAddressingMode::kStatic) {
        float_constants_dynamic_indexed_ = true;
      }
      break;

    case InstructionStorageSource::kConstantInt: {
      // ***********************************************************************
      // Loop constant
      // ***********************************************************************
      if (cbuffer_index_bool_loop_constants_ == kCbufferIndexUnallocated) {
        cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
      }
      // Convert to float and store in the intermediate register.
      // The constant buffer contains each integer replicated in XYZW so dynamic
      // indexing is possible.
      dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
      if (dxbc_operand.intermediate_register ==
          DxbcSourceOperand::kIntermediateRegisterNone) {
        dxbc_operand.intermediate_register = PushSystemTemp();
      }
      bool is_static = operand.storage_addressing_mode ==
                       InstructionStorageAddressingMode::kStatic;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(is_static ? 7 : 9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(dxbc_operand.intermediate_register);
      shader_code_.push_back(EncodeVectorReplicatedOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3,
          D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
          D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
          is_static ? D3D10_SB_OPERAND_INDEX_IMMEDIATE32
                    : D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE));
      shader_code_.push_back(cbuffer_index_bool_loop_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
      // 8 to skip bool constants.
      shader_code_.push_back(8 + uint32_t(operand.storage_index));
      if (!is_static) {
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
        shader_code_.push_back(dynamic_address_register);
        bool_loop_constants_dynamic_indexed_ = true;
      }
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
    } break;

    case InstructionStorageSource::kConstantBool: {
      // ***********************************************************************
      // Boolean constant
      // ***********************************************************************
      if (cbuffer_index_bool_loop_constants_ == kCbufferIndexUnallocated) {
        cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
      }
      // Extract, convert to float and store in the intermediate register.
      // The constant buffer contains each 32-bit vector replicated in XYZW so
      // dynamic indexing is possible.
      dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
      if (dxbc_operand.intermediate_register ==
          DxbcSourceOperand::kIntermediateRegisterNone) {
        dxbc_operand.intermediate_register = PushSystemTemp();
      }
      if (operand.storage_addressing_mode ==
          InstructionStorageAddressingMode::kStatic) {
        // Extract the bit directly.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(dxbc_operand.intermediate_register);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(1);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(uint32_t(operand.storage_index) & 31);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3));
        shader_code_.push_back(cbuffer_index_bool_loop_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
        shader_code_.push_back(uint32_t(operand.storage_index) >> 5);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      } else {
        bool_loop_constants_dynamic_indexed_ = true;
        uint32_t constant_address_register = dynamic_address_register;
        uint32_t constant_address_component = dynamic_address_component;
        if (operand.storage_index != 0) {
          // Has an offset - add it.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
          shader_code_.push_back(dxbc_operand.intermediate_register);
          shader_code_.push_back(EncodeVectorSelectOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, constant_address_component, 1));
          shader_code_.push_back(constant_address_register);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(uint32_t(operand.storage_index));
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;
          constant_address_register = dxbc_operand.intermediate_register;
          constant_address_component = 0;
        }
        // Split the index into constant index and bit offset and store them in
        // the intermediate register.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
        shader_code_.push_back(dxbc_operand.intermediate_register);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(5);
        shader_code_.push_back(3);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0);
        shader_code_.push_back(5);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(EncodeVectorReplicatedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, constant_address_component, 1));
        shader_code_.push_back(constant_address_register);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
        // Extract the bits.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(dxbc_operand.intermediate_register);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(1);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
        shader_code_.push_back(dxbc_operand.intermediate_register);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0,
                                      3, D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
                                      D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
                                      D3D10_SB_OPERAND_INDEX_RELATIVE));
        shader_code_.push_back(cbuffer_index_bool_loop_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(dxbc_operand.intermediate_register);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      }
      // Convert the bit to float and replicate it.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(dxbc_operand.intermediate_register);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(dxbc_operand.intermediate_register);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
    } break;

    default:
      // Fall back to constant zeros for invalid types.
      dxbc_operand.index = constant_component_values;
      dxbc_operand.swizzle = kSwizzleXYZW;
      return;
  }

  // If there are zeros or ones in the swizzle, force load the operand into the
  // intermediate register (applying the swizzle and the modifiers), and then
  // replace the components there.
  if (constant_components != 0) {
    if (dxbc_operand.intermediate_register ==
        DxbcSourceOperand::kIntermediateRegisterNone) {
      dxbc_operand.intermediate_register = PushSystemTemp();
    }
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                               3 + DxbcSourceOperandLength(dxbc_operand)));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(dxbc_operand.intermediate_register);
    UseDxbcSourceOperand(dxbc_operand);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Write the constant components.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     constant_components, 1));
    shader_code_.push_back(dxbc_operand.intermediate_register);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    for (uint32_t i = 0; i < 4; ++i) {
      if (constant_component_values & (1 << i)) {
        shader_code_.push_back(operand.is_negated ? 0xBF800000u : 0x3F800000u);
      } else {
        shader_code_.push_back(0);
      }
    }
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
    // Swizzle and modifiers already applied.
    dxbc_operand.swizzle = kSwizzleXYZW;
    dxbc_operand.is_negated = false;
    dxbc_operand.is_absolute_value = false;
  }
}

uint32_t DxbcShaderTranslator::DxbcSourceOperandLength(
    const DxbcSourceOperand& operand, bool negate, bool absolute) const {
  uint32_t length;
  switch (operand.type) {
    case DxbcSourceOperand::Type::kRegister:
    case DxbcSourceOperand::Type::kIntermediateRegister:
      // Either a game register (for non-indexable GPRs) or the intermediate
      // register with the data loaded (for indexable GPRs, bool and loop
      // constants).
      length = 2;
      break;
    case DxbcSourceOperand::Type::kConstantFloat:
      if (operand.addressing_mode !=
          InstructionStorageAddressingMode::kStatic) {
        // Constant buffer, 3D index - immediate 0, immediate 1, immediate plus
        // register 2.
        length = 6;
      } else {
        // Constant buffer, 3D immediate index.
        length = 4;
      }
      break;
    default:
      // Pre-negated literal of zeros and ones (no extension dword), or a
      // totally invalid operand replaced by a literal.
      return 5;
  }
  // Apply both the operand negation and the usage negation (for subtraction)
  // and absolute from both sources.
  if (operand.is_negated) {
    negate = !negate;
  }
  absolute |= operand.is_absolute_value;
  // Modifier extension - neg/abs or non-uniform binding index.
  if (negate || absolute) {
    ++length;
  }
  return length;
}

void DxbcShaderTranslator::UseDxbcSourceOperand(
    const DxbcSourceOperand& operand, uint32_t additional_swizzle,
    uint32_t select_component, bool negate, bool absolute) {
  // Apply swizzle needed by the instruction implementation in addition to the
  // operand swizzle.
  uint32_t swizzle = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t swizzle_component = (additional_swizzle >> (i * 2)) & 3;
    swizzle |= ((operand.swizzle >> (swizzle_component * 2)) & 3) << (i * 2);
  }

  // Access either the whole vector or only one component of it, depending to
  // what is needed.
  uint32_t component_bits =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT);
  if (select_component <= 3) {
    component_bits |= ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
                          D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                      (((swizzle >> (select_component * 2)) & 0x3)
                       << D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_SHIFT);
  } else {
    component_bits |= ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
                          D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                      (swizzle << D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SHIFT);
  }

  // Apply both the operand negation and the usage negation (for subtraction)
  // and absolute value from both sources.
  if (operand.is_negated) {
    negate = !negate;
  }
  absolute |= operand.is_absolute_value;
  // Build OperandToken1 for modifiers (negate, absolute, minimum precision,
  // non-uniform binding index) - if it has any, it will be non-zero.
  // NOTE: AMD GPUs or drivers do NOT support non-uniform constant buffer
  // indices as of October 1, 2018 - they were causing significant skinned mesh
  // corruption when Xenia used multiple descriptors for float constants rather
  // than remapping.
  uint32_t modifiers = 0;
  if (negate && absolute) {
    modifiers |= D3D10_SB_OPERAND_MODIFIER_ABSNEG
                 << D3D10_SB_OPERAND_MODIFIER_SHIFT;
  } else if (negate) {
    modifiers |= D3D10_SB_OPERAND_MODIFIER_NEG
                 << D3D10_SB_OPERAND_MODIFIER_SHIFT;
  } else if (absolute) {
    modifiers |= D3D10_SB_OPERAND_MODIFIER_ABS
                 << D3D10_SB_OPERAND_MODIFIER_SHIFT;
  }
  if (modifiers != 0) {
    // Mark the extension as containing modifiers.
    modifiers |= ENCODE_D3D10_SB_EXTENDED_OPERAND_TYPE(
        D3D10_SB_EXTENDED_OPERAND_MODIFIER);
  }
  uint32_t extended_bit = ENCODE_D3D10_SB_OPERAND_EXTENDED(modifiers);

  // Actually write the operand tokens.
  switch (operand.type) {
    case DxbcSourceOperand::Type::kRegister:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          component_bits | extended_bit);
      if (modifiers != 0) {
        shader_code_.push_back(modifiers);
      }
      shader_code_.push_back(operand.index);
      break;

    case DxbcSourceOperand::Type::kConstantFloat: {
      bool is_static =
          operand.addressing_mode == InstructionStorageAddressingMode::kStatic;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_3D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              2, is_static ? D3D10_SB_OPERAND_INDEX_IMMEDIATE32
                           : D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE) |
          component_bits | extended_bit);
      if (modifiers != 0) {
        shader_code_.push_back(modifiers);
      }
      shader_code_.push_back(cbuffer_index_float_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kFloatConstants));
      if (!float_constants_dynamic_indexed_) {
        // If there's no dynamic indexing in the shader, constants are compacted
        // and remapped. Store where the index has been written.
        float_constant_index_offsets_.push_back(uint32_t(shader_code_.size()));
      }
      shader_code_.push_back(operand.index);
      if (!is_static) {
        uint32_t dynamic_address_register, dynamic_address_component;
        if (operand.addressing_mode ==
            InstructionStorageAddressingMode::kAddressRelative) {
          // Addressed by aL.x.
          dynamic_address_register = system_temp_aL_;
          dynamic_address_component = 0;
        } else {
          // Addressed by a0.
          dynamic_address_register = system_temp_ps_pc_p0_a0_;
          dynamic_address_component = 3;
        }
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
        shader_code_.push_back(dynamic_address_register);
      }
    } break;

    case DxbcSourceOperand::Type::kIntermediateRegister:
      // Already loaded as float to the intermediate temporary register.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          component_bits | extended_bit);
      if (modifiers != 0) {
        shader_code_.push_back(modifiers);
      }
      shader_code_.push_back(operand.intermediate_register);
      break;

    default:
      // Only zeros and ones in the swizzle, or the safest replacement for an
      // invalid operand (such as a fetch constant).
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D) |
          component_bits);
      for (uint32_t i = 0; i < 4; ++i) {
        if (operand.index & (1 << i)) {
          shader_code_.push_back(negate ? 0xBF800000u : 0x3F800000u);
        } else {
          shader_code_.push_back(0);
        }
      }
  }
}

void DxbcShaderTranslator::UnloadDxbcSourceOperand(
    const DxbcSourceOperand& operand) {
  if (operand.intermediate_register !=
      DxbcSourceOperand::kIntermediateRegisterNone) {
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::StoreResult(const InstructionResult& result,
                                       uint32_t reg, bool replicate_x) {
  if (result.storage_target == InstructionStorageTarget::kNone ||
      !result.has_any_writes()) {
    return;
  }

  uint32_t saturate_bit =
      ENCODE_D3D10_SB_INSTRUCTION_SATURATE(result.is_clamped);

  // Scalar targets get only one component.
  if (result.storage_target == InstructionStorageTarget::kPointSize ||
      result.storage_target == InstructionStorageTarget::kDepth) {
    if (!result.write_mask[0]) {
      return;
    }
    SwizzleSource component = result.components[0];
    if (replicate_x && component <= SwizzleSource::kW) {
      component = SwizzleSource::kX;
    }
    // Both r[imm32] and imm32 operands are 2 tokens long.
    switch (result.storage_target) {
      case InstructionStorageTarget::kPointSize:
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5) | saturate_bit);
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0100, 1));
        shader_code_.push_back(uint32_t(InOutRegister::kVSOutPointParameters));
        break;
      case InstructionStorageTarget::kDepth:
        assert_true(writes_depth());
        if (writes_depth()) {
          if (edram_rov_used_) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
            shader_code_.push_back(system_temp_depth_);
          } else {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
            shader_code_.push_back(
                EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH, 0));
          }
        }
        break;
      default:
        assert_unhandled_case(result.storage_target);
        return;
    }
    if (component <= SwizzleSource::kW) {
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, uint32_t(component), 1));
      shader_code_.push_back(reg);
    } else {
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(component == SwizzleSource::k1 ? 0x3F800000 : 0);
    }
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
    return;
  }

  // Get the write masks and data required for loading of both the swizzled part
  // and the constant (zero/one) part. The write mask is treated also as a read
  // mask in DXBC, and `mov r0.zw, r1.xyzw` actually means r0.zw = r1.zw, not
  // r0.zw = r1.xy.
  uint32_t swizzle_mask = 0;
  uint32_t swizzle_components = 0;
  uint32_t constant_mask = 0;
  uint32_t constant_values = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!result.write_mask[i]) {
      continue;
    }
    SwizzleSource component = result.components[i];
    if (component <= SwizzleSource::kW) {
      swizzle_mask |= 1 << i;
      // If replicating X, just keep zero swizzle (XXXX).
      if (!replicate_x) {
        swizzle_components |= uint32_t(component) << (i * 2);
      }
    } else {
      constant_mask |= 1 << i;
      constant_values |= (component == SwizzleSource::k1 ? 1 : 0) << i;
    }
  }

  bool is_static = result.storage_addressing_mode ==
                   InstructionStorageAddressingMode::kStatic;
  // If the index is dynamic, choose where it's taken from.
  uint32_t dynamic_address_register, dynamic_address_component;
  if (result.storage_addressing_mode ==
      InstructionStorageAddressingMode::kAddressRelative) {
    // Addressed by aL.x.
    dynamic_address_register = system_temp_aL_;
    dynamic_address_component = 0;
  } else {
    // Addressed by a0.
    dynamic_address_register = system_temp_ps_pc_p0_a0_;
    dynamic_address_component = 3;
  }

  // Temporary registers for storing dynamically indexed GPRs via movc.
  uint32_t gpr_movc_source_register = UINT32_MAX;
  uint32_t gpr_movc_mask_register = UINT32_MAX;
  if (result.storage_target == InstructionStorageTarget::kRegister &&
      !is_static && !IndexableGPRsUsed()) {
    gpr_movc_source_register = PushSystemTemp();
    gpr_movc_mask_register = PushSystemTemp();
  }

  // Store both parts of the write (i == 0 - swizzled, i == 1 - constant).
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t mask = i == 0 ? swizzle_mask : constant_mask;
    if (mask == 0) {
      continue;
    }

    // r# for the swizzled part, 4-component imm32 for the constant part.
    uint32_t source_length = i != 0 ? 5 : 2;
    switch (result.storage_target) {
      case InstructionStorageTarget::kRegister:
        if (IndexableGPRsUsed()) {
          ++stat_.instruction_count;
          ++stat_.array_instruction_count;
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH((is_static ? 4 : 6) +
                                                           source_length) |
              saturate_bit);
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, mask, 2,
              D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
              is_static ? D3D10_SB_OPERAND_INDEX_IMMEDIATE32
                        : D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE));
          shader_code_.push_back(0);
          shader_code_.push_back(uint32_t(result.storage_index));
          if (!is_static) {
            shader_code_.push_back(EncodeVectorSelectOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
            shader_code_.push_back(dynamic_address_register);
          }
        } else {
          ++stat_.instruction_count;
          ++stat_.mov_instruction_count;
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + source_length) |
              saturate_bit);
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, mask, 1));
          shader_code_.push_back(is_static ? uint32_t(result.storage_index)
                                           : gpr_movc_source_register);
        }
        break;

      case InstructionStorageTarget::kInterpolant:
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + source_length) |
            saturate_bit);
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, mask, 1));
        shader_code_.push_back(uint32_t(InOutRegister::kVSOutInterpolators) +
                               uint32_t(result.storage_index));
        break;

      case InstructionStorageTarget::kPosition:
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + source_length) |
            saturate_bit);
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, mask, 1));
        shader_code_.push_back(system_temp_position_);
        break;

      case InstructionStorageTarget::kColorTarget:
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + source_length) |
            saturate_bit);
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, mask, 1));
        shader_code_.push_back(
            system_temp_color_[uint32_t(result.storage_index)]);
        break;

      default:
        continue;
    }

    if (i == 0) {
      // Copy from the source r#.
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, swizzle_components, 1));
      shader_code_.push_back(reg);
    } else {
      // Load constants.
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      for (uint32_t j = 0; j < 4; ++j) {
        shader_code_.push_back((constant_values & (1 << j)) ? 0x3F800000 : 0);
      }
    }
  }

  // Store to the GPR using lots of movc instructions if not using indexable
  // temps, but the target has a relative address.
  if (gpr_movc_source_register != UINT32_MAX) {
    for (uint32_t i = 0; i < register_count(); ++i) {
      if ((i & 3) == 0) {
        // Compare the dynamic address to each register number to check if it's
        // the one that's needed.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(gpr_movc_mask_register);
        shader_code_.push_back(EncodeVectorReplicatedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
        shader_code_.push_back(dynamic_address_register);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        for (uint32_t j = 0; j < 4; ++j) {
          shader_code_.push_back(i + j - uint32_t(result.storage_index));
        }
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
      }
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(i);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i & 3, 1));
      shader_code_.push_back(gpr_movc_mask_register);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(gpr_movc_source_register);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(i);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }
    PopSystemTemp(2);
  }

  if (edram_rov_used_ &&
      result.storage_target == InstructionStorageTarget::kColorTarget) {
    // For ROV output, mark that the color has been written to.
    // According to:
    // https://docs.microsoft.com/en-us/windows/desktop/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-output-color
    // if a color target has been written to - including due to flow control -
    // the render target must not be modified (the unwritten components of a
    // written target are undefined, but let's keep the original value in this
    // case).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, 1 << uint32_t(result.storage_index), 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, uint32_t(result.storage_index), 1));
    shader_code_.push_back(system_temp_color_written_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(swizzle_mask | constant_mask);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }
}

void DxbcShaderTranslator::UpdateExecConditionals(
    ParsedExecInstruction::Type type, uint32_t bool_constant_index,
    bool condition, bool emit_disassembly) {
  // Check if we can merge the new exec with the previous one, or the jump with
  // the previous exec. The instruction-level predicate check is also merged in
  // this case.
  bool merge = false;
  if (type == ParsedExecInstruction::Type::kConditional) {
    // Can merge conditional with conditional, as long as the bool constant and
    // the expected values are the same.
    if (cf_exec_bool_constant_ == bool_constant_index &&
        cf_exec_bool_constant_condition_ == condition) {
      merge = true;
    }
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    // Can merge predicated with predicated if the conditions are the same and
    // the previous exec hasn't modified the predicate register.
    if (!cf_exec_predicate_written_ && cf_exec_predicated_ &&
        cf_exec_predicate_condition_ == condition) {
      merge = true;
    }
  } else {
    // Can merge unconditional with unconditional.
    if (cf_exec_bool_constant_ == kCfExecBoolConstantNone &&
        !cf_exec_predicated_) {
      merge = true;
    }
  }

  if (merge) {
    // Emit the disassembly for the exec/jump merged with the previous one.
    if (emit_disassembly) {
      EmitInstructionDisassembly();
    }
    return;
  }

  CloseExecConditionals();

  // Emit the disassembly for the new exec/jump.
  if (emit_disassembly) {
    EmitInstructionDisassembly();
  }

  D3D10_SB_INSTRUCTION_TEST_BOOLEAN test =
      condition ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                : D3D10_SB_INSTRUCTION_TEST_ZERO;

  if (type == ParsedExecInstruction::Type::kConditional) {
    uint32_t bool_constant_test_register = PushSystemTemp();

    // Check the bool constant value.
    if (cbuffer_index_bool_loop_constants_ == kCbufferIndexUnallocated) {
      cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
    }
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(bool_constant_test_register);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3));
    shader_code_.push_back(cbuffer_index_bool_loop_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_code_.push_back(bool_constant_index >> 5);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1u << (bool_constant_index & 31));
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Open the new `if`.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(bool_constant_test_register);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Release bool_constant_test_register.
    PopSystemTemp();

    cf_exec_bool_constant_ = bool_constant_index;
    cf_exec_bool_constant_condition_ = condition;
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    cf_exec_predicated_ = true;
    cf_exec_predicate_condition_ = condition;
  }
}

void DxbcShaderTranslator::CloseExecConditionals() {
  // Within the exec - instruction-level predicate check.
  CloseInstructionPredication();
  // Exec level.
  if (cf_exec_bool_constant_ != kCfExecBoolConstantNone ||
      cf_exec_predicated_) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    cf_exec_bool_constant_ = kCfExecBoolConstantNone;
    cf_exec_predicated_ = false;
  }
  // Nothing relies on the predicate value being unchanged now.
  cf_exec_predicate_written_ = false;
}

void DxbcShaderTranslator::UpdateInstructionPredication(bool predicated,
                                                        bool condition,
                                                        bool emit_disassembly) {
  if (predicated) {
    if (cf_instruction_predicate_if_open_) {
      if (cf_instruction_predicate_condition_ == condition) {
        // Already in the needed instruction-level `if`.
        if (emit_disassembly) {
          EmitInstructionDisassembly();
        }
        return;
      }
      CloseInstructionPredication();
    }

    // Emit the disassembly before opening (or not opening) the new conditional.
    if (emit_disassembly) {
      EmitInstructionDisassembly();
    }

    // If the instruction predicate condition is the same as the exec predicate
    // condition, no need to open a check. However, if there was a `setp` prior
    // to this instruction, the predicate value now may be different than it was
    // in the beginning of the exec.
    if (!cf_exec_predicate_written_ && cf_exec_predicated_ &&
        cf_exec_predicate_condition_ == condition) {
      return;
    }

    D3D10_SB_INSTRUCTION_TEST_BOOLEAN test =
        condition ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                  : D3D10_SB_INSTRUCTION_TEST_ZERO;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    cf_instruction_predicate_if_open_ = true;
    cf_instruction_predicate_condition_ = condition;
  } else {
    CloseInstructionPredication();
    if (emit_disassembly) {
      EmitInstructionDisassembly();
    }
  }
}

void DxbcShaderTranslator::CloseInstructionPredication() {
  if (cf_instruction_predicate_if_open_) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    cf_instruction_predicate_if_open_ = false;
  }
}

void DxbcShaderTranslator::JumpToLabel(uint32_t address) {
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_ps_pc_p0_a0_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(address);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CONTINUE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::SwapVertexData(uint32_t vfetch_index,
                                          uint32_t write_mask) {
  // Make sure we have fetch constants.
  if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
    cbuffer_index_fetch_constants_ = cbuffer_count_++;
  }

  // Allocate temporary registers for intermediate values.
  uint32_t temp1 = PushSystemTemp();
  uint32_t temp2 = PushSystemTemp();

  // 8-in-16: Create the value being built in temp1.
  // ushr temp1, pv, l(8, 8, 8, 8)
  // pv: ABCD, temp1: BCD0
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 8-in-16: Insert A in Y of temp1.
  // bfi temp1, l(8, 8, 8, 8), l(8, 8, 8, 8), pv, temp1
  // pv: ABCD, temp1: BAD0
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 8-in-16: Create the source for C insertion in temp2.
  // ushr temp2, pv, l(16, 16, 16, 16)
  // pv: ABCD, temp1: BAD0, temp2: CD00
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 8-in-16: Insert C in W of temp1.
  // bfi temp1, l(8, 8, 8, 8), l(24, 24, 24, 24), temp2, temp1
  // pv: ABCD, temp1: BADC
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get bits indicating what swaps should be done. The endianness is located in
  // the low 2 bits of the second dword of the fetch constant:
  // - 00 for no swap.
  // - 01 for 8-in-16.
  // - 10 for 8-in-32 (8-in-16 and 16-in-32).
  // - 11 for 16-in-32.
  // ubfe temp2.xy, l(1, 1), l(0, 1), fetch.yy
  // pv: ABCD, temp1: BADC, temp2: 8in16/16in32?|8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(1);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (vfetch_index & 1) * 2 + 1, 3));
  shader_code_.push_back(cbuffer_index_fetch_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
  shader_code_.push_back(vfetch_index >> 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 16-in-32 is used as intermediate swapping step here rather than 8-in-32.
  // Thus 8-in-16 needs to be done for 8-in-16 (01) and 8-in-32 (10).
  // And 16-in-32 needs to be done for 8-in-32 (10) and 16-in-32 (11).
  // xor temp2.x, temp2.x, temp2.y
  // pv: ABCD, temp1: BADC, temp2: 8in16/8in32?|8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_XOR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(temp2);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Write the 8-in-16 value to pv if needed.
  // movc pv, temp2.xxxx, temp1, pv
  // pv: ABCD/BADC, temp2: 8in16/8in32?|8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // 16-in-32: Write the low 16 bits to temp1.
  // ushr temp1, pv, l(16, 16, 16, 16)
  // pv: ABCD/BADC, temp1: CD00/DC00, temp2: 8in16/8in32?|8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 16-in-32: Write the high 16 bits to temp1.
  // bfi temp1, l(16, 16, 16, 16), l(16, 16, 16, 16), pv, temp1
  // pv: ABCD/BADC, temp1: CDAB/DCBA, temp2: 8in16/8in32?|8in32/16in32?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(16);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Write the swapped value to pv.
  // movc pv, temp2.yyyy, temp1, pv
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, write_mask, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_pv_);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  PopSystemTemp(2);
}

void DxbcShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0) {
    // 0 already added in the beginning.
    return;
  }

  // Close flow control on the deeper levels below - prevent attempts to merge
  // execs across labels.
  CloseExecConditionals();

  if (FLAGS_dxbc_switch) {
    // Fallthrough to the label from the previous one on the next iteration if
    // no `continue` was done. Can't simply fallthrough because in DXBC, a
    // non-empty switch case must end with a break.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(cf_index);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CONTINUE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    // Close the previous label.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    // Go to the next label.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(cf_index);
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;
  } else {
    // Close the previous label.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // pc <= cf_index
    uint32_t test_register = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UGE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(test_register);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(cf_index);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // if (pc <= cf_index)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(test_register);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    // Will be emitted by UpdateExecConditionals.
  }
  UpdateExecConditionals(instr.type, instr.bool_constant_index, instr.condition,
                         true);
  // TODO(Triang3l): Find out what PredicateClean=false in exec actually means
  // (execs containing setp have PredicateClean=false, it possibly means that
  // the predicate is dirty after the exec).
}

void DxbcShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  // TODO(Triang3l): Check whether is_end is conditional or not.
  if (instr.is_end) {
    // Break out of the main loop.
    CloseInstructionPredication();
    if (FLAGS_dxbc_switch) {
      // Write an invalid value to pc.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFFFFFFFFu);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // Go to the next iteration, where switch cases won't be reached.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CONTINUE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    } else {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }
  }
}

void DxbcShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  // loop il<idx>, L<idx> - loop with loop data il<idx>, end @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    EmitInstructionDisassembly();
  }

  uint32_t loop_count_and_aL = PushSystemTemp();

  // Count (as uint) in bits 0:7 of the loop constant, aL in 8:15.
  if (cbuffer_index_bool_loop_constants_ == kCbufferIndexUnallocated) {
    cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
  }
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(loop_count_and_aL);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(8);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(8);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3));
  shader_code_.push_back(cbuffer_index_bool_loop_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
  // 8 because of bool constants.
  shader_code_.push_back(8 + instr.loop_constant_index);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Push the count to the loop count stack - move XYZ to YZW and set X to this
  // loop count.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1110, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b10010000, 1));
  shader_code_.push_back(system_temp_loop_count_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(loop_count_and_aL);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Push aL - keep the same value as in the previous loop if repeating, or the
  // new one otherwise.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(system_temp_aL_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b10010000, 1));
  shader_code_.push_back(system_temp_aL_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
  if (!instr.is_repeat) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temp_aL_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(loop_count_and_aL);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  // Release loop_count_and_aL.
  PopSystemTemp();

  // Short-circuit if loop counter is 0.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_ZERO) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_loop_count_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;
  JumpToLabel(instr.loop_skip_address);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  // endloop il<idx>, L<idx> - end loop w/ data il<idx>, head @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    EmitInstructionDisassembly();
  }

  // Subtract 1 from the loop counter.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(-1));
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Break case.

  if (instr.is_predicated_break) {
    // if (loop_count.x == 0 || [!]p0)
    uint32_t break_case_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(break_case_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    if (instr.predicate_condition) {
      // If p0 is non-zero, set the test value to 0 (since if_z is used,
      // otherwise check if the loop counter is zero).
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
    }
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_loop_count_);
    if (!instr.predicate_condition) {
      // If p0 is zero, set the test value to 0 (since if_z is used, otherwise
      // check if the loop counter is zero).
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
    }
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(break_case_temp);
    PopSystemTemp();
  } else {
    // if (loop_count.x == 0)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_loop_count_);
  }
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Pop the current loop off the stack, move YZW to XYZ and set W to 0.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11111001, 1));
  shader_code_.push_back(system_temp_loop_count_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Now going to fall through to the next exec (no need to jump).

  // Continue case.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  uint32_t aL_add_temp = PushSystemTemp();

  // Extract the value to add to aL (in bits 16:23 of the loop constant).
  if (cbuffer_index_bool_loop_constants_ == kCbufferIndexUnallocated) {
    cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
  }
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(aL_add_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3));
  shader_code_.push_back(cbuffer_index_bool_loop_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
  // 8 because of bool constants.
  shader_code_.push_back(8 + instr.loop_constant_index);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Add the needed value to aL.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_aL_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_loop_count_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(aL_add_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Release aL_add_temp.
  PopSystemTemp();

  // Jump back to the beginning of the loop body.
  JumpToLabel(instr.loop_body_address);

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    // Will be emitted by UpdateExecConditionals.
  }

  // Treat like exec, merge with execs if possible, since it's an if too.
  ParsedExecInstruction::Type type;
  if (instr.type == ParsedJumpInstruction::Type::kConditional) {
    type = ParsedExecInstruction::Type::kConditional;
  } else if (instr.type == ParsedJumpInstruction::Type::kPredicated) {
    type = ParsedExecInstruction::Type::kPredicated;
  } else {
    type = ParsedExecInstruction::Type::kUnconditional;
  }
  UpdateExecConditionals(type, instr.bool_constant_index, instr.condition,
                         true);

  // UpdateExecConditionals may not necessarily close the instruction-level
  // predicate check (it's not necessary if the execs are merged), but here the
  // instruction itself is on the flow control level, so the predicate check is
  // on the flow control level too.
  CloseInstructionPredication();

  JumpToLabel(instr.target_address);
}

void DxbcShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  if (instr.operand_count < 2 ||
      instr.operands[1].storage_source !=
          InstructionStorageSource::kVertexFetchConstant) {
    assert_always();
    return;
  }

  // Get the mask for ld_raw and byte swapping.
  uint32_t load_dword_count;
  switch (instr.attributes.data_format) {
    case VertexFormat::k_8_8_8_8:
    case VertexFormat::k_2_10_10_10:
    case VertexFormat::k_10_11_11:
    case VertexFormat::k_11_11_10:
    case VertexFormat::k_16_16:
    case VertexFormat::k_16_16_FLOAT:
    case VertexFormat::k_32:
    case VertexFormat::k_32_FLOAT:
      load_dword_count = 1;
      break;
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_16_16_16_16_FLOAT:
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_FLOAT:
      load_dword_count = 2;
      break;
    case VertexFormat::k_32_32_32_FLOAT:
      load_dword_count = 3;
      break;
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_32_32_32_32_FLOAT:
      load_dword_count = 4;
      break;
    default:
      assert_unhandled_case(instr.attributes.data_format);
      return;
  }
  // Get the result write mask.
  uint32_t result_component_count =
      GetVertexFormatComponentCount(instr.attributes.data_format);
  if (result_component_count == 0) {
    assert_always();
    return;
  }
  uint32_t result_write_mask = (1 << result_component_count) - 1;

  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    // Will be emitted by UpdateInstructionPredication.
  }
  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition,
                               true);

  // Convert the index to an integer.
  DxbcSourceOperand index_operand;
  LoadDxbcSourceOperand(instr.operands[0], index_operand);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                             3 + DxbcSourceOperandLength(index_operand)));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_pv_);
  UseDxbcSourceOperand(index_operand, kSwizzleXYZW, 0);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;
  UnloadDxbcSourceOperand(index_operand);
  // TODO(Triang3l): Index clamping maybe.

  uint32_t vfetch_index = instr.operands[1].storage_index;

  // Get the memory address (taken from the fetch constant - the low 2 bits of
  // it are removed because vertices and raw buffer operations are 4-aligned and
  // fetch type - 3 for vertices - is stored there). Vertex fetch is specified
  // by 2 dwords in fetch constants, but in our case they are 4-component, so
  // one vector of fetch constants contains two vfetches.
  // TODO(Triang3l): Clamp to buffer size maybe (may be difficult if the buffer
  // is smaller than 16).
  // http://xboxforums.create.msdn.com/forums/p/7537/39919.aspx#39919
  if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
    cbuffer_index_fetch_constants_ = cbuffer_count_++;
  }
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (vfetch_index & 1) * 2, 3));
  shader_code_.push_back(cbuffer_index_fetch_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
  shader_code_.push_back(vfetch_index >> 1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x1FFFFFFC);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Calculate the address of the vertex.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(instr.attributes.stride * 4);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_pv_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Add the element offset.
  if (instr.attributes.offset != 0) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(instr.attributes.offset * 4);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
  }

  // Load the vertex data from the shared memory at T0, register t0.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_RAW) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, (1 << load_dword_count) - 1, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_RESOURCE,
      kSwizzleXYZW & ((1 << (load_dword_count * 2)) - 1), 2));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.texture_load_instructions;

  // Byte swap the data.
  SwapVertexData(vfetch_index, (1 << load_dword_count) - 1);

  // Get the data needed for unpacking and converting.
  bool extract_signed = instr.attributes.is_signed;
  uint32_t extract_widths[4] = {}, extract_offsets[4] = {};
  uint32_t extract_swizzle = kSwizzleXXXX;
  float normalize_scales[4] = {};
  switch (instr.attributes.data_format) {
    case VertexFormat::k_8_8_8_8:
      extract_widths[0] = extract_widths[1] = extract_widths[2] =
          extract_widths[3] = 8;
      // Assuming little endian ByteAddressBuffer Load.
      extract_offsets[1] = 8;
      extract_offsets[2] = 16;
      extract_offsets[3] = 24;
      normalize_scales[0] = normalize_scales[1] = normalize_scales[2] =
          normalize_scales[3] =
              instr.attributes.is_signed ? (1.0f / 127.0f) : (1.0f / 255.0f);
      break;
    case VertexFormat::k_2_10_10_10:
      extract_widths[0] = extract_widths[1] = extract_widths[2] = 10;
      extract_widths[3] = 2;
      extract_offsets[1] = 10;
      extract_offsets[2] = 20;
      extract_offsets[3] = 30;
      normalize_scales[0] = normalize_scales[1] = normalize_scales[2] =
          instr.attributes.is_signed ? (1.0f / 511.0f) : (1.0f / 1023.0f);
      normalize_scales[3] = instr.attributes.is_signed ? 1.0f : (1.0f / 3.0f);
      break;
    case VertexFormat::k_10_11_11:
      extract_widths[0] = extract_widths[1] = 11;
      extract_widths[2] = 10;
      extract_offsets[1] = 11;
      extract_offsets[2] = 22;
      normalize_scales[0] = normalize_scales[1] =
          instr.attributes.is_signed ? (1.0f / 1023.0f) : (1.0f / 2047.0f);
      normalize_scales[2] =
          instr.attributes.is_signed ? (1.0f / 511.0f) : (1.0f / 1023.0f);
      break;
    case VertexFormat::k_11_11_10:
      extract_widths[0] = 10;
      extract_widths[1] = extract_widths[2] = 11;
      extract_offsets[1] = 10;
      extract_offsets[2] = 21;
      normalize_scales[0] =
          instr.attributes.is_signed ? (1.0f / 511.0f) : (1.0f / 1023.0f);
      normalize_scales[1] = normalize_scales[2] =
          instr.attributes.is_signed ? (1.0f / 1023.0f) : (1.0f / 2047.0f);
      break;
    case VertexFormat::k_16_16:
      extract_widths[0] = extract_widths[1] = 16;
      extract_offsets[1] = 16;
      normalize_scales[0] = normalize_scales[1] =
          instr.attributes.is_signed ? (1.0f / 32767.0f) : (1.0f / 65535.0f);
      break;
    case VertexFormat::k_16_16_16_16:
      extract_widths[0] = extract_widths[1] = extract_widths[2] =
          extract_widths[3] = 16;
      extract_offsets[1] = extract_offsets[3] = 16;
      extract_swizzle = 0b01010000;
      normalize_scales[0] = normalize_scales[1] = normalize_scales[2] =
          normalize_scales[3] = instr.attributes.is_signed ? (1.0f / 32767.0f)
                                                           : (1.0f / 65535.0f);
      break;
    case VertexFormat::k_16_16_FLOAT:
      extract_signed = false;
      extract_widths[0] = extract_widths[1] = 16;
      extract_offsets[1] = 16;
      break;
    case VertexFormat::k_16_16_16_16_FLOAT:
      extract_signed = false;
      extract_widths[0] = extract_widths[1] = extract_widths[2] =
          extract_widths[3] = 16;
      extract_offsets[1] = extract_offsets[3] = 16;
      extract_swizzle = 0b01010000;
      break;
    // For 32-bit, extraction is not done at all, so its parameters are ignored.
    case VertexFormat::k_32:
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_32_32:
      normalize_scales[0] = normalize_scales[1] = normalize_scales[2] =
          normalize_scales[3] =
              instr.attributes.is_signed ? (1.0f / 2147483647.0f)
                                         : (1.0f / 4294967295.0f);
      break;
    default:
      // 32-bit float.
      break;
  }

  // Extract components from packed data if needed.
  if (extract_widths[0] != 0) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(extract_signed ? D3D11_SB_OPCODE_IBFE
                                                   : D3D11_SB_OPCODE_UBFE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     result_write_mask, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(extract_widths[0]);
    shader_code_.push_back(extract_widths[1]);
    shader_code_.push_back(extract_widths[2]);
    shader_code_.push_back(extract_widths[3]);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(extract_offsets[0]);
    shader_code_.push_back(extract_offsets[1]);
    shader_code_.push_back(extract_offsets[2]);
    shader_code_.push_back(extract_offsets[3]);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, extract_swizzle, 1));
    shader_code_.push_back(system_temp_pv_);
    ++stat_.instruction_count;
    if (extract_signed) {
      ++stat_.int_instruction_count;
    } else {
      ++stat_.uint_instruction_count;
    }
  }

  // Convert to float and normalize if needed.
  if (instr.attributes.data_format == VertexFormat::k_16_16_FLOAT ||
      instr.attributes.data_format == VertexFormat::k_16_16_16_16_FLOAT) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_F16TOF32) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     result_write_mask, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_pv_);
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;
  } else if (normalize_scales[0] != 0.0f) {
    // If no normalize_scales, it's a float value already. Otherwise, convert to
    // float and normalize if needed.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(instr.attributes.is_signed
                                        ? D3D10_SB_OPCODE_ITOF
                                        : D3D10_SB_OPCODE_UTOF) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     result_write_mask, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_pv_);
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;
    if (!instr.attributes.is_integer) {
      // Normalize.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, result_write_mask, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      for (uint32_t i = 0; i < 4; ++i) {
        shader_code_.push_back(
            reinterpret_cast<const uint32_t*>(normalize_scales)[i]);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp to -1 (both -127 and -128 should be -1 in graphics APIs for
      // snorm8).
      if (instr.attributes.is_signed) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, result_write_mask, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0xBF800000u);
        shader_code_.push_back(0xBF800000u);
        shader_code_.push_back(0xBF800000u);
        shader_code_.push_back(0xBF800000u);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
    }
  }

  // Zero unused components if loaded a 32-bit component (because it's not
  // bfe'd, in this case, the unused components would have been zeroed already).
  if (extract_widths[0] == 0 && result_write_mask != 0b1111) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, 0b1111 & ~result_write_mask, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  // Apply the exponent bias.
  if (instr.attributes.exp_adjust != 0) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     result_write_mask, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    uint32_t exp_adjust_scale =
        uint32_t(0x3F800000 + (instr.attributes.exp_adjust << 23));
    shader_code_.push_back(exp_adjust_scale);
    shader_code_.push_back(exp_adjust_scale);
    shader_code_.push_back(exp_adjust_scale);
    shader_code_.push_back(exp_adjust_scale);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  StoreResult(instr.result, system_temp_pv_, false);
}

uint32_t DxbcShaderTranslator::FindOrAddTextureSRV(uint32_t fetch_constant,
                                                   TextureDimension dimension,
                                                   bool is_signed,
                                                   bool is_sign_required) {
  // 1D and 2D textures (including stacked ones) are treated as 2D arrays for
  // binding and coordinate simplicity.
  if (dimension == TextureDimension::k1D) {
    dimension = TextureDimension::k2D;
  }
  // 1 is added to the return value because T0/t0 is shared memory.
  for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
    TextureSRV& texture_srv = texture_srvs_[i];
    if (texture_srv.fetch_constant == fetch_constant &&
        texture_srv.dimension == dimension &&
        texture_srv.is_signed == is_signed) {
      if (is_sign_required && !texture_srv.is_sign_required) {
        // kGetTextureComputedLod uses only the unsigned SRV, which means it
        // must be bound even when all components are signed.
        texture_srv.is_sign_required = true;
      }
      return 1 + i;
    }
  }
  if (texture_srvs_.size() >= kMaxTextureSRVs) {
    assert_always();
    return 1 + (kMaxTextureSRVs - 1);
  }
  TextureSRV new_texture_srv;
  new_texture_srv.fetch_constant = fetch_constant;
  new_texture_srv.dimension = dimension;
  new_texture_srv.is_signed = is_signed;
  new_texture_srv.is_sign_required = is_sign_required;
  const char* dimension_name;
  switch (dimension) {
    case TextureDimension::k3D:
      dimension_name = "3d";
      break;
    case TextureDimension::kCube:
      dimension_name = "cube";
      break;
    default:
      dimension_name = "2d";
  }
  new_texture_srv.name =
      xe::format_string("xe_texture%u_%s_%c", fetch_constant, dimension_name,
                        is_signed ? 's' : 'u');
  uint32_t srv_register = 1 + uint32_t(texture_srvs_.size());
  texture_srvs_.emplace_back(std::move(new_texture_srv));
  return srv_register;
}

uint32_t DxbcShaderTranslator::FindOrAddSamplerBinding(
    uint32_t fetch_constant, TextureFilter mag_filter, TextureFilter min_filter,
    TextureFilter mip_filter, AnisoFilter aniso_filter) {
  // In Direct3D 12, anisotropic filtering implies linear filtering.
  if (aniso_filter != AnisoFilter::kDisabled &&
      aniso_filter != AnisoFilter::kUseFetchConst) {
    mag_filter = TextureFilter::kLinear;
    min_filter = TextureFilter::kLinear;
    mip_filter = TextureFilter::kLinear;
    aniso_filter = std::min(aniso_filter, AnisoFilter::kMax_16_1);
  }

  for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
    const SamplerBinding& sampler_binding = sampler_bindings_[i];
    if (sampler_binding.fetch_constant == fetch_constant &&
        sampler_binding.mag_filter == mag_filter &&
        sampler_binding.min_filter == min_filter &&
        sampler_binding.mip_filter == mip_filter &&
        sampler_binding.aniso_filter == aniso_filter) {
      return i;
    }
  }

  if (sampler_bindings_.size() >= kMaxSamplerBindings) {
    assert_always();
    return kMaxSamplerBindings - 1;
  }

  std::ostringstream name;
  name << "xe_sampler" << fetch_constant;
  if (aniso_filter != AnisoFilter::kUseFetchConst) {
    if (aniso_filter == AnisoFilter::kDisabled) {
      name << "_a0";
    } else {
      name << "_a" << (1u << (uint32_t(aniso_filter) - 1));
    }
  }
  if (aniso_filter == AnisoFilter::kDisabled ||
      aniso_filter == AnisoFilter::kUseFetchConst) {
    static const char* kFilterSuffixes[] = {"p", "l", "b", "f"};
    name << "_" << kFilterSuffixes[uint32_t(mag_filter)]
         << kFilterSuffixes[uint32_t(min_filter)]
         << kFilterSuffixes[uint32_t(mip_filter)];
  }

  SamplerBinding new_sampler_binding;
  new_sampler_binding.fetch_constant = fetch_constant;
  new_sampler_binding.mag_filter = mag_filter;
  new_sampler_binding.min_filter = min_filter;
  new_sampler_binding.mip_filter = mip_filter;
  new_sampler_binding.aniso_filter = aniso_filter;
  new_sampler_binding.name = name.str();
  uint32_t sampler_register = uint32_t(sampler_bindings_.size());
  sampler_bindings_.emplace_back(std::move(new_sampler_binding));
  return sampler_register;
}

void DxbcShaderTranslator::ArrayCoordToCubeDirection(uint32_t reg) {
  // This does the reverse of what the cube vector ALU instruction does, but
  // assuming S and T are normalized.
  //
  // The major axis depends on the face index (passed as a float in reg.z):
  // +X for 0, -X for 1, +Y for 2, -Y for 3, +Z for 4, -Z for 5.
  //
  // If the major axis is X:
  // * X is 1.0 or -1.0.
  // * Y is -T.
  // * Z is -S for positive X, +S for negative X.
  // If it's Y:
  // * X is +S.
  // * Y is 1.0 or -1.0.
  // * Z is +T for positive Y, -T for negative Y.
  // If it's Z:
  // * X is +S for positive Z, -S for negative Z.
  // * Y is -T.
  // * Z is 1.0 or -1.0.

  // Make 0, not 0.5, the center of S and T.
  // mad reg.xy__, reg.xy__, l(2.0, 2.0, _, _), l(-1.0, -1.0, _, _)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x40000000u);
  shader_code_.push_back(0x40000000u);
  shader_code_.push_back(0x3F800000u);
  shader_code_.push_back(0x3F800000u);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Clamp the face index to 0...5 for safety (in case an offset was applied).
  // max reg.z, reg.z, l(0.0)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // min reg.z, reg.z, l(5.0)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x40A00000);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Allocate a register for major axis info.
  uint32_t major_axis_temp = PushSystemTemp();

  // Convert the face index to an integer.
  // ftou major_axis_temp.x, reg.z
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Split the face number into major axis number and direction.
  // ubfe major_axis_temp.x__w, l(2, _, _, 1), l(1, _, _, 0),
  //      major_axis_temp.x__x
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1001, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Make booleans for whether each axis is major.
  // ieq major_axis_temp.xyz_, major_axis_temp.xxx_, l(0, 1, 2, _)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(2);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Replace the face index in the source/destination with 1.0 or -1.0 for
  // swizzling.
  // movc reg.z, major_axis_temp.w, l(-1.0), l(1.0)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000u);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Swizzle and negate the coordinates depending on which axis is major, but
  // don't negate according to the direction of the major axis (will be done
  // later).

  // X case.
  // movc reg.xyz_, major_axis_temp.xxx_, reg.zyx_, reg.xyz_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11000110, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // movc reg._yz_, major_axis_temp._xx_, -reg._yz_, reg._yz_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Y case.
  // movc reg._yz_, major_axis_temp._yy_, reg._zy_, reg._yz_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11011000, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Z case.
  // movc reg.y, major_axis_temp.z, -reg.y, reg.y
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Flip coordinates according to the direction of the major axis.

  // Z needs to be flipped if the major axis is X or Y, so make an X || Y mask.
  // X is flipped only when the major axis is Z.
  // or major_axis_temp.x, major_axis_temp.x, major_axis_temp.y
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(major_axis_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // If the major axis is positive, nothing needs to be flipped. We have
  // 0xFFFFFFFF/0 at this point in the major axis mask, but 1/0 in the major
  // axis direction (didn't include W in ieq to waste less scalar operations),
  // but AND would result in 1/0, which is fine for movc too.
  // and major_axis_temp.x_z_, major_axis_temp.x_z_, major_axis_temp.w_w_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(major_axis_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Flip axes that need to be flipped.
  // movc reg.x_z_, major_axis_temp.z_x_, -reg.x_z_, reg.x_z_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11000110, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Release major_axis_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    // Will be emitted later explicitly or by UpdateInstructionPredication.
  }

  // Predication should not affect derivative calculation:
  // https://docs.microsoft.com/en-us/windows/desktop/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-output-color
  // Do the part involving derivative calculation unconditionally, and re-enter
  // the predicate check before writing the result.
  bool suppress_predication = false;
  if (IsDXBCPixelShader()) {
    if (instr.opcode == FetchOpcode::kGetTextureComputedLod ||
        instr.opcode == FetchOpcode::kGetTextureGradients) {
      suppress_predication = true;
    } else if (instr.opcode == FetchOpcode::kTextureFetch) {
      suppress_predication = instr.attributes.use_computed_lod &&
                             !instr.attributes.use_register_lod;
    }
  }
  uint32_t exec_p0_temp = UINT32_MAX;
  if (suppress_predication) {
    // Emit the disassembly before all this to indicate the reason of going
    // unconditional.
    EmitInstructionDisassembly();
    // Close instruction-level predication.
    CloseInstructionPredication();
    // Temporarily close exec-level predication - will reopen at the end, so not
    // changing cf_exec_predicated_.
    if (cf_exec_predicated_) {
      if (cf_exec_predicate_written_) {
        // Restore the predicate value in the beginning of the exec and put it
        // in exec_p0_temp.
        exec_p0_temp = PushSystemTemp();
        // `if` case - the value was cf_exec_predicate_condition_.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exec_p0_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(cf_exec_predicate_condition_ ? 0xFFFFFFFFu : 0u);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
        // `else` case - the value was !cf_exec_predicate_condition_.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exec_p0_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(cf_exec_predicate_condition_ ? 0u : 0xFFFFFFFFu);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
      }
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }
  } else {
    UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition,
                                 true);
  }

  bool store_result = false;
  // Whether the result is only in X and all components should be remapped to X
  // while storing.
  bool replicate_result = false;

  DxbcSourceOperand operand;
  uint32_t operand_length = 0;
  if (instr.operand_count >= 1) {
    LoadDxbcSourceOperand(instr.operands[0], operand);
    operand_length = DxbcSourceOperandLength(operand);
  }

  uint32_t tfetch_index = instr.operands[1].storage_index;
  // Fetch constants are laid out like:
  // tf0[0] tf0[1] tf0[2] tf0[3]
  // tf0[4] tf0[5] tf1[0] tf1[1]
  // tf1[2] tf1[3] tf1[4] tf1[5]
  uint32_t tfetch_pair_offset = (tfetch_index >> 1) * 3;

  // TODO(Triang3l): kGetTextureBorderColorFrac.
  if (!IsDXBCPixelShader() &&
      (instr.opcode == FetchOpcode::kGetTextureComputedLod ||
       instr.opcode == FetchOpcode::kGetTextureGradients)) {
    // Quickly skip everything if tried to get anything involving derivatives
    // not in a pixel shader because only the pixel shader has derivatives.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  } else if (instr.opcode == FetchOpcode::kTextureFetch ||
             instr.opcode == FetchOpcode::kGetTextureComputedLod ||
             instr.opcode == FetchOpcode::kGetTextureWeights) {
    store_result = true;

    // 0 is unsigned, 1 is signed.
    uint32_t srv_registers[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t srv_registers_stacked[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t sampler_register = UINT32_MAX;
    // Only the fetch constant needed for kGetTextureWeights.
    if (instr.opcode != FetchOpcode::kGetTextureWeights) {
      if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
        // The LOD is a scalar and it doesn't depend on the texture contents, so
        // require any variant - unsigned in this case because more texture
        // formats support it.
        srv_registers[0] =
            FindOrAddTextureSRV(tfetch_index, instr.dimension, false, true);
        if (instr.dimension == TextureDimension::k3D) {
          // 3D or 2D stacked is selected dynamically.
          srv_registers_stacked[0] = FindOrAddTextureSRV(
              tfetch_index, TextureDimension::k2D, false, true);
        }
      } else {
        srv_registers[0] =
            FindOrAddTextureSRV(tfetch_index, instr.dimension, false);
        srv_registers[1] =
            FindOrAddTextureSRV(tfetch_index, instr.dimension, true);
        if (instr.dimension == TextureDimension::k3D) {
          // 3D or 2D stacked is selected dynamically.
          srv_registers_stacked[0] =
              FindOrAddTextureSRV(tfetch_index, TextureDimension::k2D, false);
          srv_registers_stacked[1] =
              FindOrAddTextureSRV(tfetch_index, TextureDimension::k2D, true);
        }
      }
      sampler_register = FindOrAddSamplerBinding(
          tfetch_index, instr.attributes.mag_filter,
          instr.attributes.min_filter, instr.attributes.mip_filter,
          instr.attributes.aniso_filter);
    }

    uint32_t coord_temp = PushSystemTemp();
    // Move coordinates to pv temporarily so zeros can be added to expand them
    // to Texture2DArray coordinates and to apply offset. Or, if the instruction
    // is getWeights, move them to pv because their fractional part will be
    // returned.
    uint32_t coord_mask = 0b0111;
    switch (instr.dimension) {
      case TextureDimension::k1D:
        coord_mask = 0b0001;
        break;
      case TextureDimension::k2D:
        coord_mask = 0b0011;
        break;
      case TextureDimension::k3D:
        coord_mask = 0b0111;
        break;
      case TextureDimension::kCube:
        // Don't need the 3rd component for getWeights because it's the face
        // index, so it doesn't participate in bilinear filtering.
        coord_mask =
            instr.opcode == FetchOpcode::kGetTextureWeights ? 0b0011 : 0b0111;
        break;
    }
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
    shader_code_.push_back(coord_temp);
    UseDxbcSourceOperand(operand);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // If 1D or 2D, fill the unused coordinates with zeros (sampling the only
    // row of the only slice). For getWeights, also clear the 4th component
    // because the coordinates will be returned.
    uint32_t coord_all_components_mask =
        instr.opcode == FetchOpcode::kGetTextureWeights ? 0b1111 : 0b0111;
    uint32_t coord_zero_mask = coord_all_components_mask & ~coord_mask;
    if (coord_zero_mask) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, coord_zero_mask, 1));
      shader_code_.push_back(coord_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
    }

    // Get the offset to see if the size of the texture is needed.
    // It's probably applicable to tfetchCube too, we're going to assume it's
    // used for them the same way as for stacked textures.
    // http://web.archive.org/web/20090511231340/http://msdn.microsoft.com:80/en-us/library/bb313959.aspx
    // Adding 1/1024 - quarter of one fixed-point unit of subpixel precision
    // (not to touch rounding when the GPU is converting to fixed-point) - to
    // resolve the ambiguity when the texture coordinate is directly between two
    // pixels, which hurts nearest-neighbor sampling (fixes the XBLA logo being
    // blocky in Banjo-Kazooie and the outlines around things and overall
    // blockiness in Halo 3).
    float offset_x = instr.attributes.offset_x;
    if (instr.opcode != FetchOpcode::kGetTextureWeights) {
      offset_x += 1.0f / 1024.0f;
    }
    float offset_y = 0.0f, offset_z = 0.0f;
    if (instr.dimension == TextureDimension::k2D ||
        instr.dimension == TextureDimension::k3D ||
        instr.dimension == TextureDimension::kCube) {
      offset_y = instr.attributes.offset_y;
      if (instr.opcode != FetchOpcode::kGetTextureWeights) {
        offset_y += 1.0f / 1024.0f;
      }
      // Don't care about the Z offset for cubemaps when getting weights because
      // zero Z will be returned anyway (the face index doesn't participate in
      // bilinear filtering).
      if (instr.dimension == TextureDimension::k3D ||
          (instr.dimension == TextureDimension::kCube &&
           instr.opcode != FetchOpcode::kGetTextureWeights)) {
        offset_z = instr.attributes.offset_z;
        if (instr.opcode != FetchOpcode::kGetTextureWeights &&
            instr.dimension == TextureDimension::k3D) {
          // Z is the face index for cubemaps, so don't apply the epsilon to it.
          offset_z += 1.0f / 1024.0f;
        }
      }
    }

    // Get the texture size if needed, apply offset and switch between
    // normalized and unnormalized coordinates if needed. The offset is
    // fractional on the Xbox 360 (has 0.5 granularity), unlike in Direct3D 12,
    // and cubemaps possibly can have offset and their coordinates are different
    // than in Direct3D 12 (like an array texture rather than a direction).
    // getWeights instructions also need the texture size because they work like
    // frac(coord * texture_size).
    // TODO(Triang3l): Unnormalized coordinates should be disabled when the
    // wrap mode is not a clamped one, though it's probably a very rare case,
    // unlikely to be used on purpose.
    // http://web.archive.org/web/20090514012026/http://msdn.microsoft.com:80/en-us/library/bb313957.aspx
    uint32_t size_and_is_3d_temp = UINT32_MAX;
    // With 1/1024 this will always be true anyway, but let's keep the shorter
    // path without the offset in case some day this hack won't be used anymore
    // somehow.
    bool has_offset = offset_x != 0.0f || offset_y != 0.0f || offset_z != 0.0f;
    if (instr.opcode == FetchOpcode::kGetTextureWeights || has_offset ||
        instr.attributes.unnormalized_coordinates ||
        instr.dimension == TextureDimension::k3D) {
      size_and_is_3d_temp = PushSystemTemp();

      // Will use fetch constants for the size.
      if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
        cbuffer_index_fetch_constants_ = cbuffer_count_++;
      }

      // Get 2D texture size and array layer count, in bits 0:12, 13:25, 26:31
      // of dword 2 ([0].z or [2].x).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(13);
      shader_code_.push_back(instr.dimension != TextureDimension::k1D ? 13 : 0);
      shader_code_.push_back(instr.dimension == TextureDimension::k3D ? 6 : 0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(13);
      shader_code_.push_back(26);
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                        2 - 2 * (tfetch_index & 1), 3));
      shader_code_.push_back(cbuffer_index_fetch_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
      shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1) * 2);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      if (instr.dimension == TextureDimension::k3D) {
        // Write whether the texture is 3D to W if it's 3D/stacked, as
        // 0xFFFFFFFF for 3D or 0 for stacked. The dimension is in dword 5 in
        // bits 9:10.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        // Dword 5 is [1].y or [2].w.
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      1 + 2 * (tfetch_index & 1), 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + 1 + (tfetch_index & 1));
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3 << 9);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(uint32_t(Dimension::k3D) << 9);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;

        uint32_t size_3d_temp = PushSystemTemp();

        // Get 3D texture size to a temporary variable (in the same constant,
        // but 11:11:10).
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
        shader_code_.push_back(size_3d_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(11);
        shader_code_.push_back(11);
        shader_code_.push_back(10);
        shader_code_.push_back(0);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0);
        shader_code_.push_back(11);
        shader_code_.push_back(22);
        shader_code_.push_back(0);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                          2 - 2 * (tfetch_index & 1), 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1) * 2);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;

        // Replace the 2D size with the 3D one if the texture is 3D.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(size_3d_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;

        // Release size_3d_temp.
        PopSystemTemp();
      }

      // Convert the size to float.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;

      // Add 1 to the size because fetch constants store size minus one.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      if (instr.opcode == FetchOpcode::kGetTextureWeights) {
        // Weights for bilinear filtering - need to get the fractional part of
        // unnormalized coordinates.

        if (instr.attributes.unnormalized_coordinates) {
          if (has_offset) {
            // Apply the offset.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_x));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_y));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_z));
            shader_code_.push_back(0);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        } else {
          // Unnormalize the coordinates and apply the offset.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(has_offset ? D3D10_SB_OPCODE_MAD
                                                     : D3D10_SB_OPCODE_MUL) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(has_offset ? 12
                                                                      : 7));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          if (has_offset) {
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_x));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_y));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_z));
            shader_code_.push_back(0);
          }
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;
        }

        if (instr.dimension == TextureDimension::k3D) {
          // Ignore Z if it's the texture is stacked - it's the array layer, so
          // there's no filtering across Z. Keep it only for 3D textures. This
          // assumes that the 3D/stacked flag is 0xFFFFFFFF or 0.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          ++stat_.instruction_count;
          ++stat_.uint_instruction_count;
        }
      } else {
        // Texture fetch - need to get normalized coordinates (with unnormalized
        // Z for stacked textures).

        if (instr.dimension == TextureDimension::k3D) {
          // Both 3D textures and 2D arrays have their Z coordinate normalized,
          // however, on PC, array elements have unnormalized indices.
          // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
          // Put the array layer in W - Z * depth if the fetch uses normalized
          // coordinates, and Z if it uses unnormalized.
          if (instr.attributes.unnormalized_coordinates) {
            ++stat_.instruction_count;
            if (offset_z != 0.0f) {
              ++stat_.float_instruction_count;
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            } else {
              ++stat_.mov_instruction_count;
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
            }
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(coord_temp);
            if (offset_z != 0.0f) {
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(
                  *reinterpret_cast<const uint32_t*>(&offset_x));
            }
          } else {
            if (offset_z != 0.0f) {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
            } else {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            }
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            if (offset_z != 0.0f) {
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(
                  *reinterpret_cast<const uint32_t*>(&offset_x));
            }
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        }

        if (has_offset || instr.attributes.unnormalized_coordinates) {
          // Take the reciprocal of the size to normalize the coordinates and
          // the offset (this is not necessary to just sample 3D/array with
          // normalized coordinates and no offset). For cubemaps, there will be
          // 1 in Z, so this will work.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_RCP) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;

          // Normalize the coordinates.
          if (instr.attributes.unnormalized_coordinates) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }

          // Apply the offset (coord = offset * 1/size + coord).
          if (has_offset) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_x));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_y));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_z));
            shader_code_.push_back(0);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        }
      }
    }

    if (instr.opcode == FetchOpcode::kGetTextureWeights) {
      // Return the fractional part of unnormalized coordinates as bilinear
      // filtering weights.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(coord_temp);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } else {
      if (instr.dimension == TextureDimension::kCube) {
        // Convert cubemap coordinates passed as 2D array texture coordinates to
        // a 3D direction. We can't use a 2D array to emulate cubemaps because
        // at the edges, especially in pixel shader helper invocations, the
        // major axis changes, causing S/T to jump between 0 and 1, breaking
        // gradient calculation and causing the 1x1 mipmap to be sampled.
        ArrayCoordToCubeDirection(coord_temp);
      }

      // Bias the register LOD if fetching with explicit LOD (so this is not
      // done two or four times due to 3D/stacked and unsigned/signed).
      uint32_t lod_temp = system_temp_grad_h_lod_, lod_temp_component = 3;
      if (instr.opcode == FetchOpcode::kTextureFetch &&
          instr.attributes.use_register_lod &&
          instr.attributes.lod_bias != 0.0f) {
        lod_temp = PushSystemTemp();
        lod_temp_component = 0;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(lod_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(system_temp_grad_h_lod_);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(
            *reinterpret_cast<const uint32_t*>(&instr.attributes.lod_bias));
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }

      // Allocate the register for the value from the signed texture, and later
      // for biasing and gamma correction.
      uint32_t signs_value_temp = instr.opcode == FetchOpcode::kTextureFetch
                                      ? PushSystemTemp()
                                      : UINT32_MAX;

      // tfetch1D/2D/Cube just fetch directly. tfetch3D needs to fetch either
      // the 3D texture or the 2D stacked texture, so two sample instructions
      // selected conditionally are used in this case.
      if (instr.dimension == TextureDimension::k3D) {
        assert_true(size_and_is_3d_temp != UINT32_MAX);
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                               ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                   D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        ++stat_.instruction_count;
        ++stat_.dynamic_flow_control_count;
      }
      // Sample both 3D and 2D array bindings for tfetch3D.
      for (uint32_t i = 0;
           i < (instr.dimension == TextureDimension::k3D ? 2u : 1u); ++i) {
        if (i != 0) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }
        // Sample both unsigned and signed.
        for (uint32_t j = 0; j < 2; ++j) {
          uint32_t srv_register_current =
              i != 0 ? srv_registers_stacked[j] : srv_registers[j];
          uint32_t target_temp_current =
              j != 0 ? signs_value_temp : system_temp_pv_;
          if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
            // The non-pixel-shader case should be handled before because it
            // just returns a constant in this case.
            assert_true(IsDXBCPixelShader());
            replicate_result = true;
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_1_SB_OPCODE_LOD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
            shader_code_.push_back(target_temp_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 2));
            shader_code_.push_back(sampler_register);
            shader_code_.push_back(sampler_register);
            ++stat_.instruction_count;
            ++stat_.lod_instructions;
            // Apply the LOD bias if used.
            if (instr.attributes.lod_bias != 0.0f) {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
              shader_code_.push_back(EncodeVectorMaskedOperand(
                  D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
              shader_code_.push_back(target_temp_current);
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
              shader_code_.push_back(target_temp_current);
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(*reinterpret_cast<const uint32_t*>(
                  &instr.attributes.lod_bias));
              ++stat_.instruction_count;
              ++stat_.float_instruction_count;
            }
            // In this case, only the unsigned variant is accessed because data
            // doesn't matter.
            break;
          } else if (instr.attributes.use_register_lod) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_L) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
            shader_code_.push_back(target_temp_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 2));
            shader_code_.push_back(sampler_register);
            shader_code_.push_back(sampler_register);
            shader_code_.push_back(EncodeVectorSelectOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, lod_temp_component, 1));
            shader_code_.push_back(lod_temp);
            ++stat_.instruction_count;
            ++stat_.texture_normal_instructions;
          } else if (instr.attributes.use_register_gradients) {
            // TODO(Triang3l): Apply the LOD bias somehow for register gradients
            // (possibly will require moving the bias to the sampler, which may
            // be not very good considering the sampler count is very limited).
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_D) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
            shader_code_.push_back(target_temp_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 2));
            shader_code_.push_back(sampler_register);
            shader_code_.push_back(sampler_register);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(system_temp_grad_h_lod_);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(system_temp_grad_v_);
            ++stat_.instruction_count;
            ++stat_.texture_gradient_instructions;
          } else {
            // 3 different DXBC opcodes handled here:
            // - sample_l, when not using a computed LOD or not in a pixel
            //   shader, in this case, LOD (0 + bias) is sampled.
            // - sample, when sampling in a pixel shader (thus with derivatives)
            //   with a computed LOD.
            // - sample_b, when sampling in a pixel shader with a biased
            //   computed LOD.
            // Both sample_l and sample_b should add the LOD bias as the last
            // operand in our case.
            bool explicit_lod =
                !instr.attributes.use_computed_lod || !IsDXBCPixelShader();
            if (explicit_lod) {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_L) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
            } else if (instr.attributes.lod_bias != 0.0f) {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_B) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
            } else {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
            }
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
            shader_code_.push_back(target_temp_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(srv_register_current);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 2));
            shader_code_.push_back(sampler_register);
            shader_code_.push_back(sampler_register);
            if (explicit_lod || instr.attributes.lod_bias != 0.0f) {
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(*reinterpret_cast<const uint32_t*>(
                  &instr.attributes.lod_bias));
            }
            ++stat_.instruction_count;
            if (!explicit_lod && instr.attributes.lod_bias != 0.0f) {
              ++stat_.texture_bias_instructions;
            } else {
              ++stat_.texture_normal_instructions;
            }
          }
        }
      }
      if (instr.dimension == TextureDimension::k3D) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
      }

      if (instr.opcode == FetchOpcode::kTextureFetch) {
        // Will take sign values and exponent bias from the fetch constant.
        if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
          cbuffer_index_fetch_constants_ = cbuffer_count_++;
        }

        assert_true(signs_value_temp != UINT32_MAX);
        uint32_t signs_temp = PushSystemTemp();
        uint32_t signs_select_temp = PushSystemTemp();

        // Multiplex unsigned and signed SRVs, apply sign bias (2 * color - 1)
        // and linearize gamma textures. This is done before applying the
        // exponent bias because biasing and linearization must be done on color
        // values in 0...1 range, and this is closer to the storage format,
        // while exponent bias is closer to the actual usage in shaders.
        // Extract the sign values from dword 0 ([0].x or [1].z) of the fetch
        // constant, in bits 2:3, 4:5, 6:7 and 8:9.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(signs_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(2);
        shader_code_.push_back(2);
        shader_code_.push_back(2);
        shader_code_.push_back(2);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(2);
        shader_code_.push_back(4);
        shader_code_.push_back(6);
        shader_code_.push_back(8);
        shader_code_.push_back(EncodeVectorReplicatedOperand(
            D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (tfetch_index & 1) * 2, 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1));
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;

        // Replace the components fetched from the unsigned texture from those
        // fetched from the signed where needed (the signed values are already
        // loaded to signs_value_temp).
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(signs_select_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(uint32_t(TextureSign::kSigned));
        shader_code_.push_back(uint32_t(TextureSign::kSigned));
        shader_code_.push_back(uint32_t(TextureSign::kSigned));
        shader_code_.push_back(uint32_t(TextureSign::kSigned));
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_select_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_value_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;

        // Reusing signs_value_temp from now because the value from the signed
        // texture has already been copied.

        // Expand 0...1 to -1...1 (for normal and DuDv maps, for instance).
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(signs_value_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0x40000000u);
        shader_code_.push_back(0x40000000u);
        shader_code_.push_back(0x40000000u);
        shader_code_.push_back(0x40000000u);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0xBF800000u);
        shader_code_.push_back(0xBF800000u);
        shader_code_.push_back(0xBF800000u);
        shader_code_.push_back(0xBF800000u);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Change the color to the biased one where needed.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(signs_select_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(uint32_t(TextureSign::kUnsignedBiased));
        shader_code_.push_back(uint32_t(TextureSign::kUnsignedBiased));
        shader_code_.push_back(uint32_t(TextureSign::kUnsignedBiased));
        shader_code_.push_back(uint32_t(TextureSign::kUnsignedBiased));
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_select_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_value_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;

        // Linearize the texture if it's stored in a gamma format.
        for (uint32_t i = 0; i < 4; ++i) {
          // Calculate how far we are on each piece of the curve. Multiply by
          // 1/width of each piece, subtract start/width of it and saturate.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
              ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(signs_select_temp);
          shader_code_.push_back(
              EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          // 1.0 / 0.25
          shader_code_.push_back(0x40800000u);
          // 1.0 / 0.125
          shader_code_.push_back(0x41000000u);
          // 1.0 / 0.375
          shader_code_.push_back(0x402AAAABu);
          // 1.0 / 0.25
          shader_code_.push_back(0x40800000u);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          // -0.0 / 0.25
          shader_code_.push_back(0);
          // -0.25 / 0.125
          shader_code_.push_back(0xC0000000u);
          // -0.375 / 0.375
          shader_code_.push_back(0xBF800000u);
          // -0.75 / 0.25
          shader_code_.push_back(0xC0400000u);
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;
          // Combine the contribution of all pieces to the resulting linearized
          // value - multiply each piece by slope*width and sum them.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP4) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
          shader_code_.push_back(signs_value_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(signs_select_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          // 0.25 * 0.25
          shader_code_.push_back(0x3D800000u);
          // 0.5 * 0.125
          shader_code_.push_back(0x3D800000u);
          // 1.0 * 0.375
          shader_code_.push_back(0x3EC00000u);
          // 2.0 * 0.25
          shader_code_.push_back(0x3F000000u);
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;
        }
        // Change the color to the linearized one where needed.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(signs_select_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(uint32_t(TextureSign::kGamma));
        shader_code_.push_back(uint32_t(TextureSign::kGamma));
        shader_code_.push_back(uint32_t(TextureSign::kGamma));
        shader_code_.push_back(uint32_t(TextureSign::kGamma));
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_select_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(signs_value_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;

        // Release signs_temp and signs_select_temp.
        PopSystemTemp(2);

        // Apply exponent bias.
        uint32_t exp_adjust_temp = PushSystemTemp();
        // Get the bias value in bits 13:18 of dword 3, which is [0].w or [2].y.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(6);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(13);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      3 - 2 * (tfetch_index & 1), 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1) * 2);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
        // Shift it into float exponent bits.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(23);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        // Add this to the exponent of 1.0.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3F800000);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        // Multiply the value from the texture by 2.0^bias.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(exp_adjust_temp);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Release exp_adjust_temp.
        PopSystemTemp();
      }

      if (signs_value_temp != UINT32_MAX) {
        PopSystemTemp();
      }
      if (lod_temp != system_temp_grad_h_lod_) {
        PopSystemTemp();
      }
    }

    if (size_and_is_3d_temp != UINT32_MAX) {
      PopSystemTemp();
    }
    // Release coord_temp.
    PopSystemTemp();
  } else if (instr.opcode == FetchOpcode::kGetTextureGradients) {
    assert_true(IsDXBCPixelShader());
    store_result = true;
    // pv.xz = ddx(coord.xy)
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DERIV_RTX_COARSE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
    shader_code_.push_back(system_temp_pv_);
    UseDxbcSourceOperand(operand, 0b01010000);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // pv.yw = ddy(coord.xy)
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DERIV_RTY_COARSE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1010, 1));
    shader_code_.push_back(system_temp_pv_);
    UseDxbcSourceOperand(operand, 0b01010000);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // Get the exponent bias (horizontal in bits 22:26, vertical in bits 27:31
    // of dword 4 ([1].x or [2].z) of the fetch constant).
    if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
      cbuffer_index_fetch_constants_ = cbuffer_count_++;
    }
    uint32_t exp_bias_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(5);
    shader_code_.push_back(5);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(22);
    shader_code_.push_back(27);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (tfetch_index & 1) * 2, 3));
    shader_code_.push_back(cbuffer_index_fetch_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
    shader_code_.push_back(tfetch_pair_offset + 1 + (tfetch_index & 1));
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Shift the exponent bias into float exponent bits.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(23);
    shader_code_.push_back(23);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Add the bias to the exponent of 1.0.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Apply the exponent bias.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_pv_);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01000100, 1));
    shader_code_.push_back(exp_bias_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // Release exp_bias_temp.
    PopSystemTemp();
  } else if (instr.opcode == FetchOpcode::kSetTextureLod) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temp_grad_h_lod_);
    UseDxbcSourceOperand(operand, kSwizzleXYZW, 0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  } else if (instr.opcode == FetchOpcode::kSetTextureGradientsHorz ||
             instr.opcode == FetchOpcode::kSetTextureGradientsVert) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(instr.opcode == FetchOpcode::kSetTextureGradientsVert
                               ? system_temp_grad_v_
                               : system_temp_grad_h_lod_);
    UseDxbcSourceOperand(operand);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  if (instr.operand_count >= 1) {
    UnloadDxbcSourceOperand(operand);
  }

  // Re-enter conditional execution if closed it.
  if (suppress_predication) {
    // Re-enter exec-level predication.
    if (cf_exec_predicated_) {
      D3D10_SB_INSTRUCTION_TEST_BOOLEAN test =
          cf_exec_predicate_condition_ ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                                       : D3D10_SB_INSTRUCTION_TEST_ZERO;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, exec_p0_temp != UINT32_MAX ? 0 : 2, 1));
      shader_code_.push_back(
          exec_p0_temp != UINT32_MAX ? exec_p0_temp : system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;
      if (exec_p0_temp != UINT32_MAX) {
        PopSystemTemp();
      }
    }
    // Update instruction-level predication to the one needed by this tfetch.
    UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition,
                                 false);
  }

  if (store_result) {
    StoreResult(instr.result, system_temp_pv_, replicate_result);
  }
}

void DxbcShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    // Will be emitted by UpdateInstructionPredication.
  }
  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition,
                               true);
  // Whether the instruction has changed the predicate and it needs to be
  // checked again later.
  bool predicate_written = false;

  // Whether the result is only in X and all components should be remapped to X
  // while storing.
  bool replicate_result = false;

  DxbcSourceOperand dxbc_operands[3];
  uint32_t operand_length_sums[3];
  for (uint32_t i = 0; i < uint32_t(instr.operand_count); ++i) {
    LoadDxbcSourceOperand(instr.operands[i], dxbc_operands[i]);
    operand_length_sums[i] = DxbcSourceOperandLength(dxbc_operands[i]);
    if (i != 0) {
      operand_length_sums[i] += operand_length_sums[i - 1];
    }
  }

  // So the same code can be used for instructions with the same format.
  static const uint32_t kCoreOpcodes[] = {
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_MIN,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_FRC,
      D3D10_SB_OPCODE_ROUND_Z,
      D3D10_SB_OPCODE_ROUND_NI,
      D3D10_SB_OPCODE_MAD,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_DP4,
      D3D10_SB_OPCODE_DP3,
      D3D10_SB_OPCODE_DP2,
      0,
      0,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_NE,
      0,
      D3D10_SB_OPCODE_MAX,
  };

  switch (instr.vector_opcode) {
    case AluVectorOpcode::kAdd:
    case AluVectorOpcode::kMax:
    // max is commonly used as mov, but probably better not to convert it to
    // make sure things like flusing denormals aren't affected.
    case AluVectorOpcode::kMin:
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      UseDxbcSourceOperand(dxbc_operands[1]);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluVectorOpcode::kMul: {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      UseDxbcSourceOperand(dxbc_operands[1]);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0),
      // flushing denormals (must be done using eq - doing bitwise comparison
      // doesn't flush denormals).
      // With Shader Model 4 behavior, Halo 3 has a significant portion of the
      // image missing because rcp(0) is multiplied by 0, which results in NaN
      // rather than 0.
      uint32_t is_subnormal_temp = PushSystemTemp();
      // Check the first operand.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 8 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Check the second operand.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              8 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[1]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
    } break;

    case AluVectorOpcode::kSeq:
    case AluVectorOpcode::kSgt:
    case AluVectorOpcode::kSge:
    case AluVectorOpcode::kSne:
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      if (instr.vector_opcode == AluVectorOpcode::kSgt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[1]);
        UseDxbcSourceOperand(dxbc_operands[0]);
      } else {
        UseDxbcSourceOperand(dxbc_operands[0]);
        UseDxbcSourceOperand(dxbc_operands[1]);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Convert 0xFFFFFFFF to 1.0f.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      break;

    case AluVectorOpcode::kFrc:
    case AluVectorOpcode::kTrunc:
    case AluVectorOpcode::kFloor:
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluVectorOpcode::kMad: {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[2]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      UseDxbcSourceOperand(dxbc_operands[1]);
      UseDxbcSourceOperand(dxbc_operands[2]);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
      // If any operand is zero or denormalized, just leave the addition part.
      uint32_t is_subnormal_temp = PushSystemTemp();
      // Check the first operand.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 8 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              7 + DxbcSourceOperandLength(dxbc_operands[2])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[2]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Check the second operand.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              8 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[1]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              7 + DxbcSourceOperandLength(dxbc_operands[2])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[2]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
    } break;

    // Using true eq to compare with zero because it handles denormals and -0.
    case AluVectorOpcode::kCndEq:
    case AluVectorOpcode::kCndGe:
    case AluVectorOpcode::kCndGt:
      // dest = src0 op 0.0 ? src1 : src2
      // Compare src0 to zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 8 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      if (instr.vector_opcode != AluVectorOpcode::kCndGt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0]);
      }
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      if (instr.vector_opcode == AluVectorOpcode::kCndGt) {
        UseDxbcSourceOperand(dxbc_operands[0]);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Select src1 or src2.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              5 + operand_length_sums[2] - operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[1]);
      UseDxbcSourceOperand(dxbc_operands[2]);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      break;

    case AluVectorOpcode::kDp4:
    case AluVectorOpcode::kDp3:
    case AluVectorOpcode::kDp2Add: {
      uint32_t operand_mask;
      if (instr.vector_opcode == AluVectorOpcode::kDp2Add) {
        operand_mask = 0b0011;
      } else if (instr.vector_opcode == AluVectorOpcode::kDp3) {
        operand_mask = 0b0111;
      } else {
        operand_mask = 0b1111;
      }
      // Load the operands into pv and a temp register, zeroing if the other
      // operand is zero or denormalized, reproducing the Shader Model 3
      // multiplication behavior (0 * anything = 0).
      uint32_t src1_temp = PushSystemTemp();
      // Load the first operand into pv.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              8 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, operand_mask, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[1]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 10 + operand_length_sums[0]));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, operand_mask, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      UseDxbcSourceOperand(dxbc_operands[0]);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Load the second operand into src1_temp.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 8 + operand_length_sums[0]));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, operand_mask, 1));
      shader_code_.push_back(src1_temp);
      UseDxbcSourceOperand(dxbc_operands[0]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              10 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, operand_mask, 1));
      shader_code_.push_back(src1_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(src1_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      UseDxbcSourceOperand(dxbc_operands[1]);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Calculate the dot product.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(src1_temp);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Release src1_temp.
      PopSystemTemp();
      // Add src2.x for dp2add.
      if (instr.vector_opcode == AluVectorOpcode::kDp2Add) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                5 + DxbcSourceOperandLength(dxbc_operands[2])));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(system_temp_pv_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_pv_);
        UseDxbcSourceOperand(dxbc_operands[2], kSwizzleXXXX);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
      break;
    }

    case AluVectorOpcode::kCube: {
      // 3D cubemap direction -> (T, S, 2.0 * major axis, face ID).
      // src0 is the direction swizzled as .zzxy, src1 is the same direction as
      // .yxzz, but we don't need it.
      //
      // If the major axis is X (X >= Y && X >= Z):
      // * T is -Y.
      // * S is -Z for positive X, +Z for negative X.
      // * Face is 0 for positive X, 1 for negative X.
      // Otherwise, if the major axis is Y (Y >= Z):
      // * T is +Z for positive Y, -Z for negative Y.
      // * S is +X.
      // * Face is 2 for positive Y, 3 for negative Y.
      // Otherwise, if the major axis is Z:
      // * T is -Y.
      // * S is +X for positive Z, -X for negative Z.
      // * Face is 4 for positive Z, 5 for negative Z.

      // For making swizzle masks when using src0.
      const uint32_t cube_src0_x = 2;
      const uint32_t cube_src0_y = 3;
      const uint32_t cube_src0_z = 1;

      // Used for various masks, as 0xFFFFFFFF/0, 2.0/0.0.
      uint32_t cube_mask_temp = PushSystemTemp();

      // 1) Choose which axis is the major one - resulting in (0xFFFFFFFF, 0, 0)
      // for X major axis, (0, 0xFFFFFFFF, 0) for Y, (0, 0, 0xFFFFFFFF) for Z.

      // Mask = (X >= Y, Y >= Z, Z >= Z, X >= Z), let's hope nothing passes NaN
      // in Z.
      // ge mask, |src.xyzx|, |src.yzzz|
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              3 + 2 * DxbcSourceOperandLength(dxbc_operands[0], false, true)));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(cube_mask_temp);
      UseDxbcSourceOperand(dxbc_operands[0],
                           cube_src0_x | (cube_src0_y << 2) |
                               (cube_src0_z << 4) | (cube_src0_x << 6),
                           4, false, true);
      UseDxbcSourceOperand(dxbc_operands[0],
                           cube_src0_y | (cube_src0_z << 2) |
                               (cube_src0_z << 4) | (cube_src0_z << 6),
                           4, false, true);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Mask = (X >= Y && X >= Z, Y >= Z, Z >= Z, unused).
      // and mask.x, mask.x, mask.w
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(cube_mask_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // If X is MA, Y and Z can't be MA.
      // movc mask._yz_, mask._xx_, l(_, 0, 0, _), mask._yz_
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(cube_mask_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // If Y is MA, Z can't be MA.
      // movc mask.z, mask.y, l(0), mask.z
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(cube_mask_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // 2) Get T and S as if the major axis was positive (sign changing for
      // negative major axis will be done later).

      uint32_t minus_src0_length =
          DxbcSourceOperandLength(dxbc_operands[0], true);

      // T is +Z if Y is major, -Y otherwise.
      // movc pv.x, mask.y, src.z, -src.y
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              5 + operand_length_sums[0] + minus_src0_length));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(cube_mask_temp);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, cube_src0_z);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, cube_src0_y, true);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // S is -Z if X is major, +X otherwise.
      // movc pv.y, mask.x, -src.z, src.x
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + minus_src0_length +
                                                       operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(cube_mask_temp);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, cube_src0_z, true);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, cube_src0_x);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // 3) Get 2.0 * major axis.

      // Convert the mask to float and double it (because we need 2 * MA).
      // and mask.xyz_, mask.xyz_, l(0x40000000, 0x40000000, 0x40000000, _)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x40000000);
      shader_code_.push_back(0x40000000);
      shader_code_.push_back(0x40000000);
      shader_code_.push_back(0x40000000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Select 2.0 * needed component (mask always has 2.0 in one component and
      // 0.0 in the rest).
      // dp3 pv.__z_, src.xyz_, mask.xyz_
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP3) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 5 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0], cube_src0_x | (cube_src0_y << 2) |
                                                 (cube_src0_z << 4));
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(cube_mask_temp);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // 4) Check whether the major axis is negative and get the face index.

      // Test if the major axis is negative.
      // lt mask.w, pv.z, l(0.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LT) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Convert the negative mask to float the same way (multiplied by 2)
      // because it will be used in bitwise operations with other mask
      // components.
      // and mask.w, mask.w, l(0x40000000)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x40000000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Get the face index. If major axis is X, it's 0, if it's Y, it's 2, if
      // Z, it's 4, but also, being negative also adds 1 to the index. Since YZW
      // of the mask contain 2.0 for whether YZ are the major axis and the major
      // axis is negative, the factor is divided by 2.
      // dp3 pv.___w, mask.yzw_, l(1.0, 2.0, 0.5, _)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP3) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b11111001, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x40000000);
      shader_code_.push_back(0x3F000000);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // 5) Flip axes if the major axis is negative - if major axis is Y, flip
      // T, otherwise flip S.

      // S needs to flipped if the major axis is X or Z, so make an X || Z mask.
      // or mask.x, mask.x, mask.z
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(cube_mask_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Don't flip anything if the major axis is positive (AND 2.0 and 2.0 if
      // it's negative).
      // and mask.xy__, mask.xy__, mask.ww__
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(cube_mask_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Flip T or S.
      // movc pv.xy__, mask.yx__, -pv.xy__, pv.xy__
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b11100001, 1));
      shader_code_.push_back(cube_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
                                 D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
                             ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
      shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
          D3D10_SB_OPERAND_MODIFIER_NEG));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // 6) Move T and S to the proper coordinate system.

      // Subtract abs(2.0 * major axis) from T and S.
      // add pv.xy__, pv.xy__, -|pv.zz__|
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
      shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
          D3D10_SB_OPERAND_MODIFIER_ABSNEG));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Release cube_mask_temp.
      PopSystemTemp();
    } break;

    case AluVectorOpcode::kMax4:
      replicate_result = true;
      // pv.xy = max(src0.xy, src0.zw)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      UseDxbcSourceOperand(dxbc_operands[0], 0b01001110);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // pv.x = max(pv.x, pv.y)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluVectorOpcode::kSetpEqPush:
    case AluVectorOpcode::kSetpNePush:
    case AluVectorOpcode::kSetpGtPush:
    case AluVectorOpcode::kSetpGePush:
      predicate_written = true;
      replicate_result = true;
      // pv.xy = (src0.x == 0.0, src0.w == 0.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 8 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0], 0b11001100);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // pv.zw = (src1.x op 0.0, src1.w op 0.0)
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              8 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
      shader_code_.push_back(system_temp_pv_);
      if (instr.vector_opcode != AluVectorOpcode::kSetpGtPush) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[1], 0b11000000);
      }
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      if (instr.vector_opcode == AluVectorOpcode::kSetpGtPush) {
        UseDxbcSourceOperand(dxbc_operands[1], 0b11000000);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // p0 = src0.w == 0.0 && src1.w op 0.0
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // pv.x = src0.x == 0.0 && src1.x op 0.0
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // pv.x = (src0.x == 0.0 && src1.x op 0.0) ? -1.0 : src0.x
      // (1.0 is going to be added, thus -1.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 7 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xBF800000u);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // pv.x += 1.0
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluVectorOpcode::kKillEq:
    case AluVectorOpcode::kKillGt:
    case AluVectorOpcode::kKillGe:
    case AluVectorOpcode::kKillNe:
      replicate_result = true;
      // pv = src0 op src1
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 kCoreOpcodes[uint32_t(instr.vector_opcode)]) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      if (instr.vector_opcode == AluVectorOpcode::kKillGt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[1]);
        UseDxbcSourceOperand(dxbc_operands[0]);
      } else {
        UseDxbcSourceOperand(dxbc_operands[0]);
        UseDxbcSourceOperand(dxbc_operands[1]);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // pv = any(src0 op src1)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Convert 0xFFFFFFFF to 1.0f.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Discard.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DISCARD) |
          ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
              D3D10_SB_INSTRUCTION_TEST_NONZERO) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      break;

    case AluVectorOpcode::kDst: {
      // Not shortening so there are no write-read dependencies and less scalar
      // operations.
      // pv.x = 1.0
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // pv.y = src0.y * src1.y
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
      // This is an attenuation calculation function, so infinity is probably
      // not very unlikely.
      uint32_t is_subnormal_temp = PushSystemTemp();
      // Check if src0.y is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 5 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0]);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if src1.y is zero.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              5 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[1]);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(is_subnormal_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Do the multiplication.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set pv.y to zero if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
      // pv.z = src0.z
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 2);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // pv.w = src1.w
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              3 + DxbcSourceOperandLength(dxbc_operands[1])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 3);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
    } break;

    case AluVectorOpcode::kMaxA:
      // The `a0 = int(clamp(floor(src0.w + 0.5), -256.0, 255.0))` part.
      //
      // Using specifically floor(src0.w + 0.5) rather than round(src0.w)
      // because the R600 ISA reference and MSDN say so - this makes a
      // difference at 0.5 because round_ni rounds to the nearest even.
      // There's one deviation from the R600 specification though - the value is
      // clamped to 255 rather than set to -256 if it's over 255. We don't know
      // yet which is the correct - the mova_int description, for example, says
      // "clamp" explicitly. MSDN, however, says the value should actually be
      // clamped.
      // http://web.archive.org/web/20100705151335/http://msdn.microsoft.com:80/en-us/library/bb313931.aspx
      //
      // pv.x (temporary) = src0.w + 0.5
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 5 + operand_length_sums[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 3);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F000000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // pv.x = floor(src0.w + 0.5)
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // pv.x = max(floor(src0.w + 0.5), -256.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xC3800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // pv.x = clamp(floor(src0.w + 0.5), -256.0, 255.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x437F0000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // a0 = int(clamp(floor(src0.w + 0.5), -256.0, 255.0))
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
      // The `pv = max(src0, src1)` part.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      UseDxbcSourceOperand(dxbc_operands[1]);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    default:
      assert_always();
      // Unknown instruction - don't modify pv.
      break;
  }

  for (uint32_t i = 0; i < uint32_t(instr.operand_count); ++i) {
    UnloadDxbcSourceOperand(dxbc_operands[instr.operand_count - 1 - i]);
  }

  StoreResult(instr.result, system_temp_pv_, replicate_result);

  if (predicate_written) {
    cf_exec_predicate_written_ = true;
    CloseInstructionPredication();
  }
}

void DxbcShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    // Will be emitted by UpdateInstructionPredication.
  }
  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition,
                               true);
  // Whether the instruction has changed the predicate and it needs to be
  // checked again later.
  bool predicate_written = false;

  DxbcSourceOperand dxbc_operands[3];
  uint32_t operand_lengths[3];
  for (uint32_t i = 0; i < uint32_t(instr.operand_count); ++i) {
    LoadDxbcSourceOperand(instr.operands[i], dxbc_operands[i]);
    operand_lengths[i] = DxbcSourceOperandLength(dxbc_operands[i]);
  }

  // So the same code can be used for instructions with the same format.
  static const uint32_t kCoreOpcodes[] = {
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_MIN,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_FRC,
      D3D10_SB_OPCODE_ROUND_Z,
      D3D10_SB_OPCODE_ROUND_NI,
      D3D10_SB_OPCODE_EXP,
      D3D10_SB_OPCODE_LOG,
      D3D10_SB_OPCODE_LOG,
      D3D11_SB_OPCODE_RCP,
      D3D11_SB_OPCODE_RCP,
      D3D11_SB_OPCODE_RCP,
      D3D10_SB_OPCODE_RSQ,
      D3D10_SB_OPCODE_RSQ,
      D3D10_SB_OPCODE_RSQ,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      0,
      0,
      0,
      0,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_SQRT,
      0,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_SINCOS,
      D3D10_SB_OPCODE_SINCOS,
  };

  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds:
    case AluScalarOpcode::kMaxs:
    case AluScalarOpcode::kMins:
    case AluScalarOpcode::kSubs: {
      bool subtract = instr.scalar_opcode == AluScalarOpcode::kSubs;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              3 + operand_lengths[0] +
              DxbcSourceOperandLength(dxbc_operands[0], subtract)));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1, subtract);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } break;

    case AluScalarOpcode::kAddsPrev:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kMuls: {
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if the operands are zero or denormalized.
      uint32_t is_subnormal_temp = PushSystemTemp();
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(is_subnormal_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Set the result to zero if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMulsPrev: {
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
      uint32_t is_subnormal_temp = PushSystemTemp();
      // Check if the first operand (src0.x) is zero.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if the second operand (ps) is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(is_subnormal_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Do the multiplication.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set the result to zero if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMulsPrev2: {
      // Implemented like MUL_LIT in the R600 ISA documentation, where src0 is
      // src0.x, src1 is ps, and src2 is src0.y.
      // Check if -FLT_MAX needs to be written - if any of the following
      // checks pass.
      uint32_t minus_max_mask = PushSystemTemp();
      // ps == -FLT_MAX || ps == -Infinity (as ps <= -FLT_MAX)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // isnan(ps)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // src0.y <= 0.0
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // isnan(src0.y)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(minus_max_mask);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // minus_max_mask = any(minus_max_mask)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
      shader_code_.push_back(minus_max_mask);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(minus_max_mask);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Calculate the product for the regular path of the instruction.
      // ps = src0.x * ps
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Write -FLT_MAX if needed.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release minus_max_mask.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kSeqs:
    case AluScalarOpcode::kSgts:
    case AluScalarOpcode::kSges:
    case AluScalarOpcode::kSnes:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kSgts) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      if (instr.scalar_opcode == AluScalarOpcode::kSgts) {
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Convert 0xFFFFFFFF to 1.0f.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      break;

    case AluScalarOpcode::kFrcs:
    case AluScalarOpcode::kTruncs:
    case AluScalarOpcode::kFloors:
    case AluScalarOpcode::kExp:
    case AluScalarOpcode::kLog:
    case AluScalarOpcode::kRcp:
    case AluScalarOpcode::kRsq:
    case AluScalarOpcode::kSqrt:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kLogc:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LOG) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp -Infinity to -FLT_MAX.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kRcpc:
    case AluScalarOpcode::kRsqc:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp -Infinity to -FLT_MAX.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp +Infinity to +FLT_MAX.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x7F7FFFFFu);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kRcpf:
    case AluScalarOpcode::kRsqf: {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Change Infinity to positive or negative zero (the sign of zero has
      // effect on some instructions, such as rcp itself).
      uint32_t isinf_and_sign = PushSystemTemp();
      // Separate the value into the magnitude and the sign bit.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x7FFFFFFFu);
      shader_code_.push_back(0x80000000u);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Check if the magnitude is infinite.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x7F800000u);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
      // Zero ps if the magnitude is infinite (the signed zero is already in Y
      // of isinf_and_sign).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release isinf_and_sign.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMaxAs:
    case AluScalarOpcode::kMaxAsf:
      // The `a0 = int(clamp(round(src0.x), -256.0, 255.0))` part.
      //
      // See AluVectorOpcode::kMaxA handling for details regarding rounding and
      // clamping.
      //
      // a0 = round(src0.x) (towards the nearest integer via floor(src0.x + 0.5)
      // for maxas and towards -Infinity for maxasf).
      if (instr.scalar_opcode == AluScalarOpcode::kMaxAs) {
        // a0 = src0.x + 0.5
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3F000000u);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // a0 = floor(src0.x + 0.5)
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      } else {
        // a0 = floor(src0.x)
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
      // a0 = max(round(src0.x), -256.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xC3800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // a0 = clamp(round(src0.x), -256.0, 255.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x437F0000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // a0 = int(clamp(floor(src0.x + 0.5), -256.0, 255.0))
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
      // The `ps = max(src0.x, src0.y)` part.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kSubsPrev:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
              D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(0) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
      shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
          D3D10_SB_OPERAND_MODIFIER_NEG));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kSetpEq:
    case AluScalarOpcode::kSetpNe:
    case AluScalarOpcode::kSetpGt:
    case AluScalarOpcode::kSetpGe:
      predicate_written = true;
      // Set p0 to whether the comparison with zero passes.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kSetpGt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      if (instr.scalar_opcode == AluScalarOpcode::kSetpGt) {
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set ps to 0.0 if the comparison passes or to 1.0 if it fails.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      break;

    case AluScalarOpcode::kSetpInv:
      predicate_written = true;
      // Compare src0 to 0.0 (taking denormals into account, for instance) to
      // know what to set ps to in case src0 is not 1.0.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Assuming src0 is not 1.0 (this case will be handled later), set ps to
      // src0, except when it's zero - in this case, set ps to 1.0.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Set p0 to whether src0 is 1.0.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // If src0 is 1.0, set ps to zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      break;

    case AluScalarOpcode::kSetpPop:
      predicate_written = true;
      // ps = src0 - 1.0
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xBF800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set p0 to whether (src0 - 1.0) is 0.0 or smaller.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // If (src0 - 1.0) is 0.0 or smaller, set ps to 0.0 (already has
      // (src0 - 1.0), so clamping to zero is enough).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kSetpClr:
      predicate_written = true;
      // ps = FLT_MAX
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x7F7FFFFF);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // p0 = false
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      break;

    case AluScalarOpcode::kSetpRstr:
      predicate_written = true;
      // Copy src0 to ps.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // Set p0 to whether src0 is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kKillsEq:
    case AluScalarOpcode::kKillsGt:
    case AluScalarOpcode::kKillsGe:
    case AluScalarOpcode::kKillsNe:
    case AluScalarOpcode::kKillsOne:
      // ps = src0.x op 0.0 (or src0.x == 1.0 for kills_one)
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kKillsGt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(
          instr.scalar_opcode == AluScalarOpcode::kKillsOne ? 0x3F800000 : 0);
      if (instr.scalar_opcode == AluScalarOpcode::kKillsGt) {
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Convert 0xFFFFFFFF to 1.0f.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Discard.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DISCARD) |
          ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
              D3D10_SB_INSTRUCTION_TEST_NONZERO) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      break;

    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1: {
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
      uint32_t is_subnormal_temp = PushSystemTemp();
      // Check if the first operand (src0.x) is zero.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if the second operand (src1.x) is zero.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(is_subnormal_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Do the multiplication.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_lengths[0] + operand_lengths[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set the result to zero if any operand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1:
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1: {
      bool subtract = instr.scalar_opcode == AluScalarOpcode::kSubsc0 ||
                      instr.scalar_opcode == AluScalarOpcode::kSubsc1;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              3 + operand_lengths[0] +
              DxbcSourceOperandLength(dxbc_operands[1], subtract)));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 0, subtract);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } break;

    case AluScalarOpcode::kSin:
    case AluScalarOpcode::kCos: {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SINCOS) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4 + operand_lengths[0]));
      // sincos ps, null, src0.x for sin
      // sincos null, ps, src0.x for cos
      const uint32_t null_operand_token =
          ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_0_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_NULL) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);
      if (instr.scalar_opcode != AluScalarOpcode::kSin) {
        shader_code_.push_back(null_operand_token);
      }
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kCos) {
        shader_code_.push_back(null_operand_token);
      }
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } break;

    default:
      // May be retain_prev, in this case the current ps should be written, or
      // something invalid that's better to ignore.
      assert_true(instr.scalar_opcode == AluScalarOpcode::kRetainPrev);
      break;
  }

  for (uint32_t i = 0; i < uint32_t(instr.operand_count); ++i) {
    UnloadDxbcSourceOperand(dxbc_operands[instr.operand_count - 1 - i]);
  }

  StoreResult(instr.result, system_temp_ps_pc_p0_a0_, true);

  if (predicate_written) {
    cf_exec_predicate_written_ = true;
    CloseInstructionPredication();
  }
}

void DxbcShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  switch (instr.type) {
    case ParsedAluInstruction::Type::kNop:
      break;
    case ParsedAluInstruction::Type::kVector:
      ProcessVectorAluInstruction(instr);
      break;
    case ParsedAluInstruction::Type::kScalar:
      ProcessScalarAluInstruction(instr);
      break;
  }
}

uint32_t DxbcShaderTranslator::AppendString(std::vector<uint32_t>& dest,
                                            const char* source) {
  size_t size = std::strlen(source) + 1;
  size_t size_aligned = xe::align(size, sizeof(uint32_t));
  size_t dest_position = dest.size();
  dest.resize(dest_position + size_aligned / sizeof(uint32_t));
  std::memcpy(&dest[dest_position], source, size);
  // Don't leave uninitialized data, and make sure multiple invocations of the
  // translator for the same Xenos shader give the same DXBC.
  std::memset(reinterpret_cast<uint8_t*>(&dest[dest_position]) + size, 0xAB,
              size_aligned - size);
  return uint32_t(size_aligned);
}

const DxbcShaderTranslator::RdefType DxbcShaderTranslator::rdef_types_[size_t(
    DxbcShaderTranslator::RdefTypeIndex::kCount)] = {
    {"float", 0, 3, 1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"float2", 1, 3, 1, 2, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"float3", 1, 3, 1, 3, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"float4", 1, 3, 1, 4, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"int", 0, 2, 1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"uint", 0, 19, 1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"uint2", 1, 19, 1, 2, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"uint4", 1, 19, 1, 4, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // Float constants - size written dynamically.
    {nullptr, 1, 3, 1, 4, 0, 0, RdefTypeIndex::kFloat4, nullptr},
    {nullptr, 1, 19, 1, 4, 8, 0, RdefTypeIndex::kUint4, nullptr},
    {nullptr, 1, 19, 1, 4, 32, 0, RdefTypeIndex::kUint4, nullptr},
    {nullptr, 1, 19, 1, 4, 48, 0, RdefTypeIndex::kUint4, nullptr},
};

const DxbcShaderTranslator::SystemConstantRdef DxbcShaderTranslator::
    system_constant_rdef_[DxbcShaderTranslator::kSysConst_Count] = {
        // vec4 0
        {"xe_flags", RdefTypeIndex::kUint, 0, 4},
        {"xe_vertex_index_endian", RdefTypeIndex::kUint, 4, 4},
        {"xe_vertex_base_index", RdefTypeIndex::kUint, 8, 4},
        {"xe_pixel_pos_reg", RdefTypeIndex::kUint, 12, 4},
        // vec4 1
        {"xe_ndc_scale", RdefTypeIndex::kFloat3, 16, 12},
        {"xe_pixel_half_pixel_offset", RdefTypeIndex::kFloat, 28, 4},
        // vec4 2
        {"xe_ndc_offset", RdefTypeIndex::kFloat3, 32, 12},
        {"xe_alpha_test", RdefTypeIndex::kInt, 44, 4},
        // vec4 3
        {"xe_point_size", RdefTypeIndex::kFloat2, 48, 8},
        {"xe_point_size_min_max", RdefTypeIndex::kFloat2, 56, 8},
        // vec4 4
        {"xe_point_screen_to_ndc", RdefTypeIndex::kFloat2, 64, 8},
        {"xe_sample_count_log2", RdefTypeIndex::kUint2, 72, 8},
        // vec4 5
        {"xe_alpha_test_range", RdefTypeIndex::kFloat2, 80, 8},
        {"xe_edram_pitch_tiles", RdefTypeIndex::kUint, 88, 4},
        {"xe_edram_depth_base_dwords", RdefTypeIndex::kUint, 92, 4},
        // vec4 6
        {"xe_color_exp_bias", RdefTypeIndex::kFloat4, 96, 16},
        // vec4 7
        {"xe_color_output_map", RdefTypeIndex::kUint4, 112, 16},
        // vec4 8
        {"xe_edram_depth_range", RdefTypeIndex::kFloat2, 128, 8},
        {"xe_edram_poly_offset_front", RdefTypeIndex::kFloat2, 136, 8},
        // vec4 9
        {"xe_edram_poly_offset_back", RdefTypeIndex::kFloat2, 144, 8},
        // vec4 10
        {"xe_edram_stencil_reference", RdefTypeIndex::kUint, 160, 4},
        {"xe_edram_stencil_read_mask", RdefTypeIndex::kUint, 164, 4},
        {"xe_edram_stencil_write_mask", RdefTypeIndex::kUint, 168, 4},
        // vec4 11
        {"xe_edram_stencil_front", RdefTypeIndex::kUint4, 176, 16},
        // vec4 12
        {"xe_edram_stencil_back", RdefTypeIndex::kUint4, 192, 16},
        // vec4 13
        {"xe_edram_base_dwords", RdefTypeIndex::kUint4, 208, 16},
        // vec4 14
        {"xe_edram_rt_flags", RdefTypeIndex::kUint4, 224, 16},
        // vec4 15
        {"xe_edram_rt_pack_width_low", RdefTypeIndex::kUint4, 240, 16},
        // vec4 16
        {"xe_edram_rt_pack_offset_low", RdefTypeIndex::kUint4, 256, 16},
        // vec4 17
        {"xe_edram_rt_pack_width_high", RdefTypeIndex::kUint4, 272, 16},
        // vec4 18
        {"xe_edram_rt_pack_offset_high", RdefTypeIndex::kUint4, 288, 16},
        // vec4 19
        {"xe_edram_load_mask_low_rt01", RdefTypeIndex::kUint4, 304, 16},
        // vec4 20
        {"xe_edram_load_mask_low_rt23", RdefTypeIndex::kUint4, 320, 16},
        // vec4 21
        {"xe_edram_load_scale_rt01", RdefTypeIndex::kFloat4, 336, 16},
        // vec4 22
        {"xe_edram_load_scale_rt23", RdefTypeIndex::kFloat4, 352, 16},
        // vec4 23
        {"xe_edram_blend_rt01", RdefTypeIndex::kUint4, 368, 16},
        // vec4 24
        {"xe_edram_blend_rt23", RdefTypeIndex::kUint4, 384, 16},
        // vec4 25
        {"xe_edram_blend_constant", RdefTypeIndex::kFloat4, 400, 16},
        // vec4 26
        {"xe_edram_store_min_rt01", RdefTypeIndex::kFloat4, 416, 16},
        // vec4 27
        {"xe_edram_store_min_rt23", RdefTypeIndex::kFloat4, 432, 16},
        // vec4 28
        {"xe_edram_store_max_rt01", RdefTypeIndex::kFloat4, 448, 16},
        // vec4 29
        {"xe_edram_store_max_rt23", RdefTypeIndex::kFloat4, 464, 16},
        // vec4 30
        {"xe_edram_store_scale_rt01", RdefTypeIndex::kFloat4, 480, 16},
        // vec4 31
        {"xe_edram_store_scale_rt23", RdefTypeIndex::kFloat4, 496, 16},
};

void DxbcShaderTranslator::WriteResourceDefinitions() {
  // ***************************************************************************
  // Preparation
  // ***************************************************************************

  // Float constant count.
  uint32_t float_constant_count = 0;
  if (cbuffer_index_float_constants_ != kCbufferIndexUnallocated) {
    for (uint32_t i = 0; i < 4; ++i) {
      float_constant_count +=
          xe::bit_count(constant_register_map().float_bitmap[i]);
    }
  }

  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  // ***************************************************************************
  // Header
  // ***************************************************************************

  // Constant buffer count.
  shader_object_.push_back(cbuffer_count_);
  // Constant buffer offset (set later).
  shader_object_.push_back(0);
  // Bound resource count (samplers, SRV, UAV, CBV).
  uint32_t resource_count = cbuffer_count_;
  if (!is_depth_only_pixel_shader_) {
    // + 1 for shared memory (vfetches can probably appear in pixel shaders too,
    // they are handled safely there anyway).
    resource_count +=
        uint32_t(sampler_bindings_.size()) + 1 + uint32_t(texture_srvs_.size());
  }
  if (IsDXBCPixelShader() && edram_rov_used_) {
    // EDRAM.
    ++resource_count;
  }
  shader_object_.push_back(resource_count);
  // Bound resource buffer offset (set later).
  shader_object_.push_back(0);
  if (IsDXBCVertexShader()) {
    // vs_5_1
    shader_object_.push_back(0xFFFE0501u);
  } else {
    assert_true(IsDXBCPixelShader());
    // ps_5_1
    shader_object_.push_back(0xFFFF0501u);
  }
  // Compiler flags - default for SM 5.1 (no preshader, prefer flow control),
  // and also skip optimization and IEEE strictness.
  shader_object_.push_back(0x2504);
  // Generator offset (directly after the RDEF header in our case).
  shader_object_.push_back(60);
  // RD11, but with nibbles inverted (unlike in SM 5.0).
  shader_object_.push_back(0x25441313);
  // Unknown fields.
  shader_object_.push_back(60);
  shader_object_.push_back(24);
  // Was 32 in SM 5.0.
  shader_object_.push_back(40);
  shader_object_.push_back(40);
  shader_object_.push_back(36);
  shader_object_.push_back(12);
  shader_object_.push_back(0);
  // Generator name.
  AppendString(shader_object_, "Xenia");

  // ***************************************************************************
  // Constant types
  // ***************************************************************************

  // Type names.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t type_name_offsets[size_t(RdefTypeIndex::kCount)];
  for (uint32_t i = 0; i < uint32_t(RdefTypeIndex::kCount); ++i) {
    const RdefType& type = rdef_types_[i];
    if (type.name == nullptr) {
      // Array - use the name of the element type.
      type_name_offsets[i] =
          type_name_offsets[uint32_t(type.array_element_type)];
      continue;
    }
    type_name_offsets[i] = new_offset;
    new_offset += AppendString(shader_object_, type.name);
  }
  // Types.
  uint32_t types_position_dwords = uint32_t(shader_object_.size());
  const uint32_t type_size_dwords = 9;
  uint32_t types_offset =
      (types_position_dwords - chunk_position_dwords) * sizeof(uint32_t);
  const uint32_t type_size = type_size_dwords * sizeof(uint32_t);
  for (uint32_t i = 0; i < uint32_t(RdefTypeIndex::kCount); ++i) {
    const RdefType& type = rdef_types_[i];
    shader_object_.push_back(type.type_class | (type.type << 16));
    shader_object_.push_back(type.row_count | (type.column_count << 16));
    if (RdefTypeIndex(i) == RdefTypeIndex::kFloat4ConstantArray) {
      // Declaring a 0-sized array may not be safe, so write something valid
      // even if they aren't used.
      shader_object_.push_back(std::max(float_constant_count, 1u));
    } else {
      shader_object_.push_back(type.element_count |
                               (type.struct_member_count << 16));
    }
    // Struct member offset (set later).
    shader_object_.push_back(0);
    // Unknown.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(type_name_offsets[i]);
  }

#if 0
  // Structure members. Structures are not used currently, but were used in the
  // past, so the code is kept here.
  for (uint32_t i = 0; i < uint32_t(RdefTypeIndex::kCount); ++i) {
    const RdefType& type = rdef_types_[i];
    const RdefStructMember* struct_members = type.struct_members;
    if (struct_members == nullptr) {
      continue;
    }
    uint32_t struct_member_position_dwords = uint32_t(shader_object_.size());
    shader_object_[types_position_dwords + i * type_size_dwords + 3] =
        (struct_member_position_dwords - chunk_position_dwords) *
        sizeof(uint32_t);
    uint32_t struct_member_count = type.struct_member_count;
    // Reserve space for names and write types and offsets.
    for (uint32_t j = 0; j < struct_member_count; ++j) {
      shader_object_.push_back(0);
      shader_object_.push_back(types_offset +
                               uint32_t(struct_members[j].type) * type_size);
      shader_object_.push_back(struct_members[j].offset);
    }
    // Write member names.
    new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                 sizeof(uint32_t);
    for (uint32_t j = 0; j < struct_member_count; ++j) {
      shader_object_[struct_member_position_dwords + j * 3] = new_offset;
      new_offset += AppendString(shader_object_, struct_members[j].name);
    }
  }
#endif

  // ***************************************************************************
  // Constants
  // ***************************************************************************

  // Names.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t constant_name_offsets_system[kSysConst_Count];
  if (cbuffer_index_system_constants_ != kCbufferIndexUnallocated) {
    for (uint32_t i = 0; i < kSysConst_Count; ++i) {
      constant_name_offsets_system[i] = new_offset;
      new_offset += AppendString(shader_object_, system_constant_rdef_[i].name);
    }
  }
  uint32_t constant_name_offset_float = new_offset;
  if (cbuffer_index_float_constants_ != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_float_constants");
  }
  uint32_t constant_name_offset_bool = new_offset;
  uint32_t constant_name_offset_loop = constant_name_offset_bool;
  if (cbuffer_index_bool_loop_constants_ != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_bool_constants");
    constant_name_offset_loop = new_offset;
    new_offset += AppendString(shader_object_, "xe_loop_constants");
  }
  uint32_t constant_name_offset_fetch = new_offset;
  if (constant_name_offset_fetch != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_fetch_constants");
  }

  const uint32_t constant_size = 10 * sizeof(uint32_t);

  // System constants.
  uint32_t constant_offset_system = new_offset;
  if (cbuffer_index_system_constants_ != kCbufferIndexUnallocated) {
    for (uint32_t i = 0; i < kSysConst_Count; ++i) {
      const SystemConstantRdef& constant = system_constant_rdef_[i];
      shader_object_.push_back(constant_name_offsets_system[i]);
      shader_object_.push_back(constant.offset);
      shader_object_.push_back(constant.size);
      // Flag 0x2 is D3D_SVF_USED.
      shader_object_.push_back((system_constants_used_ & (1ull << i)) ? 0x2
                                                                      : 0);
      shader_object_.push_back(types_offset +
                               uint32_t(constant.type) * type_size);
      // Default value (always 0).
      shader_object_.push_back(0);
      // Unknown.
      shader_object_.push_back(0xFFFFFFFFu);
      shader_object_.push_back(0);
      shader_object_.push_back(0xFFFFFFFFu);
      shader_object_.push_back(0);
      new_offset += constant_size;
    }
  }

  // Float constants.
  uint32_t constant_offset_float = new_offset;
  if (cbuffer_index_float_constants_ != kCbufferIndexUnallocated) {
    shader_object_.push_back(constant_name_offset_float);
    shader_object_.push_back(0);
    shader_object_.push_back(std::max(float_constant_count, 1u) * 4 *
                             sizeof(float));
    shader_object_.push_back(0x2);
    shader_object_.push_back(types_offset +
                             uint32_t(RdefTypeIndex::kFloat4ConstantArray) *
                                 type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // Bool and loop constants.
  uint32_t constant_offset_bool_loop = new_offset;
  if (cbuffer_index_bool_loop_constants_ != kCbufferIndexUnallocated) {
    shader_object_.push_back(constant_name_offset_bool);
    shader_object_.push_back(0);
    shader_object_.push_back(8 * 4 * sizeof(uint32_t));
    shader_object_.push_back(0x2);
    shader_object_.push_back(types_offset +
                             uint32_t(RdefTypeIndex::kUint4Array8) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
    shader_object_.push_back(constant_name_offset_loop);
    shader_object_.push_back(8 * 4 * sizeof(uint32_t));
    shader_object_.push_back(32 * 4 * sizeof(uint32_t));
    shader_object_.push_back(0x2);
    shader_object_.push_back(
        types_offset + uint32_t(RdefTypeIndex::kUint4Array32) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // Fetch constants.
  uint32_t constant_offset_fetch = new_offset;
  if (cbuffer_index_fetch_constants_ != kCbufferIndexUnallocated) {
    shader_object_.push_back(constant_name_offset_fetch);
    shader_object_.push_back(0);
    shader_object_.push_back(32 * 6 * sizeof(uint32_t));
    shader_object_.push_back(0x2);
    shader_object_.push_back(
        types_offset + uint32_t(RdefTypeIndex::kUint4Array48) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // ***************************************************************************
  // Constant buffers
  // ***************************************************************************

  // Write the names.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t cbuffer_name_offset_system = new_offset;
  if (cbuffer_index_system_constants_ != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_system_cbuffer");
  }
  uint32_t cbuffer_name_offset_float = new_offset;
  if (cbuffer_index_float_constants_ != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_float_cbuffer");
  }
  uint32_t cbuffer_name_offset_bool_loop = new_offset;
  if (cbuffer_index_bool_loop_constants_ != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_bool_loop_cbuffer");
  }
  uint32_t cbuffer_name_offset_fetch = new_offset;
  if (cbuffer_index_fetch_constants_ != kCbufferIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_fetch_cbuffer");
  }

  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 1] = new_offset;

  // Write all the constant buffers, sorted by their binding index.
  for (uint32_t i = 0; i < cbuffer_count_; ++i) {
    if (i == cbuffer_index_system_constants_) {
      shader_object_.push_back(cbuffer_name_offset_system);
      shader_object_.push_back(kSysConst_Count);
      shader_object_.push_back(constant_offset_system);
      shader_object_.push_back(
          uint32_t(xe::align(sizeof(SystemConstants), 4 * sizeof(uint32_t))));
      // D3D_CT_CBUFFER.
      shader_object_.push_back(0);
      // No D3D_SHADER_CBUFFER_FLAGS.
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_float_constants_) {
      shader_object_.push_back(cbuffer_name_offset_float);
      shader_object_.push_back(1);
      shader_object_.push_back(constant_offset_float);
      shader_object_.push_back(std::max(float_constant_count, 1u) * 4 *
                               sizeof(float));
      shader_object_.push_back(0);
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_bool_loop_constants_) {
      shader_object_.push_back(cbuffer_name_offset_bool_loop);
      // Bool constants and loop constants are separate for easier debugging.
      shader_object_.push_back(2);
      shader_object_.push_back(constant_offset_bool_loop);
      shader_object_.push_back((8 + 32) * 4 * sizeof(uint32_t));
      shader_object_.push_back(0);
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_fetch_constants_) {
      shader_object_.push_back(cbuffer_name_offset_fetch);
      shader_object_.push_back(1);
      shader_object_.push_back(constant_offset_fetch);
      shader_object_.push_back(32 * 6 * sizeof(uint32_t));
      shader_object_.push_back(0);
      shader_object_.push_back(0);
    }
  }

  // ***************************************************************************
  // Bindings, in s#, t#, u#, cb# order
  // ***************************************************************************

  // Write used resource names, except for constant buffers because we have
  // their names already.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t sampler_name_offset = 0;
  uint32_t shared_memory_name_offset = 0;
  uint32_t texture_name_offset = 0;
  if (!is_depth_only_pixel_shader_) {
    sampler_name_offset = new_offset;
    for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
      new_offset +=
          AppendString(shader_object_, sampler_bindings_[i].name.c_str());
    }
    shared_memory_name_offset = new_offset;
    new_offset += AppendString(shader_object_, "xe_shared_memory");
    texture_name_offset = new_offset;
    for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
      new_offset += AppendString(shader_object_, texture_srvs_[i].name.c_str());
    }
  }
  uint32_t edram_name_offset = new_offset;
  if (IsDXBCPixelShader() && edram_rov_used_) {
    new_offset += AppendString(shader_object_, "xe_edram");
  }

  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 3] = new_offset;

  if (!is_depth_only_pixel_shader_) {
    // Samplers.
    for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
      const SamplerBinding& sampler_binding = sampler_bindings_[i];
      shader_object_.push_back(sampler_name_offset);
      // D3D_SIT_SAMPLER.
      shader_object_.push_back(3);
      // No D3D_RESOURCE_RETURN_TYPE.
      shader_object_.push_back(0);
      // D3D_SRV_DIMENSION_UNKNOWN (not an SRV).
      shader_object_.push_back(0);
      // Multisampling not applicable.
      shader_object_.push_back(0);
      // Register s[i].
      shader_object_.push_back(i);
      // One binding.
      shader_object_.push_back(1);
      // No D3D_SHADER_INPUT_FLAGS.
      shader_object_.push_back(0);
      // Register space 0.
      shader_object_.push_back(0);
      // Sampler ID S[i].
      shader_object_.push_back(i);
      sampler_name_offset += GetStringLength(sampler_binding.name.c_str());
    }

    // Shared memory.
    shader_object_.push_back(shared_memory_name_offset);
    // D3D_SIT_BYTEADDRESS.
    shader_object_.push_back(7);
    // D3D_RETURN_TYPE_MIXED.
    shader_object_.push_back(6);
    // D3D_SRV_DIMENSION_BUFFER.
    shader_object_.push_back(1);
    // Multisampling not applicable.
    shader_object_.push_back(0);
    // Register t0.
    shader_object_.push_back(0);
    // One binding.
    shader_object_.push_back(1);
    // No D3D_SHADER_INPUT_FLAGS.
    shader_object_.push_back(0);
    // Register space 0.
    shader_object_.push_back(0);
    // SRV ID T0.
    shader_object_.push_back(0);

    for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
      const TextureSRV& texture_srv = texture_srvs_[i];
      shader_object_.push_back(texture_name_offset);
      // D3D_SIT_TEXTURE.
      shader_object_.push_back(2);
      // D3D_RETURN_TYPE_FLOAT.
      shader_object_.push_back(5);
      switch (texture_srv.dimension) {
        case TextureDimension::k3D:
          // D3D_SRV_DIMENSION_TEXTURE3D.
          shader_object_.push_back(8);
          break;
        case TextureDimension::kCube:
          // D3D_SRV_DIMENSION_TEXTURECUBE.
          shader_object_.push_back(9);
          break;
        default:
          // D3D_SRV_DIMENSION_TEXTURE2DARRAY.
          shader_object_.push_back(5);
      }
      // Not multisampled.
      shader_object_.push_back(0xFFFFFFFFu);
      // Register t[1 + i] - t0 is shared memory.
      shader_object_.push_back(1 + i);
      // One binding.
      shader_object_.push_back(1);
      // D3D_SIF_TEXTURE_COMPONENTS (4-component).
      shader_object_.push_back(0xC);
      // Register space 0.
      shader_object_.push_back(0);
      // SRV ID T[1 + i] - T0 is shared memory.
      shader_object_.push_back(1 + i);
      texture_name_offset += GetStringLength(texture_srv.name.c_str());
    }
  }

  if (IsDXBCPixelShader() && edram_rov_used_) {
    // EDRAM uint32 buffer.
    shader_object_.push_back(edram_name_offset);
    // D3D_SIT_UAV_RWTYPED.
    shader_object_.push_back(4);
    // D3D_RETURN_TYPE_UINT.
    shader_object_.push_back(4);
    // D3D_UAV_DIMENSION_BUFFER.
    shader_object_.push_back(1);
    // Not multisampled.
    shader_object_.push_back(0xFFFFFFFFu);
    // Register u0.
    shader_object_.push_back(0);
    // One binding.
    shader_object_.push_back(1);
    // No D3D_SHADER_INPUT_FLAGS.
    shader_object_.push_back(0);
    // Register space 0.
    shader_object_.push_back(0);
    // UAV ID U0.
    shader_object_.push_back(0);
  }

  // Constant buffers.
  for (uint32_t i = 0; i < cbuffer_count_; ++i) {
    uint32_t register_index = 0;
    if (i == cbuffer_index_system_constants_) {
      shader_object_.push_back(cbuffer_name_offset_system);
      register_index = uint32_t(CbufferRegister::kSystemConstants);
    } else if (i == cbuffer_index_float_constants_) {
      shader_object_.push_back(cbuffer_name_offset_float);
      register_index = uint32_t(CbufferRegister::kFloatConstants);
    } else if (i == cbuffer_index_bool_loop_constants_) {
      shader_object_.push_back(cbuffer_name_offset_bool_loop);
      register_index = uint32_t(CbufferRegister::kBoolLoopConstants);
    } else if (i == cbuffer_index_fetch_constants_) {
      shader_object_.push_back(cbuffer_name_offset_fetch);
      register_index = uint32_t(CbufferRegister::kFetchConstants);
    }
    // D3D_SIT_CBUFFER.
    shader_object_.push_back(0);
    // No D3D_RESOURCE_RETURN_TYPE.
    shader_object_.push_back(0);
    // D3D_SRV_DIMENSION_UNKNOWN (not an SRV).
    shader_object_.push_back(0);
    // Multisampling not applicable.
    shader_object_.push_back(0);
    shader_object_.push_back(register_index);
    // One binding.
    shader_object_.push_back(1);
    // D3D_SIF_USERPACKED if a `cbuffer` rather than a `ConstantBuffer<T>`, but
    // we don't use indexable constant buffer descriptors.
    shader_object_.push_back(0);
    // Register space 0.
    shader_object_.push_back(0);
    // CBV ID CB[i].
    shader_object_.push_back(i);
  }
}

void DxbcShaderTranslator::WriteInputSignature() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  const uint32_t signature_position_dwords = 2;
  const uint32_t signature_size_dwords = 6;

  if (IsDXBCVertexShader()) {
    // Only unswapped vertex index.
    shader_object_.push_back(1);
    // Unknown.
    shader_object_.push_back(8);

    // Vertex index.
    // Semantic name SV_VertexID (the only one in the signature).
    shader_object_.push_back(
        (signature_position_dwords + signature_size_dwords) * sizeof(uint32_t));
    // Semantic index.
    shader_object_.push_back(0);
    // D3D_NAME_VERTEX_ID.
    shader_object_.push_back(6);
    // D3D_REGISTER_COMPONENT_UINT32.
    shader_object_.push_back(1);
    shader_object_.push_back(uint32_t(InOutRegister::kVSInVertexIndex));
    // x present, x used (always written to GPR 0).
    shader_object_.push_back(0b0001 | (0b0001 << 8));

    // Vertex index semantic name.
    AppendString(shader_object_, "SV_VertexID");
  } else {
    assert_true(IsDXBCPixelShader());
    // Interpolators, point parameters (coordinates, size), clip space ZW,
    // screen position, is front face.
    shader_object_.push_back(kInterpolatorCount + 4);
    // Unknown.
    shader_object_.push_back(8);

    // Intepolators.
    for (uint32_t i = 0; i < kInterpolatorCount; ++i) {
      // Reserve space for the semantic name (TEXCOORD).
      shader_object_.push_back(0);
      shader_object_.push_back(i);
      // D3D_NAME_UNDEFINED.
      shader_object_.push_back(0);
      // D3D_REGISTER_COMPONENT_FLOAT32.
      shader_object_.push_back(3);
      shader_object_.push_back(uint32_t(InOutRegister::kPSInInterpolators) + i);
      // Interpolators are copied to GPRs in the beginning of the shader. If
      // there's a register to copy to, this interpolator is used.
      uint32_t interpolator_used =
          (!is_depth_only_pixel_shader_ && i < register_count()) ? (0b1111 << 8)
                                                                 : 0b0000;
      shader_object_.push_back(0b1111 | interpolator_used);
    }

    // Point parameters - coordinate on the point and point size as a float3
    // TEXCOORD (but the size in Z is not needed). Always used because
    // ps_param_gen is handled dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(kPointParametersTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(uint32_t(InOutRegister::kPSInPointParameters));
    shader_object_.push_back(
        0b0111 | (is_depth_only_pixel_shader_ ? 0b0000 : (0b0011 << 8)));

    // Z and W in clip space, for getting per-sample depth with ROV.
    shader_object_.push_back(0);
    shader_object_.push_back(kClipSpaceZWTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
    shader_object_.push_back(0b0011 |
                             ((edram_rov_used_ ? 0b0011 : 0b0000) << 8));

    // Position (only XY needed for ps_param_gen and for EDRAM address
    // calculation). Z is not needed - ROV depth testing calculates the depth
    // from the clip space Z/W texcoord, and if oDepth is used, it must be
    // written to on every execution path anyway.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_POSITION.
    shader_object_.push_back(1);
    shader_object_.push_back(3);
    shader_object_.push_back(uint32_t(InOutRegister::kPSInPosition));
    shader_object_.push_back(0b1111 | (0b0011 << 8));

    // Is front face. Always used because ps_param_gen is handled dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_IS_FRONT_FACE.
    shader_object_.push_back(9);
    shader_object_.push_back(1);
    shader_object_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
    if (edram_rov_used_) {
      shader_object_.push_back(0b0001 | (0b0001 << 8));
    } else {
      shader_object_.push_back(
          0b0001 | (is_depth_only_pixel_shader_ ? 0b0000 : (0b0001 << 8)));
    }

    // Write the semantic names.
    new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                 sizeof(uint32_t);
    for (uint32_t i = 0; i < kInterpolatorCount + 2; ++i) {
      uint32_t texcoord_name_position_dwords = chunk_position_dwords +
                                               signature_position_dwords +
                                               i * signature_size_dwords;
      shader_object_[texcoord_name_position_dwords] = new_offset;
    }
    new_offset += AppendString(shader_object_, "TEXCOORD");

    uint32_t position_name_position_dwords =
        chunk_position_dwords + signature_position_dwords +
        (kInterpolatorCount + 2) * signature_size_dwords;
    shader_object_[position_name_position_dwords] = new_offset;
    new_offset += AppendString(shader_object_, "SV_Position");

    uint32_t front_face_name_position_dwords =
        position_name_position_dwords + signature_size_dwords;
    shader_object_[front_face_name_position_dwords] = new_offset;
    new_offset += AppendString(shader_object_, "SV_IsFrontFace");
  }
}

void DxbcShaderTranslator::WriteOutputSignature() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  const uint32_t signature_position_dwords = 2;
  const uint32_t signature_size_dwords = 6;

  if (IsDXBCVertexShader()) {
    // Interpolators, point parameters (coordinates, size), clip space ZW,
    // screen position.
    shader_object_.push_back(kInterpolatorCount + 3);
    // Unknown.
    shader_object_.push_back(8);

    // Intepolators.
    for (uint32_t i = 0; i < kInterpolatorCount; ++i) {
      // Reserve space for the semantic name (TEXCOORD).
      shader_object_.push_back(0);
      // Semantic index.
      shader_object_.push_back(i);
      // D3D_NAME_UNDEFINED.
      shader_object_.push_back(0);
      // D3D_REGISTER_COMPONENT_FLOAT32.
      shader_object_.push_back(3);
      shader_object_.push_back(uint32_t(InOutRegister::kVSOutInterpolators) +
                               i);
      // Unlike in ISGN, the second byte contains the unused components, not the
      // used ones. All components are always used because they are reset to 0.
      shader_object_.push_back(0b1111);
    }

    // Point parameters - coordinate on the point and point size as a float3
    // TEXCOORD. Always used because reset to (0, 0, -1).
    shader_object_.push_back(0);
    shader_object_.push_back(kPointParametersTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(uint32_t(InOutRegister::kVSOutPointParameters));
    shader_object_.push_back(0b0111 | (0b1000 << 8));

    // Z and W in clip space, for getting per-sample depth with ROV.
    shader_object_.push_back(0);
    shader_object_.push_back(kClipSpaceZWTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(uint32_t(InOutRegister::kVSOutClipSpaceZW));
    shader_object_.push_back(0b0011 | (0b1100 << 8));

    // Position.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_POSITION.
    shader_object_.push_back(1);
    shader_object_.push_back(3);
    shader_object_.push_back(uint32_t(InOutRegister::kVSOutPosition));
    shader_object_.push_back(0b1111);

    // Write the semantic names.
    new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                 sizeof(uint32_t);
    for (uint32_t i = 0; i < kInterpolatorCount + 2; ++i) {
      uint32_t texcoord_name_position_dwords = chunk_position_dwords +
                                               signature_position_dwords +
                                               i * signature_size_dwords;
      shader_object_[texcoord_name_position_dwords] = new_offset;
    }
    new_offset += AppendString(shader_object_, "TEXCOORD");
    uint32_t position_name_position_dwords =
        chunk_position_dwords + signature_position_dwords +
        (kInterpolatorCount + 2) * signature_size_dwords;
    shader_object_[position_name_position_dwords] = new_offset;
    new_offset += AppendString(shader_object_, "SV_Position");
  } else {
    assert_true(IsDXBCPixelShader());
    if (edram_rov_used_) {
      // No outputs - only ROV read/write.
      shader_object_.push_back(0);
      // Unknown.
      shader_object_.push_back(8);
    } else {
      // Color render targets, optionally depth.
      shader_object_.push_back((is_depth_only_pixel_shader_ ? 0 : 4) +
                               (writes_depth() ? 1 : 0));
      // Unknown.
      shader_object_.push_back(8);

      // Color render targets.
      if (!is_depth_only_pixel_shader_) {
        for (uint32_t i = 0; i < 4; ++i) {
          // Reserve space for the semantic name (SV_Target).
          shader_object_.push_back(0);
          shader_object_.push_back(i);
          // D3D_NAME_UNDEFINED for some reason - this is correct.
          shader_object_.push_back(0);
          shader_object_.push_back(3);
          // Register must match the render target index.
          shader_object_.push_back(i);
          // All are used because X360 RTs are dynamically remapped to D3D12 RTs
          // to make the indices consecutive.
          shader_object_.push_back(0xF);
        }
      }

      // Depth.
      if (writes_depth()) {
        // Reserve space for the semantic name (SV_Depth).
        shader_object_.push_back(0);
        shader_object_.push_back(0);
        shader_object_.push_back(0);
        shader_object_.push_back(3);
        shader_object_.push_back(0xFFFFFFFFu);
        shader_object_.push_back(0x1 | (0xE << 8));
      }

      // Write the semantic names.
      new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                   sizeof(uint32_t);
      if (!is_depth_only_pixel_shader_) {
        for (uint32_t i = 0; i < 4; ++i) {
          uint32_t color_name_position_dwords = chunk_position_dwords +
                                                signature_position_dwords +
                                                i * signature_size_dwords;
          shader_object_[color_name_position_dwords] = new_offset;
        }
      }
      new_offset += AppendString(shader_object_, "SV_Target");
      if (writes_depth()) {
        uint32_t depth_name_position_dwords = chunk_position_dwords +
                                              signature_position_dwords +
                                              4 * signature_size_dwords;
        shader_object_[depth_name_position_dwords] = new_offset;
        new_offset += AppendString(shader_object_, "SV_Depth");
      }
    }
  }
}

void DxbcShaderTranslator::WriteShaderCode() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());

  shader_object_.push_back(ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(
      IsDXBCVertexShader() ? D3D10_SB_VERTEX_SHADER : D3D10_SB_PIXEL_SHADER, 5,
      1));
  // Reserve space for the length token.
  shader_object_.push_back(0);

  // Declarations (don't increase the instruction count stat, and only inputs
  // and outputs are counted in dcl_count).
  //
  // Binding declarations have 3D-indexed operands with XYZW swizzle, the first
  // index being the binding ID (local to the shader), the second being the
  // lower register index bound, and the third being the highest register index
  // bound. Also dcl_ instructions for bindings are followed by the register
  // space index.
  //
  // Inputs/outputs have 1D-indexed operands with a component mask and a
  // register index.

  // Don't allow refactoring when converting to native code to maintain position
  // invariance (needed even in pixel shaders for oDepth invariance).
  shader_object_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) |
      D3D11_1_SB_GLOBAL_FLAG_SKIP_OPTIMIZATION |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));

  // Constant buffers, from most frequenly accessed to least frequently accessed
  // (the order is a hint to the driver according to the DXBC header).
  if (cbuffer_index_float_constants_ != kCbufferIndexUnallocated) {
    uint32_t float_constant_count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      float_constant_count +=
          xe::bit_count(constant_register_map().float_bitmap[i]);
    }
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            float_constants_dynamic_indexed_
                ? D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED
                : D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_float_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kFloatConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kFloatConstants));
    shader_object_.push_back(float_constant_count);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_system_constants_ != kCbufferIndexUnallocated) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_system_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_object_.push_back((sizeof(SystemConstants) + 15) >> 4);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_fetch_constants_ != kCbufferIndexUnallocated) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_fetch_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kFetchConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kFetchConstants));
    shader_object_.push_back(48);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_bool_loop_constants_ != kCbufferIndexUnallocated) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            bool_loop_constants_dynamic_indexed_
                ? D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED
                : D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_bool_loop_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_object_.push_back(40);
    shader_object_.push_back(0);
  }

  // Samplers.
  for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
    const SamplerBinding& sampler_binding = sampler_bindings_[i];
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_SAMPLER) |
        ENCODE_D3D10_SB_SAMPLER_MODE(D3D10_SB_SAMPLER_MODE_DEFAULT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 3));
    shader_object_.push_back(i);
    shader_object_.push_back(i);
    shader_object_.push_back(i);
    shader_object_.push_back(0);
  }

  // Shader resources.
  // Shared memory ByteAddressBuffer (T0, at t0, space0).
  shader_object_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_RESOURCE_RAW) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
  shader_object_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 3));
  shader_object_.push_back(0);
  shader_object_.push_back(0);
  shader_object_.push_back(0);
  shader_object_.push_back(0);

  // Textures.
  for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
    const TextureSRV& texture_srv = texture_srvs_[i];
    D3D10_SB_RESOURCE_DIMENSION texture_srv_dimension;
    switch (texture_srv.dimension) {
      case TextureDimension::k3D:
        texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
        break;
      case TextureDimension::kCube:
        texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE;
        break;
      default:
        texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY;
    }
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_RESOURCE) |
        ENCODE_D3D10_SB_RESOURCE_DIMENSION(texture_srv_dimension) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 3));
    // T0 is shared memory.
    shader_object_.push_back(1 + i);
    // t0 is shared memory.
    shader_object_.push_back(1 + i);
    shader_object_.push_back(1 + i);
    shader_object_.push_back(
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 0) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 1) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 2) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 3));
    shader_object_.push_back(0);
  }

  // Unordered access views.
  if (IsDXBCPixelShader() && edram_rov_used_) {
    // EDRAM uint32 rasterizer-ordered buffer (U0, at u0, space0).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(
            D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED) |
        ENCODE_D3D10_SB_RESOURCE_DIMENSION(D3D10_SB_RESOURCE_DIMENSION_BUFFER) |
        D3D11_SB_RASTERIZER_ORDERED_ACCESS |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 3));
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 0) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 1) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 2) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 3));
    shader_object_.push_back(0);
  }

  // Inputs and outputs.
  if (IsDXBCVertexShader()) {
    // Unswapped vertex index input (only X component).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_SGV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSInVertexIndex));
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_VERTEX_ID));
    ++stat_.dcl_count;
    // Interpolator output.
    for (uint32_t i = 0; i < kInterpolatorCount; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kVSOutInterpolators) +
                               i);
      ++stat_.dcl_count;
    }
    // Point parameters output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0111, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSOutPointParameters));
    ++stat_.dcl_count;
    // Clip space Z and W output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0011, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSOutClipSpaceZW));
    ++stat_.dcl_count;
    // Position output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSOutPosition));
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
    ++stat_.dcl_count;
  } else if (IsDXBCPixelShader()) {
    // Interpolator input.
    if (!is_depth_only_pixel_shader_) {
      uint32_t interpolator_count =
          std::min(kInterpolatorCount, register_count());
      for (uint32_t i = 0; i < interpolator_count; ++i) {
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
            ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
                D3D10_SB_INTERPOLATION_LINEAR) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_object_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b1111, 1));
        shader_object_.push_back(uint32_t(InOutRegister::kPSInInterpolators) +
                                 i);
        ++stat_.dcl_count;
      }
      // Point parameters input (only coordinates, not size, needed).
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              D3D10_SB_INTERPOLATION_LINEAR) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kPSInPointParameters));
      ++stat_.dcl_count;
    }
    if (edram_rov_used_) {
      // Z and W in clip space, for per-sample depth.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              D3D10_SB_INTERPOLATION_LINEAR) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
      ++stat_.dcl_count;
    }
    // Position input (only XY needed for ps_param_gen, and the ROV depth code
    // calculates the depth from clip space Z and W).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SIV) |
        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
            D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kPSInPosition));
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
    ++stat_.dcl_count;
    if (edram_rov_used_ || !is_depth_only_pixel_shader_) {
      // Is front face.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SGV) |
          // This needs to be set according to FXC output, despite the
          // description in d3d12TokenizedProgramFormat.hpp saying bits 11:23
          // are ignored.
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              D3D10_SB_INTERPOLATION_CONSTANT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
      shader_object_.push_back(
          ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_IS_FRONT_FACE));
      ++stat_.dcl_count;
    }
    if (edram_rov_used_) {
      // Sample coverage input.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
      shader_object_.push_back(
          EncodeScalarOperand(D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK, 0));
      ++stat_.dcl_count;
    } else {
      if (!is_depth_only_pixel_shader_) {
        // Color output.
        for (uint32_t i = 0; i < 4; ++i) {
          shader_object_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_object_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
          shader_object_.push_back(i);
          ++stat_.dcl_count;
        }
      }
      // Depth output.
      if (writes_depth()) {
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
        shader_object_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH, 0));
        ++stat_.dcl_count;
      }
    }
  }

  // Temporary registers - guest general-purpose registers if not using dynamic
  // indexing and Xenia internal registers.
  stat_.temp_register_count = system_temp_count_max_;
  if (!is_depth_only_pixel_shader_ && !IndexableGPRsUsed()) {
    stat_.temp_register_count += register_count();
  }
  if (stat_.temp_register_count != 0) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_TEMPS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
    shader_object_.push_back(stat_.temp_register_count);
  }

  // General-purpose registers if using dynamic indexing (x0).
  if (!is_depth_only_pixel_shader_ && IndexableGPRsUsed()) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    // x0.
    shader_object_.push_back(0);
    shader_object_.push_back(register_count());
    // 4 components in each.
    shader_object_.push_back(4);
    stat_.temp_array_count += register_count();
  }

  // Initialize the depth output if used, which must be initialized on every
  // execution path.
  if (!edram_rov_used_ && IsDXBCPixelShader() && writes_depth()) {
    shader_object_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH, 0));
    shader_object_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_object_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  // Write the translated shader code.
  size_t code_size_dwords = shader_code_.size();
  // So [] won't crash in case the size is zero somehow.
  if (code_size_dwords != 0) {
    shader_object_.resize(shader_object_.size() + code_size_dwords);
    std::memcpy(&shader_object_[shader_object_.size() - code_size_dwords],
                shader_code_.data(), code_size_dwords * sizeof(uint32_t));
  }

  // Write the length.
  shader_object_[chunk_position_dwords + 1] =
      uint32_t(shader_object_.size()) - chunk_position_dwords;
}

}  // namespace gpu
}  // namespace xe
