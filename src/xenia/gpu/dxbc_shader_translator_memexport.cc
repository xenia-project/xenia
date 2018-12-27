/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include <algorithm>

#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::ExportToMemory() {
  static const float k32bppMaxValuesSigned[4][4] = {
      {127.0f, 127.0f, 127.0f, 127.0f},
      {511.0f, 511.0f, 511.0f, 1.0f},
      {1023.0f, 1023.0f, 511.0f, 0.0f},
      {511.0f, 1023.0f, 1023.0f, 0.0f},
  };
  static const float k32bppMaxValuesUnsigned[4][4] = {
      {255.0f, 255.0f, 255.0f, 255.0f},
      {1023.0f, 1023.0f, 1023.0f, 3.0f},
      {2047.0f, 2047.0f, 1023.0f, 0.0f},
      {1023.0f, 2047.0f, 2047.0f, 0.0f},
  };
  static const uint32_t k32bppMasks[4][4] = {
      {255, 255, 255, 255},
      {1023, 1023, 1023, 3},
      {2047, 2047, 1023, 0},
      {1023, 2047, 2047, 0},
  };
  static const uint32_t k32bppShifts[4][3] = {
      {8, 16, 24},
      {10, 20, 30},
      {11, 22, 0},
      {10, 21, 0},
  };

  if (system_temp_memexport_written_ == UINT32_MAX) {
    // No exports in the shader.
    return;
  }

  // Allocate a register that will be used for storing:
  // - Bits for checking whether export can be done at all.
  // - Format info.
  // - Calculated element size.
  // - Endian swap bits.
  // - Element size comparison results (for choosing the store with the right
  //   write mask).
  uint32_t control_temp = PushSystemTemp();

  // Safety check if the shared memory is bound as UAV.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(control_temp);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_Flags_Comp, 3));
  shader_code_.push_back(cbuffer_index_system_constants_);
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_Flags_Vec);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(kSysFlag_SharedMemoryIsUAV);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  if (IsDxbcPixelShader()) {
    // Disable memexport in pixel shaders with supersampling since VPOS is
    // ambiguous.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(control_temp);
    if (edram_rov_used_) {
      system_constants_used_ |= 1ull
                                << kSysConst_EDRAMResolutionScaleLog2_Index;
      shader_code_.push_back(EncodeVectorSelectOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
          kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
    } else {
      system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
      // Enough to check just Y because it's scaled for both 2x and 4x.
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_SampleCountLog2_Comp + 1, 3));
      shader_code_.push_back(cbuffer_index_system_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
      shader_code_.push_back(kSysConst_SampleCountLog2_Vec);
    }
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;
  }

  // Check if memexport can be done.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(control_temp);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  for (uint32_t i = 0; i < kMaxMemExports; ++i) {
    uint32_t eA_temp = system_temps_memexport_address_[i];
    if (eA_temp == UINT32_MAX) {
      // Export not used.
      continue;
    }
    // For simplicity of access, gather actually used eM# registers for this
    // export. Zero-initialize eM_offsets because excess elements of it may be
    // accessed, for stable caching.
    uint32_t eM_temps[5], eM_offsets[5] = {}, eM_count = 0;
    for (uint32_t j = 0; j < 5; ++j) {
      uint32_t eM_temp = system_temps_memexport_data_[i][j];
      if (eM_temp == UINT32_MAX) {
        continue;
      }
      eM_temps[eM_count] = eM_temp;
      eM_offsets[eM_count] = j;
      ++eM_count;
    }
    if (eM_count == 0) {
      continue;
    }

    // Extract format info to control_temp.
    // X - color format, Y - is signed, Z - fractional/integer,
    // W - red/blue swap.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(6);
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(8);
    shader_code_.push_back(16);
    shader_code_.push_back(17);
    shader_code_.push_back(19);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(eA_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Swap red and blue if needed.
    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b11000110, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }

    // Initialize element size to 4 since there are many 32-bit formats.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(4);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Allocate a register for checking if the format is equal to different
    // values.
    uint32_t format_check_temp = PushSystemTemp();

    // Check if the format is float32 - in this case, it doesn't need any
    // conversion. Compare the format to each float32 format.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(uint32_t(ColorFormat::k_32_FLOAT));
    shader_code_.push_back(uint32_t(ColorFormat::k_32_32_FLOAT));
    shader_code_.push_back(uint32_t(ColorFormat::k_32_32_32_32_FLOAT));
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Merge to check whether the format is any-dimensional float32 into X.
    for (uint32_t j = 0; j < 2; ++j) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(format_check_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(format_check_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 + j, 1));
      shader_code_.push_back(format_check_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }

    // If the format is float32, it doesn't need any conversion.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(format_check_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Set element size to 8 for k_32_32_FLOAT or 16 for k_32_32_32_32_FLOAT.
    for (uint32_t j = 0; j < 2; ++j) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 + j, 1));
      shader_code_.push_back(format_check_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(8 << j);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(control_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }

    // If the format is not float32, do conversion and packing. Can reuse
    // format_check_temp in the `else` case.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Check if the format is float16 to convert and pack.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(uint32_t(ColorFormat::k_16_16_FLOAT));
    shader_code_.push_back(uint32_t(ColorFormat::k_16_16_16_16_FLOAT));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Merge to check whether the format is any-dimensional float16 into X.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(format_check_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // If the format is float16, convert and pack.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(format_check_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Set element size to 8 for k_16_16_16_16_FLOAT.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(8);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(control_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      // Convert to float16.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_F32TOF16) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
      // Pack a float16 vector (in the little-endian way).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(16);
      shader_code_.push_back(16);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(16);
      shader_code_.push_back(16);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b00001101, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b00001000, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }

    // If the format is not float16, do float->integer and packing. Can reuse
    // format_check_temp in the `else` case.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Check if the format is each of packed 32-bit formats, but not 16_16 (it
    // will be handled separately for simplicity).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(uint32_t(ColorFormat::k_8_8_8_8));
    shader_code_.push_back(uint32_t(ColorFormat::k_2_10_10_10));
    shader_code_.push_back(uint32_t(ColorFormat::k_10_11_11));
    shader_code_.push_back(uint32_t(ColorFormat::k_11_11_10));
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Check if the format is each of packed 32-bit formats blended as fixed16.
    uint32_t format_as16_check_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(format_as16_check_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(uint32_t(ColorFormat::k_8_8_8_8_AS_16_16_16_16));
    shader_code_.push_back(uint32_t(ColorFormat::k_2_10_10_10_AS_16_16_16_16));
    shader_code_.push_back(uint32_t(ColorFormat::k_10_11_11_AS_16_16_16_16));
    shader_code_.push_back(uint32_t(ColorFormat::k_11_11_10_AS_16_16_16_16));
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(format_as16_check_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // Release format_as16_check_temp.
    PopSystemTemp();

    // Allocate a register for format bit representation parameters.
    uint32_t format_param_temp = PushSystemTemp();

    // Denormalize, clamp and convert to integer.
    // TODO(Triang3l): Really clamp? It's GPUSURFACENUMBER_UREPEAT, not clamp,
    // but need to verify since not clamping doesn't look very safe.
    // A lot of the code is similar for both signed and unsigned. Start by
    // checking the signedness.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(control_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    for (uint32_t j = 0; j < 2; ++j) {
      if (j != 0) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
      }
      // Write the maximum integer value to format_param_temp. Default to
      // 16_16_16_16, but override if it's a different packed 32bpp format.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(format_param_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      float norm16_max = j ? 65535.0f : 32767.0f;
      shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&norm16_max));
      shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&norm16_max));
      shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&norm16_max));
      shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&norm16_max));
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      for (uint32_t k = 0; k < 4; ++k) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(format_param_temp);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
        shader_code_.push_back(format_check_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        const uint32_t* norm_max = reinterpret_cast<const uint32_t*>(
            j ? k32bppMaxValuesUnsigned[k] : k32bppMaxValuesSigned[k]);
        shader_code_.push_back(norm_max[0]);
        shader_code_.push_back(norm_max[1]);
        shader_code_.push_back(norm_max[2]);
        shader_code_.push_back(norm_max[3]);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(format_param_temp);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;
      }
      // If fractional, denormalize.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_ZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(control_temp);
      ++stat_.instruction_count;
      ++stat_.dynamic_flow_control_count;
      for (uint32_t k = 0; k < eM_count; ++k) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(eM_temps[k]);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(eM_temps[k]);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(format_param_temp);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
      for (uint32_t k = 0; k < eM_count; ++k) {
        uint32_t eM_temp = eM_temps[k];
        // Clamp to the minimum (-max for signed, 0 for unsigned).
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(j ? 10 : 8));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(eM_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(eM_temp);
        if (j != 0) {
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          shader_code_.push_back(0);
          shader_code_.push_back(0);
          shader_code_.push_back(0);
          shader_code_.push_back(0);
        } else {
          shader_code_.push_back(
              EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                          kSwizzleXYZW, 1) |
              ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
          shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
              D3D10_SB_OPERAND_MODIFIER_NEG));
          shader_code_.push_back(format_param_temp);
        }
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Clamp to the maximum.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(eM_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(eM_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(format_param_temp);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Round to the nearest integer, according to the rules of handling
        // integer formats in Direct3D.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(eM_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(eM_temp);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Convert to integer.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(j ? D3D10_SB_OPCODE_FTOU
                                          : D3D10_SB_OPCODE_FTOI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(eM_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(eM_temp);
        ++stat_.instruction_count;
        ++stat_.conversion_instruction_count;
      }
      if (j == 0) {
        // Drop sign extension bits.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(format_param_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(65535);
        shader_code_.push_back(65535);
        shader_code_.push_back(65535);
        shader_code_.push_back(65535);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        for (uint32_t k = 0; k < 4; ++k) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(format_param_temp);
          shader_code_.push_back(
              EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, k, 1));
          shader_code_.push_back(format_check_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          const uint32_t* mask = k32bppMasks[k];
          shader_code_.push_back(mask[0]);
          shader_code_.push_back(mask[1]);
          shader_code_.push_back(mask[2]);
          shader_code_.push_back(mask[3]);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(format_param_temp);
          ++stat_.instruction_count;
          ++stat_.movc_instruction_count;
        }
        for (uint32_t k = 0; k < eM_count; ++k) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(eM_temps[k]);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(eM_temps[k]);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(format_param_temp);
          ++stat_.instruction_count;
          ++stat_.uint_instruction_count;
        }
      }
    }
    // Close the signedness check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Shift each component into its location before packing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1110, 1));
    shader_code_.push_back(format_param_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(16);
    shader_code_.push_back(0);
    shader_code_.push_back(16);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
    for (uint32_t j = 0; j < 4; ++j) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1110, 1));
      shader_code_.push_back(format_param_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
      shader_code_.push_back(format_check_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      const uint32_t* shift = k32bppShifts[j];
      shader_code_.push_back(0);
      shader_code_.push_back(shift[0]);
      shader_code_.push_back(shift[1]);
      shader_code_.push_back(shift[2]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(format_param_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }
    for (uint32_t j = 0; j < eM_count; ++j) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1110, 1));
      shader_code_.push_back(eM_temps[j]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temps[j]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(format_param_temp);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
    }

    // Release format_param_temp.
    PopSystemTemp();

    // Merge XZ and YW into XY - this is common for both 16_16/16_16_16_16 and
    // other formats.
    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b00001000, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b00001101, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }

    // Check if the format is norm16 since it needs its own packing.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(uint32_t(ColorFormat::k_16_16));
    shader_code_.push_back(uint32_t(ColorFormat::k_16_16_16_16));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Set element size to 8 for k_16_16_16_16.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(8);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(control_temp);
    ++stat_.instruction_count;
    ++stat_.movc_instruction_count;

    // Merge to check whether the format is any-dimensional norm16 into X.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(format_check_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(format_check_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // If the format is not norm16, merge all components into X.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(format_check_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Close the float16 check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Close the float32 check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // Release format_check_temp.
    PopSystemTemp();

    // Extract endianness into control_temp.xyz.
    // X set for 8-in-16, 16-in-32 and 8-in-128.
    // Y set for 8-in-32 and 16-in-32.
    // Z set for 8-in-64 and 8-in-128.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(control_temp);
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
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(eA_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Check if need to do 8-in-64 or 8-in-128 swap.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(control_temp);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;

    // Do 32-in-64 or 32-in-128 swap.
    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b00011011, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b10110001, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }

    // Change 8-in-64 or 8-in-128 to 8-in-32.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(1);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // Close the 8-in-64 or 8-in-128 check.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;

    // 16-in-32 is used as intermediate swapping step here rather than 8-in-32.
    // Thus 8-in-16 needs to be done for 8-in-16 (01) and 8-in-32 (10).
    // And 16-in-32 needs to be done for 8-in-32 (10) and 16-in-32 (11).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_XOR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(control_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Allocate temporary registers for swapping.
    uint32_t swap_temp1 = PushSystemTemp();
    uint32_t swap_temp2 = PushSystemTemp();

    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];

      // 8-in-16: Create the value being built in temp1.
      // ushr temp1, eM, l(8, 8, 8, 8)
      // eM: ABCD, temp1: BCD0
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(swap_temp1);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(8);
      shader_code_.push_back(8);
      shader_code_.push_back(8);
      shader_code_.push_back(8);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // 8-in-16: Insert A in Y of temp1.
      // bfi temp1, l(8, 8, 8, 8), l(8, 8, 8, 8), eM, temp1
      // eM: ABCD, temp1: BAD0
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(swap_temp1);
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
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(swap_temp1);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // 8-in-16: Create the source for C insertion in temp2.
      // ushr temp2, eM, l(16, 16, 16, 16)
      // eM: ABCD, temp1: BAD0, temp2: CD00
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(swap_temp2);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
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
      // eM: ABCD, temp1: BADC
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(swap_temp1);
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
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(swap_temp2);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(swap_temp1);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Write the 8-in-16 value to eM if needed.
      // movc eM, control.xxxx, temp1, eM
      // eM: ABCD/BADC
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(swap_temp1);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;

      // 16-in-32: Write the low 16 bits to temp1.
      // ushr temp1, eM, l(16, 16, 16, 16)
      // eM: ABCD/BADC, temp1: CD00/DC00
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_USHR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(swap_temp1);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(16);
      shader_code_.push_back(16);
      shader_code_.push_back(16);
      shader_code_.push_back(16);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // 16-in-32: Write the high 16 bits to temp1.
      // bfi temp1, l(16, 16, 16, 16), l(16, 16, 16, 16), eM, temp1
      // eM: ABCD/BADC, temp1: CDAB/DCBA
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_BFI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(swap_temp1);
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
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(swap_temp1);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      // Write the swapped value to eM.
      // movc eM, control.yyyy, temp1, eM
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(eM_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(swap_temp1);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(eM_temp);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
    }

    // Release swap_temp1 and swap_temp2.
    PopSystemTemp(2);

    // Multiply the base address by dword size, also dropping the 0x40000000
    // bit.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(eA_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(eA_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(2);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Drop the exponent in the element index.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
    shader_code_.push_back(eA_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(eA_temp);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back((1 << 23) - 1);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // Add the offset of the first written element to the base address.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(eA_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(eA_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(eA_temp);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;

    // If the first written element is not eM0, add the offset to it.
    if (eM_offsets[0] != 0) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 eM_offsets[0] == 1 ? D3D10_SB_OPCODE_IADD
                                                    : D3D10_SB_OPCODE_UMAD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 eM_offsets[0] == 1 ? 7 : 9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(eA_temp);
      if (eM_offsets[0] != 1) {
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(eM_offsets[0]);
      }
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(eA_temp);
      ++stat_.instruction_count;
      if (eM_offsets[0] == 1) {
        ++stat_.int_instruction_count;
      } else {
        ++stat_.uint_instruction_count;
      }
    }

    // If there are multiple eM# written, calculate offset of each.
    uint32_t other_addresses_temp;
    if (eM_count > 1) {
      other_addresses_temp = PushSystemTemp();
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UMAD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, (1 << (eM_count - 1)) - 1, 1));
      shader_code_.push_back(other_addresses_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      // eM_offsets[0] already added to eA.x.
      shader_code_.push_back(eM_offsets[1] - eM_offsets[0]);
      shader_code_.push_back(eM_offsets[2] - eM_offsets[0]);
      shader_code_.push_back(eM_offsets[3] - eM_offsets[0]);
      shader_code_.push_back(eM_offsets[4] - eM_offsets[0]);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(control_temp);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(eA_temp);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    } else {
      other_addresses_temp = UINT32_MAX;
    }

    // Extract the mask of eM register actually written to on the execution
    // path.
    uint32_t eM_written_temps = PushSystemTemp(false, eM_count > 4 ? 2 : 1);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, (1 << std::min(eM_count, 4u)) - 1, 1));
    shader_code_.push_back(eM_written_temps);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, i >> 2, 1));
    shader_code_.push_back(system_temp_memexport_written_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    uint32_t eM_written_base = 1u << ((i & 3) << 3);
    shader_code_.push_back(eM_written_base << eM_offsets[0]);
    shader_code_.push_back(eM_written_base << eM_offsets[1]);
    shader_code_.push_back(eM_written_base << eM_offsets[2]);
    shader_code_.push_back(eM_written_base << eM_offsets[3]);
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    if (eM_count > 4) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(eM_written_temps + 1);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i >> 2, 1));
      shader_code_.push_back(system_temp_memexport_written_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(eM_written_base << eM_offsets[4]);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
    }

    // Check which Store (store_raw write mask) should be used according to the
    // element size. Compare the element size to 16 and 8 into
    // control_temp.xy.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
    shader_code_.push_back(control_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(16);
    shader_code_.push_back(8);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;

    // Actually store the data.
    // if (element_size == 16) {
    //   Store4 (j = 0)
    // } else {
    //   if (element_size == 8) {
    //     Store2 (j = 1)
    //   } else {
    //     Store (j = 2)
    //   }
    // }
    for (uint32_t j = 0; j < 3; ++j) {
      if (j < 2) {
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                               ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                   D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
        shader_code_.push_back(control_temp);
        ++stat_.instruction_count;
        ++stat_.dynamic_flow_control_count;
      }

      // 0b1111 for j = 0, 0b0011 for j = 1, 0b0001 for j = 2.
      uint32_t store_mask = (1 << (1 << (2 - j))) - 1;
      uint32_t store_swizzle = kSwizzleXYZW & ((1 << ((1 << (2 - j)) * 2)) - 1);

      for (uint32_t k = 0; k < eM_count; ++k) {
        // Check if the eM was actually written to on the execution path.
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                               ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                   D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, k & 3, 1));
        shader_code_.push_back(eM_written_temps + (k >> 2));
        ++stat_.instruction_count;
        ++stat_.dynamic_flow_control_count;

        // Store.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_STORE_RAW) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, store_mask, 2));
        shader_code_.push_back(0);
        shader_code_.push_back(uint32_t(UAVRegister::kSharedMemory));
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, k ? (k - 1) : 0, 1));
        shader_code_.push_back(k ? other_addresses_temp : eA_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, store_swizzle, 1));
        shader_code_.push_back(eM_temps[k]);
        ++stat_.instruction_count;
        ++stat_.c_texture_store_instructions;

        // Close the eM write check.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
      }

      if (j < 2) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
      }
    }
    for (uint32_t j = 0; j < 2; ++j) {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
      ++stat_.instruction_count;
    }

    // Release eM_written_temps.
    PopSystemTemp(eM_count > 4 ? 2 : 1);

    if (other_addresses_temp != UINT32_MAX) {
      PopSystemTemp();
    }
  }

  // Close the memexport possibility check.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));

  // Release control_temp.
  PopSystemTemp();
}

}  // namespace gpu
}  // namespace xe
