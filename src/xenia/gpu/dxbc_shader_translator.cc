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

DxbcShaderTranslator::DxbcShaderTranslator() {}
DxbcShaderTranslator::~DxbcShaderTranslator() = default;

void DxbcShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  shader_code_.clear();

  rdef_constants_used_ = 0;
  writes_depth_ = false;
}

std::vector<uint8_t> DxbcShaderTranslator::CompleteTranslation() {
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
  size_t size_aligned = xe::align(size_aligned, sizeof(uint32_t));
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

        {"xe_float_constants", RdefTypeIndex::kFloatConstantPageArray, 0,
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

void DxbcShaderTranslator::WriteResourceDefinitions() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  // TODO(Triang3l): Include shared memory, textures, samplers.
  uint32_t binding_count = uint32_t(RdefConstantBufferIndex::kCount);

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
  // Bindings
  // ***************************************************************************
  // TODO(Triang3l): Shared memory, textures, samplers.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  // TODO(Triang3l): t# and s# names.
  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 3] = new_offset;
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
    // Not multisampled.
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
    // TEXCOORD. Always used because ps_param_gen is handled dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(kPointParametersTexCoord);
    shader_object_.push_back(0);
    shader_object_.push_back(3);
    shader_object_.push_back(kPSInPointParametersRegister);
    shader_object_.push_back(0x7 | (0x7 << 8));

    // Position (only XY). Always used because ps_param_gen is handled
    // dynamically.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    // D3D_NAME_POSITION.
    shader_object_.push_back(1);
    shader_object_.push_back(3);
    shader_object_.push_back(kPSInPositionRegister);
    shader_object_.push_back(0xF | (0xF << 8));

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
    shader_object_.push_back(1);
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

}  // namespace gpu
}  // namespace xe
