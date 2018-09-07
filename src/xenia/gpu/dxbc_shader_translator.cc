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
#include <cstring>

#include "third_party/dxbc/DXBCChecksum.h"
#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

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

DxbcShaderTranslator::DxbcShaderTranslator() {
  // Don't allocate again and again for the first shader.
  shader_code_.reserve(8192);
  shader_object_.reserve(16384);
}
DxbcShaderTranslator::~DxbcShaderTranslator() = default;

void DxbcShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  shader_code_.clear();

  rdef_constants_used_ = 0;
  system_temp_count_current_ = 0;
  system_temp_count_max_ = 0;
  writes_depth_ = false;

  std::memset(&stat_, 0, sizeof(stat_));
}

uint32_t DxbcShaderTranslator::PushSystemTemp(bool zero) {
  uint32_t register_index = system_temp_count_current_;
  if (!uses_register_dynamic_addressing()) {
    // Guest shader registers first if they're not in x0.
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
  if (uses_register_dynamic_addressing()) {
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
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXXXX, 1));
  shader_code_.push_back(kVSInVertexIndexRegister);
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
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysVertexIndexEndian);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_VertexIndexEndian_Comp,
      3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(reg);
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysVertexBaseIndex);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_VertexBaseIndex_Comp,
      3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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

  if (uses_register_dynamic_addressing()) {
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
    shader_code_.push_back(kVSOutInterpolatorRegister + i);
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
  shader_code_.push_back(kVSOutPointParametersRegister);
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
  // Copy interpolants to GPRs.
  uint32_t interpolator_count = std::min(kInterpolatorCount, register_count());
  if (uses_register_dynamic_addressing()) {
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
      shader_code_.push_back(kPSInInterpolatorRegister + i);
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
      shader_code_.push_back(kPSInInterpolatorRegister + i);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
    }
  }

  // TODO(Triang3l): ps_param_gen.

  // Initialize color indexable temporary registers so they have a defined value
  // in case the shader doesn't write to all used outputs on all execution
  // paths. This must be done via r#.
  uint32_t zero_temp_register = PushSystemTemp(true);
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b1111, 2));
    shader_code_.push_back(GetColorIndexableTemp());
    shader_code_.push_back(i);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(zero_temp_register);
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
  }
  PopSystemTemp();
}

void DxbcShaderTranslator::StartTranslation() {
  // Request global system temporary variables.
  system_temp_pv_ = PushSystemTemp(true);
  system_temp_ps_pc_p0_a0_ = PushSystemTemp(true);
  system_temp_aL_ = PushSystemTemp(true);
  system_temp_loop_count_ = PushSystemTemp(true);
  if (is_vertex_shader()) {
    system_temp_position_ = PushSystemTemp(true);
  }

  // Write stage-specific prologue.
  if (is_vertex_shader()) {
    StartVertexShader();
  } else if (is_pixel_shader()) {
    StartPixelShader();
  }
}

void DxbcShaderTranslator::CompleteVertexShader() {
  // TODO(Triang3l): vtx_fmt, disabled viewport, half pixel offset.
  // Write the position to the output.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
  shader_code_.push_back(kVSOutPositionRegister);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(system_temp_position_);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
}

void DxbcShaderTranslator::CompletePixelShader() {
  uint32_t color_indexable_temp = GetColorIndexableTemp();

  // Allocate temporary registers for alpha testing, color indexable temp
  // load/store and storing the color output map.
  uint32_t temp1 = PushSystemTemp();
  uint32_t temp2 = PushSystemTemp();

  // TODO(Triang3l): Alpha testing.

  // Apply color exponent bias (need to use a temporary register because
  // indexable temps are load/store).
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysColorExpBias);
  for (uint32_t i = 0; i < 4; ++i) {
    // Load the color from the indexable temp.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, kSwizzleXYZW, 2));
    shader_code_.push_back(color_indexable_temp);
    shader_code_.push_back(i);
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
    // Multiply by the exponent bias (the constant contains 2.0^b).
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(temp1);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_ColorExpBias_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // Store the biased color back to the indexable temp.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b1111, 2));
    shader_code_.push_back(color_indexable_temp);
    shader_code_.push_back(i);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
  }

  // Remap guest render target indices to host since because on the host, the
  // indices of the bound render targets are consecutive.
  // temp1 = xe_color_output_map
  // temp2 = oC[temp1.x]
  // SV_Target0 = temp2
  // temp2 = oC[temp1.y]
  // SV_Target1 = temp2
  // temp2 = oC[temp1.w]
  // SV_Target2 = temp2
  // temp2 = oC[temp1.z]
  // SV_Target3 = temp2
  //
  // Load the constant to a temporary register so it can be used for relative
  // addressing.
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysColorOutputMap);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
  shader_code_.push_back(temp1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_ColorOutputMap_Vec);
  ++stat_.instruction_count;
  ++stat_.mov_instruction_count;
  // Do the remapping.
  for (uint32_t i = 0; i < 4; ++i) {
    // Copy to r# because indexable temps must be loaded/stored via r#.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(temp2);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, kSwizzleXYZW, 2,
        D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
        D3D10_SB_OPERAND_INDEX_RELATIVE));
    shader_code_.push_back(color_indexable_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
    shader_code_.push_back(temp1);
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
    // Write to o#.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
    shader_code_.push_back(i);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(temp2);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  // Free the temporary registers.
  PopSystemTemp(2);
}

