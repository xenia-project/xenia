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

  // IF SYSTEM CONSTANTS ARE CHANGED OR ADDED, THE FOLLOWING MUST BE UPDATED:
  // - kSysConst enum (registers and first components).
  // - rdef_constants_.
  // - rdef_constant_buffers_ system constant buffer size.
  // - d3d12/shaders/xenos_draw.hlsli (for geometry shaders).
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

  void StartTranslation() override;

  std::vector<uint8_t> CompleteTranslation() override;

  void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  static constexpr uint32_t kFloatConstantsPerPage = 32;
  static constexpr uint32_t kFloatConstantPageCount = 8;

  // Constant buffer bindings in space 0.
  enum class CbufferRegister {
    kSystemConstants,
    kBoolLoopConstants,
    kFetchConstants,
    kFloatConstantsFirst,
    kFloatConstantsLast = kFloatConstantsFirst + kFloatConstantPageCount - 1,
  };

  enum : uint32_t {
    kSysConst_MulRcpW_Vec = 0,
    kSysConst_MulRcpW_Comp = 0,
    kSysConst_VertexBaseIndex_Vec = 0,
    kSysConst_VertexBaseIndex_Comp = 3,

    kSysConst_NDCScale_Vec = 1,
    kSysConst_NDCScale_Comp = 0,
    kSysConst_VertexIndexEndian_Vec = 1,
    kSysConst_VertexIndexEndian_Comp = 3,

    kSysConst_NDCOffset_Vec = 2,
    kSysConst_NDCOffset_Comp = 0,
    kSysConst_PixelHalfPixelOffset_Vec = 2,
    kSysConst_PixelHalfPixelOffset_Comp = 3,

    kSysConst_PointSize_Vec = 3,
    kSysConst_PointSize_Comp = 0,
    kSysConst_SSAAInvScale_Vec = 0,
    kSysConst_SSAAInvScale_Comp = 2,

    kSysConst_PixelPosReg_Vec = 4,
    kSysConst_PixelPosReg_Comp = 0,
    kSysConst_AlphaTest_Vec = 4,
    kSysConst_AlphaTest_Comp = 1,
    kSysConst_AlphaTestRange_Vec = 4,
    kSysConst_AlphaTestRange_Comp = 2,

    kSysConst_ColorExpBias_Vec = 5,

    kSysConst_ColorOutputMap_Vec = 6,
  };

  static constexpr uint32_t kInterpolatorCount = 16;
  static constexpr uint32_t kPointParametersTexCoord = kInterpolatorCount;

  // IF ANY OF THESE ARE CHANGED, WriteInputSignature and WriteOutputSignature
  // MUST BE UPDATED!

  static constexpr uint32_t kVSInVertexIndexRegister = 0;
  static constexpr uint32_t kVSOutInterpolatorRegister = 0;
  static constexpr uint32_t kVSOutPointParametersRegister =
      kVSOutInterpolatorRegister + kInterpolatorCount;
  static constexpr uint32_t kVSOutPositionRegister =
      kVSOutPointParametersRegister + 1;

  static constexpr uint32_t kPSInInterpolatorRegister = 0;
  static constexpr uint32_t kPSInPointParametersRegister =
      kPSInInterpolatorRegister + kInterpolatorCount;
  static constexpr uint32_t kPSInPositionRegister =
      kPSInPointParametersRegister + 1;
  static constexpr uint32_t kPSInFrontFaceRegister = kPSInPositionRegister + 1;

  static constexpr uint32_t kSwizzleXYZW = 0b11100100;
  static constexpr uint32_t kSwizzleXXXX = 0b00000000;
  static constexpr uint32_t kSwizzleYYYY = 0b01010101;
  static constexpr uint32_t kSwizzleZZZZ = 0b10101010;
  static constexpr uint32_t kSwizzleWWWW = 0b11111111;

  // Operand encoding, with 32-bit immediate indices by default. None of the
  // arguments must be shifted when calling.
  static constexpr uint32_t EncodeScalarOperand(
      uint32_t type, uint32_t index_dimension,
      uint32_t index_representation_0 = 0, uint32_t index_representation_1 = 0,
      uint32_t index_representation_2 = 0) {
    // D3D10_SB_OPERAND_1_COMPONENT.
    return 1 | (type << 12) | (index_dimension << 20) |
           (index_representation_0 << 22) | (index_representation_1 << 25) |
           (index_representation_0 << 28);
  }
  // For writing to vectors. Mask literal can be written as 0bWZYX.
  static constexpr uint32_t EncodeVectorMaskedOperand(
      uint32_t type, uint32_t mask, uint32_t index_dimension,
      uint32_t index_representation_0 = 0, uint32_t index_representation_1 = 0,
      uint32_t index_representation_2 = 0) {
    // D3D10_SB_OPERAND_4_COMPONENT, D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE.
    return 2 | (0 << 2) | (mask << 4) | (type << 12) | (index_dimension << 20) |
           (index_representation_0 << 22) | (index_representation_1 << 25) |
           (index_representation_2 << 28);
  }
  // For reading from vectors. Swizzle can be written as 0bWWZZYYXX.
  static constexpr uint32_t EncodeVectorSwizzledOperand(
      uint32_t type, uint32_t swizzle, uint32_t index_dimension,
      uint32_t index_representation_0 = 0, uint32_t index_representation_1 = 0,
      uint32_t index_representation_2 = 0) {
    // D3D10_SB_OPERAND_4_COMPONENT, D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE.
    return 2 | (1 << 2) | (swizzle << 4) | (type << 12) |
           (index_dimension << 20) | (index_representation_0 << 22) |
           (index_representation_1 << 25) | (index_representation_2 << 28);
  }
  // For reading a single component of a vector as a 4-component vector.
  static constexpr uint32_t EncodeVectorReplicatedOperand(
      uint32_t type, uint32_t component, uint32_t index_dimension,
      uint32_t index_representation_0 = 0, uint32_t index_representation_1 = 0,
      uint32_t index_representation_2 = 0) {
    // D3D10_SB_OPERAND_4_COMPONENT, D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE.
    return 2 | (1 << 2) | (component << 4) | (component << 6) |
           (component << 8) | (component << 10) | (type << 12) |
           (index_dimension << 20) | (index_representation_0 << 22) |
           (index_representation_1 << 25) | (index_representation_2 << 28);
  }
  // For reading scalars from vectors.
  static constexpr uint32_t EncodeVectorSelectOperand(
      uint32_t type, uint32_t component, uint32_t index_dimension,
      uint32_t index_representation_0 = 0, uint32_t index_representation_1 = 0,
      uint32_t index_representation_2 = 0) {
    // D3D10_SB_OPERAND_4_COMPONENT, D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE.
    return 2 | (2 << 2) | (component << 4) | (type << 12) |
           (index_dimension << 20) | (index_representation_0 << 22) |
           (index_representation_1 << 25) | (index_representation_2 << 28);
  }

  // Allocates a new r# register for internal use and returns its index.
  uint32_t PushSystemTemp(bool zero = false);
  // Frees the last allocated internal r# registers for later reuse.
  void PopSystemTemp(uint32_t count = 1);

  // Gets the x# register array used for color outputs (since colors are
  // remapped).
  inline uint32_t GetColorIndexableTemp() const {
    return uses_register_dynamic_addressing() ? 1 : 0;
  }

  // Writing the prologue.
  void StartVertexShader_LoadVertexIndex();
  void StartVertexShader();
  void StartPixelShader();

  // Writing the epilogue.
  void CompleteVertexShader();
  void CompletePixelShader();
  void CompleteShaderCode();

  // Abstract 4-component vector source operand.
  struct DxbcSourceOperand {
    enum class Type {
      // GPR number in the index - used only when GPRs are not dynamically
      // indexed in the shader and there are no constant zeros and ones in the
      // swizzle.
      kRegister,
      // Immediate: float constant vector number in the index.
      // Dynamic: intermediate X contains page number, intermediate Y contains
      // vector number in the page.
      kConstantFloat,
      // The whole value preloaded to the intermediate register - used for GPRs
      // when they are indexable, for bool/loop constants pre-converted to
      // float, and for other operands if their swizzle contains 0 or 1.
      kIntermediateRegister,
      // Literal vector of zeros and positive or negative ones - when the
      // swizzle contains only them, or when the parsed operand is invalid (for
      // example, if it's a fetch constant in a non-tfetch texture instruction).
      // 0 or 1 specified in the index as bits, can be negated.
      kZerosOnes,
    };

    Type type;
    uint32_t index;
    bool is_dynamic_indexed;

    uint32_t swizzle;
    bool is_negated;
    bool is_absolute_value;

    // Temporary register containing data required to access the value if it has
    // to be accessed in multiple operations (allocated with PushSystemTemp).
    uint32_t intermediate_register;
    static constexpr uint32_t kIntermediateRegisterNone = UINT32_MAX;
  };
  // Each Load must be followed by Unload, otherwise there may be a temporary
  // register leak.
  void LoadDxbcSourceOperand(const InstructionOperand& operand,
                             DxbcSourceOperand& dxbc_operand);
  // Number of tokens this operand adds to the instruction length when used.
  uint32_t DxbcSourceOperandLength(const DxbcSourceOperand& operand,
                                   bool negate = false) const;
  // Writes the operand access tokens to the instruction (either for a scalar if
  // select_component is <= 3, or for a vector).
  void UseDxbcSourceOperand(const DxbcSourceOperand& operand,
                            uint32_t additional_swizzle = kSwizzleXYZW,
                            uint32_t select_component = 4, bool negate = false);
  void UnloadDxbcSourceOperand(const DxbcSourceOperand& operand);

  // Writes xyzw or xxxx of the specified r# to the destination.
  void StoreResult(const InstructionResult& result, uint32_t reg,
                   bool replicate_x);

  // Emits copde for endian swapping of the data located in pv.
  void SwapVertexData(uint32_t vfetch_index, uint32_t write_mask);

  void ProcessVectorAluInstruction(const ParsedAluInstruction& instr);
  void ProcessScalarAluInstruction(const ParsedAluInstruction& instr);

  // Appends a string to a DWORD stream, returns the DWORD-aligned length.
  static uint32_t AppendString(std::vector<uint32_t>& dest, const char* source);

  void WriteResourceDefinitions();
  void WriteInputSignature();
  void WriteOutputSignature();
  void WriteShaderCode();

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
    // Bool constants.
    kUint4Array8,
    // Loop constants.
    kUint4Array32,
    // Fetch constants.
    kUint4Array48,
    // Float constants in one page.
    kFloatConstantPageArray,
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
    CbufferRegister register_index;
    uint32_t binding_count;
    // True if created like `cbuffer`, false for `ConstantBuffer<T>`.
    bool user_packed;
    bool dynamic_indexed;
  };
  static const RdefConstantBuffer
      rdef_constant_buffers_[size_t(RdefConstantBufferIndex::kCount)];

  // Order of dcl_constantbuffer instructions, from most frequenly accessed to
  // least frequently accessed (hint to driver according to the DXBC header).
  static const RdefConstantBufferIndex
      constant_buffer_dcl_order_[size_t(RdefConstantBufferIndex::kCount)];

  // Number of currently allocated Xenia internal r# registers.
  uint32_t system_temp_count_current_;
  // Total maximum number of temporary registers ever used during this
  // translation (for the declaration).
  uint32_t system_temp_count_max_;

  // Vector ALU result/scratch (since Xenos write masks can contain swizzles).
  uint32_t system_temp_pv_;
  // Temporary register ID for previous scalar result, program counter,
  // predicate and absolute address register.
  uint32_t system_temp_ps_pc_p0_a0_;
  // Loop index stack - .x is the active loop, shifted right to .yzw on push.
  uint32_t system_temp_aL_;
  // Loop counter stack, .x is the active loop. Represents number of times
  // remaining to loop.
  uint32_t system_temp_loop_count_;

  // Position in vertex shaders (because viewport and W transformations can be
  // applied in the end of the shader).
  uint32_t system_temp_position_;

  bool writes_depth_;

  // The STAT chunk (based on Wine d3dcompiler_parse_stat).
  struct Statistics {
    uint32_t instruction_count;
    uint32_t temp_register_count;
    // Unknown in Wine.
    uint32_t def_count;
    // Only inputs and outputs.
    uint32_t dcl_count;
    uint32_t float_instruction_count;
    uint32_t int_instruction_count;
    uint32_t uint_instruction_count;
    // endif, ret.
    uint32_t static_flow_control_count;
    // if (but not else).
    uint32_t dynamic_flow_control_count;
    // Unknown in Wine.
    uint32_t macro_instruction_count;
    uint32_t temp_array_count;
    uint32_t array_instruction_count;
    uint32_t cut_instruction_count;
    uint32_t emit_instruction_count;
    uint32_t texture_normal_instructions;
    uint32_t texture_load_instructions;
    uint32_t texture_comp_instructions;
    uint32_t texture_bias_instructions;
    uint32_t texture_gradient_instructions;
    // Not including indexable temp load/store.
    uint32_t mov_instruction_count;
    // Unknown in Wine.
    uint32_t movc_instruction_count;
    uint32_t conversion_instruction_count;
    // Unknown in Wine.
    uint32_t unknown_22;
    uint32_t input_primitive;
    uint32_t gs_output_topology;
    uint32_t gs_max_output_vertex_count;
    uint32_t unknown_26;
    uint32_t unknown_27;
    uint32_t unknown_28;
    uint32_t unknown_29;
    uint32_t c_control_points;
    uint32_t hs_output_primitive;
    uint32_t hs_partitioning;
    uint32_t tessellator_domain;
    // Unknown in Wine.
    uint32_t c_barrier_instructions;
    // Unknown in Wine.
    uint32_t c_interlocked_instructions;
    // Unknown in Wine.
    uint32_t c_texture_store_instructions;
  };
  Statistics stat_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DXBC_SHADER_TRANSLATOR_H_
