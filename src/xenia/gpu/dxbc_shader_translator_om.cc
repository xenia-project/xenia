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

void DxbcShaderTranslator::ROV_GetColorFormatSystemConstants(
    ColorRenderTargetFormat format, uint32_t write_mask, float& clamp_rgb_low,
    float& clamp_alpha_low, float& clamp_rgb_high, float& clamp_alpha_high,
    uint32_t& keep_mask_low, uint32_t& keep_mask_high) {
  keep_mask_low = keep_mask_high = 0;
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0xFF) << (i * 8);
        }
      }
    } break;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0x3FF) << (i * 10);
        }
      }
      if (!(write_mask & 0b1000)) {
        keep_mask_low |= uint32_t(3) << 30;
      }
    } break;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = 31.875f;
      clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0x3FF) << (i * 10);
        }
      }
      if (!(write_mask & 0b1000)) {
        keep_mask_low |= uint32_t(3) << 30;
      }
    } break;
    case ColorRenderTargetFormat::k_16_16:
    case ColorRenderTargetFormat::k_16_16_16_16:
      // Alpha clamping affects blending source, so it's non-zero for alpha for
      // k_16_16 (the render target is fixed-point).
      clamp_rgb_low = clamp_alpha_low = -32.0f;
      clamp_rgb_high = clamp_alpha_high = 32.0f;
      if (!(write_mask & 0b0001)) {
        keep_mask_low |= 0xFFFFu;
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_low |= 0xFFFF0000u;
      }
      if (format == ColorRenderTargetFormat::k_16_16_16_16) {
        if (!(write_mask & 0b0100)) {
          keep_mask_high |= 0xFFFFu;
        }
        if (!(write_mask & 0b1000)) {
          keep_mask_high |= 0xFFFF0000u;
        }
      } else {
        write_mask &= 0b0011;
      }
      break;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      // No NaNs on the Xbox 360 GPU, though can't use the extended range with
      // f32tof16.
      clamp_rgb_low = clamp_alpha_low = -65504.0f;
      clamp_rgb_high = clamp_alpha_high = 65504.0f;
      if (!(write_mask & 0b0001)) {
        keep_mask_low |= 0xFFFFu;
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_low |= 0xFFFF0000u;
      }
      if (format == ColorRenderTargetFormat::k_16_16_16_16_FLOAT) {
        if (!(write_mask & 0b0100)) {
          keep_mask_high |= 0xFFFFu;
        }
        if (!(write_mask & 0b1000)) {
          keep_mask_high |= 0xFFFF0000u;
        }
      } else {
        write_mask &= 0b0011;
      }
      break;
    case ColorRenderTargetFormat::k_32_FLOAT:
      // No clamping - let min/max always pick the original value.
      clamp_rgb_low = clamp_alpha_low = clamp_rgb_high = clamp_alpha_high =
          std::nanf("");
      write_mask &= 0b0001;
      if (!(write_mask & 0b0001)) {
        keep_mask_low = ~uint32_t(0);
      }
      break;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      // No clamping - let min/max always pick the original value.
      clamp_rgb_low = clamp_alpha_low = clamp_rgb_high = clamp_alpha_high =
          std::nanf("");
      write_mask &= 0b0011;
      if (!(write_mask & 0b0001)) {
        keep_mask_low = ~uint32_t(0);
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_high = ~uint32_t(0);
      }
      break;
    default:
      assert_unhandled_case(format);
      // Disable invalid render targets.
      write_mask = 0;
      break;
  }
  // Special case handled in the shaders for empty write mask to completely skip
  // a disabled render target: all keep bits are set.
  if (!write_mask) {
    keep_mask_low = keep_mask_high = ~uint32_t(0);
  }
}