void DxbcShaderTranslator::CompleteShaderCode() {
  // Release the following system temporary values so epilogue can reuse them:
  // - system_temp_pv_.
  // - system_temp_ps_pc_p0_a0_.
  // - system_temp_aL_.
  // - system_temp_loop_count_.
  PopSystemTemp(4);

  // Write stage-specific epilogue.
  if (is_vertex_shader()) {
    CompleteVertexShader();
  } else if (is_pixel_shader()) {
    CompletePixelShader();
  }

  // Return from `main`.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  ++stat_.static_flow_control_count;
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

void DxbcShaderTranslator::LoadDxbcSourceOperand(
    const InstructionOperand& operand, DxbcSourceOperand& dxbc_operand) {
  // Initialize the values to their defaults.
  dxbc_operand.type = DxbcSourceOperand::Type::kZerosOnes;
  dxbc_operand.index = 0;
  dxbc_operand.swizzle = kSwizzleXYZW;
  dxbc_operand.is_dynamic_indexed = false;
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
  uint32_t dynamic_address_register;
  uint32_t dynamic_address_component;
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
      if (uses_register_dynamic_addressing()) {
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
        // GPRs are in r# - can access directly.
        dxbc_operand.type = DxbcSourceOperand::Type::kRegister;
        dxbc_operand.index = uint32_t(operand.storage_index);
      }
      break;

    case InstructionStorageSource::kConstantFloat:
      // ***********************************************************************
      // Float constant
      // ***********************************************************************
      dxbc_operand.type = DxbcSourceOperand::Type::kConstantFloat;
      if (operand.storage_addressing_mode ==
          InstructionStorageAddressingMode::kStatic) {
        // Constant buffers with a constant index can be used directly.
        dxbc_operand.index = uint32_t(operand.storage_index);
      } else {
        dxbc_operand.is_dynamic_indexed = true;
        if (dxbc_operand.intermediate_register ==
            DxbcSourceOperand::kIntermediateRegisterNone) {
          dxbc_operand.intermediate_register = PushSystemTemp();
        }
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
        // Load the high part (page, 3 bits) and the low part (vector, 5 bits)
        // of the index.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
        shader_code_.push_back(dxbc_operand.intermediate_register);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(3);
        shader_code_.push_back(5);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(5);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(0);
        shader_code_.push_back(EncodeVectorReplicatedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, constant_address_component, 1));
        shader_code_.push_back(constant_address_register);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      }
      break;

    case InstructionStorageSource::kConstantInt: {
      // ***********************************************************************
      // Loop constant
      // ***********************************************************************
      // Convert to float and store in the intermediate register.
      // The constant buffer contains each integer replicated in XYZW so dynamic
      // indexing is possible.
      dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
      if (dxbc_operand.intermediate_register ==
          DxbcSourceOperand::kIntermediateRegisterNone) {
        dxbc_operand.intermediate_register = PushSystemTemp();
      }
      rdef_constants_used_ |= 1ull
                              << uint32_t(RdefConstantIndex::kLoopConstants);
      bool is_static = operand.storage_addressing_mode ==
                       InstructionStorageAddressingMode::kStatic;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(is_static ? 7 : 9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(dxbc_operand.intermediate_register);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXXXX, 3,
          D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
          D3D10_SB_OPERAND_INDEX_IMMEDIATE32,
          is_static ? D3D10_SB_OPERAND_INDEX_IMMEDIATE32
                    : D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE));
      shader_code_.push_back(
          uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
      shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
      // 8 to skip bool constants.
      shader_code_.push_back(8 + uint32_t(operand.storage_index));
      if (!is_static) {
        shader_code_.push_back(EncodeVectorSelectOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, dynamic_address_component, 1));
        shader_code_.push_back(dynamic_address_register);
      }
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
    } break;

    case InstructionStorageSource::kConstantBool: {
      // ***********************************************************************
      // Boolean constant
      // ***********************************************************************
      // Extract, convert to float and store in the intermediate register.
      // The constant buffer contains each 32-bit vector replicated in XYZW so
      // dynamic indexing is possible.
      dxbc_operand.type = DxbcSourceOperand::Type::kIntermediateRegister;
      if (dxbc_operand.intermediate_register ==
          DxbcSourceOperand::kIntermediateRegisterNone) {
        dxbc_operand.intermediate_register = PushSystemTemp();
      }
      rdef_constants_used_ |= 1ull
                              << uint32_t(RdefConstantIndex::kBoolConstants);
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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
        shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
        shader_code_.push_back(uint32_t(operand.storage_index) >> 5);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
      } else {
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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
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
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXXXX, 1));
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
    const DxbcSourceOperand& operand, bool negate) const {
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
      if (operand.is_dynamic_indexed) {
        // Constant buffer, 3D index - immediate 0, immediate + register 1,
        // register 2.
        length = 7;
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
  // Apply both the operand negation and the usage negation (for subtraction).
  if (operand.is_negated) {
    negate = !negate;
  }
  // Modifier extension - neg/abs or non-uniform binding index.
  if (negate || operand.is_absolute_value ||
      (operand.type == DxbcSourceOperand::Type::kConstantFloat &&
       operand.is_dynamic_indexed)) {
    ++length;
  }
  return length;
}

void DxbcShaderTranslator::UseDxbcSourceOperand(
    const DxbcSourceOperand& operand, uint32_t additional_swizzle,
    uint32_t select_component, bool negate) {
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
                      (((operand.swizzle >> (select_component * 2)) & 0x3)
                       << D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_SHIFT);
  } else {
    component_bits |=
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
            D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
        (operand.swizzle << D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SHIFT);
  }

  // Apply both the operand negation and the usage negation (for subtraction).
  if (operand.is_negated) {
    negate = !negate;
  }
  // Build OperandToken1 for modifiers (negate, absolute, minimum precision,
  // non-uniform binding index) - if it has any, it will be non-zero.
  uint32_t modifiers = 0;
  if (negate && operand.is_absolute_value) {
    modifiers |= D3D10_SB_OPERAND_MODIFIER_ABSNEG
                 << D3D10_SB_OPERAND_MODIFIER_SHIFT;
  } else if (negate) {
    modifiers |= D3D10_SB_OPERAND_MODIFIER_NEG
                 << D3D10_SB_OPERAND_MODIFIER_SHIFT;
  } else if (operand.is_absolute_value) {
    modifiers |= D3D10_SB_OPERAND_MODIFIER_ABS
                 << D3D10_SB_OPERAND_MODIFIER_SHIFT;
  }
  // Dynamic constant indices can have any values, and we use pages bound to
  // different b#.
  if (operand.type == DxbcSourceOperand::Type::kConstantFloat &&
      operand.is_dynamic_indexed) {
    modifiers |= ENCODE_D3D12_SB_OPERAND_NON_UNIFORM(1);
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

    case DxbcSourceOperand::Type::kConstantFloat:
      rdef_constants_used_ |= 1ull
                              << uint32_t(RdefConstantIndex::kFloatConstants);
      if (operand.is_dynamic_indexed) {
        // Index loaded as uint2 in the intermediate register, the page number
        // in X, the vector number in the page in Y.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPERAND_TYPE(
                D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
            ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_3D) |
            ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
                0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
            ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
                1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE) |
            ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
                2, D3D10_SB_OPERAND_INDEX_RELATIVE) |
            component_bits | extended_bit);
        if (modifiers != 0) {
          shader_code_.push_back(modifiers);
        }
        // Dimension 0 (CB#) - immediate.
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kFloatConstants));
        // Dimension 1 (b#) - immediate + temporary X.
        shader_code_.push_back(uint32_t(CbufferRegister::kFloatConstantsFirst));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(operand.intermediate_register);
        // Dimension 2 (vector) - temporary Y.
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
        shader_code_.push_back(operand.intermediate_register);
      } else {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPERAND_TYPE(
                D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
            ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_3D) |
            ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
                0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
            ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
                1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
            ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
                2, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
            component_bits | extended_bit);
        if (modifiers != 0) {
          shader_code_.push_back(modifiers);
        }
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kFloatConstants));
        shader_code_.push_back(uint32_t(CbufferRegister::kFloatConstantsFirst) +
                               (operand.index >> 5));
        shader_code_.push_back(operand.index & 31);
      }
      break;

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
        shader_code_.push_back(kVSOutPointParametersRegister);
        break;
      case InstructionStorageTarget::kDepth:
        writes_depth_ = true;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4) | saturate_bit);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH, 0));
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

  // If writing to an indexable temp, the constant part must be written via r#.
  uint32_t constant_temp = UINT32_MAX;
  if (constant_mask != 0) {
    if ((result.storage_target == InstructionStorageTarget::kRegister &&
         uses_register_dynamic_addressing()) ||
        result.storage_target == InstructionStorageTarget::kColorTarget) {
      constant_temp = PushSystemTemp();
    }
  }
  if (constant_temp != UINT32_MAX) {
    // Load constants to r#.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, constant_mask, 1));
    shader_code_.push_back(constant_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    for (uint32_t j = 0; j < 4; ++j) {
      shader_code_.push_back((constant_values & (1 << j)) ? 0x3F800000 : 0);
    }
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  // Store both parts of the write (i == 0 - swizzled, i == 1 - constant).
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t mask = i == 0 ? swizzle_mask : constant_mask;
    if (mask == 0) {
      continue;
    }

    // r# for the swizzled part, 4-component imm32 for the constant part.
    uint32_t source_length = (i == 1 && constant_temp == UINT32_MAX) ? 5 : 2;
    switch (result.storage_target) {
      case InstructionStorageTarget::kRegister:
        if (uses_register_dynamic_addressing()) {
          bool is_static = result.storage_addressing_mode ==
                           InstructionStorageAddressingMode::kStatic;
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
            uint32_t dynamic_address_register;
            uint32_t dynamic_address_component;
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
          shader_code_.push_back(uint32_t(result.storage_index));
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
        shader_code_.push_back(kVSOutInterpolatorRegister +
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
        ++stat_.array_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4 + source_length) |
            saturate_bit);
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, mask, 2));
        shader_code_.push_back(GetColorIndexableTemp());
        shader_code_.push_back(uint32_t(result.storage_index));
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
      if (constant_temp != UINT32_MAX) {
        // Load constants from the r#.
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(constant_temp);
      } else {
        // Load constants from an immediate.
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        for (uint32_t j = 0; j < 4; ++j) {
          shader_code_.push_back((constant_values & (1 << j)) ? 0x3F800000 : 0);
        }
      }
    }
  }

  // Free the r# with constants if used.
  if (constant_temp != UINT32_MAX) {
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::SwapVertexData(uint32_t vfetch_index,
                                          uint32_t write_mask) {
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
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kFetchConstants);
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (vfetch_index & 1) * 2 + 1, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kFetchConstants));
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
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXXXX, 1));
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
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleYYYY, 1));
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

  // TODO(Triang3l): Predicate.

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
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kFetchConstants);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (vfetch_index & 1) * 2, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kFetchConstants));
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
          normalize_scales[3] = instr.attributes.is_signed ? (1.0f / 127.0f)
                                                           : (1.0f / 255.0f);
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
      normalize_scales[0] = normalize_scales[1] =
          instr.attributes.is_signed ? (1.0f / 32767.0f) : (1.0f / 65535.0f);
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
  }

  // Extract components from packed data if needed.
  if (extract_widths[0] != 0) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(extract_signed ? D3D11_SB_OPCODE_IBFE
                                                   : D3D11_SB_OPCODE_UBFE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, result_write_mask, 1));
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
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, result_write_mask, 1));
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
        ENCODE_D3D10_SB_OPCODE_TYPE(
            instr.attributes.is_signed ? D3D10_SB_OPCODE_ITOF
                                       : D3D10_SB_OPCODE_UTOF) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, result_write_mask, 1));
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
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, result_write_mask, 1));
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

void DxbcShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  // TODO(Triang3l): Predicate.

  // Whether the instruction has changed the predicate and it needs to be
  // checked again.
  bool close_predicate_block = false;

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
    case AluVectorOpcode::kMul:
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

    case AluVectorOpcode::kMad:
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
      break;

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
      // Replicated implicitly.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP4) |
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

    case AluVectorOpcode::kDp3:
      // Replicated implicitly.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP3) |
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

    case AluVectorOpcode::kDp2Add:
      // (dot(src0.xy, src1.xy) + src2.x).xxxx
      replicate_result = true;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP2) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_length_sums[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[0]);
      UseDxbcSourceOperand(dxbc_operands[1]);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              5 + DxbcSourceOperandLength(dxbc_operands[2])));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_pv_);
      UseDxbcSourceOperand(dxbc_operands[2], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

      // TODO(Triang3l): AluVectorOpcode::kCube.

    case AluVectorOpcode::kMax4:
      replicate_result = true;
      // pv.xy = max(src0.xy, src0.zw)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_length_sums[1]));
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
      close_predicate_block = true;
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

    case AluVectorOpcode::kDst:
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
      break;

    case AluVectorOpcode::kMaxA:
      // The `a0 = int(clamp(floor(src0.w + 0.5), -256.0, 255.0))` part.
      //
      // Using specifically floor(src0.w + 0.5) rather than round(src0.w)
      // because the R600 ISA reference says so - this makes a difference at
      // 0.5 because round rounds to the nearest even.
      // There's one deviation from the R600 specification though - the value is
      // clamped to 255 rather than set to -256 if it's over 255. We don't know
      // yet which is the correct - the mova_int description, for example, says
      // "clamp" explicitly.
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
      // TODO(Triang3l): Add this assert when kCube is added.
      // assert_always();
      // Unknown instruction - don't modify pv.
      break;
  }

  for (uint32_t i = 0; i < uint32_t(instr.operand_count); ++i) {
    UnloadDxbcSourceOperand(dxbc_operands[instr.operand_count - 1 - i]);
  }

  StoreResult(instr.result, system_temp_pv_, replicate_result);

  // TODO(Triang3l): Close predicate check.
}

void DxbcShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  // TODO(Triang3l): Predicate.

  // Whether the instruction has changed the predicate and it needs to be
  // checked again.
  bool close_predicate_block = false;

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
    case AluScalarOpcode::kMuls:
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
    case AluScalarOpcode::kMulsPrev:
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

      // TODO(Triang3l): kMulsPrev2.

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

      // TODO(Triang3l): kLogc, kRcpc, kRcpf, kRcp, kRsqc, kRsqf, kRsq.

    case AluScalarOpcode::kMaxAs:
    case AluScalarOpcode::kMaxAsf:
      // The `a0 = int(clamp(round(src0.x), -256.0, 255.0))` part.
      //
      // See AluVectorOpcode::kMaxA handling for details regarding rounding and
      // clamping.
      //
      // ps (temporary) = round(src0.x) (towards the nearest integer via
      // floor(src0.x + 0.5) for maxas and towards -Infinity for maxasf).
      if (instr.scalar_opcode == AluScalarOpcode::kMaxAs) {
        // ps = src0.x + 0.5
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3F000000u);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // ps = floor(src0.x + 0.5)
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      } else {
        // ps = floor(src0.x)
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
      // ps = max(round(src0.x), -256.0)
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
      shader_code_.push_back(0xC3800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // ps = clamp(round(src0.x), -256.0, 255.0)
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
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
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
      close_predicate_block = true;
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
      close_predicate_block = true;
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
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      break;

    case AluScalarOpcode::kSetpPop:
      close_predicate_block = true;
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
      close_predicate_block = true;
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
      close_predicate_block = true;
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
      shader_code_.push_back(EncodeScalarOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32,
          instr.scalar_opcode == AluScalarOpcode::kKillsOne ? 0x3F800000 : 0));
      shader_code_.push_back(0);
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
    case AluScalarOpcode::kMulsc1:
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
      // TODO(Triang3l): Enable this assert when all instruction are added.
      // May be retain_prev, in this case the current ps should be written, or
      // something invalid that's better to ignore.
      // assert_true(instr.scalar_opcode == AluScalarOpcode::kRetainPrev);
      break;
  }

  for (uint32_t i = 0; i < uint32_t(instr.operand_count); ++i) {
    UnloadDxbcSourceOperand(dxbc_operands[instr.operand_count - 1 - i]);
  }

  StoreResult(instr.result, system_temp_ps_pc_p0_a0_, true);

  // TODO(Triang3l): Close predicate check.
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
  std::memset(reinterpret_cast<uint8_t*>(&dest[dest_position]) + size, 0xAB,
              size_aligned - size);
  return uint32_t(size_aligned);
}

