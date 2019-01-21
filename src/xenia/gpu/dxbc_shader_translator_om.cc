/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/math.h"

namespace xe {
namespace gpu {
using namespace ucode;

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
      // k_2_10_10_10_AS_10_10_10_10
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
    case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
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

  // Don't allow the shift to overflow, since in DXBC the lower 5 bits of the
  // shift amount are used (otherwise 0 becomes 8).
  // t2 = min(113 - (f32 >> 23), 24)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(temp2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t1 = ((f32 & 0x7FFFFF) | 0x800000) >> min(113 - (f32 >> 23), 24)
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

  // Round to the nearest even integer. This seems to be the correct way:
  // rounding towards zero gives 0xFF instead of 0x100 in clear shaders in, for
  // instance, Halo 3, but other clear shaders in it are also broken if 0.5 is
  // added before ftou instead of round_ne.
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

void DxbcShaderTranslator::CompletePixelShader_ApplyColorExpBias() {
  if (is_depth_only_pixel_shader_) {
    return;
  }
  // The constant contains 2.0^bias.
  for (uint32_t i = 0; i < 4; ++i) {
    if (!writes_color_target(i)) {
      continue;
    }
    system_constants_used_ |= 1ull << kSysConst_ColorExpBias_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temps_color_ + i);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_color_ + i);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_ColorExpBias_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
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

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs_AlphaToCoverage() {
  if (is_depth_only_pixel_shader_ || !writes_color_target(0)) {
    return;
  }

  // Refer to CompletePixelShader_WriteToROV_GetCoverage for the description of
  // the alpha to coverage pattern used.

  uint32_t atoc_temp = PushSystemTemp();

  // Extract the flag to check if alpha to coverage is enabled.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_AlphaToCoverage);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if alpha to coverage is enabled.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Convert SSAA sample position to integer.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Get SSAA sample coordinates in the pixel.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_SampleCountLog2_Comp |
          ((kSysConst_SampleCountLog2_Comp + 1) << 2),
      3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get the sample index - 0 and 2 being the top ones, 1 and 3 being the bottom
  // ones (because at 2x SSAA, 1 is the bottom).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Create a mask to choose the specific threshold to compare to.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(2);
  shader_code_.push_back(3);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  uint32_t atoc_thresholds_temp = PushSystemTemp();

  // Choose the thresholds based on the sample count - first between 2 and 1
  // samples.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_thresholds_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_SampleCountLog2_Comp + 1, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 0.25
  shader_code_.push_back(0x3E800000);
  // 0.75
  shader_code_.push_back(0x3F400000);
  // NaN - comparison always fails
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 0.5
  shader_code_.push_back(0x3F000000);
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(0x7FC00000);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Choose the thresholds based on the sample count - between 4 or 1/2 samples.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(14));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_thresholds_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_SampleCountLog2_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 0.625
  shader_code_.push_back(0x3F200000);
  // 0.125
  shader_code_.push_back(0x3E000000);
  // 0.375
  shader_code_.push_back(0x3EC00000);
  // 0.875
  shader_code_.push_back(0x3F600000);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_thresholds_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Choose the threshold to compare the alpha to according to the current
  // sample index - mask.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_thresholds_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Release atoc_thresholds_temp.
  PopSystemTemp();

  // Choose the threshold to compare the alpha to according to the current
  // sample index - select within pairs.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Choose the threshold to compare the alpha to according to the current
  // sample index - combine pairs.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Compare the alpha to the threshold.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_color_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Discard the SSAA sample if it's not covered.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DISCARD) |
      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_ZERO) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;

  // Close the alpha to coverage check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Release atoc_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs() {
  // Check if this sample needs to be discarded by alpha to coverage.
  CompletePixelShader_WriteToRTVs_AlphaToCoverage();

