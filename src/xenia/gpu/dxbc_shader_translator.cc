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
  cf_currently_predicated_ = false;
  cf_exec_predicated_ = false;
  cf_exec_bool_constant_ = kCfExecBoolConstantNone;
  writes_depth_ = false;
  texture_srvs_.clear();
  sampler_bindings_.clear();

  std::memset(&stat_, 0, sizeof(stat_));
}

uint32_t DxbcShaderTranslator::PushSystemTemp(bool zero) {
  uint32_t register_index = system_temp_count_current_;
  if (!IndexableGPRsUsed()) {
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
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_VertexIndexEndian_Comp, 3));
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
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_VertexBaseIndex_Comp, 3));
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

  // Write screen and point coordinates to the specified interpolator register
  // (ps_param_gen).
  uint32_t param_gen_select_temp = PushSystemTemp();
  uint32_t param_gen_value_temp = PushSystemTemp();
  // Check if they need to be written.
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kSysPixelPosReg);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ULT) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(param_gen_select_temp);
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_PixelPosReg_Comp, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  // Write VPOS (without supersampling because SSAA is used to fake MSAA, and
  // at integer coordinates rather than half-pixel if needed) to XY.
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kSysSSAAInvScale);
  rdef_constants_used_ |=
      1ull << uint32_t(RdefConstantIndex::kSysPixelHalfPixelOffset);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(param_gen_value_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
  shader_code_.push_back(kPSInPositionRegister);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_SSAAInvScale_Comp | ((kSysConst_SSAAInvScale_Comp + 1) << 2),
      3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_SSAAInvScale_Vec);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                    kSysConst_PixelHalfPixelOffset_Comp, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  shader_code_.push_back(kPSInPointParametersRegister);
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
    shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  // Request global system temporary variables.
  system_temp_pv_ = PushSystemTemp(true);
  system_temp_ps_pc_p0_a0_ = PushSystemTemp(true);
  system_temp_aL_ = PushSystemTemp(true);
  system_temp_loop_count_ = PushSystemTemp(true);
  system_temp_grad_h_lod_ = PushSystemTemp(true);
  system_temp_grad_v_ = PushSystemTemp(true);
  if (is_vertex_shader()) {
    system_temp_position_ = PushSystemTemp(true);
  } else if (is_pixel_shader()) {
    for (uint32_t i = 0; i < 4; ++i) {
      system_temp_color_[i] = PushSystemTemp(true);
    }
  }

  // Write stage-specific prologue.
  if (is_vertex_shader()) {
    StartVertexShader();
  } else if (is_pixel_shader()) {
    StartPixelShader();
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
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                               D3D10_SB_INSTRUCTION_TEST_ZERO));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
  }
}

void DxbcShaderTranslator::CompleteVertexShader() {
  // Revert getting the reciprocal of W and dividing XY by W if needed.
  // TODO(Triang3l): Check if having XY or Z pre-divided by W should enable
  // affine interpolation.
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysVertexWFormat);
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
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                kSysConst_VertexWFormat_Comp + 2, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_VertexWFormat_Vec);
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
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(system_temp_position_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_VertexWFormat_Comp | (kSysConst_VertexWFormat_Comp << 2) |
          ((kSysConst_VertexWFormat_Comp + 1) << 4),
      3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_VertexWFormat_Vec);
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

  // Apply scale for drawing without a viewport.
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kSysNDCScale);
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_NDCScale_Vec);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Apply offset (multiplied by W) for drawing without a viewport and for half
  // pixel offset.
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kSysNDCOffset);
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  // Apply color exponent bias (the constant contains 2.0^bias).
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kSysColorExpBias);
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
    shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_ColorExpBias_Vec);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }

  // Alpha test.
  // Check if alpha test is enabled (if the constant is not 0).
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kSysAlphaTest);
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysAlphaTestRange);
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(EncodeVectorSelectOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSysConst_AlphaTest_Comp, 3));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
  // Remap guest render target indices to host since because on the host, the
  // indices of the bound render targets are consecutive. This is done using 16
  // movc instructions because indexable temps are known to be causing
  // performance issues on some Nvidia GPUs. In the map, the components are host
  // render target indices, and the values are the guest ones.
  uint32_t remap_movc_mask_register = PushSystemTemp();
  uint32_t remap_movc_target_register = PushSystemTemp();
  rdef_constants_used_ |= 1ull
                          << uint32_t(RdefConstantIndex::kSysColorOutputMap);
  // Host RT i, guest RT j.
  for (uint32_t i = 0; i < 4; ++i) {
    // mask = map.iiii == (0, 1, 2, 3)
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(remap_movc_mask_register);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, i, 3));
    shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
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
      shader_code_.push_back(remap_movc_target_register);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, j, 1));
      shader_code_.push_back(remap_movc_mask_register);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_color_[j]);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(remap_movc_target_register);
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
    shader_code_.push_back(remap_movc_target_register);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }
  // Free the temporary registers used for remapping.
  PopSystemTemp(2);
}

