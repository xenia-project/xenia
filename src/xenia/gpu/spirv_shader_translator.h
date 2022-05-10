/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "third_party/glslang/SPIRV/SpvBuilder.h"
#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {

class SpirvShaderTranslator : public ShaderTranslator {
 public:
  union Modification {
    // If anything in this is structure is changed in a way not compatible with
    // the previous layout, invalidate the pipeline storages by increasing this
    // version number (0xYYYYMMDD)!
    // TODO(Triang3l): Change to 0xYYYYMMDD once it's out of the rapid
    // prototyping stage (easier to do small granular updates with an
    // incremental counter).
    static constexpr uint32_t kVersion = 3;

    struct {
      // Dynamically indexable register count from SQ_PROGRAM_CNTL.
      uint32_t dynamic_addressable_register_count : 8;
      // Pipeline stage and input configuration.
      Shader::HostVertexShaderType host_vertex_shader_type
          : Shader::kHostVertexShaderTypeBitCount;
    } vertex;
    struct PixelShaderModification {
      // Dynamically indexable register count from SQ_PROGRAM_CNTL.
      uint32_t dynamic_addressable_register_count : 8;
      // Color outputs removed from the shader to implement a zero color write
      // mask when independent blending (and thus independent write masks) is
      // not supported without switching to a render pass with some attachments
      // actually excluded.
      uint32_t color_outputs_disabled : 4;
    } pixel;
    uint64_t value = 0;

    Modification(uint64_t modification_value = 0) : value(modification_value) {}
  };

  enum : uint32_t {
    kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal_Shift,

    kSysFlag_Count,

    kSysFlag_XYDividedByW = 1u << kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW = 1u << kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal = 1u << kSysFlag_WNotReciprocal_Shift,
  };
  static_assert(kSysFlag_Count <= 32, "Too many flags in the system constants");

  // IF SYSTEM CONSTANTS ARE CHANGED OR ADDED, THE FOLLOWING MUST BE UPDATED:
  // - SystemConstantIndex enum.
  // - Structure members in BeginTranslation.
  struct SystemConstants {
    uint32_t flags;
    xenos::Endian vertex_index_endian;
    int32_t vertex_base_index;
    uint32_t padding_vertex_base_index;

    float ndc_scale[3];
    uint32_t padding_ndc_scale;

    float ndc_offset[3];
    uint32_t padding_ndc_offset;
  };

  // The minimum limit for maxPerStageDescriptorStorageBuffers is 4, and for
  // maxStorageBufferRange it's 128 MB. These are the values of those limits on
  // Arm Mali as of November 2020. Xenia needs 512 MB shared memory to be bound,
  // therefore SSBOs must only be used for shared memory - all other storage
  // resources must be images or texel buffers.
  enum DescriptorSet : uint32_t {
    // According to the "Pipeline Layout Compatibility" section of the Vulkan
    // specification:
    // "Two pipeline layouts are defined to be "compatible for set N" if they
    //  were created with identically defined descriptor set layouts for sets
    //  zero through N, and if they were created with identical push constant
    //  ranges."
    // "Place the least frequently changing descriptor sets near the start of
    //  the pipeline layout, and place the descriptor sets representing the most
    //  frequently changing resources near the end. When pipelines are switched,
    //  only the descriptor set bindings that have been invalidated will need to
    //  be updated and the remainder of the descriptor set bindings will remain
    //  in place."
    // This is partially the reverse of the Direct3D 12's rule of placing the
    // most frequently changed descriptor sets in the beginning. Here all
    // descriptor sets with an immutable layout are placed first, in reverse
    // frequency of changing, and sets that may be different for different
    // pipeline states last.

    // Always the same descriptor set layouts for all pipeline layouts:

    // Never changed.
    kDescriptorSetSharedMemoryAndEdram,
    // Pretty rarely used and rarely changed - flow control constants.
    kDescriptorSetBoolLoopConstants,
    // May stay the same across many draws.
    kDescriptorSetSystemConstants,
    // Less frequently changed (per-material).
    kDescriptorSetFloatConstantsPixel,
    // Quite frequently changed (for one object drawn multiple times, for
    // instance - may contain projection matrices).
    kDescriptorSetFloatConstantsVertex,
    // Very frequently changed, especially for UI draws, and for models drawn in
    // multiple parts - contains vertex and texture fetch constants.
    kDescriptorSetFetchConstants,