void DxbcShaderTranslator::StartPixelShader_LoadROVParameters() {
  bool color_targets_written = writes_any_color_target();

  // ***************************************************************************
  // Get EDRAM offsets for the pixel:
  // system_temp_rov_params_.y - for depth (absolute).
  // system_temp_rov_params_.z - for 32bpp color (base-relative).
  // system_temp_rov_params_.w - for 64bpp color (base-relative).
  // ***************************************************************************

  // Extract the resolution scale as log2(scale)/2 specific for 1 (-> 0) and
  // 4 (-> 1) to a temp SGPR.
  uint32_t resolution_scale_log2_temp = PushSystemTemp();
  system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionSquareScale_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(resolution_scale_log2_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMResolutionSquareScale_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMResolutionSquareScale_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(2);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert the pixel position (if resolution scale is 4, this will be 2x2
  // bigger) to integer to system_temp_rov_params_.zw.
  // system_temp_rov_params_.z = X host pixel position as uint
  // system_temp_rov_params_.w = Y host pixel position as uint
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b01000000, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Revert the resolution scale to convert the position to guest pixels.
  // system_temp_rov_params_.z = X guest pixel position / sample width
  // system_temp_rov_params_.w = Y guest pixel position / sample height
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(resolution_scale_log2_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert the position from pixels to samples.
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      (kSysConst_SampleCountLog2_Comp << 4) |
          ((kSysConst_SampleCountLog2_Comp + 1) << 6),
      3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Get 80x16 samples tile index - start dividing X by 80 by getting the high
  // part of the result of multiplication of X by 0xCCCCCCCD into X.
  // system_temp_rov_params_.x = (X * 0xCCCCCCCD) >> 32, or X / 80 * 64
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_NULL, 0));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xCCCCCCCDu);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get 80x16 samples tile index - finish dividing X by 80 and divide Y by 16
  // into system_temp_rov_params_.xy.
  // system_temp_rov_params_.x = X tile position
  // system_temp_rov_params_.y = Y tile position
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00001100, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(6);
  shader_code_.push_back(4);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get the tile index to system_temp_rov_params_.y.
  // system_temp_rov_params_.x = X tile position
  // system_temp_rov_params_.y = tile index
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  system_constants_used_ |= 1ull << kSysConst_EDRAMPitchTiles_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMPitchTiles_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMPitchTiles_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert the tile index into a tile offset.
  // system_temp_rov_params_.x = X tile position
  // system_temp_rov_params_.y = tile offset
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_NULL, 0));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1280);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get tile-local X sample index into system_temp_rov_params_.z.
  // system_temp_rov_params_.y = tile offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  // system_temp_rov_params_.w = Y guest sample 0 position
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(-80));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_rov_params_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Get tile-local Y sample index into system_temp_rov_params_.w.
  // system_temp_rov_params_.y = tile offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  // system_temp_rov_params_.w = Y sample 0 position within the tile
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(15);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Go to the target row within the tile in system_temp_rov_params_.y.
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(80);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Choose in which 40-sample half of the tile the pixel is, for swapping
  // 40-sample columns when accessing the depth buffer - games expect this
  // behavior when writing depth back to the EDRAM via color writing (GTA IV,
  // Halo 3).
  // system_temp_rov_params_.x = tile-local sample 0 X >= 40
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UGE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(40);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Choose what to add to the depth/stencil X position.
  // system_temp_rov_params_.x = 40 or -40 offset for the depth buffer
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(-40));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(40);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Flip tile halves for the depth/stencil buffer.
  // system_temp_rov_params_.x = X sample 0 position within the depth tile
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  if (color_targets_written) {
    // Write 32bpp color offset to system_temp_rov_params_.z.
    // system_temp_rov_params_.x = X sample 0 position within the depth tile
    // system_temp_rov_params_.y = row offset
    // system_temp_rov_params_.z = unscaled 32bpp color offset
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_rov_params_);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
  }

  // Write depth/stencil offset to system_temp_rov_params_.y.
  // system_temp_rov_params_.y = unscaled 32bpp depth/stencil offset
  // system_temp_rov_params_.z = unscaled 32bpp color offset if needed
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Add the EDRAM base for depth/stencil.
  // system_temp_rov_params_.y = unscaled 32bpp depth/stencil address
  // system_temp_rov_params_.z = unscaled 32bpp color offset if needed
  system_constants_used_ |= 1ull << kSysConst_EDRAMDepthBaseDwords_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMDepthBaseDwords_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMDepthBaseDwords_Vec);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Apply the resolution scale.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(resolution_scale_log2_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Release resolution_scale_log2_temp.
  PopSystemTemp();

  uint32_t offsets_masked, offsets_select;
  uint32_t offsets_immediate, offsets_components;
  if (color_targets_written) {
    offsets_masked =
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1);
    offsets_select = EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                 kSwizzleXYZW, 1);
    offsets_immediate = EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0);
    offsets_components = 4;
  } else {
    offsets_masked =
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1);
    offsets_select =
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1);
    offsets_immediate =
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0);
    offsets_components = 1;
  }

  // Scale the offsets by the resolution scale.
  // system_temp_rov_params_.y = scaled 32bpp depth/stencil first host pixel
  //                             address
  // system_temp_rov_params_.z = scaled 32bpp color first host pixel offset if
  //                             needed
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6 + offsets_components));
  shader_code_.push_back(offsets_masked);
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(offsets_select);
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(offsets_immediate);
  for (uint32_t i = 0; i < offsets_components; ++i) {
    shader_code_.push_back(2);
  }
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Add host pixel offsets.
  // system_temp_rov_params_.y = scaled 32bpp depth/stencil address
  // system_temp_rov_params_.z = scaled 32bpp color offset if needed
  for (uint32_t i = 0; i < 2; ++i) {
    // Convert a position component to integer.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, i, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;

    // Insert the host pixel offset on each axis.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                               9 + offsets_components * 2));
    shader_code_.push_back(offsets_masked);
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(offsets_immediate);
    for (uint32_t j = 0; j < offsets_components; ++j) {
      shader_code_.push_back(1);
    }
    shader_code_.push_back(offsets_immediate);
    for (uint32_t j = 0; j < offsets_components; ++j) {
      shader_code_.push_back(i);
    }
    if (color_targets_written) {
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    } else {
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    }
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(offsets_select);
    shader_code_.push_back(system_temp_rov_params_);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }

  // Close the resolution scale conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  if (color_targets_written) {
    // Get the 64bpp color offset to system_temp_rov_params_.w.
    // TODO(Triang3l): Find some game that aliases 64bpp with 32bpp to emulate
    // the real layout.
    // system_temp_rov_params_.y = scaled 32bpp depth/stencil address
    // system_temp_rov_params_.z = scaled 32bpp color offset
    // system_temp_rov_params_.w = scaled 64bpp color offset
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
  }

  // ***************************************************************************
  // Sample coverage to system_temp_rov_params_.x.
  // ***************************************************************************

  // Using ForcedSampleCount of 4 (2 is not supported on Nvidia), so for 2x
  // MSAA, handling samples 0 and 3 (upper-left and lower-right) as 0 and 1.

  // Check if 4x MSAA is enabled.
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

  // Copy the 4x AA coverage to system_temp_rov_params_.x.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK, 0, 0));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back((1 << 4) - 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Handle 1 or 2 samples.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract sample 3 coverage, which will be used as sample 1.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK, 0, 0));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Combine coverage of samples 0 (in bit 0 of vCoverage) and 3 (in bit 0 of
  // system_temp_rov_params_.x).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(31);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK, 0, 0));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Close the 4x MSAA conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::ROV_DepthStencilTest() {
  uint32_t temp1 = PushSystemTemp();

  // Check whether depth/stencil is enabled. 1 SGPR taken.
  // temp1.x = kSysFlag_ROVDepthStencil
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
  shader_code_.push_back(kSysFlag_ROVDepthStencil);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Open the depth/stencil enabled conditional. 1 SGPR released.
  // temp1.x = free
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  if (writes_depth()) {
    // Convert the shader-generated depth to 24-bit - move the 32-bit depth to
    // the conversion subroutine's argument.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_rov_depth_stencil_);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Convert the shader-generated depth to 24-bit.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CALL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
    shader_code_.push_back(label_rov_depth_to_24bit_);
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    // Store a copy of the depth in temp1.x to reload later.
    // temp1.x = 24-bit oDepth
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temps_subroutine_);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  } else {
    // Load the first sample's Z and W to system_temps_subroutine_[0] - need
    // this regardless of coverage for polygon offset.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0);
    ++stat_.instruction_count;

    // Calculate the first sample's Z/W to system_temps_subroutine_[0].x for
    // conversion to 24-bit and depth test.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temps_subroutine_);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Apply viewport Z range to the first sample because this would affect the
    // slope-scaled depth bias (tested on PC on Direct3D 12, by comparing the
    // fraction of the polygon's area with depth clamped - affected by the
    // constant bias, but not affected by the slope-scaled bias, also depth
    // range clamping should be done after applying the offset as well).
    system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMDepthRangeScale_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMDepthRangeOffset_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Get the derivatives of a sample's depth, for the slope-scaled polygon
    // offset. Probably not very significant that it's for the sample 0 rather
    // than for the center, likely neither is accurate because Xenos probably
    // calculates the slope between 16ths of a pixel according to the meaning of
    // the slope-scaled polygon offset in R5xx Acceleration. Take 2 VGPRs.
    // temp1.x = ddx(z)
    // temp1.y = ddy(z)
    for (uint32_t i = 0; i < 2; ++i) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(i ? D3D11_SB_OPCODE_DERIV_RTY_COARSE
                                        : D3D11_SB_OPCODE_DERIV_RTX_COARSE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b0001 << i, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temps_subroutine_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    }

    // Get the maximum depth slope for polygon offset to temp1.y.
    // Release 1 VGPR (Y derivative).
    // temp1.x = max(|ddx(z)|, |ddy(z)|)
    // temp1.y = free
    // https://docs.microsoft.com/en-us/windows/desktop/direct3d9/depth-bias
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1) |
        ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_ABS));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
        ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_ABS));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Copy the needed polygon offset values to temp1.yz. Take 2 VGPRs.
    // temp1.x = max(|ddx(z)|, |ddy(z)|)
    // temp1.y = polygon offset scale
    // temp1.z = polygon offset bias
    system_constants_used_ |= (1ull << kSysConst_EDRAMPolyOffsetFront_Index) |
                              (1ull << kSysConst_EDRAMPolyOffsetBack_Index);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        (kSysConst_EDRAMPolyOffsetFrontScale_Comp << 2) |
            (kSysConst_EDRAMPolyOffsetFrontOffset_Comp << 4),
        3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMPolyOffsetFront_Vec);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        (kSysConst_EDRAMPolyOffsetBackScale_Comp << 2) |
            (kSysConst_EDRAMPolyOffsetBackOffset_Comp << 4),
        3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMPolyOffsetBack_Vec);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Apply the slope scale and the constant bias to the offset, and release 2
    // VGPRs.
    // temp1.x = polygon offset
    // temp1.y = free
    // temp1.z = free
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Calculate the upper Z range bound to temp1.y for clamping after biasing,
    // taking 1 SGPR.
    // temp1.x = polygon offset
    // temp1.y = viewport maximum depth
    system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMDepthRangeOffset_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMDepthRangeScale_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  for (uint32_t i = 0; i < 4; ++i) {
    // Get if the current sample is covered to temp1.y. Take 1 VGPR.
    // temp1.x = polygon offset or 24-bit oDepth
    // temp1.y = viewport maximum depth if not writing to oDepth
    // temp1.z = coverage of the current sample
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1 << i);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if the current sample is covered. Release 1 VGPR.
    // temp1.x = polygon offset or 24-bit oDepth
    // temp1.y = viewport maximum depth if not writing to oDepth
    // temp1.z = free
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    if (writes_depth()) {
      // Same depth for all samples, already converted to 24-bit - only move it
      // to the depth/stencil sample subroutine argument from temp1.x if it's
      // not already there (it's there for the first sample - returned from the
      // conversion to 24-bit).
      if (i) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temps_subroutine_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(temp1);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
      }
    } else {
      if (i) {
        // Sample's depth precalculated for sample 0 (for slope-scaled depth
        // bias calculation), but need to calculate it for other samples.

        // Using system_temps_subroutine_[0].xy as temps for Z/W since Y will
        // contain the result anyway after the call, and temp1.x contains the
        // polygon offset.

        if (i == 1) {
          // Using ForcedSampleCount of 4 (2 is not supported on Nvidia), so for
          // 2x MSAA, handling samples 0 and 3 (upper-left and lower-right) as 0
          // and 1. Thus, evaluate Z/W at sample 3 when 4x is not enabled.
          system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
          shader_code_.push_back(system_temps_subroutine_);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                        kSysConst_SampleCountLog2_Comp, 3));
          shader_code_.push_back(cbuffer_index_system_constants_);
          shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
          shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(3);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(1);
          ++stat_.instruction_count;
          ++stat_.movc_instruction_count;

          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
          shader_code_.push_back(system_temps_subroutine_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
          shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
          shader_code_.push_back(system_temps_subroutine_);
          ++stat_.instruction_count;
        } else {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
          shader_code_.push_back(system_temps_subroutine_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
          shader_code_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(1 << i);
          ++stat_.instruction_count;
        }

        // Calculate Z/W for the current sample from the evaluated Z and W.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
            ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temps_subroutine_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(system_temps_subroutine_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
        shader_code_.push_back(system_temps_subroutine_);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;

        // Apply viewport Z range the same way as it was applied to sample 0.
        system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
            ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temps_subroutine_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(system_temps_subroutine_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      kSysConst_EDRAMDepthRangeScale_Comp, 3));
        shader_code_.push_back(cbuffer_index_system_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
        shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      kSysConst_EDRAMDepthRangeOffset_Comp, 3));
        shader_code_.push_back(cbuffer_index_system_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
        shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }

      // Add the bias to the depth of the sample.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Clamp the biased depth to the lower viewport depth bound.
      system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_EDRAMDepthRangeOffset_Comp, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMDepthRange_Vec);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Clamp the biased depth to the upper viewport depth bound.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                             ENCODE_D3D10_SB_INSTRUCTION_SATURATE(1) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Convert the depth to 24-bit - takes system_temps_subroutine_[0].x,
      // returns also in system_temps_subroutine_[0].x.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CALL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
      shader_code_.push_back(label_rov_depth_to_24bit_);
      ++stat_.instruction_count;
      ++stat_.static_flow_control_count;
    }

    // Perform depth/stencil test for the sample, get the result in bits 4
    // (passed) and 8 (new depth/stencil buffer value is different).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CALL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
    shader_code_.push_back(label_rov_depth_stencil_sample_);
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    // Write the resulting depth/stencil value in system_temps_subroutine_[0].x
    // to the sample's depth in system_temp_rov_depth_stencil_.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(system_temp_rov_depth_stencil_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temps_subroutine_);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    if (i) {
      // Shift the result bits to the correct position.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(system_temps_subroutine_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(i);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
    }

    // Add the result in system_temps_subroutine_[0].y to
    // system_temp_rov_params_.x. Bits 0:3 will be cleared in case of test
    // failure (only doing this for covered samples), bits 4:7 will be added if
    // need to defer writing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_XOR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temps_subroutine_);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Close the sample conditional.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Go to the next sample (samples are at +0, +80, +1, +81, so need to do
    // +80, -79, +80 and -81 after each sample).
    system_constants_used_ |= 1ull
                              << kSysConst_EDRAMResolutionSquareScale_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back((i & 1) ? -78 - i : 80);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
        kSysConst_EDRAMResolutionSquareScale_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMResolutionSquareScale_Vec);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_rov_params_);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
  }

  if (ROV_IsDepthStencilEarly()) {
    // Check if safe to discard the whole 2x2 quad early, without running the
    // translated pixel shader, by checking if coverage is 0 in all pixels in
    // the quad and if there are no samples which failed the depth test, but
    // where stencil was modified and needs to be written in the end. Must
    // reject at 2x2 quad granularity because texture fetches need derivatives.

    // temp1.x = coverage | deferred depth/stencil write
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0b11111111);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // temp1.x = 1.0 if any sample is covered or potentially needs stencil write
    // in the end of the shader in the current pixel
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    for (uint32_t i = 0; i < 2; ++i) {
      // temp1.x = 1.0 if anything is covered in the current pixel (i = 0) /
      //           the current half of the quad (i = 1)
      // temp1.y = non-zero if anything is covered in the pixel across X
      //           (i = 0) / the two pixels across Y (i = 1)
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(i ? D3D11_SB_OPCODE_DERIV_RTY_COARSE
                                        : D3D11_SB_OPCODE_DERIV_RTX_FINE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // temp1.x = 1.0 if anything is covered in the current half of the quad
      //           (i = 0) / the whole quad (i = 1)
      // temp1.y = free
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }

    // End the shader if nothing is covered in the 2x2 quad after early
    // depth/stencil.
    // temp1.x = free
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RETC) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
  }

  // Close the large depth/stencil conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Release temp1.
  PopSystemTemp();
}

