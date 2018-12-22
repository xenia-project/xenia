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
#include <memory>
#include <sstream>

#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/string.h"

namespace xe {
namespace gpu {
using namespace ucode;

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

  // Select whether shared memory is an SRV or a UAV (depending on whether
  // memexport is used in the pipeline) - check the flag.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(system_temp_pv_);
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

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                         ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                             D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(system_temp_pv_);
  ++stat_.instruction_count;
  ++stat_.dynamic_flow_control_count;

  // Load the vertex data from the shared memory at U0.
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_RAW) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
  shader_code_.push_back(EncodeVectorMaskedOperand(
      D3D10_SB_OPERAND_TYPE_TEMP, (1 << load_dword_count) - 1, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(system_temp_pv_);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW,
      kSwizzleXYZW & ((1 << (load_dword_count * 2)) - 1), 2));
  shader_code_.push_back(0);
  shader_code_.push_back(uint32_t(UAVRegister::kSharedMemory));
  ++stat_.instruction_count;
  ++stat_.texture_load_instructions;

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

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

  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  ++stat_.instruction_count;

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
  if (IsDxbcPixelShader()) {
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
  if (!IsDxbcPixelShader() &&
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
            assert_true(IsDxbcPixelShader());
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
            shader_code_.push_back(
                EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
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
            shader_code_.push_back(
                EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
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
            shader_code_.push_back(
                EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
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
                !instr.attributes.use_computed_lod || !IsDxbcPixelShader();
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
            shader_code_.push_back(
                EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
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
    assert_true(IsDxbcPixelShader());
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

}  // namespace gpu
}  // namespace xe