    // Mutable part of the pipeline layout:
    kDescriptorSetMutableLayoutsStart,

    // Rarely used at all, but may be changed at an unpredictable rate when
    // vertex textures are used, combined images and samplers.
    kDescriptorSetTexturesVertex = kDescriptorSetMutableLayoutsStart,
    // Per-material, combined images and samplers.
    kDescriptorSetTexturesPixel,
    kDescriptorSetCount,
  };

  // "Xenia Emulator Microcode Translator".
  // https://github.com/KhronosGroup/SPIRV-Headers/blob/c43a43c7cc3af55910b9bec2a71e3e8a622443cf/include/spirv/spir-v.xml#L79
  static constexpr uint32_t kSpirvMagicToolId = 26;

  struct Features {
    explicit Features(const ui::vulkan::VulkanProvider& provider);
    explicit Features(bool all = false);
    unsigned int spirv_version;
    uint32_t max_storage_buffer_range;
    bool clip_distance;
    bool cull_distance;
    bool signed_zero_inf_nan_preserve_float32;
    bool denorm_flush_to_zero_float32;
  };
  SpirvShaderTranslator(const Features& features);

  uint64_t GetDefaultVertexShaderModification(
      uint32_t dynamic_addressable_register_count,
      Shader::HostVertexShaderType host_vertex_shader_type =
          Shader::HostVertexShaderType::kVertex) const override;
  uint64_t GetDefaultPixelShaderModification(
      uint32_t dynamic_addressable_register_count) const override;

  static constexpr uint32_t GetSharedMemoryStorageBufferCountLog2(
      uint32_t max_storage_buffer_range) {
    if (max_storage_buffer_range >= 512 * 1024 * 1024) {
      return 0;
    }
    if (max_storage_buffer_range >= 256 * 1024 * 1024) {
      return 1;
    }
    return 2;
  }
  uint32_t GetSharedMemoryStorageBufferCountLog2() const {
    return GetSharedMemoryStorageBufferCountLog2(
        features_.max_storage_buffer_range);
  }

  // Common functions useful not only for the translator, but also for EDRAM
  // emulation via conventional render targets.

  // Converts the color value externally clamped to [0, 31.875] to 7e3 floating
  // point, with zeros in bits 10:31, rounding to the nearest even.
  static spv::Id PreClampedFloat32To7e3(spv::Builder& builder,
                                        spv::Id f32_scalar,
                                        spv::Id ext_inst_glsl_std_450);
  // Same as PreClampedFloat32To7e3, but clamps the input to [0, 31.875].
  static spv::Id UnclampedFloat32To7e3(spv::Builder& builder,
                                       spv::Id f32_scalar,
                                       spv::Id ext_inst_glsl_std_450);
  // Converts the 7e3 number in bits [f10_shift, f10_shift + 10) to a 32-bit
  // float.
  static spv::Id Float7e3To32(spv::Builder& builder, spv::Id f10_uint_scalar,
                              uint32_t f10_shift, bool result_as_uint,
                              spv::Id ext_inst_glsl_std_450);
  // Converts the depth value externally clamped to the representable [0, 2)
  // range to 20e4 floating point, with zeros in bits 24:31, rounding to the
  // nearest even. If remap_from_0_to_0_5 is true, it's assumed that 0...1 is
  // pre-remapped to 0...0.5 in the input.
  static spv::Id PreClampedDepthTo20e4(spv::Builder& builder,
                                       spv::Id f32_scalar,
                                       bool remap_from_0_to_0_5,
                                       spv::Id ext_inst_glsl_std_450);
  // Converts the 20e4 number in bits [f24_shift, f24_shift + 10) to a 32-bit
  // float.
  static spv::Id Depth20e4To32(spv::Builder& builder, spv::Id f24_uint_scalar,
                               uint32_t f24_shift, bool remap_to_0_to_0_5,
                               bool result_as_uint,
                               spv::Id ext_inst_glsl_std_450);

