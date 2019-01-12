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

#include "third_party/dxbc/DXBCChecksum.h"
#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/assert.h"

DEFINE_bool(dxbc_switch, true,
            "Use switch rather than if for flow control. Turning this off or "
            "on may improve stability, though this heavily depends on the "
            "driver - on AMD, it's recommended to have this set to true, as "
            "Halo 3 appears to crash when if is used for flow control "
            "(possibly the shader compiler tries to flatten them). On Intel "
            "HD Graphics, this is ignored because of a crash with the switch "
            "instruction.");
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

DxbcShaderTranslator::DxbcShaderTranslator(uint32_t vendor_id,
                                           bool edram_rov_used)
    : vendor_id_(vendor_id), edram_rov_used_(edram_rov_used) {
  // Don't allocate again and again for the first shader.
  shader_code_.reserve(8192);
  shader_object_.reserve(16384);
  float_constant_index_offsets_.reserve(512);
}
DxbcShaderTranslator::~DxbcShaderTranslator() = default;

std::vector<uint8_t> DxbcShaderTranslator::ForceEarlyDepthStencil(
    const uint8_t* shader) {
  const uint32_t* old_shader = reinterpret_cast<const uint32_t*>(shader);

  // To return something anyway even if patching fails.
  std::vector<uint8_t> new_shader;
  uint32_t shader_size_bytes = old_shader[6];
  new_shader.resize(shader_size_bytes);
  std::memcpy(new_shader.data(), shader, shader_size_bytes);

  // Find the SHEX chunk.
  uint32_t chunk_count = old_shader[7];
  for (uint32_t i = 0; i < chunk_count; ++i) {
    uint32_t chunk_offset_bytes = old_shader[8 + i];
    const uint32_t* chunk = old_shader + chunk_offset_bytes / sizeof(uint32_t);
    if (chunk[0] != 'XEHS') {
      continue;
    }
    // Find dcl_globalFlags and patch it.
    uint32_t code_size_dwords = chunk[3];
    chunk += 4;
    for (uint32_t j = 0; j < code_size_dwords;) {
      uint32_t opcode_token = chunk[j];
      uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);
      if (opcode == D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) {
        opcode_token |= D3D11_SB_GLOBAL_FLAG_FORCE_EARLY_DEPTH_STENCIL;
        std::memcpy(new_shader.data() +
                        (chunk_offset_bytes + (4 + j) * sizeof(uint32_t)),
                    &opcode_token, sizeof(uint32_t));
        // Recalculate the checksum since the shader was modified.
        CalculateDXBCChecksum(
            reinterpret_cast<unsigned char*>(new_shader.data()),
            shader_size_bytes,
            reinterpret_cast<unsigned int*>(new_shader.data() +
                                            sizeof(uint32_t)));
        break;
      }
      if (opcode == D3D10_SB_OPCODE_CUSTOMDATA) {
        j += chunk[j + 1];
      } else {
        j += DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token);
      }
    }
    break;
  }

  return std::move(new_shader);
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

  memexport_alloc_current_count_ = 0;

  std::memset(&stat_, 0, sizeof(stat_));
}

bool DxbcShaderTranslator::UseSwitchForControlFlow() const {
  // Xenia crashes on Intel HD Graphics 4000 with switch.
  return FLAGS_dxbc_switch && vendor_id_ != 0x8086;
}

uint32_t DxbcShaderTranslator::PushSystemTemp(bool zero, uint32_t count) {
  uint32_t register_index = system_temp_count_current_;
  if (!uses_register_dynamic_addressing() && !is_depth_only_pixel_shader_) {
    // Guest shader registers first if they're not in x0. Depth-only pixel
    // shader is a special case of the DXBC translator usage, where there are no
    // GPRs because there's no shader to translate, and a guest shader is not
    // loaded.
    register_index += register_count();
  }
  system_temp_count_current_ += count;
  system_temp_count_max_ =
      std::max(system_temp_count_max_, system_temp_count_current_);

  if (zero) {
    for (uint32_t i = 0; i < count; ++i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
      shader_code_.push_back(register_index + i);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
    }
  }

  return register_index;
}

void DxbcShaderTranslator::PopSystemTemp(uint32_t count) {
  assert_true(count <= system_temp_count_current_);
  system_temp_count_current_ -= std::min(count, system_temp_count_current_);
}