void DxbcShaderTranslator::CompleteShaderCode() {
  // Close the last label and the switch.
  if (FLAGS_dxbc_switch) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDSWITCH) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  } else {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }
  // End the main loop.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_BREAK) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDLOOP) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

  if (is_vertex_shader()) {
    // Release system_temp_position_.
    PopSystemTemp();
  } else if (is_pixel_shader()) {
    // Release system_temp_color_.
    PopSystemTemp(4);
  }
  // Release the following system temporary values so epilogue can reuse them:
  // - system_temp_pv_.
  // - system_temp_ps_pc_p0_a0_.
  // - system_temp_aL_.
  // - system_temp_loop_count_.
  // - system_temp_grad_h_lod_.
  // - system_temp_grad_v_.
  PopSystemTemp(6);

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
  // Apply both the operand negation and the usage negation (for subtraction)
  // and absolute from both sources.
  if (operand.is_negated) {
    negate = !negate;
  }
  absolute |= operand.is_absolute_value;
  // Modifier extension - neg/abs or non-uniform binding index.
  if (negate || absolute ||
      (operand.type == DxbcSourceOperand::Type::kConstantFloat &&
       operand.is_dynamic_indexed)) {
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

  bool is_static = result.storage_addressing_mode ==
                   InstructionStorageAddressingMode::kStatic;
  // If the index is dynamic, choose where it's taken from.
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
}

void DxbcShaderTranslator::ClosePredicate() {
  if (cf_currently_predicated_) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    cf_currently_predicated_ = false;
  }
}

void DxbcShaderTranslator::CheckPredicate(
    bool instruction_predicated, bool instruction_predicate_condition) {
  // If the instruction doesn't have its own predicate check, inherit it from
  // the exec.
  if (!instruction_predicated) {
    instruction_predicated = cf_exec_predicated_;
    instruction_predicate_condition = cf_exec_predicate_condition_;
  }
  // Close the current predicate if the conditions don't match or not predicated
  // anymore.
  if (cf_currently_predicated_ &&
      (!instruction_predicated ||
       cf_current_predicate_condition_ != instruction_predicate_condition)) {
    ClosePredicate();
  }
  // Open a new predicate if predicated now, but the conditions don't match (or
  // the previous instruction wasn't predicated).
  if (instruction_predicated &&
      (!cf_currently_predicated_ ||
       cf_current_predicate_condition_ != instruction_predicate_condition)) {
    D3D10_SB_INSTRUCTION_TEST_BOOLEAN test =
        instruction_predicate_condition ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                                        : D3D10_SB_INSTRUCTION_TEST_ZERO;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    cf_currently_predicated_ = true;
    cf_current_predicate_condition_ = instruction_predicate_condition;
  }
}

void DxbcShaderTranslator::SetExecBoolConstant(uint32_t index, bool condition) {
  if (cf_exec_bool_constant_ == index &&
      (index == kCfExecBoolConstantNone ||
       cf_exec_bool_constant_condition_ == condition)) {
    return;
  }
  if (cf_exec_bool_constant_ != kCfExecBoolConstantNone) {
    // Predicates are checked deeper than the bool constant.
    ClosePredicate();
    // Close the current `if`.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
    cf_exec_bool_constant_ = kCfExecBoolConstantNone;
  }
  if (index != kCfExecBoolConstantNone) {
    uint32_t bool_constant_test_register = PushSystemTemp();
    // Check the bool constant's value.
    rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kBoolConstants);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(bool_constant_test_register);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3));
    shader_code_.push_back(
        uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
    shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_code_.push_back(index >> 5);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1u << (index & 31));
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // Open the new `if`.
    D3D10_SB_INSTRUCTION_TEST_BOOLEAN test =
        condition ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                  : D3D10_SB_INSTRUCTION_TEST_ZERO;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(bool_constant_test_register);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    // Release bool_constant_test_register.
    PopSystemTemp();
    cf_exec_bool_constant_ = index;
    cf_exec_bool_constant_condition_ = condition;
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

void DxbcShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0) {
    // 0 already added in the beginning.
    return;
  }

  // Force close all `if`s on the levels below for safety (they should be closed
  // anyway, but what if).
  // TODO(Triang3l): See if that's enough. At least in Halo 3, labels are only
  // placed between different `exec`s - however, if in some game they can be
  // located within `exec`s, this would require restoring all those `if`s after
  // the label.
  ClosePredicate();
  SetExecBoolConstant(kCfExecBoolConstantNone, false);

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
  // Force close the last `exec` if ProcessExecInstructionEnd was somehow not
  // called, just for safety.
  ClosePredicate();
  cf_exec_predicated_ = false;
  SetExecBoolConstant(kCfExecBoolConstantNone, false);

  // TODO(Triang3l): Handle PredicateClean=true somehow - still not known how it
  // should be done (execs doing setp are marked as PredicateClean=false,
  // however it's very unlikely that PredicateClean=true means clean the
  // predicate after the exec - shaders in Halo 3 have sequences of (p0) exec
  // without setp in them and without PredicateClean=false, if it was actually
  // cleaned after exec, all but the first would never be executed. Let's just
  // ignore them for now.

  if (instr.type == ParsedExecInstruction::Type::kConditional) {
    SetExecBoolConstant(instr.bool_constant_index, instr.condition);
  } else if (instr.type == ParsedExecInstruction::Type::kPredicated) {
    // The predicate will actually be checked by the next ALU/fetch instruction.
    cf_exec_predicated_ = true;
    cf_exec_predicate_condition_ = instr.condition;
  }
}

void DxbcShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  // TODO(Triang3l): Check whether is_end is conditional or not.
  if (instr.is_end) {
    // In case some instruction has flipped the predicate condition.
    if (cf_exec_predicated_) {
      CheckPredicate(cf_exec_predicated_, cf_exec_predicate_condition_);
    }
    // Break out of the main loop.
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
  ClosePredicate();
  cf_exec_predicated_ = false;
  SetExecBoolConstant(kCfExecBoolConstantNone, false);
}

void DxbcShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  // loop il<idx>, L<idx> - loop with loop data il<idx>, end @ L<idx>

  uint32_t loop_count_and_aL = PushSystemTemp();

  // Count (as uint) in bits 0:7 of the loop constant, aL in 8:15.
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kLoopConstants);
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
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
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_ZERO));
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
  rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kLoopConstants);
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
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
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
  D3D10_SB_INSTRUCTION_TEST_BOOLEAN test =
      instr.condition ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                      : D3D10_SB_INSTRUCTION_TEST_ZERO;

  if (instr.type == ParsedJumpInstruction::Type::kConditional) {
    uint32_t bool_constant_test_register = PushSystemTemp();
    // Check the bool constant's value.
    rdef_constants_used_ |= 1ull << uint32_t(RdefConstantIndex::kBoolConstants);
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
    shader_code_.push_back(bool_constant_test_register);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, 3));
    shader_code_.push_back(
        uint32_t(RdefConstantBufferIndex::kBoolLoopConstants));
    shader_code_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_code_.push_back(instr.bool_constant_index >> 5);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(1u << (instr.bool_constant_index & 31));
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
    // Open the `if`.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
    shader_code_.push_back(bool_constant_test_register);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
    // Release bool_constant_test_register.
    PopSystemTemp();
  } else if (instr.type == ParsedJumpInstruction::Type::kPredicated) {
    // Called outside of exec - need to check the predicate explicitly.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                           ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(test));
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(system_temp_ps_pc_p0_a0_);
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
  }

  JumpToLabel(instr.target_address);

  if (instr.type == ParsedJumpInstruction::Type::kConditional ||
      instr.type == ParsedJumpInstruction::Type::kPredicated) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    ++stat_.instruction_count;
  }
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

  CheckPredicate(instr.is_predicated, instr.predicate_condition);

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
                                                   TextureDimension dimension) {
  // 1D and 2D textures (including stacked ones) are treated as 2D arrays for
  // binding and coordinate simplicity.
  if (dimension == TextureDimension::k1D) {
    dimension = TextureDimension::k2D;
  }
  // 1 is added to the return value because T0/t0 is shared memory.
  for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
    const TextureSRV& texture_srv = texture_srvs_[i];
    if (texture_srv.fetch_constant == fetch_constant &&
        texture_srv.dimension == dimension) {
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
      xe::format_string("xe_texture%u_%s", fetch_constant, dimension_name);
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
  CheckPredicate(instr.is_predicated, instr.predicate_condition);

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

  // TODO(Triang3l): kGetTextureBorderColorFrac, kGetTextureGradients.
  if (!is_pixel_shader() &&
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

    uint32_t srv_register;
    uint32_t srv_register_stacked;
    uint32_t sampler_register;
    if (instr.opcode == FetchOpcode::kGetTextureWeights) {
      // Only the fetch constant needed.
      srv_register = UINT32_MAX;
      srv_register_stacked = UINT32_MAX;
      sampler_register = UINT32_MAX;
    } else {
      srv_register = FindOrAddTextureSRV(tfetch_index, instr.dimension);
      // 3D or 2D stacked is selected dynamically.
      if (instr.dimension == TextureDimension::k3D) {
        srv_register_stacked =
            FindOrAddTextureSRV(tfetch_index, TextureDimension::k2D);
      } else {
        srv_register_stacked = UINT32_MAX;
      }
      sampler_register = FindOrAddSamplerBinding(
          tfetch_index, instr.attributes.mag_filter,
          instr.attributes.min_filter, instr.attributes.mip_filter,
          instr.attributes.aniso_filter);
    }

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
    shader_code_.push_back(system_temp_pv_);
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

    // Get the offset to see if the size of the texture is needed.
    // It's probably applicable to tfetchCube too, we're going to assume it's
    // used for them the same way as for stacked textures.
    // http://web.archive.org/web/20090511231340/http://msdn.microsoft.com:80/en-us/library/bb313959.aspx
    float offset_x = instr.attributes.offset_x;
    float offset_y = 0.0f, offset_z = 0.0f;
    if (instr.dimension == TextureDimension::k2D ||
        instr.dimension == TextureDimension::k3D ||
        instr.dimension == TextureDimension::kCube) {
      offset_y = instr.attributes.offset_y;
      // Don't care about the Z offset for cubemaps when getting weights because
      // zero Z will be returned anyway (the face index doesn't participate in
      // bilinear filtering).
      if (instr.dimension == TextureDimension::k3D ||
          (instr.dimension == TextureDimension::kCube &&
           instr.opcode != FetchOpcode::kGetTextureWeights)) {
        offset_z = instr.attributes.offset_z;
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
    bool has_offset = offset_x != 0.0f || offset_y != 0.0f || offset_z != 0.0f;
    if (instr.opcode == FetchOpcode::kGetTextureWeights || has_offset ||
        instr.attributes.unnormalized_coordinates ||
        instr.dimension == TextureDimension::k3D) {
      size_and_is_3d_temp = PushSystemTemp();

      rdef_constants_used_ |= 1ull
                              << uint32_t(RdefConstantIndex::kFetchConstants);

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
      shader_code_.push_back(
          uint32_t(RdefConstantBufferIndex::kFetchConstants));
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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kFetchConstants));
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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kFetchConstants));
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
          // Apply the offset.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&offset_x));
          shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&offset_y));
          shader_code_.push_back(*reinterpret_cast<const uint32_t*>(&offset_z));
          shader_code_.push_back(0);
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;
        } else {
          // Unnormalize the coordinates and apply the offset.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(has_offset ? D3D10_SB_OPCODE_MAD
                                                     : D3D10_SB_OPCODE_MUL) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(has_offset ? 12
                                                                      : 7));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(system_temp_pv_);
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
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
          shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(system_temp_pv_);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(system_temp_pv_);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(system_temp_pv_);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(system_temp_pv_);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        }
      }
    }

    if (instr.opcode == FetchOpcode::kGetTextureWeights) {
      // Return the fractional part of unnormalized coordinates (already in pv)
      // as bilinear filtering weights.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
      shader_code_.push_back(system_temp_pv_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(system_temp_pv_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } else {
      if (instr.dimension == TextureDimension::kCube) {
        // Convert cubemap coordinates passed as 2D array texture coordinates to
        // a 3D direction. We can't use a 2D array to emulate cubemaps because
        // at the edges, especially in pixel shader helper invocations, the
        // major axis changes, causing S/T to jump between 0 and 1, breaking
        // gradient calculation and causing the 1x1 mipmap to be sampled.
        ArrayCoordToCubeDirection(system_temp_pv_);
      }

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
      for (uint32_t i = 0;
           i < (instr.dimension == TextureDimension::k3D ? 2u : 1u); ++i) {
        if (i != 0) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }
        uint32_t srv_register_current =
            i != 0 ? srv_register_stacked : srv_register;
        if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
          // The non-pixel-shader case should be handled before because it just
          // returns a constant in this case.
          assert_true(is_pixel_shader());
          replicate_result = true;
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_1_SB_OPCODE_LOD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(system_temp_pv_);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
            shader_code_.push_back(system_temp_pv_);
            shader_code_.push_back(
                EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&instr.attributes.lod_bias));
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        } else if (instr.attributes.use_register_lod) {
          uint32_t lod_register, lod_component;
          if (instr.attributes.lod_bias != 0.0f) {
            // Bias the LOD in the register.
            lod_register = PushSystemTemp();
            lod_component = 0;
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
            shader_code_.push_back(lod_register);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
            shader_code_.push_back(system_temp_grad_h_lod_);
            shader_code_.push_back(
                EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&instr.attributes.lod_bias));
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          } else {
            lod_register = system_temp_grad_h_lod_;
            lod_component = 3;
          }
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_L) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
          shader_code_.push_back(srv_register_current);
          shader_code_.push_back(srv_register_current);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 2));
          shader_code_.push_back(sampler_register);
          shader_code_.push_back(sampler_register);
          shader_code_.push_back(EncodeVectorSelectOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, lod_component, 1));
          shader_code_.push_back(lod_register);
          ++stat_.instruction_count;
          ++stat_.texture_normal_instructions;
          if (instr.attributes.lod_bias != 0.0f) {
            // Release the allocated lod_register.
            PopSystemTemp();
          }
        } else if (instr.attributes.use_register_gradients) {
          // TODO(Triang3l): Apply the LOD bias somehow for register gradients
          // (possibly will require moving the bias to the sampler, which may be
          // not very good considering the sampler count is very limited).
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_D) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(system_temp_pv_);
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
          // - sample_l, when not using a computed LOD or not in a pixel shader,
          //   in this case, LOD (0 + bias) is sampled.
          // - sample, when sampling in a pixel shader (thus with derivatives)
          //   with a computed LOD.
          // - sample_b, when sampling in a pixel shader with a biased computed
          //   LOD.
          // Both sample_l and sample_b should add the LOD bias as the last
          // operand in our case.
          bool explicit_lod =
              !instr.attributes.use_computed_lod || !is_pixel_shader();
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
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
          shader_code_.push_back(system_temp_pv_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(system_temp_pv_);
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
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&instr.attributes.lod_bias));
          }
          ++stat_.instruction_count;
          if (!explicit_lod && instr.attributes.lod_bias != 0.0f) {
            ++stat_.texture_bias_instructions;
          } else {
            ++stat_.texture_normal_instructions;
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
        rdef_constants_used_ |= 1ull
                                << uint32_t(RdefConstantIndex::kFetchConstants);

        // Apply sign bias (2 * color - 1) and linearize gamma textures. This is
        // done before applying the exponent bias because this must be done on
        // color values in 0...1 range, and this is closer to the storage
        // format, while exponent bias is closer to the actual usage in shaders.
        uint32_t signs_temp = PushSystemTemp();
        // Additionally allocate some temps needed to apply this.
        uint32_t signs_value_temp = PushSystemTemp();
        uint32_t signs_select_temp = PushSystemTemp();
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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kFetchConstants));
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1));
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;

        // TODO(Triang3l): Handle TextureSign::kSigned somehow - would possibly
        // require conditionally sampling unsigned and signed versions of the
        // texture.

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
        // TODO(Triang3l): Check how SetPWLGamma effects this - currently using
        // the default curve.
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
          // 1 / 0.25
          shader_code_.push_back(0x40800000u);
          // 1 / 0.125
          shader_code_.push_back(0x41000000u);
          // 1 / 0.375
          shader_code_.push_back(0x402AAAABu);
          // 1 / 0.25
          shader_code_.push_back(0x40800000u);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
          // -0 / 0.25
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

        // Release signs_temp, signs_value_temp and signs_select_temp.
        PopSystemTemp(3);

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
        shader_code_.push_back(
            uint32_t(RdefConstantBufferIndex::kFetchConstants));
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
    }

    if (size_and_is_3d_temp != UINT32_MAX) {
      PopSystemTemp();
    }
  } else if (instr.opcode == FetchOpcode::kGetTextureGradients) {
    assert_true(is_pixel_shader());
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
    rdef_constants_used_ |= 1ull
                            << uint32_t(RdefConstantIndex::kFetchConstants);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (tfetch_index & 1) * 2, 3));
    shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kFetchConstants));
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

  if (store_result) {
    StoreResult(instr.result, system_temp_pv_, replicate_result);
  }
}

void DxbcShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  CheckPredicate(instr.is_predicated, instr.predicate_condition);
  // Whether the instruction has changed the predicate and it needs to be
  // checked again.
  bool close_predicate = false;

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
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXXXX, 1));
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
      close_predicate = true;
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

  if (close_predicate) {
    ClosePredicate();
  }
}

void DxbcShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  CheckPredicate(instr.is_predicated, instr.predicate_condition);
  // Whether the instruction has changed the predicate and it needs to be
  // checked again.
  bool close_predicate = false;

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
      UseDxbcSourceOperand(dxbc_operands[0]);
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
      close_predicate = true;
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
      close_predicate = true;
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
      close_predicate = true;
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
      close_predicate = true;
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
      close_predicate = true;
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
      UseDxbcSourceOperand(dxbc_operands[0]);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if the second operand (src0.y) is zero.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[1]));
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

  if (close_predicate) {
    ClosePredicate();
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
    {"uint3", 1, 19, 1, 3, 0, 0, RdefTypeIndex::kUnknown, nullptr},
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
        {"xe_vertex_w_format", RdefTypeIndex::kUint3, 0, 12},
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

  // ***************************************************************************
  // Header
  // ***************************************************************************

  // Constant buffer count.
  shader_object_.push_back(uint32_t(RdefConstantBufferIndex::kCount));
  // Constant buffer offset (set later).
  shader_object_.push_back(0);
  // Bound resource count (samplers, SRV, UAV, CBV).
  // + 1 for shared memory (vfetches can probably appear in pixel shaders too,
  // they are handled safely there anyway).
  shader_object_.push_back(uint32_t(sampler_bindings_.size()) + 1 +
                           uint32_t(texture_srvs_.size()) +
                           uint32_t(RdefConstantBufferIndex::kCount));
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
  // Bindings, in s#, t#, cb# order
  // ***************************************************************************

  // Write used resource names, except for constant buffers because we have
  // their names already.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t sampler_name_offset = new_offset;
  for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
    new_offset +=
        AppendString(shader_object_, sampler_bindings_[i].name.c_str());
  }
  uint32_t shared_memory_name_offset = new_offset;
  new_offset += AppendString(shader_object_, "xe_shared_memory");
  uint32_t texture_name_offset = new_offset;
  for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
    new_offset += AppendString(shader_object_, texture_srvs_[i].name.c_str());
  }

  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 3] = new_offset;

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
    // Multisampling not applicable.
    shader_object_.push_back(0);
    shader_object_.push_back(uint32_t(cbuffer.register_index));
    shader_object_.push_back(cbuffer.binding_count);
    // D3D_SIF_USERPACKED if a `cbuffer` rather than a `ConstantBuffer<T>`.
    shader_object_.push_back(cbuffer.user_packed ? 1 : 0);
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
    // Lower register bound in the space.
    shader_object_.push_back(uint32_t(cbuffer.register_index));
    // Upper register bound in the space.
    shader_object_.push_back(uint32_t(cbuffer.register_index) +
                             cbuffer.binding_count - 1);
    shader_object_.push_back((cbuffer.size + 15) >> 4);
    // Space 0.
    shader_object_.push_back(0);
  }

  // Samplers.
  for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
    const SamplerBinding& sampler_binding = sampler_bindings_[i];
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_SAMPLER) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6) |
        ENCODE_D3D10_SB_SAMPLER_MODE(D3D10_SB_SAMPLER_MODE_DEFAULT));
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
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7) |
        ENCODE_D3D10_SB_RESOURCE_DIMENSION(texture_srv_dimension));
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
  stat_.temp_register_count = system_temp_count_max_;
  if (!IndexableGPRsUsed()) {
    stat_.temp_register_count += register_count();
  }
  if (stat_.temp_register_count != 0) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_TEMPS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
    shader_object_.push_back(stat_.temp_register_count);
  }

  // General-purpose registers if using dynamic indexing (x0).
  if (IndexableGPRsUsed()) {
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