void DxbcShaderTranslator::ROV_UnpackColor(
    uint32_t rt_index, uint32_t packed_temp, uint32_t packed_temp_components,
    uint32_t color_temp, uint32_t temp1, uint32_t temp1_component,
    uint32_t temp2, uint32_t temp2_component) {
  assert_true(color_temp != packed_temp || packed_temp_components == 0);

  uint32_t temp1_mask = 1 << temp1_component;
  uint32_t temp2_mask = 1 << temp2_component;

  // Break register dependencies and initialize if there are not enough
  // components. The rest of the function will write at least RG (k_32_FLOAT and
  // k_32_32_FLOAT handled with the same default label), and if packed_temp is
  // the same as color_temp, the packed color won't be touched.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0x3F800000);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Choose the packing based on the render target's format.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // ***************************************************************************
  // k_8_8_8_8
  // k_8_8_8_8_GAMMA
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_8_8_8_8_GAMMA
                                  : ColorRenderTargetFormat::k_8_8_8_8));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    // Unpack the components.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(color_temp);
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
        D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
    shader_code_.push_back(packed_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Convert from fixed-point.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;

    // Normalize.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    // 1.0 / 255.0
    shader_code_.push_back(0x3B808081);
    shader_code_.push_back(0x3B808081);
    shader_code_.push_back(0x3B808081);
    shader_code_.push_back(0x3B808081);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    if (i) {
      for (uint32_t j = 0; j < 3; ++j) {
        ConvertPWLGamma(false, color_temp, j, color_temp, j, temp1,
                        temp1_component, temp2, temp2_component);
      }
    }

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // ***************************************************************************
  // k_2_10_10_10
  // k_2_10_10_10_AS_10_10_10_10
  // ***************************************************************************
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  // Unpack the components.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(10);
  shader_code_.push_back(10);
  shader_code_.push_back(10);
  shader_code_.push_back(2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(10);
  shader_code_.push_back(20);
  shader_code_.push_back(30);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
  shader_code_.push_back(packed_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert from fixed-point.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(color_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Normalize.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  // 1.0 / 1023.0
  shader_code_.push_back(0x3A802008);
  shader_code_.push_back(0x3A802008);
  shader_code_.push_back(0x3A802008);
  // 1.0 / 3.0
  shader_code_.push_back(0x3EAAAAAB);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // k_2_10_10_10_FLOAT
  // k_2_10_10_10_FLOAT_AS_16_16_16_16
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10_FLOAT));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  // Unpack the alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(30);
  shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   packed_temp_components, 1));
  shader_code_.push_back(packed_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert the alpha from fixed-point.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(color_temp);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Normalize the alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  // 1.0 / 3.0
  shader_code_.push_back(0x3EAAAAAB);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Process the components in reverse order because color_temp.r stores the
  // packed color which shouldn't be touched until G and B are converted if
  // packed_temp and color_temp are the same.
  for (int32_t i = 2; i >= 0; --i) {
    // Unpack the exponent to the temp.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(3);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(i * 10 + 7);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
    shader_code_.push_back(packed_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Unpack the mantissa to the result.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(7);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(i * 10);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
    shader_code_.push_back(packed_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if the number is denormalized.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Check if the number is non-zero (if the mantissa isn't zero - the
    // exponent is known to be zero at this point).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Normalize the mantissa.
    // Note that HLSL firstbithigh(x) is compiled to DXBC like:
    // `x ? 31 - firstbit_hi(x) : -1`
    // (it returns the index from the LSB, not the MSB, but -1 for zero too).
    // temp = firstbit_hi(mantissa)
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_FIRSTBIT_HI) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // temp  = 7 - (31 - firstbit_hi(mantissa))
    // Or, if expanded:
    // temp = firstbit_hi(mantissa) - 24
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(uint32_t(-24));
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // mantissa = mantissa << (7 - firstbithigh(mantissa))
    // AND 0x7F not needed after this - BFI will do it.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Get the normalized exponent.
    // exponent = 1 - (7 - firstbithigh(mantissa))
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1) |
                           ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_NEG));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // The number is zero.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Set the unbiased exponent to -124 for zero - 124 will be added later,
    // resulting in zero float32.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(uint32_t(-124));
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Close the non-zero check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Close the denormal check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Bias the exponent and move it to the correct location in f32.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1 << 23);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(124 << 23);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Combine the mantissa and the exponent.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(7);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(16);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
  }

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // k_16_16
  // k_16_16_16_16 (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16
                                  : ColorRenderTargetFormat::k_16_16));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    uint32_t color_mask = i ? 0b1111 : 0b0011;

    // Unpack the components.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, color_mask, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(16);
    shader_code_.push_back(16);
    shader_code_.push_back(16);
    shader_code_.push_back(16);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(16);
    shader_code_.push_back(0);
    shader_code_.push_back(16);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP,
        0b01010000 + packed_temp_components * 0b01010101, 1));
    shader_code_.push_back(packed_temp);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Convert from fixed-point.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, color_mask, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;

    // Normalize.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, color_mask, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    // 32.0 / 32767.0
    shader_code_.push_back(0x3A800100);
    shader_code_.push_back(0x3A800100);
    shader_code_.push_back(0x3A800100);
    shader_code_.push_back(0x3A800100);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // ***************************************************************************
  // k_16_16_FLOAT
  // k_16_16_16_16_FLOAT (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16_FLOAT
                                  : ColorRenderTargetFormat::k_16_16_FLOAT));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    uint32_t color_mask = i ? 0b1111 : 0b0011;

    // Unpack the components.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, color_mask, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(16);
    shader_code_.push_back(16);
    shader_code_.push_back(16);
    shader_code_.push_back(16);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(16);
    shader_code_.push_back(0);
    shader_code_.push_back(16);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP,
        0b01010000 + packed_temp_components * 0b01010101, 1));
    shader_code_.push_back(packed_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Convert from 16-bit float.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_F16TOF32) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, color_mask, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  if (packed_temp != color_temp) {
    // Assume k_32_FLOAT or k_32_32_FLOAT for the rest.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DEFAULT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, 0b0100 + packed_temp_components * 0b0101,
        1));
    shader_code_.push_back(packed_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::ROV_PackPreClampedColor(
    uint32_t rt_index, uint32_t color_temp, uint32_t packed_temp,
    uint32_t packed_temp_components, uint32_t temp1, uint32_t temp1_component,
    uint32_t temp2, uint32_t temp2_component) {
  assert_true(color_temp != packed_temp || packed_temp_components == 0);

  uint32_t temp1_mask = 1 << temp1_component;
  uint32_t temp2_mask = 1 << temp2_component;

  // Choose the packing based on the render target's format.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // ***************************************************************************
  // k_8_8_8_8
  // k_8_8_8_8_GAMMA
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_8_8_8_8_GAMMA
                                  : ColorRenderTargetFormat::k_8_8_8_8));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    for (uint32_t j = 0; j < 4; ++j) {
      if (i && j < 3) {
        ConvertPWLGamma(true, color_temp, j, temp1, temp1_component, temp1,
                        temp1_component, temp2, temp2_component);

        // Denormalize.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
        shader_code_.push_back(temp1);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
        shader_code_.push_back(temp1);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        // 255.0
        shader_code_.push_back(0x437F0000);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      } else {
        // Denormalize.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
        shader_code_.push_back(temp1);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
        shader_code_.push_back(color_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        // 255.0
        shader_code_.push_back(0x437F0000);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }

      // Round towards the nearest even integer. Rounding towards the nearest
      // (adding +-0.5 before truncating) is giving incorrect results for depth,
      // so better to use round_ne here too.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Convert to fixed-point.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      if (j) {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
        shader_code_.push_back(temp1);
      } else {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
        shader_code_.push_back(packed_temp);
      }
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;

      // Pack the upper components.
      if (j) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
        shader_code_.push_back(packed_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(8);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(j * 8);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
        shader_code_.push_back(temp1);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
        shader_code_.push_back(packed_temp);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      }
    }

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // ***************************************************************************
  // k_2_10_10_10
  // k_2_10_10_10_AS_10_10_10_10
  // ***************************************************************************
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  for (uint32_t i = 0; i < 4; ++i) {
    // Denormalize.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    // 1023.0 or 3.0
    shader_code_.push_back(i < 3 ? 0x447FC000 : 0x40400000);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Round towards the nearest even integer. Rounding towards the nearest
    // (adding +-0.5 before truncating) is giving incorrect results for depth,
    // so better to use round_ne here too.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;

    // Convert to fixed-point.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    if (i) {
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
      shader_code_.push_back(temp1);
    } else {
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
      shader_code_.push_back(packed_temp);
    }
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.conversion_instruction_count;

    // Pack the upper components.
    if (i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
      shader_code_.push_back(packed_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(i < 3 ? 10 : 2);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(i * 10);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
      shader_code_.push_back(packed_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }
  }

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // k_2_10_10_10_FLOAT
  // k_2_10_10_10_FLOAT_AS_16_16_16_16
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10_FLOAT));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  for (uint32_t i = 0; i < 3; ++i) {
    // Check if the number is too small to be represented as normalized 7e3.
    // temp2 = f32 < 0x3E800000
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp2_mask, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0x3E800000);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Handle denormalized numbers separately.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp2_component, 1));
    shader_code_.push_back(temp2);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // temp2 = f32 >> 23
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp2_mask, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(23);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // temp2 = 125 - (f32 >> 23)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp2_mask, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(125);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp2_component, 1) |
                           ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_NEG));
    shader_code_.push_back(temp2);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Don't allow the shift to overflow, since in DXBC the lower 5 bits of the
    // shift amount are used.
    // temp2 = min(125 - (f32 >> 23), 24)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMIN) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp2_mask, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp2_component, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(24);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // biased_f32 = (f32 & 0x7FFFFF) | 0x800000
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(9);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(23);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // biased_f32 = ((f32 & 0x7FFFFF) | 0x800000) >> min(125 - (f32 >> 23), 24)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp2_component, 1));
    shader_code_.push_back(temp2);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Not denormalized?
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Bias the exponent.
    // biased_f32 = f32 + 0xC2000000
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(color_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0xC2000000u);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Close the denormal check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Build the 7e3 number.
    // temp2 = (biased_f32 >> 16) & 1
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp2_mask, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(16);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // f10 = biased_f32 + 0x7FFF
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0x7FFF);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // f10 = biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp2_component, 1));
    shader_code_.push_back(temp2);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // f10 = ((biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)) >> 16) & 0x3FF
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    if (i) {
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
      shader_code_.push_back(temp1);
    } else {
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
      shader_code_.push_back(packed_temp);
    }
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(10);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(16);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     temp1_component, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Pack the upper components.
    if (i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
      shader_code_.push_back(packed_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(10);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(i * 10);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components, 1));
      shader_code_.push_back(packed_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }
  }

  // Denormalize the alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(color_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  // 3.0
  shader_code_.push_back(0x40400000);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Round the alpha towards the nearest even integer. Rounding towards the
  // nearest (adding +-0.5 before truncating) is giving incorrect results for
  // depth, so better to use round_ne here too.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   temp1_component, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Convert the alpha to fixed-point.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   temp1_component, 1));
  shader_code_.push_back(temp1);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Pack the alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, 1 << packed_temp_components, 1));
  shader_code_.push_back(packed_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(30);
  shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   temp1_component, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   packed_temp_components, 1));
  shader_code_.push_back(packed_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // k_16_16
  // k_16_16_16_16 (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16
                                  : ColorRenderTargetFormat::k_16_16));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    for (uint32_t j = 0; j < (uint32_t(2) << i); ++j) {
      // Denormalize.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
      shader_code_.push_back(color_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      // 32767.0 / 32.0
      shader_code_.push_back(0x447FFE00);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Round towards the nearest even integer. Rounding towards the nearest
      // (adding +-0.5 before truncating) is giving incorrect results for depth,
      // so better to use round_ne here too.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
      shader_code_.push_back(temp1);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      // Convert to fixed-point.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      if (j & 1) {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
        shader_code_.push_back(temp1);
      } else {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP,
            1 << (packed_temp_components + (j >> 1)), 1));
        shader_code_.push_back(packed_temp);
      }
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
      shader_code_.push_back(temp1);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;

      // Pack green or alpha.
      if (j & 1) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP,
            1 << (packed_temp_components + (j >> 1)), 1));
        shader_code_.push_back(packed_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(16);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(16);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
        shader_code_.push_back(temp1);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components + (j >> 1), 1));
        shader_code_.push_back(packed_temp);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      }
    }

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // ***************************************************************************
  // k_16_16_FLOAT
  // k_16_16_16_16_FLOAT (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16_FLOAT
                                  : ColorRenderTargetFormat::k_16_16_FLOAT));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    for (uint32_t j = 0; j < (uint32_t(2) << i); ++j) {
      // Convert to 16-bit float.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_F32TOF16) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      if (j & 1) {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_mask, 1));
        shader_code_.push_back(temp1);
      } else {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP,
            1 << (packed_temp_components + (j >> 1)), 1));
        shader_code_.push_back(packed_temp);
      }
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
      shader_code_.push_back(color_temp);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;

      // Pack green or alpha.
      if (j & 1) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP,
            1 << (packed_temp_components + (j >> 1)), 1));
        shader_code_.push_back(packed_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(16);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(16);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, temp1_component, 1));
        shader_code_.push_back(temp1);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, packed_temp_components + (j >> 1), 1));
        shader_code_.push_back(packed_temp);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      }
    }

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  if (packed_temp != color_temp) {
    // Assume k_32_FLOAT or k_32_32_FLOAT for the rest.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DEFAULT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, 0b11 << packed_temp_components, 1));
    shader_code_.push_back(packed_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, 0b0100 << (packed_temp_components * 2), 1));
    shader_code_.push_back(color_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::ROV_HandleColorBlendFactorCases(
    uint32_t src_temp, uint32_t dst_temp, uint32_t factor_temp) {
  // kOne.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOne));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kSrcColor

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kSrcColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  if (factor_temp != src_temp) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(src_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusSrcColor

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusSrcColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(src_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kSrcAlpha

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kSrcAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(src_temp);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusSrcAlpha

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusSrcAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(src_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kDstColor

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kDstColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  if (factor_temp != dst_temp) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(dst_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusDstColor

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusDstColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(dst_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kDstAlpha

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kDstAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(dst_temp);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusDstAlpha

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusDstAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(dst_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Factors involving the constant.
  system_constants_used_ |= 1ull << kSysConst_EDRAMBlendConstant_Index;

  // kConstantColor

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kConstantColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusConstantColor

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusConstantColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSwizzleXYZW, 3) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kConstantAlpha

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kConstantAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusConstantAlpha

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusConstantAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
                             D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 3) |
                         ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kSrcAlphaSaturate

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kSrcAlphaSaturate));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(dst_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(src_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(factor_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kZero default.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DEFAULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::ROV_HandleAlphaBlendFactorCases(
    uint32_t src_temp, uint32_t dst_temp, uint32_t factor_temp,
    uint32_t factor_component) {
  uint32_t factor_mask = 1 << factor_component;

  // kOne, kSrcAlphaSaturate.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOne));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kSrcAlphaSaturate));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kSrcColor, kSrcAlpha.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kSrcColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kSrcAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  if (factor_temp != src_temp || factor_component != 3) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(src_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusSrcColor, kOneMinusSrcAlpha.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusSrcColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusSrcAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(src_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kDstColor, kDstAlpha.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kDstColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kDstAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  if (factor_temp != dst_temp || factor_component != 3) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
    shader_code_.push_back(factor_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(dst_temp);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusDstColor, kOneMinusDstAlpha.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusDstColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusDstAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(dst_temp);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Factors involving the constant.
  system_constants_used_ |= 1ull << kSysConst_EDRAMBlendConstant_Index;

  // kConstantColor, kConstantAlpha.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kConstantColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kConstantAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kOneMinusConstantColor, kOneMinusConstantAlpha.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusConstantColor));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(uint32_t(BlendFactor::kOneMinusConstantAlpha));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 3) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMBlendConstant_Vec);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // kZero default.

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DEFAULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, factor_mask, 1));
  shader_code_.push_back(factor_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_ApplyColorExpBias(
    uint32_t rt_index) {
  if (!writes_color_target(rt_index)) {
    return;
  }
  // The constant contains 2.0^bias.
  system_constants_used_ |= 1ull << kSysConst_ColorExpBias_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(system_temps_color_[rt_index]);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_color_[rt_index]);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_ColorExpBias_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs_AlphaToCoverage() {
  if (!writes_color_target(0)) {
    return;
  }

  // Refer to CompletePixelShader_ROV_AlphaToCoverage for the description of the
  // alpha to coverage pattern used.

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
  shader_code_.push_back(system_temps_color_[0]);
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
  if (!writes_any_color_target()) {
    return;
  }

  // Check if this sample needs to be discarded by alpha to coverage.
  CompletePixelShader_WriteToRTVs_AlphaToCoverage();

  // Get the write mask as components, and also apply the exponent bias after
  // alpha to coverage because it needs the unbiased alpha from the shader.
  uint32_t guest_rt_mask = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!writes_color_target(i)) {
      continue;
    }
    guest_rt_mask |= 1 << i;
    CompletePixelShader_ApplyColorExpBias(i);
  }

  // Convert to gamma space - this is incorrect, since it must be done after
  // blending on the Xbox 360, but this is just one of many blending issues in
  // the RTV path.
  uint32_t gamma_temp = PushSystemTemp();
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(guest_rt_mask & (1 << i))) {
      continue;
    }

    system_constants_used_ |= 1ull << kSysConst_Flags_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(gamma_temp);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_Flags_Vec);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(kSysFlag_Color0Gamma << i);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(gamma_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    for (uint32_t j = 0; j < 3; ++j) {
      ConvertPWLGamma(true, system_temps_color_[i], j, system_temps_color_[i],
                      j, gamma_temp, 0, gamma_temp, 1);
    }

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
    shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     guest_rt_mask, 1));
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
    bool guest_rt_first = true;
    for (uint32_t j = 0; j < 4; ++j) {
      // If map.i == j, move guest color j to the temporary host color.
      if (!(guest_rt_mask & (1 << j))) {
        continue;
      }
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 guest_rt_first ? 12 : 9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(remap_movc_target_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
      shader_code_.push_back(remap_movc_mask_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temps_color_[j]);
      if (guest_rt_first) {
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        guest_rt_first = false;
      } else {
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(remap_movc_target_temp);
      }
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

void DxbcShaderTranslator::CompletePixelShader_ROV_CheckAnyCovered(
    bool check_deferred_stencil_write, uint32_t temp, uint32_t temp_component) {
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   1 << temp_component, 1));
  shader_code_.push_back(temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(check_deferred_stencil_write ? 0b11111111 : 0b1111);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RETC) |
      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_ZERO) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp_component, 1));
  shader_code_.push_back(temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;
}

void DxbcShaderTranslator::CompletePixelShader_ROV_AlphaToCoverageSample(
    uint32_t sample_index, float threshold, uint32_t temp,
    uint32_t temp_component) {
  // Check if alpha of oC0 is at or greater than the threshold.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   1 << temp_component, 1));
  shader_code_.push_back(temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_color_[0]);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&threshold));
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Keep all bits in system_temp_rov_params_.x but the ones that need to be
  // removed in case of failure (coverage and deferred depth/stencil write are
  // removed).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                   1 << temp_component, 1));
  shader_code_.push_back(temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp_component, 1));
  shader_code_.push_back(temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(~(uint32_t(0b00010001) << sample_index));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Clear the coverage for samples that have failed the test.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, temp_component, 1));
  shader_code_.push_back(temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_ROV_AlphaToCoverage() {
  // Check if alpha to coverage can be done at all in this shader.
  if (!writes_color_target(0)) {
    return;
  }

  // 1 VGPR or 1 SGPR.
  uint32_t temp = PushSystemTemp();

  // Extract the flag to check if alpha to coverage is enabled (1 SGPR).
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(temp);
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
  shader_code_.push_back(temp);
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

  // The test must effect not only the coverage bits, but also the deferred
  // depth/stencil write bits since the coverage is zeroed for samples that have
  // failed the depth/stencil test, but stencil may still require writing - but
  // if the sample is discarded by alpha to coverage, it must not be written at
  // all.

  // Check if any MSAA is enabled.
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

  // Check if MSAA is 4x or 2x.
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

  CompletePixelShader_ROV_AlphaToCoverageSample(0, 0.625f, temp, 0);
  CompletePixelShader_ROV_AlphaToCoverageSample(1, 0.375f, temp, 0);
  CompletePixelShader_ROV_AlphaToCoverageSample(2, 0.125f, temp, 0);
  CompletePixelShader_ROV_AlphaToCoverageSample(3, 0.875f, temp, 0);

  // 2x MSAA is used.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  CompletePixelShader_ROV_AlphaToCoverageSample(0, 0.25f, temp, 0);
  CompletePixelShader_ROV_AlphaToCoverageSample(1, 0.75f, temp, 0);

  // Close the 4x check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // MSAA is disabled.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  CompletePixelShader_ROV_AlphaToCoverageSample(0, 0.5f, temp, 0);

  // Close the 2x/4x check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Check if any sample is still covered.
  CompletePixelShader_ROV_CheckAnyCovered(true, temp, 0);

  // Release temp.
  PopSystemTemp();

  // Close the alpha to coverage check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV() {
  // Discard samples with alpha to coverage.
  CompletePixelShader_ROV_AlphaToCoverage();

  // 2 VGPR (at most, as temp when packing during blending) or 1 SGPR.
  uint32_t temp = PushSystemTemp();

  // Do late depth/stencil test (which includes writing) if needed or deferred
  // depth writing.
  if (ROV_IsDepthStencilEarly()) {
    // Write modified depth/stencil.
    for (uint32_t i = 0; i < 4; ++i) {
      // Get if need to write to temp1.x.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_rov_params_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(1 << (4 + i));
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Check if need to write.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Write the new depth/stencil.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
      shader_code_.push_back(ROV_GetEDRAMUAVIndex());
      shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(system_temp_rov_params_);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
      shader_code_.push_back(system_temp_rov_depth_stencil_);
      ++stat_.instruction_count;
      ++stat_.c_texture_store_instructions;

      // Close the write check.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;

      // Go to the next sample (samples are at +0, +80, +1, +81, so need to do
      // +80, -79, +80 and -81 after each sample).
      if (i < 3) {
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMResolutionSquareScale_Index;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
        shader_code_.push_back(system_temp_rov_params_);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back((i & 1) ? -78 - i : 80);
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
            kSysConst_EDRAMResolutionSquareScale_Comp, 3));
        shader_code_.push_back(cbuffer_index_system_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
        shader_code_.push_back(kSysConst_EDRAMResolutionSquareScale_Vec);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
        shader_code_.push_back(system_temp_rov_params_);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
      }
    }
  } else {
    ROV_DepthStencilTest();
  }

  // Check if any sample is still covered after depth testing and writing, skip
  // color writing completely in this case.
  CompletePixelShader_ROV_CheckAnyCovered(false, temp, 0);

  // Apply the exponent bias after alpha to coverage because it needs the
  // unbiased alpha from the shader.
  for (uint32_t i = 0; i < 4; ++i) {
    CompletePixelShader_ApplyColorExpBias(i);
  }

  // Write color values.
  for (uint32_t i = 0; i < 4; ++i) {
    if (!writes_color_target(i)) {
      continue;
    }

    uint32_t keep_mask_vec = kSysConst_EDRAMRTKeepMask_Vec + (i >> 1);
    uint32_t keep_mask_component = (i & 1) * 2;

    // Check if color writing is disabled - special keep mask constant case,
    // both 32bpp parts are forced UINT32_MAX, but also check whether the shader
    // has written anything to this target at all.

    // Combine both parts of the keep mask to check if both are 0xFFFFFFFF.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, keep_mask_component, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(keep_mask_vec);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, keep_mask_component + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(keep_mask_vec);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Flip the bits so both UINT32_MAX would result in 0 - not writing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOT) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Get the bits that will be used for checking wherther the render target
    // has been written to on the taken execution path - if the write mask is
    // empty, AND zero with the test bit to always get zero.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Check if the render target was written to on the execution path.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1 << (8 + i));
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if need to write anything to the render target.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Add the EDRAM bases of the render target to system_temp_rov_params_.zw.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBaseDwordsScaled_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTBaseDwordsScaled_Vec);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Get if not blending to pack the color once for all 4 samples.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0x00010001);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Check if not blending.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Clamp the color to the render target's representable range - will be
    // packed.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
    for (uint32_t j = 0; j < 2; ++j) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(j ? D3D10_SB_OPCODE_MIN
                                        : D3D10_SB_OPCODE_MAX) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(system_temps_color_[i]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temps_color_[i]);
      shader_code_.push_back(
          EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      j ? 0b11101010 : 0b01000000, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + i);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    }

    // Pack the color once if blending.
    ROV_PackPreClampedColor(i, system_temps_color_[i], system_temps_subroutine_,
                            0, temp, 0, temp, 1);

    // Blending is enabled.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Get if the blending source color is fixed-point for clamping if it is.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(kRTFormatFlag_FixedPointColor);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if the blending source color is fixed-point and needs clamping.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Clamp the blending source color if needed.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
    for (uint32_t j = 0; j < 2; ++j) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(j ? D3D10_SB_OPCODE_MIN
                                        : D3D10_SB_OPCODE_MAX) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
      shader_code_.push_back(system_temps_color_[i]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temps_color_[i]);
      shader_code_.push_back(EncodeVectorReplicatedOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, j * 2, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + i);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    }

    // Close the fixed-point color check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Get if the blending source alpha is fixed-point for clamping if it is.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(kRTFormatFlag_FixedPointAlpha);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if the blending source alpha is fixed-point and needs clamping.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Clamp the blending source alpha if needed.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
    for (uint32_t j = 0; j < 2; ++j) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(j ? D3D10_SB_OPCODE_MIN
                                        : D3D10_SB_OPCODE_MAX) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temps_color_[i]);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temps_color_[i]);
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, j * 2 + 1, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + i);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    }

    // Close the fixed-point alpha check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Break register dependency in the color sample subroutine.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Close the blending check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Blend, mask and write all samples.
    for (uint32_t j = 0; j < 4; ++j) {
      // Get if the sample is covered.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_rov_params_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(1 << j);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Do ROP for the sample if it's covered.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CALLC) |
          ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
              D3D10_SB_INSTRUCTION_TEST_NONZERO) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
      shader_code_.push_back(label_rov_color_sample_[i]);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;

      // Go to the next sample (samples are at +0, +80, +1, +81, so need to do
      // +80, -79, +80 and -81 after each sample).
      system_constants_used_ |= 1ull
                                << kSysConst_EDRAMResolutionSquareScale_Index;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(14));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
      shader_code_.push_back(system_temp_rov_params_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(uint32_t((j & 1) ? -78 - j : 80));
      shader_code_.push_back(uint32_t(((j & 1) ? -78 - j : 80) * 2));
      shader_code_.push_back(EncodeVectorReplicatedOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
          kSysConst_EDRAMResolutionSquareScale_Comp, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMResolutionSquareScale_Vec);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_rov_params_);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
    }

    // Revert adding the EDRAM bases of the render target to
    // system_temp_rov_params_.zw.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBaseDwordsScaled_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_rov_params_);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
                               D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3) |
                           ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
    shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
        D3D10_SB_OPERAND_MODIFIER_NEG));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTBaseDwordsScaled_Vec);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Close the render target write check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Release temp.
  PopSystemTemp();
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
      shader_code_.push_back(system_temps_color_[0]);
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
    // Discard the pixel if has failed the test.
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

