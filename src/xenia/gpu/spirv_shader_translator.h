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

#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/spirv_builder.h"
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
    static constexpr uint32_t kVersion = 6;

    enum class DepthStencilMode : uint32_t {
      kNoModifiers,
      // Early fragment tests - enable if alpha test and alpha to coverage are
      // disabled; ignored if anything in the shader blocks early Z writing.
      kEarlyHint,
      // TODO(Triang3l): Unorm24 (rounding) and float24 (truncating and
      // rounding) output modes.
    };

    struct {
      // uint32_t 0.
      // Interpolators written by the vertex shader and needed by the pixel
      // shader.
      uint32_t interpolator_mask : xenos::kMaxInterpolators;
      // For HostVertexShaderType kPointListAsTriangleStrip, whether to output
      // the point coordinates.
      // For other HostVertexShaderTypes (though truly reachable only for
      // kVertex), whether to output the point size.
      uint32_t output_point_parameters : 1;
      // Dynamically indexable register count from SQ_PROGRAM_CNTL.
      uint32_t dynamic_addressable_register_count : 8;
      // Pipeline stage and input configuration.
      Shader::HostVertexShaderType host_vertex_shader_type
          : Shader::kHostVertexShaderTypeBitCount;
    } vertex;
    struct PixelShaderModification {
      // uint32_t 0.
      // Interpolators written by the vertex shader and needed by the pixel
      // shader.
      uint32_t interpolator_mask : xenos::kMaxInterpolators;
      uint32_t interpolators_centroid : xenos::kMaxInterpolators;
      // uint32_t 1.
      // Dynamically indexable register count from SQ_PROGRAM_CNTL.
      uint32_t dynamic_addressable_register_count : 8;
      uint32_t param_gen_enable : 1;
      uint32_t param_gen_interpolator : 4;
      // If param_gen_enable is set, this must be set for point primitives, and
      // must not be set for other primitive types - enables the point sprite
      // coordinates input, and also effects the flag bits in PsParamGen.
      uint32_t param_gen_point : 1;
      // For host render targets - depth / stencil output mode.
      DepthStencilMode depth_stencil_mode : 3;
    } pixel;
    uint64_t value = 0;

    explicit Modification(uint64_t modification_value = 0)
        : value(modification_value) {
      static_assert_size(*this, sizeof(value));
    }
  };

  enum : uint32_t {
    kSysFlag_VertexIndexLoad_Shift,
    kSysFlag_ComputeOrPrimitiveVertexIndexLoad_Shift,
    kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit_Shift,
    kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal_Shift,
    kSysFlag_PrimitivePolygonal_Shift,
    kSysFlag_PrimitiveLine_Shift,
    kSysFlag_MsaaSamples_Shift,
    kSysFlag_DepthFloat24_Shift =
        kSysFlag_MsaaSamples_Shift + xenos::kMsaaSamplesBits,
    kSysFlag_AlphaPassIfLess_Shift,
    kSysFlag_AlphaPassIfEqual_Shift,
    kSysFlag_AlphaPassIfGreater_Shift,
    kSysFlag_ConvertColor0ToGamma_Shift,
    kSysFlag_ConvertColor1ToGamma_Shift,
    kSysFlag_ConvertColor2ToGamma_Shift,
    kSysFlag_ConvertColor3ToGamma_Shift,

    kSysFlag_FSIDepthStencil_Shift,
    kSysFlag_FSIDepthPassIfLess_Shift,
    kSysFlag_FSIDepthPassIfEqual_Shift,
    kSysFlag_FSIDepthPassIfGreater_Shift,
    // 1 to write new depth to the depth buffer, 0 to keep the old one if the
    // depth test passes.
    kSysFlag_FSIDepthWrite_Shift,
    kSysFlag_FSIStencilTest_Shift,
    // If the depth / stencil test has failed, but resulted in a stencil value
    // that is different than the one currently in the depth buffer, write it
    // anyway and don't run the rest of the shader (to check if the sample may
    // be discarded some way) - use when alpha test and alpha to coverage are
    // disabled. Ignored by the shader if not applicable to it (like if it has
    // kill instructions or writes the depth output).
    // TODO(Triang3l): Investigate replacement with an alpha-to-mask flag,
    // checking `(flags & (alpha test | alpha to mask)) == (always | disabled)`,
    // taking into account the potential relation with occlusion queries (but
    // should be safe at least temporarily).
    kSysFlag_FSIDepthStencilEarlyWrite_Shift,

    kSysFlag_Count,

    // For HostVertexShaderType kVertex, if fullDrawIndexUint32 is not
    // supported (ignored otherwise), whether to fetch the index manually
    // (32-bit only - 16-bit indices are always fetched via the Vulkan index
    // buffer).
    kSysFlag_VertexIndexLoad = 1u << kSysFlag_VertexIndexLoad_Shift,
    // For HostVertexShaderTypes kMemexportCompute, kPointListAsTriangleStrip,
    // kRectangleListAsTriangleStrip, whether the vertex index needs to be
    // loaded from the index buffer (rather than using autogenerated indices),
    // and whether it's 32-bit. This is separate from kSysFlag_VertexIndexLoad
    // because the same system constants may be used for the memexporting
    // compute shader and the vertex shader for the same draw, but
    // kSysFlag_VertexIndexLoad may be not needed.
    kSysFlag_ComputeOrPrimitiveVertexIndexLoad =
        1u << kSysFlag_ComputeOrPrimitiveVertexIndexLoad_Shift,
    kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit =
        1u << kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit_Shift,
    kSysFlag_XYDividedByW = 1u << kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW = 1u << kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal = 1u << kSysFlag_WNotReciprocal_Shift,
    kSysFlag_PrimitivePolygonal = 1u << kSysFlag_PrimitivePolygonal_Shift,
    kSysFlag_PrimitiveLine = 1u << kSysFlag_PrimitiveLine_Shift,
    kSysFlag_DepthFloat24 = 1u << kSysFlag_DepthFloat24_Shift,
    kSysFlag_AlphaPassIfLess = 1u << kSysFlag_AlphaPassIfLess_Shift,
    kSysFlag_AlphaPassIfEqual = 1u << kSysFlag_AlphaPassIfEqual_Shift,
    kSysFlag_AlphaPassIfGreater = 1u << kSysFlag_AlphaPassIfGreater_Shift,
    kSysFlag_ConvertColor0ToGamma = 1u << kSysFlag_ConvertColor0ToGamma_Shift,
    kSysFlag_ConvertColor1ToGamma = 1u << kSysFlag_ConvertColor1ToGamma_Shift,
    kSysFlag_ConvertColor2ToGamma = 1u << kSysFlag_ConvertColor2ToGamma_Shift,
    kSysFlag_ConvertColor3ToGamma = 1u << kSysFlag_ConvertColor3ToGamma_Shift,
    kSysFlag_FSIDepthStencil = 1u << kSysFlag_FSIDepthStencil_Shift,
    kSysFlag_FSIDepthPassIfLess = 1u << kSysFlag_FSIDepthPassIfLess_Shift,
    kSysFlag_FSIDepthPassIfEqual = 1u << kSysFlag_FSIDepthPassIfEqual_Shift,
    kSysFlag_FSIDepthPassIfGreater = 1u << kSysFlag_FSIDepthPassIfGreater_Shift,
    kSysFlag_FSIDepthWrite = 1u << kSysFlag_FSIDepthWrite_Shift,
    kSysFlag_FSIStencilTest = 1u << kSysFlag_FSIStencilTest_Shift,
    kSysFlag_FSIDepthStencilEarlyWrite =
        1u << kSysFlag_FSIDepthStencilEarlyWrite_Shift,
  };
  static_assert(kSysFlag_Count <= 32, "Too many flags in the system constants");

  // IF SYSTEM CONSTANTS ARE CHANGED OR ADDED, THE FOLLOWING MUST BE UPDATED:
  // - SystemConstantIndex enum.
  // - Structure members in BeginTranslation.
  //
  // Using the std140 layout - vec2 must be aligned to 8 bytes, vec3 and vec4 to
  // 16 bytes.
  struct SystemConstants {
    uint32_t flags;
    uint32_t vertex_index_load_address;
    xenos::Endian vertex_index_endian;
    int32_t vertex_base_index;

    float ndc_scale[3];
    float point_vertex_diameter_min;

    float ndc_offset[3];
    float point_vertex_diameter_max;

    float point_constant_diameter[2];
    // Diameter in guest screen coordinates > radius (0.5 * diameter) in the NDC
    // for the host viewport.
    float point_screen_diameter_to_ndc_radius[2];

    // Each byte contains post-swizzle TextureSign values for each of the needed
    // components of each of the 32 used texture fetch constants.
    uint32_t texture_swizzled_signs[8];

    // If the imageViewFormatSwizzle portability subset is not supported, the
    // component swizzle (taking both guest and host swizzles into account) to
    // apply to the result directly in the shader code. In each uint32_t,
    // swizzles for 2 texture fetch constants (in bits 0:11 and 12:23).
    uint32_t texture_swizzles[16];

    float alpha_test_reference;
    uint32_t edram_32bpp_tile_pitch_dwords_scaled;
    uint32_t edram_depth_base_dwords_scaled;
    float padding_edram_depth_base_dwords_scaled;

    float color_exp_bias[4];

    float edram_poly_offset_front_scale;
    float edram_poly_offset_back_scale;
    float edram_poly_offset_front_offset;
    float edram_poly_offset_back_offset;

    union {
      struct {
        uint32_t edram_stencil_front_reference_masks;
        uint32_t edram_stencil_front_func_ops;

        uint32_t edram_stencil_back_reference_masks;
        uint32_t edram_stencil_back_func_ops;
      };
      struct {
        uint32_t edram_stencil_front[2];
        uint32_t edram_stencil_back[2];
      };
    };

    uint32_t edram_rt_base_dwords_scaled[4];

    // RT format combined with RenderTargetCache::kPSIColorFormatFlag values
    // (pass via RenderTargetCache::AddPSIColorFormatFlags).
    uint32_t edram_rt_format_flags[4];

    // Render target blending options - RB_BLENDCONTROL, with only the relevant
    // options (factors and operations - AND 0x1FFF1FFF). If 0x00010001
    // (1 * src + 0 * dst), blending is disabled for the render target.
    uint32_t edram_rt_blend_factors_ops[4];

    // Format info - mask to apply to the old packed RT data, and to apply as
    // inverted to the new packed data, before storing (more or less the inverse
    // of the write mask packed like render target channels). This can be used
    // to bypass unpacking if blending is not used. If 0 and not blending,
    // reading the old data from the EDRAM buffer is not required.
    uint32_t edram_rt_keep_mask[4][2];

    // Format info - values to clamp the color to before blending or storing.
    // Low color, low alpha, high color, high alpha.
    float edram_rt_clamp[4][4];

    // The constant blend factor for the respective modes.
    float edram_blend_constant[4];
  };

  enum ConstantBuffer : uint32_t {
    kConstantBufferSystem,
    kConstantBufferFloatVertex,
    kConstantBufferFloatPixel,
    kConstantBufferBoolLoop,
    kConstantBufferFetch,

    kConstantBufferCount,
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
    // Changed in case of changes in the data.
    kDescriptorSetConstants,

    // Mutable part of the pipeline layout:
    kDescriptorSetMutableLayoutsStart,

    // Rarely used at all, but may be changed at an unpredictable rate when
    // vertex textures are used (for example, for bones of an object, which may
    // consist of multiple draw commands with different materials).
    kDescriptorSetTexturesVertex = kDescriptorSetMutableLayoutsStart,
    // Per-material textures.
    kDescriptorSetTexturesPixel,

    kDescriptorSetCount,
  };
  static_assert(
      kDescriptorSetCount <= 4,
      "The number of descriptor sets used by translated shaders must be within "
      "the minimum Vulkan maxBoundDescriptorSets requirement of 4, which is "
      "the limit on most GPUs used in Android devices - Arm Mali, Imagination "
      "PowerVR, Qualcomm Adreno 6xx and older, as well as on old PC Nvidia "
      "drivers");

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
    bool demote_to_helper_invocation;
    bool fragment_shader_sample_interlock;
    bool full_draw_index_uint32;
    bool image_view_format_swizzle;
    bool signed_zero_inf_nan_preserve_float32;
    bool denorm_flush_to_zero_float32;
  };

  SpirvShaderTranslator(const Features& features,
                        bool native_2x_msaa_with_attachments,
                        bool native_2x_msaa_no_attachments,
                        bool edram_fragment_shader_interlock)
      : features_(features),
        native_2x_msaa_with_attachments_(native_2x_msaa_with_attachments),
        native_2x_msaa_no_attachments_(native_2x_msaa_no_attachments),
        edram_fragment_shader_interlock_(edram_fragment_shader_interlock) {}

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

  // Creates a special fragment shader without color outputs - this resets the
  // state of the translator.
  std::vector<uint8_t> CreateDepthOnlyFragmentShader();

  // Common functions useful not only for the translator, but also for EDRAM
  // emulation via conventional render targets.

  // Converts the color value externally clamped to [0, 31.875] to 7e3 floating
  // point, with zeros in bits 10:31, rounding to the nearest even.
  static spv::Id PreClampedFloat32To7e3(SpirvBuilder& builder,
                                        spv::Id f32_scalar,
                                        spv::Id ext_inst_glsl_std_450);
  // Same as PreClampedFloat32To7e3, but clamps the input to [0, 31.875].
  static spv::Id UnclampedFloat32To7e3(SpirvBuilder& builder,
                                       spv::Id f32_scalar,
                                       spv::Id ext_inst_glsl_std_450);
  // Converts the 7e3 number in bits [f10_shift, f10_shift + 10) to a 32-bit
  // float.
  static spv::Id Float7e3To32(SpirvBuilder& builder, spv::Id f10_uint_scalar,
                              uint32_t f10_shift, bool result_as_uint,
                              spv::Id ext_inst_glsl_std_450);
  // Converts the depth value externally clamped to the representable [0, 2)
  // range to 20e4 floating point, with zeros in bits 24:31, rounding to the
  // nearest even or towards zero. If remap_from_0_to_0_5 is true, it's assumed
  // that 0...1 is pre-remapped to 0...0.5 in the input.
  static spv::Id PreClampedDepthTo20e4(SpirvBuilder& builder,
                                       spv::Id f32_scalar,
                                       bool round_to_nearest_even,
                                       bool remap_from_0_to_0_5,
                                       spv::Id ext_inst_glsl_std_450);
  // Converts the 20e4 number in bits [f24_shift, f24_shift + 10) to a 32-bit
  // float.
  static spv::Id Depth20e4To32(SpirvBuilder& builder, spv::Id f24_uint_scalar,
                               uint32_t f24_shift, bool remap_to_0_to_0_5,
                               bool result_as_uint,
                               spv::Id ext_inst_glsl_std_450);

 protected:
  void Reset() override;

  uint32_t GetModificationRegisterCount() const override;

  void StartTranslation() override;

  std::vector<uint8_t> CompleteTranslation() override;

  void PostTranslation() override;

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
  void ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  struct TextureBinding {
    uint32_t fetch_constant;
    // Stacked and 3D are separate TextureBindings.
    xenos::FetchOpDimension dimension;
    bool is_signed;

    spv::Id variable;
  };

  struct SamplerBinding {
    uint32_t fetch_constant;
    xenos::TextureFilter mag_filter;
    xenos::TextureFilter min_filter;
    xenos::TextureFilter mip_filter;
    xenos::AnisoFilter aniso_filter;

    spv::Id variable;
  };

  // Builder helpers.
  spv::Id SpirvSmearScalarResultOrConstant(spv::Id scalar, spv::Id vector_type);

  Modification GetSpirvShaderModification() const {
    return Modification(current_translation().modification());
  }

  bool IsSpirvVertexShader() const {
    return is_vertex_shader() &&
           !Shader::IsHostVertexShaderTypeDomain(
               GetSpirvShaderModification().vertex.host_vertex_shader_type);
  }
  bool IsSpirvTessEvalShader() const {
    return is_vertex_shader() &&
           Shader::IsHostVertexShaderTypeDomain(
               GetSpirvShaderModification().vertex.host_vertex_shader_type);
  }

  bool IsExecutionModeEarlyFragmentTests() const {
    return is_pixel_shader() &&
           GetSpirvShaderModification().pixel.depth_stencil_mode ==
               Modification::DepthStencilMode::kEarlyHint &&
           !edram_fragment_shader_interlock_ &&
           current_shader().implicit_early_z_write_allowed();
  }

  uint32_t GetModificationInterpolatorMask() const {
    Modification modification = GetSpirvShaderModification();
    return is_vertex_shader() ? modification.vertex.interpolator_mask
                              : modification.pixel.interpolator_mask;
  }

  // Returns UINT32_MAX if PsParamGen doesn't need to be written.
  uint32_t GetPsParamGenInterpolator() const;

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
  void CompleteFragmentShaderInMain();

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
  // Conditionally discard the current fragment. Changes the build point.
  void KillPixel(spv::Id condition);
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

  // The source may be a floating-point scalar or a vector.
  spv::Id PWLGammaToLinear(spv::Id gamma, bool gamma_pre_saturated);
  spv::Id LinearToPWLGamma(spv::Id linear, bool linear_pre_saturated);

  size_t FindOrAddTextureBinding(uint32_t fetch_constant,
                                 xenos::FetchOpDimension dimension,
                                 bool is_signed);
  size_t FindOrAddSamplerBinding(uint32_t fetch_constant,
                                 xenos::TextureFilter mag_filter,
                                 xenos::TextureFilter min_filter,
                                 xenos::TextureFilter mip_filter,
                                 xenos::AnisoFilter aniso_filter);
  // `texture_parameters` need to be set up except for `sampler`, which will be
  // set internally, optionally doing linear interpolation between the an
  // existing value and the new one (the result location may be the same as for
  // the first lerp endpoint, but not across signedness).
  void SampleTexture(spv::Builder::TextureParameters& texture_parameters,
                     spv::ImageOperandsMask image_operands_mask,
                     spv::Id image_unsigned, spv::Id image_signed,
                     spv::Id sampler, spv::Id is_all_signed,
                     spv::Id is_any_signed, spv::Id& result_unsigned_out,
                     spv::Id& result_signed_out,
                     spv::Id lerp_factor = spv::NoResult,
                     spv::Id lerp_first_unsigned = spv::NoResult,
                     spv::Id lerp_first_signed = spv::NoResult);
  // `texture_parameters` need to be set up except for `sampler`, which will be
  // set internally.
  spv::Id QueryTextureLod(spv::Builder::TextureParameters& texture_parameters,
                          spv::Id image_unsigned, spv::Id image_signed,
                          spv::Id sampler, spv::Id is_all_signed);

  spv::Id LoadMsaaSamplesFromFlags();
  // Whether it's possible and worth skipping running the translated shader for
  // 2x2 quads.
  bool FSI_IsDepthStencilEarly() const {
    assert_true(edram_fragment_shader_interlock_);
    return !is_depth_only_fragment_shader_ &&
           !current_shader().writes_depth() &&
           !current_shader().is_valid_memexport_used();
  }
  void FSI_LoadSampleMask(spv::Id msaa_samples);
  void FSI_LoadEdramOffsets(spv::Id msaa_samples);
  // The address must be a signed int. Whether the render target is 64bpp, if
  // present at all, must be a bool (if it's NoResult, 32bpp will be assumed).
  spv::Id FSI_AddSampleOffset(spv::Id sample_0_address, uint32_t sample_index,
                              spv::Id is_64bpp = spv::NoResult);
  // Updates main_fsi_sample_mask_. Must be called outside non-uniform control
  // flow because of taking derivatives of the fragment depth.
  void FSI_DepthStencilTest(spv::Id msaa_samples,
                            bool sample_mask_potentially_narrowed_previouly);
  // Returns the first and the second 32 bits as two uints.
  std::array<spv::Id, 2> FSI_ClampAndPackColor(spv::Id color_float4,
                                               spv::Id format_with_flags);
  std::array<spv::Id, 4> FSI_UnpackColor(std::array<spv::Id, 2> color_packed,
                                         spv::Id format_with_flags);
  // The bounds must have the same number of components as the color or alpha.
  spv::Id FSI_FlushNaNClampAndInBlending(spv::Id color_or_alpha,
                                         spv::Id is_fixed_point,
                                         spv::Id min_value, spv::Id max_value);
  spv::Id FSI_ApplyColorBlendFactor(spv::Id value, spv::Id is_fixed_point,
                                    spv::Id clamp_min_value,
                                    spv::Id clamp_max_value, spv::Id factor,
                                    spv::Id source_color, spv::Id source_alpha,
                                    spv::Id dest_color, spv::Id dest_alpha,
                                    spv::Id constant_color,
                                    spv::Id constant_alpha);
  spv::Id FSI_ApplyAlphaBlendFactor(spv::Id value, spv::Id is_fixed_point,
                                    spv::Id clamp_min_value,
                                    spv::Id clamp_max_value, spv::Id factor,
                                    spv::Id source_alpha, spv::Id dest_alpha,
                                    spv::Id constant_alpha);
  // If source_color_clamped, dest_color, constant_color_clamped are
  // spv::NoResult, will blend the alpha. Otherwise, will blend the color.
  // The result will be unclamped (color packing is supposed to clamp it).
  spv::Id FSI_BlendColorOrAlphaWithUnclampedResult(
      spv::Id is_fixed_point, spv::Id clamp_min_value, spv::Id clamp_max_value,
      spv::Id source_color_clamped, spv::Id source_alpha_clamped,
      spv::Id dest_color, spv::Id dest_alpha, spv::Id constant_color_clamped,
      spv::Id constant_alpha_clamped, spv::Id equation, spv::Id source_factor,
      spv::Id dest_factor);

  Features features_;
  bool native_2x_msaa_with_attachments_;
  bool native_2x_msaa_no_attachments_;

  // For safety with different drivers (even though fragment shader interlock in
  // SPIR-V only has one control flow requirement - that both begin and end must
  // be dynamically executed exactly once in this order), adhering to the more
  // strict control flow limitations of OpenGL (GLSL) fragment shader interlock,
  // that begin and end are called only on the outermost level of the control
  // flow of the main function, and that there are no returns before either
  // (there's a single return from the shader).
  bool edram_fragment_shader_interlock_;

  // Is currently writing the empty depth-only pixel shader, such as for depth
  // and stencil testing with fragment shader interlock.
  bool is_depth_only_fragment_shader_ = false;

  std::unique_ptr<SpirvBuilder> builder_;

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
    kSystemConstantVertexIndexLoadAddress,
    kSystemConstantVertexIndexEndian,
    kSystemConstantVertexBaseIndex,
    kSystemConstantNdcScale,
    kSystemConstantPointVertexDiameterMin,
    kSystemConstantNdcOffset,
    kSystemConstantPointVertexDiameterMax,
    kSystemConstantPointConstantDiameter,
    kSystemConstantPointScreenDiameterToNdcRadius,
    kSystemConstantTextureSwizzledSigns,
    kSystemConstantTextureSwizzles,
    kSystemConstantAlphaTestReference,
    kSystemConstantEdram32bppTilePitchDwordsScaled,
    kSystemConstantEdramDepthBaseDwordsScaled,
    kSystemConstantColorExpBias,
    kSystemConstantEdramPolyOffsetFrontScale,
    kSystemConstantEdramPolyOffsetBackScale,
    kSystemConstantEdramPolyOffsetFrontOffset,
    kSystemConstantEdramPolyOffsetBackOffset,
    kSystemConstantEdramStencilFront,
    kSystemConstantEdramStencilBack,
    kSystemConstantEdramRTBaseDwordsScaled,
    kSystemConstantEdramRTFormatFlags,
    kSystemConstantEdramRTBlendFactorsOps,
    // Accessed as float4[2], not float2[4], due to std140 array stride
    // alignment.
    kSystemConstantEdramRTKeepMask,
    kSystemConstantEdramRTClamp,
    kSystemConstantEdramBlendConstant,
  };
  spv::Id uniform_system_constants_;
  spv::Id uniform_float_constants_;
  spv::Id uniform_bool_loop_constants_;
  spv::Id uniform_fetch_constants_;

  spv::Id buffers_shared_memory_;
  spv::Id buffer_edram_;

  // Not using combined images and samplers because
  // maxPerStageDescriptorSamplers is often lower than
  // maxPerStageDescriptorSampledImages, and for every fetch constant, there
  // are, for regular fetches, two bindings (unsigned and signed).
  std::vector<TextureBinding> texture_bindings_;
  std::vector<SamplerBinding> sampler_bindings_;

  // VS as VS only - int.
  spv::Id input_vertex_index_;
  // VS as TES only - int.
  spv::Id input_primitive_id_;
  // PS, only when needed - float2.
  spv::Id input_point_coordinates_;
  // PS, only when needed - float4.
  spv::Id input_fragment_coordinates_;
  // PS, only when needed - bool.
  spv::Id input_front_facing_;
  // PS, only when needed - int[1].
  spv::Id input_sample_mask_;

  // VS output or PS input, only the ones that are needed (spv::NoResult for the
  // unneeded interpolators), indexed by the guest interpolator index - float4.
  // The Qualcomm Adreno driver has strict requirements for stage linkage - as
  // Xenia uses separate variables, not an array (so the interpolation
  // qualifiers can be applied to each element separately), the interpolators
  // must also be separate variables in the other stage, including the geometry
  // shader (not just an array assuming that consecutive locations will be
  // linked as consecutive array elements, on Qualcomm, they won't be linked at
  // all).
  std::array<spv::Id, xenos::kMaxInterpolators> input_output_interpolators_;

  // VS, only for HostVertexShaderType::kPointListAsTriangleStrip when needed
  // for the PS - float2.
  spv::Id output_point_coordinates_;
  // VS, only when needed - float.
  spv::Id output_point_size_;

  enum OutputPerVertexMember : unsigned int {
    kOutputPerVertexMemberPosition,
    kOutputPerVertexMemberCount,
  };
  spv::Id output_per_vertex_;

  // With fragment shader interlock, variables in the main function.
  // Otherwise, framebuffer color attachment outputs.
  std::array<spv::Id, xenos::kMaxColorRenderTargets>
      output_or_var_fragment_data_;

  std::vector<spv::Id> main_interface_;
  spv::Function* function_main_;
  spv::Id main_system_constant_flags_;
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
  // float.
  spv::Id var_main_tfetch_lod_;
  // float3.
  spv::Id var_main_tfetch_gradients_h_;
  spv::Id var_main_tfetch_gradients_v_;
  // float4[register_count()].
  spv::Id var_main_registers_;
  // VS only - float3 (special exports).
  spv::Id var_main_point_size_edge_flag_kill_vertex_;
  // PS, only when needed - bool.
  spv::Id var_main_kill_pixel_;
  // PS, only when writing to color render targets with fragment shader
  // interlock - uint.
  // Whether color buffers have been written to, if not written on the taken
  // execution path, don't export according to Direct3D 9 register documentation
  // (some games rely on this behavior).
  spv::Id var_main_fsi_color_written_;
  // Loaded by FSI_LoadSampleMask.
  // Can be modified on the outermost control flow level in the main function.
  // 0:3 - Per-sample coverage at the current stage of the shader's execution.
  //       Affected by things like gl_SampleMaskIn, early or late depth /
  //       stencil (always resets bits for failing, no matter if need to defer
  //       writing), alpha to coverage.
  // 4:7 - Depth write deferred mask - when early depth / stencil resulted in a
  //       different value for the sample (like different stencil if the test
  //       failed), but can't write it before running the shader because it's
  //       not known if the sample will be discarded by the shader, alphatest or
  //       AtoC.
  // Early depth / stencil rejection of the pixel is possible when both 0:3 and
  // 4:7 are zero.
  spv::Id main_fsi_sample_mask_;
  // Loaded by FSI_LoadEdramOffsets.
  // Including the depth render target base.
  spv::Id main_fsi_address_depth_;
  // Not including the render target base.
  spv::Id main_fsi_offset_32bpp_;
  spv::Id main_fsi_offset_64bpp_;
  // Loaded by FSI_DepthStencilTest for early depth / stencil, the depth /
  // stencil values to write at the end of the shader if the specified in
  // main_fsi_sample_mask_ and if the samples were not discarded later after the
  // early test.
  std::array<spv::Id, 4> main_fsi_late_write_depth_stencil_;
  spv::Block* main_fsi_early_depth_stencil_execute_quad_merge_;
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