const DxbcShaderTranslator::RdefStructMember
    DxbcShaderTranslator::rdef_float_constant_page_member_ = {
        "c", RdefTypeIndex::kFloatConstantPageArray, 0};

const DxbcShaderTranslator::RdefType DxbcShaderTranslator::rdef_types_[size_t(
    DxbcShaderTranslator::RdefTypeIndex::kCount)] = {
    {"float", 0, 3, 1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"float2", 1, 3, 1, 2, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"float3", 1, 3, 1, 3, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"float4", 1, 3, 1, 4, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"int", 0, 2, 1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"uint", 0, 19, 1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {"uint4", 1, 19, 1, 4, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    {nullptr, 1, 19, 1, 4, 8, 0, RdefTypeIndex::kUint4, nullptr},
    {nullptr, 1, 19, 1, 4, 32, 0, RdefTypeIndex::kUint4, nullptr},
    {nullptr, 1, 19, 1, 4, 48, 0, RdefTypeIndex::kUint4, nullptr},
    {nullptr, 1, 3, 1, 4, kFloatConstantsPerPage, 0, RdefTypeIndex::kFloat4,
     nullptr},
    {"XeFloatConstantPage", 5, 0, 1, kFloatConstantsPerPage * 4, 1, 1,
     RdefTypeIndex::kUnknown, &rdef_float_constant_page_member_},
};

const DxbcShaderTranslator::RdefConstant
    DxbcShaderTranslator::rdef_constants_[size_t(
        DxbcShaderTranslator::RdefConstantIndex::kCount)] = {
        // SYSTEM CONSTANTS MUST BE UPDATED IF THEIR LAYOUT CHANGES!
        // System constants vec4 0.
        {"xe_mul_rcp_w", RdefTypeIndex::kFloat3, 0, 12},
        {"xe_vertex_base_index", RdefTypeIndex::kUint, 12, 4},
        // System constants vec4 1.
        {"xe_ndc_scale", RdefTypeIndex::kFloat3, 16, 12},
        {"xe_vertex_index_endian", RdefTypeIndex::kUint, 28, 4},
        // System constants vec4 2.
        {"xe_ndc_offset", RdefTypeIndex::kFloat3, 32, 12},
        {"xe_pixel_half_pixel_offset", RdefTypeIndex::kFloat, 44, 4},
        // System constants vec4 3.
        {"xe_point_size", RdefTypeIndex::kFloat2, 48, 8},
        {"xe_ssaa_inv_scale", RdefTypeIndex::kFloat2, 56, 8},
        // System constants vec4 4.
        {"xe_pixel_pos_reg", RdefTypeIndex::kUint, 64, 4},
        {"xe_alpha_test", RdefTypeIndex::kInt, 68, 4},
        {"xe_alpha_test_range", RdefTypeIndex::kFloat2, 72, 8},
        // System constants vec4 5.
        {"xe_color_exp_bias", RdefTypeIndex::kFloat4, 80, 16},
        // System constants vec4 6.
        {"xe_color_output_map", RdefTypeIndex::kUint4, 96, 16},

        {"xe_bool_constants", RdefTypeIndex::kUint4Array8, 0, 128},
        {"xe_loop_constants", RdefTypeIndex::kUint4Array32, 128, 512},

        {"xe_fetch_constants", RdefTypeIndex::kUint4Array48, 0, 768},

        {"xe_float_constants", RdefTypeIndex::kFloatConstantPageStruct, 0,
         kFloatConstantsPerPage * 16},
};

const DxbcShaderTranslator::RdefConstantBuffer
    DxbcShaderTranslator::rdef_constant_buffers_[size_t(
        DxbcShaderTranslator::RdefConstantBufferIndex::kCount)] = {
        // SYSTEM CONSTANT SIZE MUST BE UPDATED IF THEIR LAYOUT CHANGES!
        {"xe_system_cbuffer", RdefConstantIndex::kSystemConstantFirst,
         uint32_t(RdefConstantIndex::kSystemConstantCount), 112,
         CbufferRegister::kSystemConstants, 1, true, false},
        {"xe_bool_loop_cbuffer", RdefConstantIndex::kBoolConstants, 2, 40 * 16,
         CbufferRegister::kBoolLoopConstants, 1, true, true},
        {"xe_fetch_cbuffer", RdefConstantIndex::kFetchConstants, 1, 48 * 16,
         CbufferRegister::kFetchConstants, 1, true, false},
        {"xe_float_constants", RdefConstantIndex::kFloatConstants, 1,
         kFloatConstantsPerPage * 16, CbufferRegister::kFloatConstantsFirst,
         kFloatConstantPageCount, false, true},
};

const DxbcShaderTranslator::RdefConstantBufferIndex
    DxbcShaderTranslator::constant_buffer_dcl_order_[size_t(
        DxbcShaderTranslator::RdefConstantBufferIndex::kCount)] = {
        RdefConstantBufferIndex::kFloatConstants,
        RdefConstantBufferIndex::kFetchConstants,
        RdefConstantBufferIndex::kSystemConstants,
        RdefConstantBufferIndex::kBoolLoopConstants,
};

void DxbcShaderTranslator::WriteResourceDefinitions() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  // + 1 for shared memory (vfetches can probably appear in pixel shaders too,
  // they are handled safely there anyway).
  // TODO(Triang3l): Textures, samplers.
  uint32_t binding_count = uint32_t(RdefConstantBufferIndex::kCount) + 1;

  // ***************************************************************************
  // Header
  // ***************************************************************************

  // Constant buffer count.
  shader_object_.push_back(uint32_t(RdefConstantBufferIndex::kCount));
  // Constant buffer offset (set later).
  shader_object_.push_back(0);
  // Bound resource count (CBV, SRV, UAV, samplers).
  shader_object_.push_back(binding_count);
  // Bound resource buffer offset (set later).
  shader_object_.push_back(0);
  if (is_vertex_shader()) {
    // vs_5_1
    shader_object_.push_back(0xFFFE0501u);
  } else {
    assert_true(is_pixel_shader());
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
    shader_object_.push_back(type.element_count |
                             (type.struct_member_count << 16));
    // Struct member offset (set later).
    shader_object_.push_back(0);
    // Unknown.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(type_name_offsets[i]);
  }
  // Structure members.
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

  // ***************************************************************************
  // Constants
  // ***************************************************************************

  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t constant_name_offsets[size_t(RdefConstantIndex::kCount)];
  for (uint32_t i = 0; i < uint32_t(RdefConstantIndex::kCount); ++i) {
    constant_name_offsets[i] = new_offset;
    new_offset += AppendString(shader_object_, rdef_constants_[i].name);
  }
  uint32_t constants_offset = new_offset;
  const uint32_t constant_size = 40;
  for (uint32_t i = 0; i < uint32_t(RdefConstantIndex::kCount); ++i) {
    const RdefConstant& constant = rdef_constants_[i];
    shader_object_.push_back(constant_name_offsets[i]);
    shader_object_.push_back(constant.offset);
    shader_object_.push_back(constant.size);
    // Flag 2 is D3D_SVF_USED.
    shader_object_.push_back((rdef_constants_used_ & (1ull << i)) ? 2 : 0);
    shader_object_.push_back(types_offset +
                             uint32_t(constant.type) * type_size);
    // Default value (always 0).
    shader_object_.push_back(0);
    // Unknown.
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
  }

  // ***************************************************************************
  // Constant buffers
  // ***************************************************************************

  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t cbuffer_name_offsets[size_t(RdefConstantBufferIndex::kCount)];
  for (uint32_t i = 0; i < uint32_t(RdefConstantBufferIndex::kCount); ++i) {
    cbuffer_name_offsets[i] = new_offset;
    new_offset += AppendString(shader_object_, rdef_constant_buffers_[i].name);
  }
  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 1] = new_offset;
  for (uint32_t i = 0; i < uint32_t(RdefConstantBufferIndex::kCount); ++i) {
    const RdefConstantBuffer& cbuffer = rdef_constant_buffers_[i];
    shader_object_.push_back(cbuffer_name_offsets[i]);
    shader_object_.push_back(cbuffer.constant_count);
    shader_object_.push_back(constants_offset +
                             uint32_t(cbuffer.first_constant) * constant_size);
    shader_object_.push_back(cbuffer.size);
    // D3D_CT_CBUFFER.
    shader_object_.push_back(0);
    // No D3D_SHADER_CBUFFER_FLAGS.
    shader_object_.push_back(0);
  }

  // ***************************************************************************
  // Bindings, in t#, cb# order
  // ***************************************************************************

  // Write used resource names, except for constant buffers because we have
  // their names already.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t shared_memory_name_offset = new_offset;
  new_offset += AppendString(shader_object_, "xe_shared_memory");
  // TODO(Triang3l): Texture and sampler names.

  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 3] = new_offset;

  // Shared memory.
  shader_object_.push_back(shared_memory_name_offset);
  // D3D_SIT_BYTEADDRESS.
  shader_object_.push_back(7);
  // D3D_RETURN_TYPE_MIXED.
  shader_object_.push_back(6);
  // D3D_SRV_DIMENSION_UNKNOWN.
  shader_object_.push_back(1);
  // Not multisampled.
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

  // TODO(Triang3l): Textures and samplers.

  // Constant buffers.
  for (uint32_t i = 0; i < uint32_t(RdefConstantBufferIndex::kCount); ++i) {
    const RdefConstantBuffer& cbuffer = rdef_constant_buffers_[i];
    shader_object_.push_back(cbuffer_name_offsets[i]);
    // D3D_SIT_CBUFFER.
    shader_object_.push_back(0);
    // No D3D_RESOURCE_RETURN_TYPE.
    shader_object_.push_back(0);
    // D3D_SRV_DIMENSION_UNKNOWN (not an SRV).
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(uint32_t(cbuffer.register_index));
    shader_object_.push_back(cbuffer.binding_count);
    // D3D_SIF_USERPACKED if a `cbuffer` rather than a `ConstantBuffer<T>`.
    shader_object_.push_back(cbuffer.user_packed ? 1 : 0);
    // Register space 0.
    shader_object_.push_back(0);
    shader_object_.push_back(i);
  }
}