void DxbcShaderTranslator::CompleteShaderCode_ROV_DepthTo24BitSubroutine() {
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LABEL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
  shader_code_.push_back(label_rov_depth_to_24bit_);

  // Extract the depth format to Y. Take 1 SGPR.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_ROVDepthFloat24);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Convert according to the format. Release 1 SGPR.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // ***************************************************************************
  // 20e4 conversion begins here, using 1 VGPR.
  // CFloat24 from d3dref9.dll.
  // ***************************************************************************

  // Assuming the depth is already clamped to [0, 2) (in all places, the depth
  // is written with the saturate flag set).

  // Check if the number is too small to be represented as normalized 20e4.
  // Y = f32 < 0x38800000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x38800000);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Handle denormalized numbers separately.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Y = f32 >> 23
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(23);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Y = 113 - (f32 >> 23)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(113);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Don't allow the shift to overflow, since in DXBC the lower 5 bits of the
  // shift amount are used (otherwise 0 becomes 8).
  // Y = min(113 - (f32 >> 23), 24)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(24);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // biased_f32 = (f32 & 0x7FFFFF) | 0x800000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(9);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(23);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // biased_f32 = ((f32 & 0x7FFFFF) | 0x800000) >> min(113 - (f32 >> 23), 24)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Not denormalized?
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Bias the exponent.
  // biased_f32 = f32 + 0xC8000000
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xC8000000u);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Close the denormal check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Build the 20e4 number.
  // Y = (biased_f32 >> 3) & 1
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // f24 = biased_f32 + 3
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(3);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // f24 = biased_f32 + 3 + ((biased_f32 >> 3) & 1)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // f24 = ((biased_f32 + 3 + ((biased_f32 >> 3) & 1)) >> 3) & 0xFFFFFF
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
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
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
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
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Convert to fixed-point.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // ***************************************************************************
  // Unorm24 conversion ends here.
  // ***************************************************************************

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // End the subroutine.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
}