  // Apply the exponent bias after alpha to coverage because it needs the
  // unbiased alpha from the shader.
  CompletePixelShader_ApplyColorExpBias();

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
    CompletePixelShader_GammaCorrect(system_temps_color_ + i, true);
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
      shader_code_.push_back(system_temps_color_ + j);
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

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_GetCoverage(
    uint32_t coverage_out_temp) {
  // Load the coverage from the rasterizer. For 2x AA, use samples 0 and 3
  // (top-left and bottom-right), for 4x, use all, because ForcedSampleCount
  // can't be 2.
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

  // Check if alpha to coverage can be done at all in this shader.
  if (is_depth_only_pixel_shader_ || !writes_color_target(0)) {
    return;
  }

  uint32_t atoc_temp = PushSystemTemp();

  // Extract the flag to check if alpha to coverage is enabled.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_AlphaToCoverage);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if alpha to coverage is enabled.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // According to tests on an Adreno 200 device (LG Optimus L7), without
  // dithering, done by drawing 0.5x0.5 rectangles in different corners of four
  // pixels in a quad to a multisampled GLSurfaceView, the coverage is the
  // following for 4 samples:
  // 0.25)  [0.25, 0.5)  [0.5, 0.75)  [0.75, 1)   [1
  //  --        --           --          --       --
  // |  |      |  |         | #|        |##|     |##|
  // |  |      |# |         |# |        |# |     |##|
  //  --        --           --          --       --
  // (VPOS near 0 on the top, near 1 on the bottom here.)
  // For 2 samples, the top sample (closer to VPOS 0) is covered when alpha is
  // in [0.5, 1).
  // With these values, however, in Red Dead Redemption, almost all distant
  // trees are transparent, and it's also weird that the values are so
  // unbalanced (0.25-wide range with zero coverage, but only one point with
  // full coverage), so ranges are halfway offset here.
  // TODO(Triang3l): Find an Adreno device with dithering enabled, and where the
  // numbers 3, 1, 0, 2 look meaningful for pixels in quads, and implement
  // offsets.
  // Choose the thresholds based on the sample count - first between 2 and 1
  // samples.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_SampleCountLog2_Comp + 1, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 0.25
  shader_code_.push_back(0x3E800000);
  // 0.75
  shader_code_.push_back(0x3F400000);
  // NaN - comparison always fails
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 0.5
  shader_code_.push_back(0x3F000000);
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(0x7FC00000);
  shader_code_.push_back(0x7FC00000);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // Choose the thresholds based on the sample count - between 4 or 1/2 samples.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(14));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_SampleCountLog2_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 0.625
  shader_code_.push_back(0x3F200000);
  // 0.375
  shader_code_.push_back(0x3EC00000);
  // 0.125
  shader_code_.push_back(0x3E000000);
  // 0.875
  shader_code_.push_back(0x3F600000);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Check if alpha of oC0 is greater than the threshold for each sample or
  // equal to it.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_color_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Mask the sample coverage.
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
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the pixel can be discarded totally - merge masked coverage of
  // samples 01 and 23.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(coverage_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
  shader_code_.push_back(coverage_out_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the pixel can be discarded totally - merge masked coverage of
  // samples 0|2 and 1|3.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Don't even do depth/stencil for pixels fully discarded by alpha to
  // coverage.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RETC) |
      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_ZERO) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(atoc_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Close the alpha to coverage check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Release atoc_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV_DepthStencil(
    uint32_t edram_dword_offset_temp, uint32_t coverage_in_out_temp) {
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

  // Apply pixel width and height scale.
  system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionScaleLog2_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(edram_dword_offset_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(edram_dword_offset_temp);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
  }

  // Choose the pixel for 2x scaling.
  uint32_t resolution_scale_pixel_temp = PushSystemTemp();

  // 1) Convert pixel position to integer.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // 2) For 2x, get the current pixel in the quad. For 1x, write 0 for it.
  system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionScaleLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 3) Calculate dword offset of the pixel in the quad.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // 4) Add the quad pixel offset.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(edram_dword_offset_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(edram_dword_offset_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(resolution_scale_pixel_temp);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Release resolution_scale_pixel_temp.
  PopSystemTemp();

  // Load the previous depth/stencil values.
  uint32_t depth_values_temp = PushSystemTemp();
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(coverage_in_out_temp);
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
    shader_code_.push_back(GetEDRAMUAVIndex());
    shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
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
  shader_code_.push_back(coverage_in_out_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(coverage_in_out_temp);
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
    shader_code_.push_back(coverage_in_out_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
    shader_code_.push_back(GetEDRAMUAVIndex());
    shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
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

  if (!is_depth_only_pixel_shader_) {
    // Update the coverage according to the depth/stencil test result (0 or
    // 0xFFFFFFFF) after writing the new depth/stencil if stencil is enabled.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(coverage_in_out_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(coverage_in_out_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(depth_test_results_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }

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

  // Multiply the colors by the factors, with 0 * Infinity = 0 behavior.
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t factor_temp =
        i ? dest_factor_and_minmax_temp : src_factor_and_result_temp;
    uint32_t color_temp = i ? dest_color_temp : src_color_and_output_temp;

    // Get the multiplicand closer to zero to check if any of them is zero.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
                               D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
                           ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_ABS));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
                               D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
                           ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_ABS));
    shader_code_.push_back(factor_temp);
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

    // Check if the color or the factor is zero to zero the result (min isn't
    // required to flush denormals in the result).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(factor_calculation_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Zero the result if the color or the factor is zero.
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

  // Convert to fixed-point, rounding towards the nearest even integer.
  // Rounding towards the nearest (adding +-0.5 before truncating) is giving
  // incorrect results for depth, so better to use round_ne here too.
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

  // Don't allow the shift to overflow, since in DXBC the lower 5 bits of the
  // shift amount are used.
  // t2 = min(125 - (f32 >> 23), 24)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(f10_temp2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(f10_temp2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  shader_code_.push_back(24);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // t1 = ((f32 & 0x7FFFFF) | 0x800000) >> min(125 - (f32 >> 23), 24)
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

  // Get guest pixel position as if increased resolution is disabled - addresses
  // within the quad with 2x resolution will be calculated later.
  system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionScaleLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(edram_coord_pixel_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

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
      EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_NULL, 0));
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
  CompletePixelShader_WriteToROV_GetCoverage(coverage_temp);
  CompletePixelShader_WriteToROV_DepthStencil(edram_coord_pixel_depth_temp,
                                              coverage_temp);