void DxbcShaderTranslator::WriteInputSignature() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  const uint32_t signature_position_dwords = 2;
  const uint32_t signature_size_dwords = 6;

  if (is_vertex_shader()) {
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
    shader_object_.push_back(kVSInVertexIndexRegister);
    // x present, x used (always written to GPR 0).
    shader_object_.push_back(0x1 | (0x1 << 8));

    // Vertex index semantic name.
    AppendString(shader_object_, "SV_VertexID");
  } else {
    assert_true(is_pixel_shader());
    // Interpolators, point parameters (coordinates, size), screen position,
    // is front face.
    shader_object_.push_back(kInterpolatorCount + 3);
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
      shader_object_.push_back(kPSInInterpolatorRegister + i);
      // Interpolators are copied to GPRs in the beginning of the shader. If
      // there's a register to copy to, this interpolator is used.
      shader_object_.push_back(0xF | (i < register_count() ? (0xF << 8) : 0));
    }

    // Point parameters - coordinate on the point and point size as a float3
    // TEXCOORD (but the size in Z is not needed). Always used because
    // ps_param_gen is handled dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(kPointParametersTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(kPSInPointParametersRegister);
    shader_object_.push_back(0x7 | (0x3 << 8));

    // Position (only XY needed). Always used because ps_param_gen is handled
    // dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_POSITION.
    shader_object_.push_back(1);
    shader_object_.push_back(3);
    shader_object_.push_back(kPSInPositionRegister);
    shader_object_.push_back(0xF | (0x3 << 8));

    // Is front face. Always used because ps_param_gen is handled dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_IS_FRONT_FACE.
    shader_object_.push_back(9);
    shader_object_.push_back(1);
    shader_object_.push_back(kPSInFrontFaceRegister);
    shader_object_.push_back(0x1 | (0x1 << 8));

    // Write the semantic names.
    new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                 sizeof(uint32_t);
    for (uint32_t i = 0; i < kInterpolatorCount + 1; ++i) {
      uint32_t texcoord_name_position_dwords = chunk_position_dwords +
                                               signature_position_dwords +
                                               i * signature_size_dwords;
      shader_object_[texcoord_name_position_dwords] = new_offset;
    }
    new_offset += AppendString(shader_object_, "TEXCOORD");

    uint32_t position_name_position_dwords =
        chunk_position_dwords + signature_position_dwords +
        (kInterpolatorCount + 1) * signature_size_dwords;
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

  if (is_vertex_shader()) {
    // Interpolators, point parameters (coordinates, size), screen position.
    shader_object_.push_back(kInterpolatorCount + 2);
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
      shader_object_.push_back(kVSOutInterpolatorRegister + i);
      // Unlike in ISGN, the second byte contains the unused components, not the
      // used ones. All components are always used because they are reset to 0.
      shader_object_.push_back(0xF);
    }

    // Point parameters - coordinate on the point and point size as a float3
    // TEXCOORD. Always used because reset to (0, 0, -1).
    shader_object_.push_back(0);
    shader_object_.push_back(kPointParametersTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(kVSOutPointParametersRegister);
    shader_object_.push_back(0x7 | (0x8 << 8));

    // Position.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_POSITION.
    shader_object_.push_back(1);
    shader_object_.push_back(3);
    shader_object_.push_back(kVSOutPositionRegister);
    shader_object_.push_back(0xF);

    // Write the semantic names.
    new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                 sizeof(uint32_t);
    for (uint32_t i = 0; i < kInterpolatorCount + 1; ++i) {
      uint32_t texcoord_name_position_dwords = chunk_position_dwords +
                                               signature_position_dwords +
                                               i * signature_size_dwords;
      shader_object_[texcoord_name_position_dwords] = new_offset;
    }
    new_offset += AppendString(shader_object_, "TEXCOORD");
    uint32_t position_name_position_dwords =
        chunk_position_dwords + signature_position_dwords +
        (kInterpolatorCount + 1) * signature_size_dwords;
    shader_object_[position_name_position_dwords] = new_offset;
    new_offset += AppendString(shader_object_, "SV_Position");
  } else {
    assert_true(is_pixel_shader());
    // Color render targets, optionally depth.
    shader_object_.push_back(4 + (writes_depth_ ? 1 : 0));
    // Unknown.
    shader_object_.push_back(8);

    // Color render targets.
    for (uint32_t i = 0; i < 4; ++i) {
      // Reserve space for the semantic name (SV_Target).
      shader_object_.push_back(0);
      shader_object_.push_back(i);
      // D3D_NAME_UNDEFINED for some reason - this is correct.
      shader_object_.push_back(0);
      shader_object_.push_back(3);
      // Register must match the render target index.
      shader_object_.push_back(i);
      // All are used because X360 RTs are dynamically remapped to D3D12 RTs to
      // make the indices consecutive.
      shader_object_.push_back(0xF);
    }

    // Depth.
    if (writes_depth_) {
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
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t color_name_position_dwords = chunk_position_dwords +
                                            signature_position_dwords +
                                            i * signature_size_dwords;
      shader_object_[color_name_position_dwords] = new_offset;
    }
    new_offset += AppendString(shader_object_, "SV_Target");
    if (writes_depth_) {
      uint32_t depth_name_position_dwords = chunk_position_dwords +
                                            signature_position_dwords +
                                            4 * signature_size_dwords;
      shader_object_[depth_name_position_dwords] = new_offset;
      new_offset += AppendString(shader_object_, "SV_Depth");
    }
  }
}