 protected:
  void Reset() override;

  uint32_t GetModificationRegisterCount() const override;

  void StartTranslation() override;

  std::vector<uint8_t> CompleteTranslation() override;

  void ProcessLabel(uint32_t cf_index) override;

  void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(
      const ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(
      const ParsedLoopEndInstruction& instr) override;
  void ProcessJumpInstruction(const ParsedJumpInstruction& instr) override;

  void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  // Builder helpers.
  void SpirvCreateSelectionMerge(
      spv::Id merge_block_id, spv::SelectionControlMask selection_control_mask =
                                  spv::SelectionControlMaskNone) {
    std::unique_ptr<spv::Instruction> selection_merge_op =
        std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
    selection_merge_op->addIdOperand(merge_block_id);
    selection_merge_op->addImmediateOperand(selection_control_mask);
    builder_->getBuildPoint()->addInstruction(std::move(selection_merge_op));
  }

  Modification GetSpirvShaderModification() const {
    return Modification(current_translation().modification());
  }

  bool IsSpirvVertexShader() const {
    return is_vertex_shader() &&
           GetSpirvShaderModification().vertex.host_vertex_shader_type ==
               Shader::HostVertexShaderType::kVertex;
  }
  bool IsSpirvTessEvalShader() const {
    return is_vertex_shader() &&
           GetSpirvShaderModification().vertex.host_vertex_shader_type !=
               Shader::HostVertexShaderType::kVertex;
  }

  // Must be called before emitting any SPIR-V operations that must be in a
  // block in translator callbacks to ensure that if the last instruction added
  // was something like OpBranch - in this case, an unreachable block is
  // created.
  void EnsureBuildPointAvailable();

  void StartVertexOrTessEvalShaderBeforeMain();
  void StartVertexOrTessEvalShaderInMain();
  void CompleteVertexOrTessEvalShaderInMain();

  void StartFragmentShaderBeforeMain();
  void StartFragmentShaderInMain();

  // Updates the current flow control condition (to be called in the beginning
  // of exec and in jumps), closing the previous conditionals if needed.
  // However, if the condition is not different, the instruction-level predicate
  // conditional also won't be closed - this must be checked separately if
  // needed (for example, in jumps).
  void UpdateExecConditionals(ParsedExecInstruction::Type type,
                              uint32_t bool_constant_index, bool condition);
  // Opens or reopens the predicate check conditional for the instruction.
  // Should be called before processing a non-control-flow instruction.
  void UpdateInstructionPredication(bool predicated, bool condition);
  // Closes the instruction-level predicate conditional if it's open, useful if
  // a control flow instruction needs to do some code which needs to respect the
  // current exec conditional, but can't itself be predicated.
  void CloseInstructionPredication();
  // Closes conditionals opened by exec and instructions within them (but not by
  // labels) and updates the state accordingly.
  void CloseExecConditionals();

  spv::Id GetStorageAddressingIndex(
      InstructionStorageAddressingMode addressing_mode, uint32_t storage_index,
      bool is_float_constant = false);
  // Loads unswizzled operand without sign modifiers as float4.
  spv::Id LoadOperandStorage(const InstructionOperand& operand);
  spv::Id ApplyOperandModifiers(spv::Id operand_value,
                                const InstructionOperand& original_operand,
                                bool invert_negate = false,
                                bool force_absolute = false);
  // Returns the requested components, with the operand's swizzle applied, in a
  // condensed form, but without negation / absolute value modifiers. The
  // storage is float4, no matter what the component count of original_operand
  // is (the storage will be either r# or c#, but the instruction may be
  // scalar).
  spv::Id GetUnmodifiedOperandComponents(
      spv::Id operand_storage, const InstructionOperand& original_operand,
      uint32_t components);
  spv::Id GetOperandComponents(spv::Id operand_storage,
                               const InstructionOperand& original_operand,
                               uint32_t components, bool invert_negate = false,
                               bool force_absolute = false) {
    return ApplyOperandModifiers(
        GetUnmodifiedOperandComponents(operand_storage, original_operand,
                                       components),
        original_operand, invert_negate, force_absolute);
  }
  // If components are identical, the same Id will be written to both outputs.
  void GetOperandScalarXY(spv::Id operand_storage,
                          const InstructionOperand& original_operand,
                          spv::Id& a_out, spv::Id& b_out,
                          bool invert_negate = false,
                          bool force_absolute = false);
  // Gets the absolute value of the loaded operand if it's not absolute already.
  spv::Id GetAbsoluteOperand(spv::Id operand_storage,
                             const InstructionOperand& original_operand);
  // The type of the value must be a float vector consisting of
  // xe::bit_count(result.GetUsedResultComponents()) elements, or (to replicate
  // a scalar into all used components) float, or the value can be spv::NoResult
  // if there's no result to store (like constants only).
  void StoreResult(const InstructionResult& result, spv::Id value);

  // For Shader Model 3 multiplication (+-0 or denormal * anything = +0),
  // replaces the value with +0 if the minimum of the two operands is 0. This
  // must be called with absolute values of operands - use GetAbsoluteOperand!
  spv::Id ZeroIfAnyOperandIsZero(spv::Id value, spv::Id operand_0_abs,
                                 spv::Id operand_1_abs);
  // Return type is a xe::bit_count(result.GetUsedResultComponents())-component
  // float vector or a single float, depending on whether it's a reduction
  // instruction (check getTypeId of the result), or returns spv::NoResult if
  // nothing to store.
  spv::Id ProcessVectorAluOperation(const ParsedAluInstruction& instr,
                                    bool& predicate_written);
  // Returns a float value to write to the previous scalar register and to the
  // destination. If the return value is ps itself (in the retain_prev case),
  // returns spv::NoResult (handled as a special case, so if it's retain_prev,
  // but don't need to write to anywhere, no OpLoad(ps) will be done).
  spv::Id ProcessScalarAluOperation(const ParsedAluInstruction& instr,
                                    bool& predicate_written);

  // Perform endian swap of a uint scalar or vector.
  spv::Id EndianSwap32Uint(spv::Id value, spv::Id endian);

  spv::Id LoadUint32FromSharedMemory(spv::Id address_dwords_int);

  Features features_;

  std::unique_ptr<spv::Builder> builder_;

  std::vector<spv::Id> id_vector_temp_;
  // For helper functions like operand loading, so they don't conflict with
  // id_vector_temp_ usage in bigger callbacks.
  std::vector<spv::Id> id_vector_temp_util_;
  std::vector<unsigned int> uint_vector_temp_;
  std::vector<unsigned int> uint_vector_temp_util_;

  spv::Id ext_inst_glsl_std_450_;

  spv::Id type_void_;

  union {
    struct {
      spv::Id type_bool_;
      spv::Id type_bool2_;
      spv::Id type_bool3_;
      spv::Id type_bool4_;
    };
    // Index = component count - 1.
    spv::Id type_bool_vectors_[4];
  };
  union {
    struct {
      spv::Id type_int_;
      spv::Id type_int2_;
      spv::Id type_int3_;
      spv::Id type_int4_;
    };
    spv::Id type_int_vectors_[4];
  };
  union {
    struct {
      spv::Id type_uint_;
      spv::Id type_uint2_;
      spv::Id type_uint3_;
      spv::Id type_uint4_;
    };
    spv::Id type_uint_vectors_[4];
  };
  union {
    struct {
      spv::Id type_float_;
      spv::Id type_float2_;
      spv::Id type_float3_;
      spv::Id type_float4_;
    };
    spv::Id type_float_vectors_[4];
  };

  spv::Id const_int_0_;
  spv::Id const_int4_0_;
  spv::Id const_uint_0_;
  spv::Id const_uint4_0_;
  union {
    struct {
      spv::Id const_float_0_;
      spv::Id const_float2_0_;
      spv::Id const_float3_0_;
      spv::Id const_float4_0_;
    };
    spv::Id const_float_vectors_0_[4];
  };
  union {
    struct {
      spv::Id const_float_1_;
      spv::Id const_float2_1_;
      spv::Id const_float3_1_;
      spv::Id const_float4_1_;
    };
    spv::Id const_float_vectors_1_[4];
  };
  // vec2(0.0, 1.0), to arbitrarily VectorShuffle non-constant and constant
  // components.
  spv::Id const_float2_0_1_;

  enum SystemConstantIndex : unsigned int {
    kSystemConstantFlags,
    kSystemConstantIndexVertexIndexEndian,
    kSystemConstantIndexVertexBaseIndex,
    kSystemConstantNdcScale,
    kSystemConstantNdcOffset,
  };
  spv::Id uniform_system_constants_;
  spv::Id uniform_float_constants_;
  spv::Id uniform_bool_loop_constants_;
  spv::Id uniform_fetch_constants_;

  spv::Id buffers_shared_memory_;

  // VS as VS only - int.
  spv::Id input_vertex_index_;
  // VS as TES only - int.
  spv::Id input_primitive_id_;

  // In vertex or tessellation evaluation shaders - outputs, always
  // xenos::kMaxInterpolators.
  // In pixel shaders - inputs, min(xenos::kMaxInterpolators, register_count()).
  spv::Id input_output_interpolators_[xenos::kMaxInterpolators];
  static const std::string kInterpolatorNamePrefix;

  enum OutputPerVertexMember : unsigned int {
    kOutputPerVertexMemberPosition,
    kOutputPerVertexMemberCount,
  };
  spv::Id output_per_vertex_;

  std::array<spv::Id, xenos::kMaxColorRenderTargets> output_fragment_data_;

  std::vector<spv::Id> main_interface_;
  spv::Function* function_main_;
  // bool.
  spv::Id var_main_predicate_;
  // uint4.
  spv::Id var_main_loop_count_;
  // int4.
  spv::Id var_main_loop_address_;
  // int.
  spv::Id var_main_address_register_;
  // float.
  spv::Id var_main_previous_scalar_;
  // `base + index * stride` in dwords from the last vfetch_full as it may be
  // needed by vfetch_mini - int.
  spv::Id var_main_vfetch_address_;
  // float4[register_count()].
  spv::Id var_main_registers_;
  // VS only - float3 (special exports).
  spv::Id var_main_point_size_edge_flag_kill_vertex_;
  spv::Block* main_loop_header_;
  spv::Block* main_loop_continue_;
  spv::Block* main_loop_merge_;
  spv::Id main_loop_pc_next_;
  spv::Block* main_switch_header_;
  std::unique_ptr<spv::Instruction> main_switch_op_;
  spv::Block* main_switch_merge_;
  std::vector<spv::Id> main_switch_next_pc_phi_operands_;

  // If the exec bool constant / predicate conditional is open, block after it
  // (not added to the function yet).
  spv::Block* cf_exec_conditional_merge_;
  // If the instruction-level predicate conditional is open, block after it (not
  // added to the function yet).
  spv::Block* cf_instruction_predicate_merge_;
  // When cf_exec_conditional_merge_ is not null:
  // If the current exec conditional is based on a bool constant: the number of
  // the bool constant.
  // If it's based on the predicate value: kCfExecBoolConstantPredicate.
  uint32_t cf_exec_bool_constant_or_predicate_;
  static constexpr uint32_t kCfExecBoolConstantPredicate = UINT32_MAX;
  // When cf_exec_conditional_merge_ is not null, the expected bool constant or
  // predicate value for the current exec conditional.
  bool cf_exec_condition_;
  // When cf_instruction_predicate_merge_ is not null, the expected predicate
  // value for the current or the last instruction.
  bool cf_instruction_predicate_condition_;
  // Whether there was a `setp` in the current exec before the current
  // instruction, thus instruction-level predicate value can be different than
  // the exec-level predicate value, and can't merge two execs with the same
  // predicate condition anymore.
  bool cf_exec_predicate_written_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