void DxbcShaderTranslator::
    CompleteShaderCode_ROV_DepthStencilSampleSubroutine() {
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LABEL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
  shader_code_.push_back(label_rov_depth_stencil_sample_);

  // Load the old depth/stencil value to VGPR [0].z.
  // VGPR [0].x = new depth
  // VGPR [0].z = old depth/stencil
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  ++stat_.instruction_count;
  ++stat_.texture_load_instructions;

  // Extract the old depth part to VGPR [0].w.
  // VGPR [0].x = new depth
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Get the difference between the new and the old depth, > 0 - greater, == 0 -
  // equal, < 0 - less, to VGPR [1].x.
  // VGPR [0].x = new depth
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // VGPR [1].x = depth difference
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Check if the depth is "less" or "greater or equal" to VGPR [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth difference less than 0
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // VGPR [1].x = depth difference
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ILT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Choose the passed depth function bits for "less" or for "greater" to VGPR
  // [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth function passed bits for "less" or "greater"
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // VGPR [1].x = depth difference
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_ROVDepthPassIfLess);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_ROVDepthPassIfGreater);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Do the "equal" testing to VGPR [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth function passed bits
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_ROVDepthPassIfEqual);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Mask the resulting bits with the ones that should pass to VGPR [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = masked depth function passed bits
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Set bit 0 of the result to 0 (passed) or 1 (reject) based on the result of
  // the depth test.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Extract the depth write flag to SGPR [1].x.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // SGPR [1].x = depth write mask
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_ROVDepthWrite);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // If depth writing is disabled, don't change the depth.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Create packed depth/stencil, with the stencil value unchanged at this
  // point.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Extract the stencil test bit to SGPR [0].w.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // SGPR [0].w = stencil test enabled
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_ROVStencilTest);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if stencil test is enabled.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Check the current face to get the reference and apply the read mask.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t stencil_vec =
        i ? kSysConst_EDRAMStencil_Back_Vec : kSysConst_EDRAMStencil_Front_Vec;

    // Copy the read-masked stencil reference to VGPR [0].w.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = read-masked stencil reference
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMStencil_Reference_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(stencil_vec);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMStencil_ReadMask_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(stencil_vec);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Read-mask the old stencil value to VGPR [1].x.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = read-masked stencil reference
    // VGPR [1].x = read-masked old stencil
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMStencil_ReadMask_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(stencil_vec);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Go to the back face or close the face check.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(i ? D3D10_SB_OPCODE_ENDIF
                                      : D3D10_SB_OPCODE_ELSE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Get the difference between the new and the old stencil, > 0 - greater,
  // == 0 - equal, < 0 - less, to VGPR [0].w.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil difference
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Check if the stencil is "less" or "greater or equal" to VGPR [1].x.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil difference
  // VGPR [1].x = stencil difference less than 0
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ILT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Choose the passed depth function bits for "less" or for "greater" to VGPR
  // [0].y.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil difference
  // VGPR [1].x = stencil function passed bits for "less" or "greater"
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0b001);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0b100);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Do the "equal" testing to VGPR [0].w.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil function passed bits
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0b010);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Get the comparison function and the operations for the current face to
  // VGPR [1].x.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil function passed bits
  // VGPR [1].x = stencil function and operations
  system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencil_FuncOps_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencil_Front_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencil_FuncOps_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencil_Back_Vec);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Mask the resulting bits with the ones that should pass to VGPR [0].w (the
  // comparison function is in the low 3 bits of the constant, and only ANDing
  // 3-bit values with it, so safe not to UBFE the function).
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil test result
  // VGPR [1].x = stencil function and operations
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Choose the stencil pass operation depending on whether depth test has
  // failed.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil test result
  // VGPR [1].x = stencil function and operations
  // VGPR [1].y = pass or depth fail operation shift
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(9);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(6);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Merge the depth/stencil test results to VGPR [0].y.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil test result
  // VGPR [1].x = stencil function and operations
  // VGPR [1].y = pass or depth fail operation shift
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Choose the final operation to according to whether the stencil test has
  // passed.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil operation shift
  // VGPR [1].x = stencil function and operations
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(3);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Extract the needed stencil operation to VGPR [0].w.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = stencil operation
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(3);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Open the stencil operation switch for writing the new stencil (not caring
  // about bits 8:31) to VGPR [0].w.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Zero (1).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Replace (2).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(2);
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencil_Reference_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencil_Front_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencil_Reference_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencil_Back_Vec);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Increment/decrement and saturate (3/4).
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(3 + i);
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    // Clear the upper bits for saturation.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0xFF);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Increment/decrement.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(i ? uint32_t(-1) : 1);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Saturate.
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(i ? D3D10_SB_OPCODE_IMAX
                                      : D3D10_SB_OPCODE_IMIN) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(i ? 0 : 0xFF);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Invert (5).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(5);
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Increment/decrement and wrap (6/7).
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CASE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(6 + i);
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;

    // Increment/decrement.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(i ? uint32_t(-1) : 1);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Keep (0).
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DEFAULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Close the new stencil switch.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = unmasked new stencil
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Select the stencil write mask for the face to VGPR [1].x.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = unmasked new stencil
  // VGPR [1].x = stencil write mask
  system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1));
  shader_code_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencil_WriteMask_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencil_Front_Vec);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_EDRAMStencil_WriteMask_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMStencil_Back_Vec);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Apply the write mask to the new stencil, also dropping the upper 24 bits.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = masked new stencil
  // VGPR [1].x = stencil write mask
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Invert the write mask for keeping the old stencil and the depth bits to
  // VGPR [1].x.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = masked new stencil
  // VGPR [1].x = inverted stencil write mask
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Remove the bits that will be replaced from the new combined depth/stencil.
  // VGPR [0].x = masked new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = masked new stencil
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Merge the old and the new stencil.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = old depth/stencil
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Close the stencil test check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Check if the depth/stencil has failed not to modify the depth if it has.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // If the depth/stencil test has failed, don't change the depth.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Close the depth/stencil failure check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Check if need to write - if depth/stencil is different - to VGPR [0].z.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = whether depth/stencil has changed
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_INE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Check if need to write.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  bool depth_stencil_early = ROV_IsDepthStencilEarly();

  if (depth_stencil_early) {
    // Get if early depth/stencil write is enabled to SGPR [0].z.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // SGPR [0].z = whether early depth/stencil write is enabled
    system_constants_used_ |= 1ull << kSysConst_Flags_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_Flags_Vec);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(kSysFlag_ROVDepthStencilEarlyWrite);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if need to write early.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temps_subroutine_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
  }

  // Write the new depth/stencil.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.c_texture_store_instructions;

  if (depth_stencil_early) {
    // Need to still run the shader to know whether to write the depth/stencil
    // value.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Set bit 4 of the result if need to write later (after checking if the
    // sample is not discarded by a kill instruction, alphatest or
    // alpha-to-coverage).
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure, deferred write bits
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1 << 4);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Close the early depth/stencil check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }

  // Close the write check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // End the subroutine.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
}