  // ***************************************************************************
  // Write to color render targets.
  // ***************************************************************************

  if (color_targets_written) {
    // Apply the exponent bias after having done alpha to coverage, which needs
    // the original alpha from the shader.
    CompletePixelShader_ApplyColorExpBias();

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

    // Apply pixel width and height scale.
    system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionScaleLog2_Index;
    for (uint32_t i = 0; i < 2; ++i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(edram_coord_pixel_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(edram_coord_pixel_temp);
      shader_code_.push_back(EncodeVectorReplicatedOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
          kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
    }

    // Choose the pixel for 2x scaling.
    uint32_t resolution_scale_pixel_temp = PushSystemTemp();

    // 1) Convert pixel position to integer.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;

    // 2) For 2x, get the current pixel in the quad. For 1x, write 0 for it.
    system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionScaleLog2_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // 3) Calculate dword offset of the pixel in the quad.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(2);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // 4) Multiply the quad pixel offset by dword count per pixel for each RT.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(rt_64bpp_temp);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // 5) Add the quad pixel offsets.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(edram_coord_pixel_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(resolution_scale_pixel_temp);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Release resolution_scale_pixel_temp.
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

      // Get per-sample EDRAM addresses offsets.
      uint32_t edram_coord_sample_temp = PushSystemTemp();

      // 1) Choose the strides according to the resolution scale (1x or 2x2x).
      system_constants_used_ |= 1ull
                                << kSysConst_EDRAMResolutionScaleLog2_Index;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(edram_coord_sample_temp);
      shader_code_.push_back(EncodeVectorReplicatedOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
          kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(320);
      shader_code_.push_back(4);
      shader_code_.push_back(324);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(80);
      shader_code_.push_back(1);
      shader_code_.push_back(81);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // 2) Multiply the relative sample offset by sample size.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(edram_coord_sample_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(edram_coord_sample_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(rt_64bpp_temp);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;

      // 3) Add the first sample EDRAM addresses to the sample offsets.
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
          shader_code_.push_back(GetEDRAMUAVIndex());
          shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
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
      shader_code_.push_back(system_temps_color_ + i);
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
          shader_code_.push_back(GetEDRAMUAVIndex());
          shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
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

  if (writes_color_target(0)) {
    // Alpha test.
    uint32_t alpha_test_temp = PushSystemTemp();
    // Extract the comparison mask to check if the test needs to be done at all.
    // Don't care about flow control being somewhat dynamic - early Z is forced
    // using a special version of the shader anyway.
    system_constants_used_ |= 1ull << kSysConst_Flags_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(alpha_test_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(3);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(kSysFlag_AlphaPassIfLess_Shift);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_Flags_Vec);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // Compare the mask to ALWAYS to check if the test shouldn't be done (will
    // pass even for NaNs, though the expected behavior in this case hasn't been
    // checked, but let's assume this means "always", not "less, equal or
    // greater".
    // TODO(Triang3l): Check how alpha test works with NaN on Direct3D 9.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_INE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(alpha_test_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(alpha_test_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0b111);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Don't do the test if the mode is "always".
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(alpha_test_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    // Do the test.
    system_constants_used_ |= 1ull << kSysConst_AlphaTestReference_Index;
    for (uint32_t i = 0; i < 3; ++i) {
      // Get the result of the operation: less, equal or greater.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(i == 1 ? D3D10_SB_OPCODE_EQ
                                             : D3D10_SB_OPCODE_LT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
      shader_code_.push_back(alpha_test_temp);
      if (i != 0) {
        // For 1, reference == alpha. For 2, alpha > reference, but with lt,
        // reference < alpha.
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      kSysConst_AlphaTestReference_Comp, 3));
        shader_code_.push_back(cbuffer_index_system_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
        shader_code_.push_back(kSysConst_AlphaTestReference_Vec);
      }
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temps_color_);
      if (i == 0) {
        // Alpha < reference.
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      kSysConst_AlphaTestReference_Comp, 3));
        shader_code_.push_back(cbuffer_index_system_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
        shader_code_.push_back(kSysConst_AlphaTestReference_Vec);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    }
    // Extract the comparison value per-bit.
    uint32_t alpha_test_comparison_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(alpha_test_comparison_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(1);
    shader_code_.push_back(2);
    shader_code_.push_back(0);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(alpha_test_temp);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Mask the results.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(alpha_test_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(alpha_test_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(alpha_test_comparison_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // Release alpha_test_comparison_temp.
    PopSystemTemp();
    // Merge test results.
    for (uint32_t i = 0; i < 2; ++i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(alpha_test_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(alpha_test_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 + i, 1));
      shader_code_.push_back(alpha_test_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }
    // Discard the pixel if has failed the text.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(edram_rov_used_ ? D3D10_SB_OPCODE_RETC
                                                    : D3D10_SB_OPCODE_DISCARD) |
        ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
            D3D10_SB_INSTRUCTION_TEST_ZERO) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(alpha_test_temp);
    ++stat_.instruction_count;
    if (edram_rov_used_) {
      ++stat_.dynamic_flow_control_count;
    }
    // Close the "not always" check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    // Release alpha_test_temp.
    PopSystemTemp();
  }

  // Write the values to the render targets. Not applying the exponent bias yet
  // because the original 0 to 1 alpha value is needed for alpha to coverage,
  // which is done differently for ROV and RTV/DSV.
  if (edram_rov_used_) {
    CompletePixelShader_WriteToROV();
  } else {
    CompletePixelShader_WriteToRTVs();
  }
}

}  // namespace gpu
}  // namespace xe