void DxbcShaderTranslator::WriteShaderCode() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());

  D3D10_SB_TOKENIZED_PROGRAM_TYPE program_type =
      is_vertex_shader() ? D3D10_SB_VERTEX_SHADER : D3D10_SB_PIXEL_SHADER;
  shader_object_.push_back(
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(program_type, 5, 1));
  // Reserve space for the length token.
  shader_object_.push_back(0);

  // Declarations (don't increase the instruction count stat, and only inputs
  // and outputs are counted in dcl_count).
  // Binding declarations have 3D-indexed operands with XYZW swizzle, the first
  // index being the binding ID (local to the shader), the second being the
  // lower register index bound, and the third being the highest register index
  // bound.
  // Inputs/outputs have 1D-indexed operands with a component mask and a
  // register index.

  // Don't allow refactoring when converting to native code to maintain position
  // invariance (needed even in pixel shaders for oDepth invariance).
  shader_object_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1) |
      D3D11_1_SB_GLOBAL_FLAG_SKIP_OPTIMIZATION);

  // Constant buffers.
  for (uint32_t i = 0; i < uint32_t(RdefConstantBufferIndex::kCount); ++i) {
    uint32_t cbuffer_index = uint32_t(constant_buffer_dcl_order_[i]);
    const RdefConstantBuffer& cbuffer = rdef_constant_buffers_[cbuffer_index];
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            cbuffer.dynamic_indexed
                ? D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED
                : D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index);
    shader_object_.push_back(uint32_t(cbuffer.register_index));
    shader_object_.push_back(uint32_t(cbuffer.register_index) +
                             cbuffer.binding_count - 1);
    shader_object_.push_back((cbuffer.size + 15) >> 4);
    // Space 0.
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

  // Inputs and outputs.
  if (is_vertex_shader()) {
    // Unswapped vertex index input (only X component).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_SGV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
    shader_object_.push_back(kVSInVertexIndexRegister);
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_VERTEX_ID));
    ++stat_.dcl_count;
    // Interpolator output.
    for (uint32_t i = 0; i < kInterpolatorCount; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
      shader_object_.push_back(kVSOutInterpolatorRegister + i);
      ++stat_.dcl_count;
    }
    // Point parameters output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0111, 1));
    shader_object_.push_back(kVSOutPointParametersRegister);
    ++stat_.dcl_count;
    // Position output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
    shader_object_.push_back(kVSOutPositionRegister);
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
    ++stat_.dcl_count;
  } else if (is_pixel_shader()) {
    // Interpolator input.
    uint32_t interpolator_count =
        std::min(kInterpolatorCount, register_count());
    for (uint32_t i = 0; i < interpolator_count; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              D3D10_SB_INTERPOLATION_LINEAR));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b1111, 1));
      shader_object_.push_back(kPSInInterpolatorRegister + i);
      ++stat_.dcl_count;
    }
    // Point parameters input (only coordinates, not size, needed).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
            D3D10_SB_INTERPOLATION_LINEAR));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
    shader_object_.push_back(kPSInPointParametersRegister);
    ++stat_.dcl_count;
    // Position input (only XY needed).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4) |
        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
            D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
    shader_object_.push_back(kPSInPositionRegister);
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
    ++stat_.dcl_count;
    // Is front face.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SGV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4) |
        // This needs to be set according to FXC output, despite the description
        // in d3d12TokenizedProgramFormat.hpp saying bits 11:23 are ignored.
        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
            D3D10_SB_INTERPOLATION_CONSTANT));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
    shader_object_.push_back(kPSInFrontFaceRegister);
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_IS_FRONT_FACE));
    ++stat_.dcl_count;
    // Color output.
    for (uint32_t i = 0; i < 4; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
      shader_object_.push_back(i);
      ++stat_.dcl_count;
    }
    // Depth output.
    if (writes_depth_) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
      shader_object_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH, 0));
      ++stat_.dcl_count;
    }
  }

  // Temporary registers - guest general-purpose registers if not using dynamic
  // indexing and Xenia internal registers.
  stat_.temp_register_count =
      (uses_register_dynamic_addressing() ? 0 : register_count()) +
      system_temp_count_max_;
  if (stat_.temp_register_count != 0) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_TEMPS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
    shader_object_.push_back(stat_.temp_register_count);
  }

  // General-purpose registers if using dynamic indexing (x0).
  if (uses_register_dynamic_addressing()) {
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

  // Pixel shader color output (remapped in the end of the shader).
  if (is_pixel_shader()) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(GetColorIndexableTemp());
    shader_object_.push_back(4);
    shader_object_.push_back(4);
    stat_.temp_array_count += 4;
  }

  // Initialize the depth output if used, which must be initialized on every
  // execution path.
  if (is_pixel_shader() && writes_depth_) {
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
