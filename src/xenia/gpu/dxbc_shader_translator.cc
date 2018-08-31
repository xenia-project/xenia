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
  writes_depth_ = false;

  std::memset(&stat_, 0, sizeof(stat_));
}

void DxbcShaderTranslator::CompleteVertexShaderCode() {}

void DxbcShaderTranslator::CompletePixelShaderCode() {
  // Remap color outputs from Xbox 360 to Direct3D 12 indices.
  // temp = xe_color_output_map;
  // [unroll] for (uint i = 0; i < 4; ++i) {
  //   o[i] = x1[temp[i]];
  // }
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
      D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32));
  shader_code_.push_back(uint32_t(TempRegister::kColorOutputMap));
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
      D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_3D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          2, D3D10_SB_OPERAND_INDEX_IMMEDIATE32));
  shader_code_.push_back(uint32_t(RdefConstantBufferIndex::kSystemConstants));
  shader_code_.push_back(uint32_t(CbufferRegister::kSystemConstants));
  shader_code_.push_back(kSysConst_ColorOutputMap_Vec);
  ++stat_.instruction_count;
  for (uint32_t i = 0; i < 4; ++i) {
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
            D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
        D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
            0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32));
    shader_code_.push_back(i);
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
            D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
        D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP) |
        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
            0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
            1, D3D10_SB_OPERAND_INDEX_RELATIVE));
    shader_code_.push_back(1);
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
            D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(i) |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
            0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32));
    shader_code_.push_back(uint32_t(TempRegister::kColorOutputMap));
    ++stat_.instruction_count;
    ++stat_.array_instruction_count;
    ++stat_.mov_instruction_count;
  }
}

void DxbcShaderTranslator::CompleteShaderCode() {
  // Write stage-specific epilogue.
  if (is_vertex_shader()) {
    CompleteVertexShaderCode();
  } else if (is_pixel_shader()) {
    CompletePixelShaderCode();
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
  // TODO(Triang3l): Bound resource buffer offset (set later).
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
    // Interpolators, point parameters (coordinates, size), screen position.
    shader_object_.push_back(kInterpolatorCount + 2);
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
  const uint32_t binding_operand_token =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
      D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_3D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          2, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  const uint32_t input_operand_unmasked_token =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  const uint32_t output_operand_unmasked_token =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

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
    shader_object_.push_back(
        binding_operand_token |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER));
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
  shader_object_.push_back(
      binding_operand_token |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_RESOURCE));
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
    shader_object_.push_back(input_operand_unmasked_token |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_X);
    shader_object_.push_back(kVSInVertexIndexRegister);
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_VERTEX_ID));
    ++stat_.dcl_count;
    // Interpolator output.
    for (uint32_t i = 0; i < kInterpolatorCount; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(output_operand_unmasked_token |
                               D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);
      shader_object_.push_back(kVSOutInterpolatorRegister + i);
      ++stat_.dcl_count;
    }
    // Point parameters output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_object_.push_back(output_operand_unmasked_token |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_Y |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_Z);
    shader_object_.push_back(kVSOutPointParametersRegister);
    ++stat_.dcl_count;
    // Position output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(output_operand_unmasked_token |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);
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
      shader_object_.push_back(input_operand_unmasked_token |
                               D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);
      shader_object_.push_back(kPSInInterpolatorRegister + i);
      ++stat_.dcl_count;
    }
    // Point parameters input (only coordinates, not size, needed).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
            D3D10_SB_INTERPOLATION_LINEAR));
    shader_object_.push_back(input_operand_unmasked_token |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_Y);
    shader_object_.push_back(kPSInPointParametersRegister);
    ++stat_.dcl_count;
    // Position input (only XY needed).
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4) |
        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
            D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE));
    shader_object_.push_back(input_operand_unmasked_token |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                             D3D10_SB_OPERAND_4_COMPONENT_MASK_Y);
    shader_object_.push_back(kPSInPositionRegister);
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
    ++stat_.dcl_count;
    // Color output.
    for (uint32_t i = 0; i < 4; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(output_operand_unmasked_token |
                               D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);
      shader_object_.push_back(i);
      ++stat_.dcl_count;
    }
    // Depth output.
    if (writes_depth_) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D));
      ++stat_.dcl_count;
    }
  }

  // General-purpose registers (x0).
  shader_object_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
  // x0.
  shader_object_.push_back(0);
  shader_object_.push_back(register_count());
  // 4 components in each.
  shader_object_.push_back(4);
  stat_.temp_array_count += register_count();

  // Color outputs on the Xbox 360 side (x1), for remapping to D3D12 bindings.
  if (is_pixel_shader()) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(1);
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
        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH) |
        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D));
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D));
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