void DxbcShaderTranslator::StartVertexShader_LoadVertexIndex() {
  // Vertex index is in an input bound to SV_VertexID, byte swapped according to
  // xe_vertex_index_endian_and_edge_factors system constant and written to GPR
  // 0 (which is always present because register_count includes +1).

  // xe_vertex_index_endian_and_edge_factors & 0b11 is:
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
  // ubfe reg.zw, l(0, 0, 1, 1).zw, l(0, 0, 0, 1).zw,
  //      xe_vertex_index_endian_and_edge_factors.xx
  // ABCD | BADC | 8in16/16in32? | 8in32/16in32?
  system_constants_used_ |= 1ull
                            << kSysConst_VertexIndexEndianAndEdgeFactors_Index;
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
  shader_code_.push_back(EncodeVectorReplicatedOperand(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
      kSysConst_VertexIndexEndianAndEdgeFactors_Comp, 3));
  shader_code_.push_back(uint32_t(cbuffer_index_system_constants_));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_VertexIndexEndianAndEdgeFactors_Vec);
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
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
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

void DxbcShaderTranslator::StartVertexOrDomainShader() {
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

  if (IsDxbcVertexShader()) {
    // Write the vertex index to GPR 0.
    StartVertexShader_LoadVertexIndex();
  } else if (IsDxbcDomainShader()) {
    uint32_t temp_register_operand_length =
        uses_register_dynamic_addressing() ? 3 : 2;

    // Copy the domain location to r0.yz (for quad patches) or r0.xyz (for
    // triangle patches), and also set the domain in STAT.
    uint32_t domain_location_mask, domain_location_swizzle;
    if (vertex_shader_type_ == VertexShaderType::kTriangleDomain) {
      domain_location_mask = 0b0111;
      // ZYX swizzle with r1.y == 0, according to the water shader in
      // Banjo-Kazooie: Nuts & Bolts.
      domain_location_swizzle = 0b00000110;
      stat_.tessellator_domain = D3D11_SB_TESSELLATOR_DOMAIN_TRI;
    } else {
      assert_true(vertex_shader_type_ == VertexShaderType::kQuadDomain);
      // According to the ground shader in Viva Pinata, though it's impossible
      // (as of December 12th, 2018) to test there since it possibly requires
      // memexport for ground control points (the memory region with them is
      // filled with zeros).
      domain_location_mask = 0b0110;
      domain_location_swizzle = 0b00000100;
      stat_.tessellator_domain = D3D11_SB_TESSELLATOR_DOMAIN_QUAD;
    }
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                               2 + temp_register_operand_length));
    if (uses_register_dynamic_addressing()) {
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, domain_location_mask, 2));
      shader_code_.push_back(0);
    } else {
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, domain_location_mask, 1));
    }
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D11_SB_OPERAND_TYPE_INPUT_DOMAIN_POINT, domain_location_swizzle, 0));
    ++stat_.instruction_count;
    if (uses_register_dynamic_addressing()) {
      ++stat_.array_instruction_count;
    } else {
      ++stat_.mov_instruction_count;
    }

    assert_true(register_count() >= 2);

    // Copy the primitive index to r0.x (for quad patches) or r1.x (for
    // triangle patches) as a float.
    // When using indexable temps, copy through a r# because x# are apparently
    // only accessible via mov.
    // TODO(Triang3l): Investigate what should be written for primitives (or
    // even control points) for non-adaptive tessellation modes (they may
    // possibly have an index buffer).
    uint32_t primitive_id_gpr_index =
        vertex_shader_type_ == VertexShaderType::kTriangleDomain ? 1 : 0;

    if (register_count() > primitive_id_gpr_index) {
      uint32_t primitive_id_temp = uses_register_dynamic_addressing()
                                       ? PushSystemTemp()
                                       : primitive_id_gpr_index;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(primitive_id_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID, 0));
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
      if (uses_register_dynamic_addressing()) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b0001, 2));
        shader_code_.push_back(0);
        shader_code_.push_back(primitive_id_gpr_index);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(primitive_id_temp);
        ++stat_.instruction_count;
        ++stat_.array_instruction_count;
        // Release primitive_id_temp.
        PopSystemTemp();
      }
    }

    if (register_count() >= 2) {
      // Write the swizzle of the barycentric/UV coordinates to r1.x (for quad
      // patches) or r1.y (for triangle patches). It appears that the
      // tessellator offloads the reordering of coordinates for edges to game
      // shaders.
      //
      // In Banjo-Kazooie: Nuts & Bolts (triangle patches with per-edge
      // factors), the shader multiplies the first control point's position by
      // r0.z, the second CP's by r0.y, and the third CP's by r0.x. But before
      // doing that it swizzles r0.xyz the following way depending on the value
      // in r1.y:
      // - ZXY for 1.0.
      // - YZX for 2.0.
      // - XZY for 4.0.
      // - YXZ for 5.0.
      // - ZYX for 6.0.
      // Possibly, the logic here is that the value itself is the amount of
      // rotation of the swizzle to the right, and 1 << 2 is set when the
      // swizzle needs to be flipped before rotating.
      //
      // In Viva Pinata (quad patches with per-edge factors - not possible to
      // test however as of December 12th, 2018), if we assume that r0.y is V
      // and r0.z is U, the factors each control point value is multiplied by
      // are the following:
      // - (1-v)*(1-u), v*(1-u), (1-v)*u, v*u for 0.0 (base swizzle).
      // - v*(1-u), (1-v)*(1-u), v*u, (1-v)*u for 1.0 (YXWZ).
      // - v*u, (1-v)*u, v*(1-u), (1-v)*(1-u) for 2.0 (WZYX).
      // - (1-v)*u, v*u, (1-v)*(1-u), v*(1-u) for 3.0 (ZWXY).
      // According to the control point order at
      // https://www.khronos.org/registry/OpenGL/extensions/AMD/AMD_vertex_shader_tessellator.txt
      // the first is located at (0,0), the second at (0,1), the third at (1,0)
      // and the fourth at (1,1). So, swizzle index 0 appears to be the correct
      // one. But, this hasn't been tested yet.
      //
      // Direct3D 12 appears to be passing the coordinates in a consistent
      // order, so we can just use ZYX for triangle patches.
      uint32_t domain_location_swizzle_mask =
          vertex_shader_type_ == VertexShaderType::kTriangleDomain ? 0b0010
                                                                   : 0b0001;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + temp_register_operand_length));
      if (uses_register_dynamic_addressing()) {
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP,
                                      domain_location_swizzle_mask, 2));
        shader_code_.push_back(0);
      } else {
        shader_code_.push_back(EncodeVectorMaskedOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, domain_location_swizzle_mask, 1));
      }
      shader_code_.push_back(1);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      if (uses_register_dynamic_addressing()) {
        ++stat_.array_instruction_count;
      } else {
        ++stat_.mov_instruction_count;
      }
    }
  }
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
  if (uses_register_dynamic_addressing()) {
    for (uint32_t i = 0; i < interpolator_count; ++i) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0b1111, 2));
      shader_code_.push_back(0);
      shader_code_.push_back(i);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
      shader_code_.push_back(uint32_t(InOutRegister::kPSInInterpolators) + i);
      ++stat_.instruction_count;
      ++stat_.array_instruction_count;
    }
  } else {
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
  // Floor VPOS so with SSAA, the resulting pixel corner/center is written, not
  // the sample position.
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
  // Undo 2x resolution scale in VPOS.
  if (edram_rov_used_) {
    // Get inverse of the width/height scale.
    system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionScaleLog2_Index;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
    shader_code_.push_back(param_gen_value_temp);
    shader_code_.push_back(
        EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                  kSysConst_EDRAMResolutionScaleLog2_Comp, 3));
    shader_code_.push_back(cbuffer_index_system_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_code_.push_back(kSysConst_EDRAMResolutionScaleLog2_Vec);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(uint32_t(-(1 << 23)));
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(0x3F800000);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Convert to guest pixels.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(param_gen_value_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_INPUT, kSwizzleXYZW, 1));
    shader_code_.push_back(uint32_t(InOutRegister::kPSInPosition));
    shader_code_.push_back(
        EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
    shader_code_.push_back(param_gen_value_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
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
  if (uses_register_dynamic_addressing()) {
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
  if (IsDxbcVertexOrDomainShader()) {
    system_temp_position_ = PushSystemTemp(true);
  } else if (IsDxbcPixelShader()) {
    if (!is_depth_only_pixel_shader_) {
      // In the ROV path, no need to initialize the colors because original
      // values will be kept for the unwritten components.
      system_temps_color_ = PushSystemTemp(!edram_rov_used_, 4);
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
    // Allocate temporary registers for memexport addresses and data.
    std::memset(system_temps_memexport_address_, 0xFF,
                sizeof(system_temps_memexport_address_));
    std::memset(system_temps_memexport_data_, 0xFF,
                sizeof(system_temps_memexport_data_));
    system_temp_memexport_written_ = UINT32_MAX;
    const uint8_t* memexports_written = memexport_eM_written();
    for (uint32_t i = 0; i < kMaxMemExports; ++i) {
      uint32_t memexport_alloc_written = memexports_written[i];
      if (memexport_alloc_written == 0) {
        continue;
      }
      // If memexport is used at all, allocate a register containing whether eM#
      // have actually been written to.
      if (system_temp_memexport_written_ == UINT32_MAX) {
        system_temp_memexport_written_ = PushSystemTemp(true);
      }
      system_temps_memexport_address_[i] = PushSystemTemp(true);
      uint32_t memexport_data_index;
      while (xe::bit_scan_forward(memexport_alloc_written,
                                  &memexport_data_index)) {
        memexport_alloc_written &= ~(1u << memexport_data_index);
        system_temps_memexport_data_[i][memexport_data_index] =
            PushSystemTemp();
      }
    }

    // Allocate system temporary variables for the translated code.
    system_temp_pv_ = PushSystemTemp(true);
    system_temp_ps_pc_p0_a0_ = PushSystemTemp(true);
    system_temp_aL_ = PushSystemTemp(true);
    system_temp_loop_count_ = PushSystemTemp(true);
    system_temp_grad_h_lod_ = PushSystemTemp(true);
    system_temp_grad_v_ = PushSystemTemp(true);
  }

  // Write stage-specific prologue.
  if (IsDxbcVertexOrDomainShader()) {
    StartVertexOrDomainShader();
  } else if (IsDxbcPixelShader()) {
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
  if (UseSwitchForControlFlow()) {
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

void DxbcShaderTranslator::CompleteVertexOrDomainShader() {
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

void DxbcShaderTranslator::CompleteShaderCode() {
  if (!is_depth_only_pixel_shader_) {
    // Close the last exec, there's nothing to merge it with anymore, and we're
    // closing upper-level flow control blocks.
    CloseExecConditionals();
    // Close the last label and the switch.
    if (UseSwitchForControlFlow()) {
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

    // Write memexported data to the shared memory UAV.
    ExportToMemory();

    // Release memexport temporary registers.
    for (int i = kMaxMemExports - 1; i >= 0; --i) {
      if (system_temps_memexport_address_[i] == UINT32_MAX) {
        continue;
      }
      // Release exported data registers.
      for (int j = 4; j >= 0; --j) {
        if (system_temps_memexport_data_[i][j] != UINT32_MAX) {
          PopSystemTemp();
        }
      }
      // Release the address register.
      PopSystemTemp();
    }
    if (system_temp_memexport_written_ != UINT32_MAX) {
      PopSystemTemp();
    }
  }

  // Write stage-specific epilogue.
  if (IsDxbcVertexOrDomainShader()) {
    CompleteVertexOrDomainShader();
  } else if (IsDxbcPixelShader()) {
    CompletePixelShader();
  }

  if (IsDxbcVertexOrDomainShader()) {
    // Release system_temp_position_.
    PopSystemTemp();
  } else if (IsDxbcPixelShader()) {
    if (edram_rov_used_) {
      // Release system_temp_depth_.
      PopSystemTemp();
      if (!is_depth_only_pixel_shader_) {
        // Release system_temp_color_written_.
        PopSystemTemp();
      }
    }
    if (!is_depth_only_pixel_shader_) {
      // Release system_temps_color_.
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

  uint32_t has_pcsg = IsDxbcDomainShader() ? 1 : 0;

  // Write the shader object header.
  shader_object_.push_back('CBXD');
  // Checksum (set later).
  for (uint32_t i = 0; i < 4; ++i) {
    shader_object_.push_back(0);
  }
  shader_object_.push_back(1);
  // Size (set later).
  shader_object_.push_back(0);
  // 5 or 6 chunks - RDEF, ISGN, optionally PCSG, OSGN, SHEX, STAT.
  shader_object_.push_back(5 + has_pcsg);
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

  // Write Patch Constant SiGnature.
  if (has_pcsg) {
    chunk_position_dwords = uint32_t(shader_object_.size());
    shader_object_[10] = chunk_position_dwords * sizeof(uint32_t);
    shader_object_.push_back('GSCP');
    shader_object_.push_back(0);
    WritePatchConstantSignature();
    shader_object_[chunk_position_dwords + 1] =
        (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
        sizeof(uint32_t);
  }

  // Write Output SiGNature.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[10 + has_pcsg] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('NGSO');
  shader_object_.push_back(0);
  WriteOutputSignature();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write SHader EXtended.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[11 + has_pcsg] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('XEHS');
  shader_object_.push_back(0);
  WriteShaderCode();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write STATistics.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[12 + has_pcsg] = chunk_position_dwords * sizeof(uint32_t);
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
        // GPRs are in r# - accessing directly.
        assert_true(operand.storage_addressing_mode ==
                    InstructionStorageAddressingMode::kStatic);
        dxbc_operand.type = DxbcSourceOperand::Type::kRegister;
        dxbc_operand.index = uint32_t(operand.storage_index);
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
  // Apply overrides (for instance, for subtraction). Xenos operand modifiers
  // are ignored when forcing absolute value (though negated absolute can still
  // be forced in this case).
  if (!absolute) {
    if (operand.is_negated) {
      negate = !negate;
    }
    absolute |= operand.is_absolute_value;
  }
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

  // Apply overrides (for instance, for subtraction). Xenos operand modifiers
  // are ignored when forcing absolute value (though negated absolute can still
  // be forced in this case).
  if (!absolute) {
    if (operand.is_negated) {
      negate = !negate;
    }
    absolute |= operand.is_absolute_value;
  }
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
                                       uint32_t reg, bool replicate_x,
                                       bool can_store_memexport_address) {
  if (result.storage_target == InstructionStorageTarget::kNone ||
      !result.has_any_writes()) {
    return;
  }

  // Validate memexport writes (Halo 3 has some weird invalid ones).
  if (result.storage_target == InstructionStorageTarget::kExportAddress) {
    if (!can_store_memexport_address || memexport_alloc_current_count_ == 0 ||
        memexport_alloc_current_count_ > kMaxMemExports ||
        system_temps_memexport_address_[memexport_alloc_current_count_ - 1] ==
            UINT32_MAX) {
      return;
    }
  } else if (result.storage_target == InstructionStorageTarget::kExportData) {
    if (memexport_alloc_current_count_ == 0 ||
        memexport_alloc_current_count_ > kMaxMemExports ||
        system_temps_memexport_data_[memexport_alloc_current_count_ - 1]
                                    [result.storage_index] == UINT32_MAX) {
      return;
    }
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
        if (uses_register_dynamic_addressing()) {
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
          assert_true(is_static);
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

      case InstructionStorageTarget::kExportAddress:
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + source_length) |
            saturate_bit);
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, mask, 1));
        shader_code_.push_back(
            system_temps_memexport_address_[memexport_alloc_current_count_ -
                                            1]);
        break;

      case InstructionStorageTarget::kExportData:
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + source_length) |
            saturate_bit);
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, mask, 1));
        shader_code_.push_back(
            system_temps_memexport_data_[memexport_alloc_current_count_ - 1]
                                        [uint32_t(result.storage_index)]);
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
        shader_code_.push_back(system_temps_color_ +
                               uint32_t(result.storage_index));
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

  if (result.storage_target == InstructionStorageTarget::kExportData) {
    // Mark that the eM# has been written to and needs to be exported.
    uint32_t memexport_index = memexport_alloc_current_count_ - 1;
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(EncodeVectorMaskedOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, 1 << (memexport_index >> 2), 1));
    shader_code_.push_back(system_temp_memexport_written_);
    shader_code_.push_back(EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                     memexport_index >> 2, 1));
    shader_code_.push_back(system_temp_memexport_written_);
    shader_code_.push_back(
        EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
    shader_code_.push_back(
        1u << (uint32_t(result.storage_index) + ((memexport_index & 3) << 3)));
    ++stat_.instruction_count;
    ++stat_.uint_instruction_count;
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

void DxbcShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0) {
    // 0 already added in the beginning.
    return;
  }

  // Close flow control on the deeper levels below - prevent attempts to merge
  // execs across labels.
  CloseExecConditionals();

  if (UseSwitchForControlFlow()) {
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
    if (UseSwitchForControlFlow()) {
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

void DxbcShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  if (FLAGS_dxbc_source_map) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    EmitInstructionDisassembly();
  }

  if (instr.type == AllocType::kMemory) {
    ++memexport_alloc_current_count_;
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
        {"xe_vertex_index_endian_and_edge_factors", RdefTypeIndex::kUint, 4, 4},
        {"xe_vertex_base_index", RdefTypeIndex::kInt, 8, 4},
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
        {"xe_tessellation_factor_range", RdefTypeIndex::kFloat2, 128, 8},
        {"xe_edram_depth_range", RdefTypeIndex::kFloat2, 136, 8},
        // vec4 9
        {"xe_edram_poly_offset_front", RdefTypeIndex::kFloat2, 144, 8},
        {"xe_edram_poly_offset_back", RdefTypeIndex::kFloat2, 152, 8},
        // vec4 10
        {"xe_edram_resolution_scale_log2", RdefTypeIndex::kUint, 160, 4},
        {"xe_edram_stencil_reference", RdefTypeIndex::kUint, 164, 4},
        {"xe_edram_stencil_read_mask", RdefTypeIndex::kUint, 168, 4},
        {"xe_edram_stencil_write_mask", RdefTypeIndex::kUint, 172, 4},
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
    // + 2 for shared memory SRV and UAV (vfetches can appear in pixel shaders
    // too, and the UAV is needed for memexport, however, the choice between
    // SRV and UAV is per-pipeline, not per-shader - a resource can't be in a
    // read-only state (SRV, IBV) if it's in a read/write state such as UAV).
    resource_count +=
        uint32_t(sampler_bindings_.size()) + 2 + uint32_t(texture_srvs_.size());
  }
  if (IsDxbcPixelShader() && edram_rov_used_) {
    // EDRAM.
    ++resource_count;
  }
  shader_object_.push_back(resource_count);
  // Bound resource buffer offset (set later).
  shader_object_.push_back(0);
  if (IsDxbcVertexShader()) {
    // vs_5_1
    shader_object_.push_back(0xFFFE0501u);
  } else if (IsDxbcDomainShader()) {
    // ds_5_1
    shader_object_.push_back(0x44530501u);
  } else {
    assert_true(IsDxbcPixelShader());
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
  uint32_t shared_memory_srv_name_offset = 0;
  uint32_t texture_name_offset = 0;
  uint32_t shared_memory_uav_name_offset = 0;
  if (!is_depth_only_pixel_shader_) {
    sampler_name_offset = new_offset;
    for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
      new_offset +=
          AppendString(shader_object_, sampler_bindings_[i].name.c_str());
    }
    shared_memory_srv_name_offset = new_offset;
    new_offset += AppendString(shader_object_, "xe_shared_memory_srv");
    texture_name_offset = new_offset;
    for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
      new_offset += AppendString(shader_object_, texture_srvs_[i].name.c_str());
    }
    shared_memory_uav_name_offset = new_offset;
    new_offset += AppendString(shader_object_, "xe_shared_memory_uav");
  }
  uint32_t edram_name_offset = new_offset;
  if (IsDxbcPixelShader() && edram_rov_used_) {
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

    // Shared memory (when memexport isn't used in the pipeline).
    shader_object_.push_back(shared_memory_srv_name_offset);
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

    // Shared memory (when memexport is used in the pipeline).
    shader_object_.push_back(shared_memory_uav_name_offset);
    // D3D_SIT_UAV_RWBYTEADDRESS.
    shader_object_.push_back(8);
    // D3D_RETURN_TYPE_MIXED.
    shader_object_.push_back(6);
    // D3D_UAV_DIMENSION_BUFFER.
    shader_object_.push_back(1);
    // Multisampling not applicable.
    shader_object_.push_back(0);
    shader_object_.push_back(uint32_t(UAVRegister::kSharedMemory));
    // One binding.
    shader_object_.push_back(1);
    // No D3D_SHADER_INPUT_FLAGS.
    shader_object_.push_back(0);
    // Register space 0.
    shader_object_.push_back(0);
    // UAV ID U0.
    shader_object_.push_back(0);
  }

  if (IsDxbcPixelShader() && edram_rov_used_) {
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
    shader_object_.push_back(uint32_t(UAVRegister::kEDRAM));
    // One binding.
    shader_object_.push_back(1);
    // No D3D_SHADER_INPUT_FLAGS.
    shader_object_.push_back(0);
    // Register space 0.
    shader_object_.push_back(0);
    // UAV ID U1 or U0 depending on whether there's U0.
    shader_object_.push_back(GetEDRAMUAVIndex());
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

  if (IsDxbcVertexShader()) {
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
  } else if (IsDxbcDomainShader()) {
    // No inputs - tessellation factors specified in PCSG.
    shader_object_.push_back(0);
    // Unknown.
    shader_object_.push_back(8);
  } else {
    assert_true(IsDxbcPixelShader());
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

void DxbcShaderTranslator::WritePatchConstantSignature() {
  assert_true(IsDxbcDomainShader());

  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());

  const uint32_t signature_position_dwords = 2;
  const uint32_t signature_size_dwords = 6;

  // FXC refuses to compile without SV_TessFactor and SV_InsideTessFactor input,
  // so this is required.
  uint32_t tess_factor_count_edge, tess_factor_count_inside;
  if (vertex_shader_type_ == VertexShaderType::kTriangleDomain) {
    tess_factor_count_edge = 3;
    tess_factor_count_inside = 1;
  } else {
    assert_true(vertex_shader_type_ == VertexShaderType::kQuadDomain);
    tess_factor_count_edge = 4;
    tess_factor_count_inside = 2;
  }
  uint32_t tess_factor_count_total =
      tess_factor_count_edge + tess_factor_count_inside;
  shader_object_.push_back(tess_factor_count_total);
  // Unknown.
  shader_object_.push_back(8);

  for (uint32_t i = 0; i < tess_factor_count_total; ++i) {
    // Reserve space for the semantic name (SV_TessFactor or
    // SV_InsideTessFactor).
    shader_object_.push_back(0);
    shader_object_.push_back(
        i < tess_factor_count_edge ? i : (i - tess_factor_count_edge));
    if (vertex_shader_type_ == VertexShaderType::kTriangleDomain) {
      if (i < tess_factor_count_edge) {
        // D3D_NAME_FINAL_TRI_EDGE_TESSFACTOR.
        shader_object_.push_back(13);
      } else {
        // D3D_NAME_FINAL_TRI_INSIDE_TESSFACTOR.
        shader_object_.push_back(14);
      }
    } else {
      assert_true(vertex_shader_type_ == VertexShaderType::kQuadDomain);
      if (i < tess_factor_count_edge) {
        // D3D_NAME_FINAL_QUAD_EDGE_TESSFACTOR.
        shader_object_.push_back(11);
      } else {
        // D3D_NAME_FINAL_QUAD_INSIDE_TESSFACTOR.
        shader_object_.push_back(12);
      }
    }
    // D3D_REGISTER_COMPONENT_FLOAT32.
    shader_object_.push_back(3);
    // Not using any of these, and just assigning consecutive registers.
    shader_object_.push_back(i);
    // 1 component, none used.
    shader_object_.push_back(1);
  }

  // Write the semantic names.
  uint32_t new_offset =
      (uint32_t(shader_object_.size()) - chunk_position_dwords) *
      sizeof(uint32_t);
  for (uint32_t i = 0; i < tess_factor_count_edge; ++i) {
    uint32_t name_position_dwords = chunk_position_dwords +
                                    signature_position_dwords +
                                    i * signature_size_dwords;
    shader_object_[name_position_dwords] = new_offset;
  }
  new_offset += AppendString(shader_object_, "SV_TessFactor");
  for (uint32_t i = 0; i < tess_factor_count_inside; ++i) {
    uint32_t name_position_dwords =
        chunk_position_dwords + signature_position_dwords +
        (tess_factor_count_edge + i) * signature_size_dwords;
    shader_object_[name_position_dwords] = new_offset;
  }
  new_offset += AppendString(shader_object_, "SV_InsideTessFactor");
}

void DxbcShaderTranslator::WriteOutputSignature() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  const uint32_t signature_position_dwords = 2;
  const uint32_t signature_size_dwords = 6;

  if (IsDxbcVertexOrDomainShader()) {
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
    assert_true(IsDxbcPixelShader());
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

  uint32_t shader_type;
  if (IsDxbcVertexShader()) {
    shader_type = D3D10_SB_VERTEX_SHADER;
  } else if (IsDxbcDomainShader()) {
    shader_type = D3D11_SB_DOMAIN_SHADER;
  } else {
    assert_true(IsDxbcPixelShader());
    shader_type = D3D10_SB_PIXEL_SHADER;
  }
  shader_object_.push_back(
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(shader_type, 5, 1));
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

  if (IsDxbcDomainShader()) {
    // Not using control point data since Xenos only has a vertex shader acting
    // as both vertex shader and domain shader.
    uint32_t control_point_count;
    D3D11_SB_TESSELLATOR_DOMAIN domain;
    if (vertex_shader_type_ == VertexShaderType::kTriangleDomain) {
      control_point_count = 3;
      domain = D3D11_SB_TESSELLATOR_DOMAIN_TRI;
    } else {
      assert_true(vertex_shader_type_ == VertexShaderType::kQuadDomain);
      control_point_count = 4;
      domain = D3D11_SB_TESSELLATOR_DOMAIN_QUAD;
    }
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(
            D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT) |
        ENCODE_D3D11_SB_INPUT_CONTROL_POINT_COUNT(control_point_count) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    stat_.c_control_points = control_point_count;
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_TESS_DOMAIN) |
        ENCODE_D3D11_SB_TESS_DOMAIN(domain) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  }

  // Don't allow refactoring when converting to native code to maintain position
  // invariance (needed even in pixel shaders for oDepth invariance). Also this
  // dcl will be modified by ForceEarlyDepthStencil.
  shader_object_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) |
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

  if (!is_depth_only_pixel_shader_) {
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
  }

  // Unordered access views.
  if (!is_depth_only_pixel_shader_) {
    // Shared memory RWByteAddressBuffer.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(
            D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, kSwizzleXYZW, 3));
    shader_object_.push_back(0);
    shader_object_.push_back(uint32_t(UAVRegister::kSharedMemory));
    shader_object_.push_back(uint32_t(UAVRegister::kSharedMemory));
    shader_object_.push_back(0);
  }
  if (IsDxbcPixelShader() && edram_rov_used_) {
    // EDRAM uint32 rasterizer-ordered buffer.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(
            D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED) |
        ENCODE_D3D10_SB_RESOURCE_DIMENSION(D3D10_SB_RESOURCE_DIMENSION_BUFFER) |
        D3D11_SB_RASTERIZER_ORDERED_ACCESS |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, kSwizzleXYZW, 3));
    shader_object_.push_back(GetEDRAMUAVIndex());
    shader_object_.push_back(uint32_t(UAVRegister::kEDRAM));
    shader_object_.push_back(uint32_t(UAVRegister::kEDRAM));
    shader_object_.push_back(
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 0) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 1) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 2) |
        ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 3));
    shader_object_.push_back(0);
  }

  // Inputs and outputs.
  if (IsDxbcVertexOrDomainShader()) {
    if (IsDxbcDomainShader()) {
      // Domain location input (barycentric for triangles, UV for quads).
      uint32_t domain_location_mask;
      if (vertex_shader_type_ == VertexShaderType::kTriangleDomain) {
        domain_location_mask = 0b0111;
      } else {
        assert_true(vertex_shader_type_ == VertexShaderType::kQuadDomain);
        domain_location_mask = 0b0011;
      }
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
      shader_object_.push_back(EncodeVectorMaskedOperand(
          D3D11_SB_OPERAND_TYPE_INPUT_DOMAIN_POINT, domain_location_mask, 0));
      ++stat_.dcl_count;
      // Primitive index input.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
      shader_object_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID, 0));
      ++stat_.dcl_count;
    } else {
      // Unswapped vertex index input (only X component).
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_SGV) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kVSInVertexIndex));
      shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_VERTEX_ID));
      ++stat_.dcl_count;
    }
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
  } else if (IsDxbcPixelShader()) {
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
  if (!is_depth_only_pixel_shader_ && !uses_register_dynamic_addressing()) {
    stat_.temp_register_count += register_count();
  }
  if (stat_.temp_register_count != 0) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_TEMPS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
    shader_object_.push_back(stat_.temp_register_count);
  }

  // General-purpose registers if using dynamic indexing (x0).
  if (!is_depth_only_pixel_shader_ && uses_register_dynamic_addressing()) {
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
  if (!edram_rov_used_ && IsDxbcPixelShader() && writes_depth()) {
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
