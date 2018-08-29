/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_DXBC_SHADER_TRANSLATOR_H_
#define XENIA_GPU_DXBC_SHADER_TRANSLATOR_H_

#include <vector>

#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

// Generates shader model 5_1 byte code (for Direct3D 12).
class DxbcShaderTranslator : public ShaderTranslator {
 public:
  DxbcShaderTranslator();
  ~DxbcShaderTranslator() override;

  struct SystemConstants {
    // vec4 0
    float mul_rcp_w[3];
    uint32_t vertex_base_index;

    // vec4 1
    float ndc_scale[3];
    uint32_t vertex_index_endian;

    // vec4 2
    float ndc_offset[3];
    float pixel_half_pixel_offset;

    // vec4 3
    float point_size[2];
    float ssaa_inv_scale[2];

    // vec3 4
    uint32_t pixel_pos_reg;
    // 0 - disabled, 1 - passes if in range, -1 - fails if in range.
    int32_t alpha_test;
    // The range is floats as uints so it's easier to pass infinity.
    uint32_t alpha_test_range[2];

    // vec4 5
    float color_exp_bias[4];

    // vec4 6
    uint32_t color_output_map[4];
  };

 protected:
  void Reset() override;

  std::vector<uint8_t> CompleteTranslation() override;

 private:
  // Appends a string to a DWORD stream, returns the DWORD-aligned length.
  static uint32_t AppendString(std::vector<uint32_t>& dest, const char* source);

  void WriteResourceDefinitions();

  // Executable instructions - generated during translation.
  std::vector<uint32_t> shader_code_;
  // Complete shader object, with all the needed chunks and dcl_ instructions -
  // generated in the end of translation.
  std::vector<uint32_t> shader_object_;

  // Data types used in constants buffers. Listed in dependency order.
  enum class RdefTypeIndex {
    kFloat,
    kFloat2,
    kFloat3,
    kFloat4,
    kInt,
    kUint,
    kUint4,
    // Float constants in one page.
    kFloat4Array32,
    // Bool constants.
    kUint4Array8,
    // Loop constants.
    kUint4Array32,
    // Fetch constants.
    kUint4Array48,
    kFloatConstantPageStruct,

    kCount,
    kUnknown = kCount
  };

  struct RdefStructMember {
    const char* name;
    RdefTypeIndex type;
    uint32_t offset;
  };
  static const RdefStructMember rdef_float_constant_page_member_;

  struct RdefType {
    // Name ignored for arrays.
    const char* name;
    // D3D10_SHADER_VARIABLE_CLASS.
    uint32_t type_class;
    // D3D10_SHADER_VARIABLE_TYPE.
    uint32_t type;
    uint32_t row_count;
    uint32_t column_count;
    // 0 for primitive types, 1 for structures, array size for arrays.
    uint32_t element_count;
    uint32_t struct_member_count;
    RdefTypeIndex array_element_type;
    const RdefStructMember* struct_members;
  };
  static const RdefType rdef_types_[size_t(RdefTypeIndex::kCount)];

  enum class RdefConstantIndex {
    kSystemConstantFirst,
    kSysMulRcpW = kSystemConstantFirst,
    kSysVertexBaseIndex,
    kSysNDCScale,
    kSysVertexIndexEndian,
    kSysNDCOffset,
    kSysPixelHalfPixelOffset,
    kSysPointSize,
    kSysSSAAInvScale,
    kSysPixelPosReg,
    kSysAlphaTest,
    kSysAlphaTestRange,
    kSysColorExpBias,
    kSysColorOutputMap,
    kSystemConstantLast = kSysColorOutputMap,

    kBoolConstants,
    kLoopConstants,

    kFetchConstants,

    kFloatConstants,

    kCount,
    kSystemConstantCount = kSystemConstantLast - kSystemConstantFirst + 1,
  };
  struct RdefConstant {
    const char* name;
    RdefTypeIndex type;
    uint32_t offset;
    uint32_t size;
  };
  static const RdefConstant rdef_constants_[size_t(RdefConstantIndex::kCount)];
  static_assert(uint32_t(RdefConstantIndex::kCount) <= 64,
                "Too many constants in all constant buffers - can't use a 64 "
                "bit vector to store which constants are used");
  uint64_t rdef_constants_used_;

  enum class RdefConstantBufferIndex {
    kSystemConstants,
    kBoolLoopConstants,
    kFetchConstants,
    kFloatConstants,

    kCount
  };
  struct RdefConstantBuffer {
    const char* name;
    RdefConstantIndex first_constant;
    uint32_t constant_count;
    uint32_t size;
    uint32_t register_index;
    uint32_t binding_count;
    bool dynamic_indexed;
  };
  static const RdefConstantBuffer
      rdef_constant_buffers[size_t(RdefConstantBufferIndex::kCount)];
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DXBC_SHADER_TRANSLATOR_H_