void DxbcShaderTranslator::CompleteShaderCode_ROV_ColorSampleSubroutine(
    uint32_t rt_index) {
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LABEL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_LABEL, 1));
  shader_code_.push_back(label_rov_color_sample_[rt_index]);

  uint32_t keep_mask_vec = kSysConst_EDRAMRTKeepMask_Vec + (rt_index >> 1);
  uint32_t keep_mask_component = (rt_index & 1) * 2;
  uint32_t keep_mask_swizzle = (rt_index & 1) ? 0b1110 : 0b0100;

  // ***************************************************************************
  // Checking if color loading must be done - if any component needs to be kept
  // or if blending is enabled.
  // ***************************************************************************

  // Check if need to keep any components to SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - whether any components must be kept (OR of keep masks).
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, keep_mask_component, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(keep_mask_vec);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, keep_mask_component + 1, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(keep_mask_vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Blending isn't done if it's 1 * source + 0 * destination. But since the
  // previous color also needs to be loaded if any original components need to
  // be kept, force the blend control to something with blending in this case
  // in SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - blending mode used to check if need to load.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Get if the blend control requires loading the color to SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - whether need to load the color.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_INE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x00010001);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Check if need to do something with the previous color.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // ***************************************************************************
  // Loading the previous color to SGPR [0].zw.
  // ***************************************************************************

  // Get if the format is 64bpp to SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - whether the render target is 64bpp.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kRTFormatFlag_64bpp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the format is 64bpp.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Load the lower 32 bits of the 64bpp color to VGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPR [0].z - lower 32 bits of the packed color.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  ++stat_.instruction_count;
  ++stat_.texture_load_instructions;

  // Get the address of the upper 32 bits of the color to VGPR [0].w.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPR [0].z - lower 32 bits of the packed color.
  // VGPR [0].w - address of the upper 32 bits of the packed color.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Load the upper 32 bits of the 64bpp color to VGPR [0].w.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPRs [0].zw - packed destination color/alpha.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  ++stat_.instruction_count;
  ++stat_.texture_load_instructions;

  // The color is 32bpp.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Load the 32bpp color to VGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPR [0].z - packed 32bpp destination color.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  ++stat_.instruction_count;
  ++stat_.texture_load_instructions;

  // Break register dependency in VGPR [0].w if the color is 32bpp.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPRs [0].zw - packed destination color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Close the color load check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Get if blending is enabled to SGPR [1].x.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPRs [0].zw - packed destination color/alpha.
  // SGPR [1].x - whether blending is enabled.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_INE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x00010001);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Check if need to blend.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // VGPRs [0].zw - packed destination color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Now, when blending is enabled, registers [0].xy are used as scratch.

  // Unpack the destination color to VGPRs [1].xyzw, using [0].xy as temps. The
  // destination color never needs clamping because out-of-range values can't be
  // loaded.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  ROV_UnpackColor(rt_index, system_temps_subroutine_, 2,
                  system_temps_subroutine_ + 1, system_temps_subroutine_, 0,
                  system_temps_subroutine_, 1);

  // ***************************************************************************
  // Color blending.
  // ***************************************************************************

  // Extract the color min/max bit to SGPR [0].x.
  // SGPR [0].x - whether min/max should be used for color.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << (5 + 1));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if need to do min/max for color.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Extract the color min (0) or max (1) bit to SGPR [0].x.
  // SGPR [0].x - whether min or max should be used for color.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << 5);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if need to do min or max for color.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  for (uint32_t i = 0; i < 2; ++i) {
    if (i) {
      // Need to do min.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }

    // Do min/max of the colors without applying the factors to VGPRs [1].xyz.
    // VGPRs [0].zw - packed destination color/alpha.
    // VGPRs [1].xyzw - blended color, destination alpha.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_color_[rt_index]);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the min or max check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Need to do blend colors with the factors.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the source color factor to SGPR [0].x.
  // SGPR [0].x - source color factor index.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back((1 << 5) - 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the source color factor is not zero - if it is, the source must be
  // ignored completely, and Infinity and NaN in it shouldn't affect blending.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Open the switch for choosing the source color blend factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Write the source color factor to VGPRs [2].xyz.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - unclamped source color factor.
  ROV_HandleColorBlendFactorCases(system_temps_color_[rt_index],
                                  system_temps_subroutine_ + 1,
                                  system_temps_subroutine_ + 2);

  // Close the source color factor switch.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Get if the render target color is fixed-point and the source color factor
  // needs clamping to SGPR [0].x.
  // SGPR [0].x - whether color is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - unclamped source color factor.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kRTFormatFlag_FixedPointColor);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the source color factor needs clamping.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the source color factor in VGPRs [2].xyz.
  // SGPR [0].x - whether color is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color factor.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(system_temps_subroutine_ + 2);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_subroutine_ + 2);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the source color factor clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Apply the factor to the source color.
  // SGPR [0].x - whether color is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - unclamped source color part without addition sign.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_color_[rt_index]);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Check if the source color part needs clamping after the multiplication.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - unclamped source color part without addition sign.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the source color part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part without addition sign.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(system_temps_subroutine_ + 2);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_subroutine_ + 2);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the source color part clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the source color sign to SGPR [0].x.
  // SGPR [0].x - source color sign as zero for 1 and non-zero for -1.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part without addition sign.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << (5 + 2));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Apply the source color sign.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // The source color factor is zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Write zero to the source color part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Close the source color factor zero check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the destination color factor to SGPR [0].x.
  // SGPR [0].x - destination color factor index.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(5);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(8);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the destination color factor is not zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Open the switch for choosing the destination color blend factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Write the destination color factor to VGPRs [3].xyz.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  // VGPRs [3].xyz - unclamped destination color factor.
  ROV_HandleColorBlendFactorCases(system_temps_color_[rt_index],
                                  system_temps_subroutine_ + 1,
                                  system_temps_subroutine_ + 3);

  // Close the destination color factor switch.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Get if the render target color is fixed-point and the destination color
  // factor needs clamping to SGPR [0].x.
  // SGPR [0].x - whether color is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  // VGPRs [3].xyz - unclamped destination color factor.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kRTFormatFlag_FixedPointColor);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the destination color factor needs clamping.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the destination color factor in VGPRs [3].xyz.
  // SGPR [0].x - whether color is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - destination color/alpha.
  // VGPRs [2].xyz - source color part.
  // VGPRs [3].xyz - destination color factor.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(system_temps_subroutine_ + 3);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_subroutine_ + 3);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the destination color factor clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Apply the factor to the destination color in VGPRs [1].xyz.
  // SGPR [0].x - whether color is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - unclamped destination color part without addition sign.
  // VGPR [1].w - destination alpha.
  // VGPRs [2].xyz - source color part.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 3);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Check if the destination color part needs clamping after the
  // multiplication.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - unclamped destination color part without addition sign.
  // VGPR [1].w - destination alpha.
  // VGPRs [2].xyz - source color part.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the destination color part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - destination color part without addition sign.
  // VGPR [1].w - destination alpha.
  // VGPRs [2].xyz - source color part.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the destination color part clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the destination color sign to SGPR [0].x.
  // SGPR [0].x - destination color sign as zero for 1 and non-zero for -1.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - destination color part without addition sign.
  // VGPR [1].w - destination alpha.
  // VGPRs [2].xyz - source color part.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << 5);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Select the sign for destination multiply-add as 1.0 or -1.0 to SGPR [0].x.
  // SGPR [0].x - destination color sign as float.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - destination color part without addition sign.
  // VGPR [1].w - destination alpha.
  // VGPRs [2].xyz - source color part.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000u);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Perform color blending to VGPRs [1].xyz.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - unclamped blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // The destination color factor is zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Write the source color part without applying the destination color.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - unclamped blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Close the destination color factor zero check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Clamp the color in VGPRs [1].xyz before packing.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the color min/max enabled check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // Alpha blending.
  // ***************************************************************************

  // Extract the alpha min/max bit to SGPR [0].x.
  // SGPR [0].x - whether min/max should be used for alpha.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << (21 + 1));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if need to do min/max for alpha.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Extract the alpha min (0) or max (1) bit to SGPR [0].x.
  // SGPR [0].x - whether min or max should be used for alpha.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << 21);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if need to do min or max for alpha.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  for (uint32_t i = 0; i < 2; ++i) {
    if (i) {
      // Need to do min.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }

    // Do min/max of the alphas without applying the factors to VGPRs [1].xyz.
    // VGPRs [0].zw - packed destination color/alpha.
    // VGPRs [1].xyz - blended color/alpha.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temps_color_[rt_index]);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the min or max check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Need to do blend colors with the factors.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the source alpha factor to SGPR [0].x.
  // SGPR [0].x - source alpha factor index.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(5);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(16);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the source alpha factor is not zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Open the switch for choosing the source alpha blend factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Write the source alpha factor to VGPR [0].x.
  // VGPR [0].x - unclamped source alpha factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  ROV_HandleAlphaBlendFactorCases(system_temps_color_[rt_index],
                                  system_temps_subroutine_ + 1,
                                  system_temps_subroutine_, 0);

  // Close the source alpha factor switch.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Get if the render target alpha is fixed-point and the source alpha factor
  // needs clamping to SGPR [0].y.
  // VGPR [0].x - unclamped source alpha factor.
  // SGPR [0].y - whether alpha is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kRTFormatFlag_FixedPointAlpha);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the source alpha factor needs clamping.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the source alpha factor in VGPR [0].x.
  // VGPR [0].x - source alpha factor.
  // SGPR [0].y - whether alpha is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2 + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the source alpha factor clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Apply the factor to the source alpha.
  // VGPR [0].x - unclamped source alpha part without addition sign.
  // SGPR [0].y - whether alpha is fixed-point.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_color_[rt_index]);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Check if the source alpha part needs clamping after the multiplication.
  // VGPR [0].x - unclamped source alpha part without addition sign.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the source alpha part.
  // VGPR [0].x - source alpha part without addition sign.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2 + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the source alpha part clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the source alpha sign to SGPR [0].y.
  // VGPR [0].x - source alpha part without addition sign.
  // SGPR [0].y - source alpha sign as zero for 1 and non-zero for -1.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << (21 + 2));
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Apply the source alpha sign.
  // VGPR [0].x - source alpha part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // The source alpha factor is zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Write zero to the source alpha part.
  // VGPR [0].x - source alpha part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Close the source alpha factor zero check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the destination alpha factor to SGPR [1].y.
  // VGPR [0].x - source alpha part.
  // SGPR [0].y - destination alpha factor index.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(5);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(24);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the destination alpha factor is not zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Open the switch for choosing the destination alpha blend factor.
  // VGPR [0].x - source alpha part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SWITCH) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Write the destination alpha factor to VGPR [0].y.
  // VGPR [0].x - source alpha part.
  // VGPR [0].y - unclamped destination alpha factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  ROV_HandleAlphaBlendFactorCases(system_temps_color_[rt_index],
                                  system_temps_subroutine_ + 1,
                                  system_temps_subroutine_, 1);

  // Close the destination alpha factor switch.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Get if the render target alpha is fixed-point and the destination alpha
  // factor needs clamping to SGPR [2].x.
  // VGPR [0].x - source alpha part.
  // VGPR [0].y - unclamped destination alpha factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  // SGPR [2].x - whether alpha is fixed-point.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kRTFormatFlag_FixedPointAlpha);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the destination alpha factor needs clamping.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the destination alpha factor in VGPR [0].y.
  // VGPR [0].x - source alpha part.
  // VGPR [0].y - destination alpha factor.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha.
  // SGPR [2].x - whether alpha is fixed-point.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temps_subroutine_);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2 + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the destination alpha factor clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Apply the factor to the destination alpha in VGPR [1].w.
  // VGPR [0].x - source alpha part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - unclamped destination alpha part without addition sign.
  // SGPR [2].x - whether alpha is fixed-point.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Check if the destination alpha part needs clamping after the
  // multiplication.
  // VGPR [0].x - source alpha part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - unclamped destination alpha part without addition sign.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_ + 2);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Clamp the destination alpha part.
  // VGPR [0].x - source alpha part.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha part without addition sign.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2 + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the destination alpha factor clamping check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Extract the destination alpha sign to SGPR [0].y.
  // VGPR [0].x - source alpha part.
  // SGPR [0].y - destination alpha sign as zero for 1 and non-zero for -1.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha part without addition sign.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTBlendFactorsOps_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1 << 21);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Select the sign for destination multiply-add as 1.0 or -1.0 to SGPR [0].y.
  // VGPR [0].x - source alpha part.
  // SGPR [0].y - destination alpha sign as float.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - destination alpha part without addition sign.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000u);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Perform alpha blending to VGPR [1].w.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - unclamped blended alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // The destination alpha factor is zero.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Write the source alpha part without applying the destination alpha.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyz - blended color.
  // VGPR [1].w - unclamped blended alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;

  // Close the destination alpha factor zero check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Clamp the alpha in VGPR [1].w before packing.
  // VGPRs [0].zw - packed destination color/alpha.
  // VGPRs [1].xyzw - blended color/alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
  for (uint32_t i = 0; i < 2; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                               i ? D3D10_SB_OPCODE_MIN : D3D10_SB_OPCODE_MAX) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(system_temps_subroutine_ + 1);
    shader_code_.push_back(EncodeVectorSelectOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i * 2 + 1, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMRTClamp_Vec + rt_index);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Close the alpha min/max enabled check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Pack the new color/alpha to VGPRs [0].xy, using VGPRs [2].xy as temporary.
  // VGPRs [0].xy - packed new color/alpha.
  // VGPRs [0].zw - packed old color/alpha.
  ROV_PackPreClampedColor(
      rt_index, system_temps_subroutine_ + 1, system_temps_subroutine_, 0,
      system_temps_subroutine_ + 2, 0, system_temps_subroutine_ + 2, 1);

  // Close the blending check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // Write mask application
  // ***************************************************************************

  // Apply the keep mask to the previous packed color/alpha in VGPRs [0].zw.
  // VGPRs [0].xy - packed new color/alpha.
  // VGPRs [0].zw - masked packed old color/alpha.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, keep_mask_swizzle << 4, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(keep_mask_vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Invert the keep mask into SGPRs [1].xy.
  // VGPRs [0].xy - packed new color/alpha.
  // VGPRs [0].zw - masked packed old color/alpha.
  // SGPRs [1].xy - inverted keep mask (write mask).
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, keep_mask_swizzle, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(keep_mask_vec);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Apply the write mask to the new color/alpha in VGPRs [0].xy.
  // VGPRs [0].xy - masked packed new color/alpha.
  // VGPRs [0].zw - masked packed old color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temps_subroutine_ + 1);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Combine the masked colors into VGPRs [0].xy.
  // VGPRs [0].xy - packed resulting color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00000100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b00001110, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Close the previous color load check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // ***************************************************************************
  // Writing the color
  // ***************************************************************************

  // Get if the format is 64bpp to SGPR [0].z.
  // VGPRs [0].xy - packed resulting color/alpha.
  // SGPR [0].z - whether the render target is 64bpp.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, rt_index, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_EDRAMRTFormatFlags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kRTFormatFlag_64bpp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Check if the format is 64bpp.
  // VGPRs [0].xy - packed resulting color/alpha.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Store the lower 32 bits of the 64bpp color.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.c_texture_store_instructions;

  // Get the address of the upper 32 bits of the color to VGPR [0].z (can't use
  // [0].x because components when not blending, packing is done once for all
  // samples).
  // VGPRs [0].xy - packed resulting color/alpha.
  // VGPR [0].z - address of the upper 32 bits of the packed color.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(1);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Store the upper 32 bits of the 64bpp color.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temps_subroutine_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.c_texture_store_instructions;

  // The color is 32bpp.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // Store the 32bpp color.
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_UAV_TYPED) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, 0b1111, 2));
  shader_code_.push_back(ROV_GetEDRAMUAVIndex());
  shader_code_.push_back(uint32_t(UAVRegister::kEDRAM));
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(system_temp_rov_params_);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temps_subroutine_);
  ++stat_.instruction_count;
  ++stat_.c_texture_store_instructions;

  // Close the 64bpp/32bpp conditional.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  // End the subroutine.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
}

}  // namespace gpu
}  // namespace xe
