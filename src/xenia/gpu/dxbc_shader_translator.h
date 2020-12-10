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

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

// Generates shader model 5_1 byte code (for Direct3D 12).
//
// IMPORTANT CONTRIBUTION NOTES:
//
// While DXBC may look like a flexible and high-level representation with highly
// generalized building blocks, actually it has a lot of restrictions on operand
// usage!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!!DO NOT ADD ANYTHING FXC THAT WOULD NOT PRODUCE!!!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Before adding any sequence that you haven't seen in Xenia, try writing
// equivalent code in HLSL and running it through FXC, try with /Od, try with
// full optimization, but if you see that FXC follows a different pattern than
// what you are expecting, do what FXC does!!!
// Most important limitations:
// - Absolute, negate and saturate are only supported by instructions that
//   explicitly support them. See MSDN pages of the specific instructions you
//   want to use with modifiers:
//   https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm
// - Component selection in the general case (ALU instructions - things like
//   resource access and flow control mostly explicitly need a specific
//   component selection mode defined in the specification of the instruction):
//   - 0-component - for operand types with no data (samplers, labels).
//   - 1-component - for scalar destination operand types, and for scalar source
//     operand types when the destination vector has 1 component masked
//     (including scalar immediates).
//   - Mask - for vector destination operand types.
//   - Swizzle - for both vector and scalar (replicated in this case) source
//     operand types, when the destination vector has 2 or more components
//     masked. Immediates in this case have XYZW swizzle.
//   - Select 1 - for vector source operand types, when the destination has 1
//     component masked or is of a scalar type.
// - Input operands (v#) can be used only as sources, output operands (o#) can
//   be used only as destinations.
// - Indexable temporaries (x#) can only be used as a destination or a source
//   operand (but not both at once) of a mov instruction - a load/store pattern
//   here. Also, movs involving x# are counted as ArrayInstructions rather than
//   MovInstructions in STAT. The other operand can be anything that most other
//   instructions accept, but it still must be a mov with x# on one side.
// TODO(Triang3l): Fix all places in the translator currently violating these
// rules.
// !NOTE!: The D3D11.3 Functional Specification on Microsoft's GitHub profile,
// as of March 27th, 2020, is NOT a reliable reference, even though it contains
// many DXBC details! There are multiple places where it clearly contradicts
// what FXC does, even when targeting old shader models like 4_0:
// - The limit of 1 immediate or constant buffer source operand per instruction
//   is totally ignored by FXC - in simple tests, it can emit an instruction
//   with two constant buffer sources, or one constant buffer source and one
//   immediate, or a multiply-add with two immediate operands.
// - It says x# can be used wherever r# can be used - in synthetic tests, FXC
//   always accesses x# in a load/store way via mov.
// - It says x# can be used for indexing, including nested indexing of x# (one
//   level deep), however, FXC moves the inner index operand to r# first in this
//   case.
//
// For bytecode structure, see d3d12TokenizedProgramFormat.hpp from the Windows
// Driver Kit, and DXILConv from DirectX Shader Compiler.
//
// Avoid using uninitialized register components - such as registers written to
// in "if" and not in "else", but then used outside unconditionally or with a
// different condition (or even with the same condition, but in a different "if"
// block). This will cause crashes on AMD drivers, and will also limit
// optimization possibilities as this may result in false dependencies. Always
// mov l(0, 0, 0, 0) to such components before potential branching -
// PushSystemTemp accepts a zero mask for this purpose.
//
// Clamping of non-negative values must be done first to the lower bound (using
// max), then to the upper bound (using min), to match the saturate modifier
// behavior, which results in 0 for NaN.
class DxbcShaderTranslator : public ShaderTranslator {
 public:
  DxbcShaderTranslator(uint32_t vendor_id, bool bindless_resources_used,
                       bool edram_rov_used, bool force_emit_source_map = false);
  ~DxbcShaderTranslator() override;

  union Modification {
    // If anything in this is structure is changed in a way not compatible with
    // the previous layout, invalidate the pipeline storages by increasing this
    // version number (0xYYYYMMDD)!
    static constexpr uint32_t kVersion = 0x20201203;

    enum class DepthStencilMode : uint32_t {
      kNoModifiers,
      // [earlydepthstencil] - enable if alpha test and alpha to coverage are
      // disabled; ignored if anything in the shader blocks early Z writing
      // (which is not known before translation, so this will be set anyway).
      kEarlyHint,
      // Converting the depth to the closest 32-bit float representable exactly
      // as a 20e4 float, to support invariance in cases when the guest
      // reuploads a previously resolved depth buffer to the EDRAM, rounding
      // towards zero (which contradicts the rounding used by the Direct3D 9
      // reference rasterizer, but allows SV_DepthLessEqual to be used to allow
      // slightly coarse early Z culling; also truncating regardless of whether
      // the shader writes depth and thus always uses SV_Depth, for
      // consistency). MSAA is limited - depth must be per-sample
      // (SV_DepthLessEqual also explicitly requires sample or centroid position
      // interpolation), thus the sampler has to run at sample frequency even if
      // the device supports stencil loading and thus true non-ROV MSAA via
      // SV_StencilRef.
      // Fixed-function viewport depth bounds must be snapped to float24 for
      // clamping purposes.
      kFloat24Truncating,
      // Similar to kFloat24Truncating, but rounding to the nearest even,
      // however, always using SV_Depth rather than SV_DepthLessEqual because
      // rounding up results in a bigger value. Same viewport usage rules apply.
      kFloat24Rounding,
    };

    struct {
      // VS - pipeline stage and input configuration.
      Shader::HostVertexShaderType host_vertex_shader_type
          : Shader::kHostVertexShaderTypeBitCount;
      // PS, non-ROV - depth / stencil output mode.
      DepthStencilMode depth_stencil_mode : 2;
    };
    uint32_t value = 0;

    Modification(uint32_t modification_value = 0) : value(modification_value) {}
  };

  // Constant buffer bindings in space 0.
  enum class CbufferRegister {
    kSystemConstants,
    kFloatConstants,
    kBoolLoopConstants,
    kFetchConstants,
    kDescriptorIndices,
  };

  // Some are referenced in xenos_draw.hlsli - check it too when updating!
  enum : uint32_t {
    kSysFlag_SharedMemoryIsUAV_Shift,
    kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal_Shift,
    kSysFlag_UserClipPlane0_Shift,
    kSysFlag_UserClipPlane1_Shift,
    kSysFlag_UserClipPlane2_Shift,
    kSysFlag_UserClipPlane3_Shift,
    kSysFlag_UserClipPlane4_Shift,
    kSysFlag_UserClipPlane5_Shift,
    kSysFlag_KillIfAnyVertexKilled_Shift,
    kSysFlag_PrimitivePolygonal_Shift,
    kSysFlag_AlphaPassIfLess_Shift,
    kSysFlag_AlphaPassIfEqual_Shift,
    kSysFlag_AlphaPassIfGreater_Shift,
    kSysFlag_Color0Gamma_Shift,
    kSysFlag_Color1Gamma_Shift,
    kSysFlag_Color2Gamma_Shift,
    kSysFlag_Color3Gamma_Shift,

    kSysFlag_ROVDepthStencil_Shift,
    kSysFlag_ROVDepthFloat24_Shift,
    kSysFlag_ROVDepthPassIfLess_Shift,
    kSysFlag_ROVDepthPassIfEqual_Shift,
    kSysFlag_ROVDepthPassIfGreater_Shift,
    // 1 to write new depth to the depth buffer, 0 to keep the old one if the
    // depth test passes.
    kSysFlag_ROVDepthWrite_Shift,
    kSysFlag_ROVStencilTest_Shift,
    // If the depth/stencil test has failed, but resulted in a stencil value
    // that is different than the one currently in the depth buffer, write it
    // anyway and don't run the rest of the shader (to check if the sample may
    // be discarded some way) - use when alpha test and alpha to coverage are
    // disabled. Ignored by the shader if not applicable to it (like if it has
    // kill instructions or writes the depth output).
    // TODO(Triang3l): Investigate replacement with an alpha-to-mask flag,
    // checking `(flags & (alpha test | alpha to mask)) == (always | disabled)`,
    // taking into account the potential relation with occlusion queries (but
    // should be safe at least temporarily).
    kSysFlag_ROVDepthStencilEarlyWrite_Shift,

    kSysFlag_Count,

    kSysFlag_SharedMemoryIsUAV = 1u << kSysFlag_SharedMemoryIsUAV_Shift,
    kSysFlag_XYDividedByW = 1u << kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW = 1u << kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal = 1u << kSysFlag_WNotReciprocal_Shift,
    kSysFlag_UserClipPlane0 = 1u << kSysFlag_UserClipPlane0_Shift,
    kSysFlag_UserClipPlane1 = 1u << kSysFlag_UserClipPlane1_Shift,
    kSysFlag_UserClipPlane2 = 1u << kSysFlag_UserClipPlane2_Shift,
    kSysFlag_UserClipPlane3 = 1u << kSysFlag_UserClipPlane3_Shift,
    kSysFlag_UserClipPlane4 = 1u << kSysFlag_UserClipPlane4_Shift,
    kSysFlag_UserClipPlane5 = 1u << kSysFlag_UserClipPlane5_Shift,
    kSysFlag_KillIfAnyVertexKilled = 1u << kSysFlag_KillIfAnyVertexKilled_Shift,
    kSysFlag_PrimitivePolygonal = 1u << kSysFlag_PrimitivePolygonal_Shift,
    kSysFlag_AlphaPassIfLess = 1u << kSysFlag_AlphaPassIfLess_Shift,
    kSysFlag_AlphaPassIfEqual = 1u << kSysFlag_AlphaPassIfEqual_Shift,
    kSysFlag_AlphaPassIfGreater = 1u << kSysFlag_AlphaPassIfGreater_Shift,
    kSysFlag_Color0Gamma = 1u << kSysFlag_Color0Gamma_Shift,
    kSysFlag_Color1Gamma = 1u << kSysFlag_Color1Gamma_Shift,
    kSysFlag_Color2Gamma = 1u << kSysFlag_Color2Gamma_Shift,
    kSysFlag_Color3Gamma = 1u << kSysFlag_Color3Gamma_Shift,
    kSysFlag_ROVDepthStencil = 1u << kSysFlag_ROVDepthStencil_Shift,
    kSysFlag_ROVDepthFloat24 = 1u << kSysFlag_ROVDepthFloat24_Shift,
    kSysFlag_ROVDepthPassIfLess = 1u << kSysFlag_ROVDepthPassIfLess_Shift,
    kSysFlag_ROVDepthPassIfEqual = 1u << kSysFlag_ROVDepthPassIfEqual_Shift,
    kSysFlag_ROVDepthPassIfGreater = 1u << kSysFlag_ROVDepthPassIfGreater_Shift,
    kSysFlag_ROVDepthWrite = 1u << kSysFlag_ROVDepthWrite_Shift,
    kSysFlag_ROVStencilTest = 1u << kSysFlag_ROVStencilTest_Shift,
    kSysFlag_ROVDepthStencilEarlyWrite =
        1u << kSysFlag_ROVDepthStencilEarlyWrite_Shift,
  };
  static_assert(kSysFlag_Count <= 32, "Too many flags in the system constants");

  // Appended to the format in the format constant.
  enum : uint32_t {
    // Starting from bit 4 because the format itself needs 4 bits.
    kRTFormatFlag_64bpp_Shift = 4,
    // Requires clamping of blending sources and factors.
    kRTFormatFlag_FixedPointColor_Shift,
    kRTFormatFlag_FixedPointAlpha_Shift,

    kRTFormatFlag_64bpp = 1u << kRTFormatFlag_64bpp_Shift,
    kRTFormatFlag_FixedPointColor = 1u << kRTFormatFlag_FixedPointColor_Shift,
    kRTFormatFlag_FixedPointAlpha = 1u << kRTFormatFlag_FixedPointAlpha_Shift,
  };

  // IF SYSTEM CONSTANTS ARE CHANGED OR ADDED, THE FOLLOWING MUST BE UPDATED:
  // - kSysConst enum (indices, registers and first components).
  // - system_constant_rdef_.
  // - d3d12/shaders/xenos_draw.hlsli (for geometry shaders).
  struct SystemConstants {
    uint32_t flags;
    union {
      struct {
        float tessellation_factor_range_min;
        float tessellation_factor_range_max;
      };
      float tessellation_factor_range[2];
    };
    uint32_t line_loop_closing_index;

    xenos::Endian vertex_index_endian;
    int32_t vertex_base_index;
    float point_size[2];

    float point_size_min_max[2];
    // Screen point size * 2 (but not supersampled) -> size in NDC.
    float point_screen_to_ndc[2];

    float user_clip_planes[6][4];

    float ndc_scale[3];
    uint32_t interpolator_sampling_pattern;

    float ndc_offset[3];
    uint32_t ps_param_gen;

    // Each byte contains post-swizzle TextureSign values for each of the needed
    // components of each of the 32 used texture fetch constants.
    uint32_t texture_swizzled_signs[8];

    // Log2 of X and Y sample size. For SSAA with RTV/DSV, this is used to get
    // VPOS to pass to the game's shader. For MSAA with ROV, this is used for
    // EDRAM address calculation.
    uint32_t sample_count_log2[2];
    float alpha_test_reference;
    // If alpha to mask is disabled, the entire alpha_to_mask value must be 0.
    // If alpha to mask is enabled, bits 0:7 are sample offsets, and bit 8 must
    // be 1.
    uint32_t alpha_to_mask;

    float color_exp_bias[4];

    uint32_t color_output_map[4];

    uint32_t edram_resolution_square_scale;
    uint32_t edram_pitch_tiles;
    union {
      struct {
        float edram_depth_range_scale;
        float edram_depth_range_offset;
      };
      float edram_depth_range[2];
    };

    union {
      struct {
        float edram_poly_offset_front_scale;
        float edram_poly_offset_front_offset;
      };
      float edram_poly_offset_front[2];
    };
    union {
      struct {
        float edram_poly_offset_back_scale;
        float edram_poly_offset_back_offset;
      };
      float edram_poly_offset_back[2];
    };

    uint32_t edram_depth_base_dwords;
    uint32_t padding_edram_depth_base_dwords[3];

    // In stencil function/operations (they match the layout of the
    // function/operations in RB_DEPTHCONTROL):
    // 0:2 - comparison function (bit 0 - less, bit 1 - equal, bit 2 - greater).
    // 3:5 - fail operation.
    // 6:8 - pass operation.
    // 9:11 - depth fail operation.

    union {
      struct {
        uint32_t edram_stencil_front_reference;
        uint32_t edram_stencil_front_read_mask;
        uint32_t edram_stencil_front_write_mask;
        uint32_t edram_stencil_front_func_ops;

        uint32_t edram_stencil_back_reference;
        uint32_t edram_stencil_back_read_mask;
        uint32_t edram_stencil_back_write_mask;
        uint32_t edram_stencil_back_func_ops;
      };
      struct {
        uint32_t edram_stencil_front[4];
        uint32_t edram_stencil_back[4];
      };
      uint32_t edram_stencil[2][4];
    };

    uint32_t edram_rt_base_dwords_scaled[4];

    // RT format combined with kRTFormatFlags.
    uint32_t edram_rt_format_flags[4];

    // Format info - values to clamp the color to before blending or storing.
    // Low color, low alpha, high color, high alpha.
    float edram_rt_clamp[4][4];

    // Format info - mask to apply to the old packed RT data, and to apply as
    // inverted to the new packed data, before storing (more or less the inverse
    // of the write mask packed like render target channels). This can be used
    // to bypass unpacking if blending is not used. If 0 and not blending,
    // reading the old data from the EDRAM buffer is not required.
    uint32_t edram_rt_keep_mask[4][2];

    // Render target blending options - RB_BLENDCONTROL, with only the relevant
    // options (factors and operations - AND 0x1FFF1FFF). If 0x00010001
    // (1 * src + 0 * dst), blending is disabled for the render target.
    uint32_t edram_rt_blend_factors_ops[4];

    // The constant blend factor for the respective modes.
    float edram_blend_constant[4];
  };

  // Shader resource view binding spaces.
  enum class SRVSpace {
    // SRVMainSpaceRegister t# layout.
    kMain,
    kBindlessTextures2DArray,
    kBindlessTextures3D,
    kBindlessTexturesCube,
  };

  // Shader resource view bindings in SRVSpace::kMain.
  enum class SRVMainRegister {
    kSharedMemory,
    kBindfulTexturesStart,
  };

  // 192 textures at most because there are 32 fetch constants, and textures can
  // be 2D array, 3D or cube, and also signed and unsigned.
  static constexpr uint32_t kMaxTextureBindingIndexBits = 8;
  static constexpr uint32_t kMaxTextureBindings =
      (1 << kMaxTextureBindingIndexBits) - 1;
  struct TextureBinding {
    uint32_t bindful_srv_index;
    // Temporary for WriteResourceDefinitions.
    uint32_t bindful_srv_rdef_name_offset;
    uint32_t bindless_descriptor_index;
    uint32_t fetch_constant;
    // Stacked and 3D are separate TextureBindings, even for bindless for null
    // descriptor handling simplicity.
    xenos::FetchOpDimension dimension;
    bool is_signed;
    std::string name;
  };

  // Arbitrary limit - there can't be more than 2048 in a shader-visible
  // descriptor heap, though some older hardware (tier 1 resource binding -
  // Nvidia Fermi) doesn't support more than 16 samplers bound at once (we can't
  // really do anything if a game uses more than 16), but just to have some
  // limit so sampler count can easily be packed into 32-bit map keys (for
  // instance, for root signatures). But shaders can specify overrides for
  // filtering modes, and the number of possible combinations is huge - let's
  // limit it to something sane.
  static constexpr uint32_t kMaxSamplerBindingIndexBits = 7;
  static constexpr uint32_t kMaxSamplerBindings =
      (1 << kMaxSamplerBindingIndexBits) - 1;
  struct SamplerBinding {
    uint32_t bindless_descriptor_index;
    uint32_t fetch_constant;
    xenos::TextureFilter mag_filter;
    xenos::TextureFilter min_filter;
    xenos::TextureFilter mip_filter;
    xenos::AnisoFilter aniso_filter;
    std::string name;
  };

  // Unordered access view bindings in space 0.
  enum class UAVRegister {
    kSharedMemory,
    kEdram,
  };

  // Returns the format with internal flags for passing via the
  // edram_rt_format_flags system constant.
  static constexpr uint32_t ROV_AddColorFormatFlags(
      xenos::ColorRenderTargetFormat format) {
    uint32_t format_flags = uint32_t(format);
    if (format == xenos::ColorRenderTargetFormat::k_16_16_16_16 ||
        format == xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT ||
        format == xenos::ColorRenderTargetFormat::k_32_32_FLOAT) {
      format_flags |= kRTFormatFlag_64bpp;
    }
    if (format == xenos::ColorRenderTargetFormat::k_8_8_8_8 ||
        format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA ||
        format == xenos::ColorRenderTargetFormat::k_2_10_10_10 ||
        format == xenos::ColorRenderTargetFormat::k_16_16 ||
        format == xenos::ColorRenderTargetFormat::k_16_16_16_16 ||
        format == xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10) {
      format_flags |=
          kRTFormatFlag_FixedPointColor | kRTFormatFlag_FixedPointAlpha;
    } else if (format == xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
               format == xenos::ColorRenderTargetFormat::
                             k_2_10_10_10_FLOAT_AS_16_16_16_16) {
      format_flags |= kRTFormatFlag_FixedPointAlpha;
    }
    return format_flags;
  }
  // Returns the bits that need to be added to the RT flags constant - needs to
  // be done externally, not in SetColorFormatConstants, because the flags
  // contain other state.
  static void ROV_GetColorFormatSystemConstants(
      xenos::ColorRenderTargetFormat format, uint32_t write_mask,
      float& clamp_rgb_low, float& clamp_alpha_low, float& clamp_rgb_high,
      float& clamp_alpha_high, uint32_t& keep_mask_low,
      uint32_t& keep_mask_high);

  uint32_t GetDefaultModification(
      xenos::ShaderType shader_type,
      Shader::HostVertexShaderType host_vertex_shader_type =
          Shader::HostVertexShaderType::kVertex) const override;

  // Creates a special pixel shader without color outputs - this resets the
  // state of the translator.
  std::vector<uint8_t> CreateDepthOnlyPixelShader();

 protected:
  void Reset(xenos::ShaderType shader_type) override;

  void StartTranslation() override;
  std::vector<uint8_t> CompleteTranslation() override;
  void PostTranslation(Shader::Translation& translation,
                       bool setup_shader_post_translation_info) override;

  void ProcessLabel(uint32_t cf_index) override;

  void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(
      const ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(
      const ParsedLoopEndInstruction& instr) override;
  void ProcessJumpInstruction(const ParsedJumpInstruction& instr) override;
  void ProcessAllocInstruction(const ParsedAllocInstruction& instr) override;

  void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) override;
  void ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  // D3D_SHADER_VARIABLE_CLASS
  enum class DxbcRdefVariableClass : uint32_t {
    kScalar,
    kVector,
    kMatrixRows,
    kMatrixColumns,
    kObject,
    kStruct,
    kInterfaceClass,
    kInterfacePointer,
  };

  // D3D_SHADER_VARIABLE_TYPE subset
  enum class DxbcRdefVariableType : uint32_t {
    kInt = 2,
    kFloat = 3,
    kUInt = 19,
  };

  // D3D_SHADER_VARIABLE_FLAGS
  enum DxbcRdefVariableFlags : uint32_t {
    kDxbcRdefVariableFlagUserPacked = 1 << 0,
    kDxbcRdefVariableFlagUsed = 1 << 1,
    kDxbcRdefVariableFlagInterfacePointer = 1 << 2,
    kDxbcRdefVariableFlagInterfaceParameter = 1 << 3,
  };

  // D3D_CBUFFER_TYPE
  enum class DxbcRdefCbufferType : uint32_t {
    kCbuffer,
    kTbuffer,
    kInterfacePointers,
    kResourceBindInfo,
  };

  // D3D_SHADER_INPUT_TYPE
  enum class DxbcRdefInputType : uint32_t {
    kCbuffer,
    kTbuffer,
    kTexture,
    kSampler,
    kUAVRWTyped,
    kStructured,
    kUAVRWStructured,
    kByteAddress,
    kUAVRWByteAddress,
    kUAVAppendStructured,
    kUAVConsumeStructured,
    kUAVRWStructuredWithCounter,
  };

  // D3D_RESOURCE_RETURN_TYPE
  enum class DxbcRdefReturnType : uint32_t {
    kVoid,
    kUNorm,
    kSNorm,
    kSInt,
    kUInt,
    kFloat,
    kMixed,
    kDouble,
    kContinued,
  };

  // D3D12_SRV_DIMENSION/D3D12_UAV_DIMENSION
  enum class DxbcRdefDimension : uint32_t {
    kUnknown = 0,

    kSRVBuffer = 1,
    kSRVTexture1D,
    kSRVTexture1DArray,
    kSRVTexture2D,
    kSRVTexture2DArray,
    kSRVTexture2DMS,
    kSRVTexture2DMSArray,
    kSRVTexture3D,
    kSRVTextureCube,
    kSRVTextureCubeArray,

    kUAVBuffer = 1,
    kUAVTexture1D,
    kUAVTexture1DArray,
    kUAVTexture2D,
    kUAVTexture2DArray,
    kUAVTexture3D,
  };

  // D3D_SHADER_INPUT_FLAGS
  enum DxbcRdefInputFlags : uint32_t {
    // For constant buffers, UserPacked is set if it was declared as `cbuffer`
    // rather than `ConstantBuffer<T>` (not dynamically indexable; though
    // non-uniform dynamic indexing of constant buffers also didn't work on AMD
    // drivers in 2018).
    DxbcRdefInputFlagUserPacked = 1 << 0,
    DxbcRdefInputFlagComparisonSampler = 1 << 1,
    DxbcRdefInputFlagComponent0 = 1 << 2,
    DxbcRdefInputFlagComponent1 = 1 << 3,
    DxbcRdefInputFlagsComponents =
        DxbcRdefInputFlagComponent0 | DxbcRdefInputFlagComponent1,
    DxbcRdefInputFlagUnused = 1 << 4,
  };

  // D3D_NAME subset
  enum class DxbcName : uint32_t {
    kUndefined = 0,
    kPosition = 1,
    kClipDistance = 2,
    kCullDistance = 3,
    kVertexID = 6,
    kIsFrontFace = 9,
    kFinalQuadEdgeTessFactor = 11,
    kFinalQuadInsideTessFactor = 12,
    kFinalTriEdgeTessFactor = 13,
    kFinalTriInsideTessFactor = 14,
  };

  // D3D_REGISTER_COMPONENT_TYPE
  enum class DxbcSignatureRegisterComponentType : uint32_t {
    kUnknown,
    kUInt32,
    kSInt32,
    kFloat32,
  };

  // D3D10_INTERNALSHADER_PARAMETER
  struct DxbcSignatureParameter {
    // Offset in bytes from the start of the chunk.
    uint32_t semantic_name;
    uint32_t semantic_index;
    // kUndefined for pixel shader outputs - inferred from the component type
    // and what is used in the shader.
    DxbcName system_value;
    DxbcSignatureRegisterComponentType component_type;
    // o#/v# when there's linkage, SV_Target index or -1 in pixel shader output.
    uint32_t register_index;
    uint8_t mask;
    union {
      // For an output signature.
      uint8_t never_writes_mask;
      // For an input signature.
      uint8_t always_reads_mask;
    };
  };
  static_assert(alignof(DxbcSignatureParameter) <= sizeof(uint32_t));

  // D3D10_INTERNALSHADER_SIGNATURE
  struct DxbcSignature {
    uint32_t parameter_count;
    // Offset in bytes from the start of the chunk.
    uint32_t parameter_info_offset;
  };
  static_assert(alignof(DxbcSignature) <= sizeof(uint32_t));

  // D3D11_SB_TESSELLATOR_DOMAIN
  enum class DxbcTessellatorDomain : uint32_t {
    kUndefined,
    kIsoline,
    kTriangle,
    kQuad,
  };

  // D3D10_SB_OPERAND_TYPE subset
  enum class DxbcOperandType : uint32_t {
    kTemp = 0,
    kInput = 1,
    kOutput = 2,
    // Only usable as destination or source (but not both) in mov (and it
    // becomes an array instruction this way).
    kIndexableTemp = 3,
    kImmediate32 = 4,
    kSampler = 6,
    kResource = 7,
    kConstantBuffer = 8,
    kLabel = 10,
    kInputPrimitiveID = 11,
    kOutputDepth = 12,
    kNull = 13,
    kInputControlPoint = 25,
    kInputDomainPoint = 28,
    kUnorderedAccessView = 30,
    kInputCoverageMask = 35,
    kOutputDepthLessEqual = 39,
  };

  // D3D10_SB_OPERAND_INDEX_DIMENSION
  static constexpr uint32_t GetDxbcOperandIndexDimension(DxbcOperandType type) {
    switch (type) {
      case DxbcOperandType::kTemp:
      case DxbcOperandType::kInput:
      case DxbcOperandType::kOutput:
      case DxbcOperandType::kLabel:
        return 1;
      case DxbcOperandType::kIndexableTemp:
      case DxbcOperandType::kSampler:
      case DxbcOperandType::kResource:
      case DxbcOperandType::kInputControlPoint:
      case DxbcOperandType::kUnorderedAccessView:
        return 2;
      case DxbcOperandType::kConstantBuffer:
        return 3;
      default:
        return 0;
    }
  }

  // D3D10_SB_OPERAND_NUM_COMPONENTS
  enum class DxbcOperandDimension : uint32_t {
    kNoData,  // D3D10_SB_OPERAND_0_COMPONENT
    kScalar,  // D3D10_SB_OPERAND_1_COMPONENT
    kVector,  // D3D10_SB_OPERAND_4_COMPONENT
  };

  static constexpr DxbcOperandDimension GetDxbcOperandDimension(
      DxbcOperandType type, bool dest_in_dcl = false) {
    switch (type) {
      case DxbcOperandType::kSampler:
      case DxbcOperandType::kLabel:
      case DxbcOperandType::kNull:
        return DxbcOperandDimension::kNoData;
      case DxbcOperandType::kInputPrimitiveID:
      case DxbcOperandType::kOutputDepth:
      case DxbcOperandType::kOutputDepthLessEqual:
        return DxbcOperandDimension::kScalar;
      case DxbcOperandType::kInputCoverageMask:
        return dest_in_dcl ? DxbcOperandDimension::kScalar
                           : DxbcOperandDimension::kVector;
      default:
        return DxbcOperandDimension::kVector;
    }
  }

  // D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE
  enum class DxbcComponentSelection {
    kMask,
    kSwizzle,
    kSelect1,
  };

  struct DxbcIndex {
    // D3D10_SB_OPERAND_INDEX_REPRESENTATION
    enum class Representation : uint32_t {
      kImmediate32,
      kImmediate64,
      kRelative,
      kImmediate32PlusRelative,
      kImmediate64PlusRelative,
    };

    uint32_t index_;
    // UINT32_MAX if absolute. Lower 2 bits are the component index, upper bits
    // are the temp register index. Applicable to indexable temps, inputs,
    // outputs except for pixel shaders, constant buffers and bindings.
    uint32_t relative_to_temp_;

    // Implicit constructor.
    DxbcIndex(uint32_t index = 0)
        : index_(index), relative_to_temp_(UINT32_MAX) {}
    DxbcIndex(uint32_t temp, uint32_t temp_component, uint32_t offset = 0)
        : index_(offset), relative_to_temp_((temp << 2) | temp_component) {}

    Representation GetRepresentation() const {
      if (relative_to_temp_ != UINT32_MAX) {
        return index_ != 0 ? Representation::kImmediate32PlusRelative
                           : Representation::kRelative;
      }
      return Representation::kImmediate32;
    }
    uint32_t GetLength() const {
      return relative_to_temp_ != UINT32_MAX ? (index_ != 0 ? 3 : 2) : 1;
    }
    void Write(std::vector<uint32_t>& code) const {
      if (relative_to_temp_ == UINT32_MAX || index_ != 0) {
        code.push_back(index_);
      }
      if (relative_to_temp_ != UINT32_MAX) {
        // Encode selecting one component from absolute-indexed r#.
        code.push_back(uint32_t(DxbcOperandDimension::kVector) |
                       (uint32_t(DxbcComponentSelection::kSelect1) << 2) |
                       ((relative_to_temp_ & 3) << 4) |
                       (uint32_t(DxbcOperandType::kTemp) << 12) | (1 << 20) |
                       (uint32_t(Representation::kImmediate32) << 22));
        code.push_back(relative_to_temp_ >> 2);
      }
    }
  };

  struct DxbcOperandAddress {
    DxbcOperandType type_;
    DxbcIndex index_1d_, index_2d_, index_3d_;

    explicit DxbcOperandAddress(DxbcOperandType type,
                                DxbcIndex index_1d = DxbcIndex(),
                                DxbcIndex index_2d = DxbcIndex(),
                                DxbcIndex index_3d = DxbcIndex())
        : type_(type),
          index_1d_(index_1d),
          index_2d_(index_2d),
          index_3d_(index_3d) {}

    DxbcOperandDimension GetDimension(bool dest_in_dcl = false) const {
      return GetDxbcOperandDimension(type_, dest_in_dcl);
    }
    uint32_t GetIndexDimension() const {
      return GetDxbcOperandIndexDimension(type_);
    }
    uint32_t GetOperandTokenTypeAndIndex() const {
      uint32_t index_dimension = GetIndexDimension();
      uint32_t operand_token =
          (uint32_t(type_) << 12) | (index_dimension << 20);
      if (index_dimension > 0) {
        operand_token |= uint32_t(index_1d_.GetRepresentation()) << 22;
        if (index_dimension > 1) {
          operand_token |= uint32_t(index_2d_.GetRepresentation()) << 25;
          if (index_dimension > 2) {
            operand_token |= uint32_t(index_3d_.GetRepresentation()) << 28;
          }
        }
      }
      return operand_token;
    }
    uint32_t GetLength() const {
      uint32_t length = 0;
      uint32_t index_dimension = GetIndexDimension();
      if (index_dimension > 0) {
        length += index_1d_.GetLength();
        if (index_dimension > 1) {
          length += index_2d_.GetLength();
          if (index_dimension > 2) {
            length += index_3d_.GetLength();
          }
        }
      }
      return length;
    }
    void Write(std::vector<uint32_t>& code) const {
      uint32_t index_dimension = GetIndexDimension();
      if (index_dimension > 0) {
        index_1d_.Write(code);
        if (index_dimension > 1) {
          index_2d_.Write(code);
          if (index_dimension > 2) {
            index_3d_.Write(code);
          }
        }
      }
    }
  };

  // D3D10_SB_EXTENDED_OPERAND_TYPE
  enum class DxbcExtendedOperandType : uint32_t {
    kEmpty,
    kModifier,
  };

  // D3D10_SB_OPERAND_MODIFIER
  enum class DxbcOperandModifier : uint32_t {
    kNone,
    kNegate,
    kAbsolute,
    kAbsoluteNegate,
  };

  struct DxbcDest : DxbcOperandAddress {
    // Ignored for 0-component and 1-component operand types.
    uint32_t write_mask_;

    explicit DxbcDest(DxbcOperandType type, uint32_t write_mask = 0b1111,
                      DxbcIndex index_1d = DxbcIndex(),
                      DxbcIndex index_2d = DxbcIndex(),
                      DxbcIndex index_3d = DxbcIndex())
        : DxbcOperandAddress(type, index_1d, index_2d, index_3d),
          write_mask_(write_mask) {}

    static DxbcDest R(uint32_t index, uint32_t write_mask = 0b1111) {
      return DxbcDest(DxbcOperandType::kTemp, write_mask, index);
    }
    static DxbcDest O(DxbcIndex index, uint32_t write_mask = 0b1111) {
      return DxbcDest(DxbcOperandType::kOutput, write_mask, index);
    }
    static DxbcDest X(uint32_t index_1d, DxbcIndex index_2d,
                      uint32_t write_mask = 0b1111) {
      return DxbcDest(DxbcOperandType::kIndexableTemp, write_mask, index_1d,
                      index_2d);
    }
    static DxbcDest ODepth() {
      return DxbcDest(DxbcOperandType::kOutputDepth, 0b0001);
    }
    static DxbcDest Null() { return DxbcDest(DxbcOperandType::kNull, 0b0000); }
    static DxbcDest U(uint32_t index_1d, DxbcIndex index_2d,
                      uint32_t write_mask = 0b1111) {
      return DxbcDest(DxbcOperandType::kUnorderedAccessView, write_mask,
                      index_1d, index_2d);
    }
    static DxbcDest ODepthLE() {
      return DxbcDest(DxbcOperandType::kOutputDepthLessEqual, 0b0001);
    }

    uint32_t GetMask() const {
      switch (GetDimension()) {
        case DxbcOperandDimension::kNoData:
          return 0b0000;
        case DxbcOperandDimension::kScalar:
          return 0b0001;
        case DxbcOperandDimension::kVector:
          return write_mask_;
        default:
          assert_unhandled_case(GetDimension());
          return 0b0000;
      }
    }
    [[nodiscard]] DxbcDest Mask(uint32_t write_mask) const {
      return DxbcDest(type_, write_mask, index_1d_, index_2d_, index_3d_);
    }
    [[nodiscard]] DxbcDest MaskMasked(uint32_t write_mask) const {
      return DxbcDest(type_, write_mask_ & write_mask, index_1d_, index_2d_,
                      index_3d_);
    }
    static uint32_t GetMaskSingleComponent(uint32_t write_mask) {
      uint32_t component;
      if (xe::bit_scan_forward(write_mask, &component)) {
        if ((write_mask >> component) == 1) {
          return component;
        }
      }
      return UINT32_MAX;
    }
    uint32_t GetMaskSingleComponent() const {
      return GetMaskSingleComponent(GetMask());
    }

    uint32_t GetLength() const { return 1 + DxbcOperandAddress::GetLength(); }
    void Write(std::vector<uint32_t>& code, bool in_dcl = false) const {
      uint32_t operand_token = GetOperandTokenTypeAndIndex();
      DxbcOperandDimension dimension = GetDimension(in_dcl);
      operand_token |= uint32_t(dimension);
      if (dimension == DxbcOperandDimension::kVector) {
        assert_true(write_mask_ > 0b0000 && write_mask_ <= 0b1111);
        operand_token |=
            (uint32_t(DxbcComponentSelection::kMask) << 2) | (write_mask_ << 4);
      }
      code.push_back(operand_token);
      DxbcOperandAddress::Write(code);
    }
  };

  struct DxbcSrc : DxbcOperandAddress {
    enum : uint32_t {
      kXYZW = 0b11100100,
      kXXXX = 0b00000000,
      kYYYY = 0b01010101,
      kZZZZ = 0b10101010,
      kWWWW = 0b11111111,
    };

    // Ignored for 0-component and 1-component operand types.
    uint32_t swizzle_;
    bool absolute_;
    bool negate_;
    // Only valid for DxbcOperandType::kImmediate32.
    uint32_t immediate_[4];

    explicit DxbcSrc(DxbcOperandType type, uint32_t swizzle = kXYZW,
                     DxbcIndex index_1d = DxbcIndex(),
                     DxbcIndex index_2d = DxbcIndex(),
                     DxbcIndex index_3d = DxbcIndex())
        : DxbcOperandAddress(type, index_1d, index_2d, index_3d),
          swizzle_(swizzle),
          absolute_(false),
          negate_(false) {}

    static DxbcSrc R(uint32_t index, uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kTemp, swizzle, index);
    }
    static DxbcSrc V(DxbcIndex index, uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kInput, swizzle, index);
    }
    static DxbcSrc X(uint32_t index_1d, DxbcIndex index_2d,
                     uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kIndexableTemp, swizzle, index_1d,
                     index_2d);
    }
    static DxbcSrc LU(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
      DxbcSrc src(DxbcOperandType::kImmediate32, kXYZW);
      src.immediate_[0] = x;
      src.immediate_[1] = y;
      src.immediate_[2] = z;
      src.immediate_[3] = w;
      return src;
    }
    static DxbcSrc LU(uint32_t x) { return LU(x, x, x, x); }
    static DxbcSrc LI(int32_t x, int32_t y, int32_t z, int32_t w) {
      return LU(uint32_t(x), uint32_t(y), uint32_t(z), uint32_t(w));
    }
    static DxbcSrc LI(int32_t x) { return LI(x, x, x, x); }
    static DxbcSrc LF(float x, float y, float z, float w) {
      return LU(*reinterpret_cast<const uint32_t*>(&x),
                *reinterpret_cast<const uint32_t*>(&y),
                *reinterpret_cast<const uint32_t*>(&z),
                *reinterpret_cast<const uint32_t*>(&w));
    }
    static DxbcSrc LF(float x) { return LF(x, x, x, x); }
    static DxbcSrc LP(const uint32_t* xyzw) {
      return LU(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
    }
    static DxbcSrc LP(const int32_t* xyzw) {
      return LI(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
    }
    static DxbcSrc LP(const float* xyzw) {
      return LF(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
    }
    static DxbcSrc S(uint32_t index_1d, DxbcIndex index_2d) {
      return DxbcSrc(DxbcOperandType::kSampler, kXXXX, index_1d, index_2d);
    }
    static DxbcSrc T(uint32_t index_1d, DxbcIndex index_2d,
                     uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kResource, swizzle, index_1d, index_2d);
    }
    static DxbcSrc CB(uint32_t index_1d, DxbcIndex index_2d, DxbcIndex index_3d,
                      uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kConstantBuffer, swizzle, index_1d,
                     index_2d, index_3d);
    }
    static DxbcSrc Label(uint32_t index) {
      return DxbcSrc(DxbcOperandType::kLabel, kXXXX, index);
    }
    static DxbcSrc VPrim() {
      return DxbcSrc(DxbcOperandType::kInputPrimitiveID, kXXXX);
    }
    static DxbcSrc VICP(DxbcIndex index_1d, DxbcIndex index_2d,
                        uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kInputControlPoint, swizzle, index_1d,
                     index_2d);
    }
    static DxbcSrc VDomain(uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kInputDomainPoint, swizzle);
    }
    static DxbcSrc U(uint32_t index_1d, DxbcIndex index_2d,
                     uint32_t swizzle = kXYZW) {
      return DxbcSrc(DxbcOperandType::kUnorderedAccessView, swizzle, index_1d,
                     index_2d);
    }
    static DxbcSrc VCoverage() {
      return DxbcSrc(DxbcOperandType::kInputCoverageMask, kXXXX);
    }

    [[nodiscard]] DxbcSrc WithModifiers(bool absolute, bool negate) const {
      DxbcSrc new_src(*this);
      new_src.absolute_ = absolute;
      new_src.negate_ = negate;
      return new_src;
    }
    [[nodiscard]] DxbcSrc WithAbs(bool absolute) const {
      return WithModifiers(absolute, negate_);
    }
    [[nodiscard]] DxbcSrc WithNeg(bool negate) const {
      return WithModifiers(absolute_, negate);
    }
    [[nodiscard]] DxbcSrc Abs() const { return WithModifiers(true, false); }
    [[nodiscard]] DxbcSrc operator-() const {
      return WithModifiers(absolute_, !negate_);
    }
    [[nodiscard]] DxbcSrc Swizzle(uint32_t swizzle) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = swizzle;
      return new_src;
    }
    [[nodiscard]] DxbcSrc SwizzleSwizzled(uint32_t swizzle) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = 0;
      for (uint32_t i = 0; i < 4; ++i) {
        new_src.swizzle_ |= ((swizzle_ >> (((swizzle >> (i * 2)) & 3) * 2)) & 3)
                            << (i * 2);
      }
      return new_src;
    }
    [[nodiscard]] DxbcSrc Select(uint32_t component) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = component * 0b01010101;
      return new_src;
    }
    [[nodiscard]] DxbcSrc SelectFromSwizzled(uint32_t component) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = ((swizzle_ >> (component * 2)) & 3) * 0b01010101;
      return new_src;
    }

    uint32_t GetLength(uint32_t mask, bool force_vector = false) const {
      bool is_vector = force_vector ||
                       (mask != 0b0000 &&
                        DxbcDest::GetMaskSingleComponent(mask) == UINT32_MAX);
      if (type_ == DxbcOperandType::kImmediate32) {
        return is_vector ? 5 : 2;
      }
      return ((absolute_ || negate_) ? 2 : 1) + DxbcOperandAddress::GetLength();
    }
    static constexpr uint32_t GetModifiedImmediate(uint32_t value,
                                                   bool is_integer,
                                                   bool absolute, bool negate) {
      if (is_integer) {
        if (absolute) {
          *reinterpret_cast<int32_t*>(&value) =
              std::abs(*reinterpret_cast<const int32_t*>(&value));
        }
        if (negate) {
          *reinterpret_cast<int32_t*>(&value) =
              -*reinterpret_cast<const int32_t*>(&value);
        }
      } else {
        if (absolute) {
          value &= uint32_t(INT32_MAX);
        }
        if (negate) {
          value ^= uint32_t(INT32_MAX) + 1;
        }
      }
      return value;
    }
    uint32_t GetModifiedImmediate(uint32_t swizzle_index,
                                  bool is_integer) const {
      return GetModifiedImmediate(
          immediate_[(swizzle_ >> (swizzle_index * 2)) & 3], is_integer,
          absolute_, negate_);
    }
    void Write(std::vector<uint32_t>& code, bool is_integer, uint32_t mask,
               bool force_vector = false) const;
  };

  // D3D10_SB_OPCODE_TYPE subset
  enum class DxbcOpcode : uint32_t {
    kAdd = 0,
    kAnd = 1,
    kBreak = 2,
    kCall = 4,
    kCallC = 5,
    kCase = 6,
    kContinue = 7,
    kDefault = 10,
    kDiscard = 13,
    kDiv = 14,
    kDP2 = 15,
    kDP3 = 16,
    kDP4 = 17,
    kElse = 18,
    kEndIf = 21,
    kEndLoop = 22,
    kEndSwitch = 23,
    kEq = 24,
    kExp = 25,
    kFrc = 26,
    kFToI = 27,
    kFToU = 28,
    kGE = 29,
    kIAdd = 30,
    kIf = 31,
    kIEq = 32,
    kIGE = 33,
    kILT = 34,
    kIMAd = 35,
    kIMax = 36,
    kIMin = 37,
    kIMul = 38,
    kINE = 39,
    kIShL = 41,
    kIToF = 43,
    kLabel = 44,
    kLog = 47,
    kLoop = 48,
    kLT = 49,
    kMAd = 50,
    kMin = 51,
    kMax = 52,
    kMov = 54,
    kMovC = 55,
    kMul = 56,
    kNE = 57,
    kNot = 59,
    kOr = 60,
    kRet = 62,
    kRetC = 63,
    kRoundNE = 64,
    kRoundNI = 65,
    kRoundZ = 67,
    kRSq = 68,
    kSampleL = 72,
    kSampleD = 73,
    kSqRt = 75,
    kSwitch = 76,
    kSinCos = 77,
    kULT = 79,
    kUGE = 80,
    kUMul = 81,
    kUMAd = 82,
    kUMax = 83,
    kUMin = 84,
    kUShR = 85,
    kUToF = 86,
    kXOr = 87,
    kLOD = 108,
    kDerivRTXCoarse = 122,
    kDerivRTXFine = 123,
    kDerivRTYCoarse = 124,
    kDerivRTYFine = 125,
    kRcp = 129,
    kF32ToF16 = 130,
    kF16ToF32 = 131,
    kFirstBitHi = 135,
    kUBFE = 138,
    kIBFE = 139,
    kBFI = 140,
    kBFRev = 141,
    kLdUAVTyped = 163,
    kStoreUAVTyped = 164,
    kLdRaw = 165,
    kStoreRaw = 166,
    kEvalSampleIndex = 204,
    kEvalCentroid = 205,
  };

  // D3D10_SB_EXTENDED_OPCODE_TYPE
  enum class DxbcExtendedOpcodeType : uint32_t {
    kEmpty,
    kSampleControls,
    kResourceDim,
    kResourceReturnType,
  };

  static constexpr uint32_t DxbcOpcodeToken(
      DxbcOpcode opcode, uint32_t operands_length, bool saturate = false,
      uint32_t extended_opcode_count = 0) {
    return uint32_t(opcode) | (saturate ? (uint32_t(1) << 13) : 0) |
           ((uint32_t(1) + extended_opcode_count + operands_length) << 24) |
           (extended_opcode_count ? (uint32_t(1) << 31) : 0);
  }

  static constexpr uint32_t DxbcSampleControlsExtendedOpcodeToken(
      int32_t aoffimmi_u, int32_t aoffimmi_v, int32_t aoffimmi_w,
      bool extended = false) {
    return uint32_t(DxbcExtendedOpcodeType::kSampleControls) |
           ((uint32_t(aoffimmi_u) & uint32_t(0b1111)) << 9) |
           ((uint32_t(aoffimmi_v) & uint32_t(0b1111)) << 13) |
           ((uint32_t(aoffimmi_w) & uint32_t(0b1111)) << 17) |
           (extended ? (uint32_t(1) << 31) : 0);
  }

  void DxbcEmitAluOp(DxbcOpcode opcode, uint32_t src_are_integer,
                     const DxbcDest& dest, const DxbcSrc& src,
                     bool saturate = false) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t operands_length =
        dest.GetLength() + src.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length, saturate));
    dest.Write(shader_code_);
    src.Write(shader_code_, (src_are_integer & 0b1) != 0, dest_write_mask);
    ++stat_.instruction_count;
  }
  void DxbcEmitAluOp(DxbcOpcode opcode, uint32_t src_are_integer,
                     const DxbcDest& dest, const DxbcSrc& src0,
                     const DxbcSrc& src1, bool saturate = false) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t operands_length = dest.GetLength() +
                               src0.GetLength(dest_write_mask) +
                               src1.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length, saturate));
    dest.Write(shader_code_);
    src0.Write(shader_code_, (src_are_integer & 0b1) != 0, dest_write_mask);
    src1.Write(shader_code_, (src_are_integer & 0b10) != 0, dest_write_mask);
    ++stat_.instruction_count;
  }
  void DxbcEmitAluOp(DxbcOpcode opcode, uint32_t src_are_integer,
                     const DxbcDest& dest, const DxbcSrc& src0,
                     const DxbcSrc& src1, const DxbcSrc& src2,
                     bool saturate = false) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t operands_length =
        dest.GetLength() + src0.GetLength(dest_write_mask) +
        src1.GetLength(dest_write_mask) + src2.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length, saturate));
    dest.Write(shader_code_);
    src0.Write(shader_code_, (src_are_integer & 0b1) != 0, dest_write_mask);
    src1.Write(shader_code_, (src_are_integer & 0b10) != 0, dest_write_mask);
    src2.Write(shader_code_, (src_are_integer & 0b100) != 0, dest_write_mask);
    ++stat_.instruction_count;
  }
  void DxbcEmitAluOp(DxbcOpcode opcode, uint32_t src_are_integer,
                     const DxbcDest& dest, const DxbcSrc& src0,
                     const DxbcSrc& src1, const DxbcSrc& src2,
                     const DxbcSrc& src3, bool saturate = false) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t operands_length =
        dest.GetLength() + src0.GetLength(dest_write_mask) +
        src1.GetLength(dest_write_mask) + src2.GetLength(dest_write_mask) +
        src3.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length, saturate));
    dest.Write(shader_code_);
    src0.Write(shader_code_, (src_are_integer & 0b1) != 0, dest_write_mask);
    src1.Write(shader_code_, (src_are_integer & 0b10) != 0, dest_write_mask);
    src2.Write(shader_code_, (src_are_integer & 0b100) != 0, dest_write_mask);
    src3.Write(shader_code_, (src_are_integer & 0b1000) != 0, dest_write_mask);
    ++stat_.instruction_count;
  }
  void DxbcEmitAluOp(DxbcOpcode opcode, uint32_t src_are_integer,
                     const DxbcDest& dest0, const DxbcDest& dest1,
                     const DxbcSrc& src, bool saturate = false) {
    uint32_t dest_write_mask = dest0.GetMask() | dest1.GetMask();
    uint32_t operands_length =
        dest0.GetLength() + dest1.GetLength() + src.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length, saturate));
    dest0.Write(shader_code_);
    dest1.Write(shader_code_);
    src.Write(shader_code_, (src_are_integer & 0b1) != 0, dest_write_mask);
    ++stat_.instruction_count;
  }
  void DxbcEmitAluOp(DxbcOpcode opcode, uint32_t src_are_integer,
                     const DxbcDest& dest0, const DxbcDest& dest1,
                     const DxbcSrc& src0, const DxbcSrc& src1,
                     bool saturate = false) {
    uint32_t dest_write_mask = dest0.GetMask() | dest1.GetMask();
    uint32_t operands_length = dest0.GetLength() + dest1.GetLength() +
                               src0.GetLength(dest_write_mask) +
                               src1.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length, saturate));
    dest0.Write(shader_code_);
    dest1.Write(shader_code_);
    src0.Write(shader_code_, (src_are_integer & 0b1) != 0, dest_write_mask);
    src1.Write(shader_code_, (src_are_integer & 0b10) != 0, dest_write_mask);
    ++stat_.instruction_count;
  }
  void DxbcEmitFlowOp(DxbcOpcode opcode, const DxbcSrc& src,
                      bool test = false) {
    uint32_t operands_length = src.GetLength(0b0000);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length) |
                           (test ? (1 << 18) : 0));
    src.Write(shader_code_, true, 0b0000);
    ++stat_.instruction_count;
  }
  void DxbcEmitFlowOp(DxbcOpcode opcode, const DxbcSrc& src0,
                      const DxbcSrc& src1, bool test = false) {
    uint32_t operands_length = src0.GetLength(0b0000) + src1.GetLength(0b0000);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(opcode, operands_length) |
                           (test ? (1 << 18) : 0));
    src0.Write(shader_code_, true, 0b0000);
    src1.Write(shader_code_, true, 0b0000);
    ++stat_.instruction_count;
  }

  void DxbcOpAdd(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kAdd, 0b00, dest, src0, src1, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpAnd(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kAnd, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpBreak() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kBreak, 0));
    ++stat_.instruction_count;
  }
  void DxbcOpCall(const DxbcSrc& label) {
    DxbcEmitFlowOp(DxbcOpcode::kCall, label);
    ++stat_.static_flow_control_count;
  }
  void DxbcOpCallC(bool test, const DxbcSrc& src, const DxbcSrc& label) {
    DxbcEmitFlowOp(DxbcOpcode::kCallC, src, label, test);
    ++stat_.dynamic_flow_control_count;
  }
  void DxbcOpCase(const DxbcSrc& src) {
    DxbcEmitFlowOp(DxbcOpcode::kCase, src);
    ++stat_.static_flow_control_count;
  }
  void DxbcOpContinue() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kContinue, 0));
    ++stat_.instruction_count;
  }
  void DxbcOpDefault() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kDefault, 0));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;
  }
  void DxbcOpDiscard(bool test, const DxbcSrc& src) {
    DxbcEmitFlowOp(DxbcOpcode::kDiscard, src, test);
  }
  void DxbcOpDiv(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kDiv, 0b00, dest, src0, src1, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpDP2(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    uint32_t operands_length =
        dest.GetLength() + src0.GetLength(0b0011) + src1.GetLength(0b0011);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kDP2, operands_length, saturate));
    dest.Write(shader_code_);
    src0.Write(shader_code_, false, 0b0011);
    src1.Write(shader_code_, false, 0b0011);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
  void DxbcOpDP3(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    uint32_t operands_length =
        dest.GetLength() + src0.GetLength(0b0111) + src1.GetLength(0b0111);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kDP3, operands_length, saturate));
    dest.Write(shader_code_);
    src0.Write(shader_code_, false, 0b0111);
    src1.Write(shader_code_, false, 0b0111);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
  void DxbcOpDP4(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    uint32_t operands_length =
        dest.GetLength() + src0.GetLength(0b1111) + src1.GetLength(0b1111);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kDP4, operands_length, saturate));
    dest.Write(shader_code_);
    src0.Write(shader_code_, false, 0b1111);
    src1.Write(shader_code_, false, 0b1111);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
  }
  void DxbcOpElse() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kElse, 0));
    ++stat_.instruction_count;
  }
  void DxbcOpEndIf() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kEndIf, 0));
    ++stat_.instruction_count;
  }
  void DxbcOpEndLoop() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kEndLoop, 0));
    ++stat_.instruction_count;
  }
  void DxbcOpEndSwitch() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kEndSwitch, 0));
    ++stat_.instruction_count;
  }
  void DxbcOpEq(const DxbcDest& dest, const DxbcSrc& src0,
                const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kEq, 0b00, dest, src0, src1);
    ++stat_.float_instruction_count;
  }
  void DxbcOpExp(const DxbcDest& dest, const DxbcSrc& src,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kExp, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpFrc(const DxbcDest& dest, const DxbcSrc& src,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kFrc, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpFToI(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kFToI, 0b0, dest, src);
    ++stat_.conversion_instruction_count;
  }
  void DxbcOpFToU(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kFToU, 0b0, dest, src);
    ++stat_.conversion_instruction_count;
  }
  void DxbcOpGE(const DxbcDest& dest, const DxbcSrc& src0,
                const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kGE, 0b00, dest, src0, src1);
    ++stat_.float_instruction_count;
  }
  void DxbcOpIAdd(const DxbcDest& dest, const DxbcSrc& src0,
                  const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kIAdd, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIf(bool test, const DxbcSrc& src) {
    DxbcEmitFlowOp(DxbcOpcode::kIf, src, test);
    ++stat_.dynamic_flow_control_count;
  }
  void DxbcOpIEq(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kIEq, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIGE(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kIGE, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpILT(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kILT, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIMAd(const DxbcDest& dest, const DxbcSrc& mul0,
                  const DxbcSrc& mul1, const DxbcSrc& add) {
    DxbcEmitAluOp(DxbcOpcode::kIMAd, 0b111, dest, mul0, mul1, add);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIMax(const DxbcDest& dest, const DxbcSrc& src0,
                  const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kIMax, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIMin(const DxbcDest& dest, const DxbcSrc& src0,
                  const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kIMin, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIMul(const DxbcDest& dest_hi, const DxbcDest& dest_lo,
                  const DxbcSrc& src0, const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kIMul, 0b11, dest_hi, dest_lo, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpINE(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kINE, 0b11, dest, src0, src1);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIShL(const DxbcDest& dest, const DxbcSrc& value,
                  const DxbcSrc& shift) {
    DxbcEmitAluOp(DxbcOpcode::kIShL, 0b11, dest, value, shift);
    ++stat_.int_instruction_count;
  }
  void DxbcOpIToF(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kIToF, 0b1, dest, src);
    ++stat_.conversion_instruction_count;
  }
  void DxbcOpLabel(const DxbcSrc& label) {
    // The label is source, not destination, for simplicity, to unify it will
    // call/callc (in DXBC it's just a zero-component label operand).
    uint32_t operands_length = label.GetLength(0b0000);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kLabel, operands_length));
    label.Write(shader_code_, true, 0b0000);
    // Doesn't count towards stat_.instruction_count.
  }
  void DxbcOpLog(const DxbcDest& dest, const DxbcSrc& src,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kLog, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpLoop() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kLoop, 0));
    ++stat_.instruction_count;
    ++stat_.dynamic_flow_control_count;
  }
  void DxbcOpLT(const DxbcDest& dest, const DxbcSrc& src0,
                const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kLT, 0b00, dest, src0, src1);
    ++stat_.float_instruction_count;
  }
  void DxbcOpMAd(const DxbcDest& dest, const DxbcSrc& mul0, const DxbcSrc& mul1,
                 const DxbcSrc& add, bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kMAd, 0b000, dest, mul0, mul1, add, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpMin(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kMin, 0b00, dest, src0, src1, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpMax(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kMax, 0b00, dest, src0, src1, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpMov(const DxbcDest& dest, const DxbcSrc& src,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kMov, 0b0, dest, src, saturate);
    if (dest.type_ == DxbcOperandType::kIndexableTemp ||
        src.type_ == DxbcOperandType::kIndexableTemp) {
      ++stat_.array_instruction_count;
    } else {
      ++stat_.mov_instruction_count;
    }
  }
  void DxbcOpMovC(const DxbcDest& dest, const DxbcSrc& test,
                  const DxbcSrc& src_nz, const DxbcSrc& src_z,
                  bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kMovC, 0b001, dest, test, src_nz, src_z,
                  saturate);
    ++stat_.movc_instruction_count;
  }
  void DxbcOpMul(const DxbcDest& dest, const DxbcSrc& src0, const DxbcSrc& src1,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kMul, 0b00, dest, src0, src1, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpNE(const DxbcDest& dest, const DxbcSrc& src0,
                const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kNE, 0b00, dest, src0, src1);
    ++stat_.float_instruction_count;
  }
  void DxbcOpNot(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kNot, 0b1, dest, src);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpOr(const DxbcDest& dest, const DxbcSrc& src0,
                const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kOr, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpRet() {
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kRet, 0));
    ++stat_.instruction_count;
    ++stat_.static_flow_control_count;
  }
  void DxbcOpRetC(bool test, const DxbcSrc& src) {
    DxbcEmitFlowOp(DxbcOpcode::kRetC, src, test);
    ++stat_.dynamic_flow_control_count;
  }
  void DxbcOpRoundNE(const DxbcDest& dest, const DxbcSrc& src,
                     bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kRoundNE, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpRoundNI(const DxbcDest& dest, const DxbcSrc& src,
                     bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kRoundNI, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpRoundZ(const DxbcDest& dest, const DxbcSrc& src,
                    bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kRoundZ, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpRSq(const DxbcDest& dest, const DxbcSrc& src,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kRSq, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpSampleL(const DxbcDest& dest, const DxbcSrc& address,
                     uint32_t address_components, const DxbcSrc& resource,
                     const DxbcSrc& sampler, const DxbcSrc& lod,
                     int32_t aoffimmi_u = 0, int32_t aoffimmi_v = 0,
                     int32_t aoffimmi_w = 0) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t sample_controls = 0;
    if (aoffimmi_u || aoffimmi_v || aoffimmi_w) {
      sample_controls = DxbcSampleControlsExtendedOpcodeToken(
          aoffimmi_u, aoffimmi_v, aoffimmi_w);
    }
    uint32_t address_mask = (1 << address_components) - 1;
    uint32_t operands_length =
        dest.GetLength() + address.GetLength(address_mask) +
        resource.GetLength(dest_write_mask, true) + sampler.GetLength(0b0000) +
        lod.GetLength(0b0000);
    shader_code_.reserve(shader_code_.size() + 1 + (sample_controls ? 1 : 0) +
                         operands_length);
    shader_code_.push_back(DxbcOpcodeToken(
        DxbcOpcode::kSampleL, operands_length, false, sample_controls ? 1 : 0));
    if (sample_controls) {
      shader_code_.push_back(sample_controls);
    }
    dest.Write(shader_code_);
    address.Write(shader_code_, false, address_mask);
    resource.Write(shader_code_, false, dest_write_mask, true);
    sampler.Write(shader_code_, false, 0b0000);
    lod.Write(shader_code_, false, 0b0000);
    ++stat_.instruction_count;
    ++stat_.texture_normal_instructions;
  }
  void DxbcOpSampleD(const DxbcDest& dest, const DxbcSrc& address,
                     uint32_t address_components, const DxbcSrc& resource,
                     const DxbcSrc& sampler, const DxbcSrc& x_derivatives,
                     const DxbcSrc& y_derivatives,
                     uint32_t derivatives_components, int32_t aoffimmi_u = 0,
                     int32_t aoffimmi_v = 0, int32_t aoffimmi_w = 0) {
    // If the address is 1-component, the derivatives are 1-component, if the
    // address is 4-component, the derivatives are 4-component.
    assert_true(derivatives_components <= address_components);
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t sample_controls = 0;
    if (aoffimmi_u || aoffimmi_v || aoffimmi_w) {
      sample_controls = DxbcSampleControlsExtendedOpcodeToken(
          aoffimmi_u, aoffimmi_v, aoffimmi_w);
    }
    uint32_t address_mask = (1 << address_components) - 1;
    uint32_t derivatives_mask = (1 << derivatives_components) - 1;
    uint32_t operands_length =
        dest.GetLength() + address.GetLength(address_mask) +
        resource.GetLength(dest_write_mask, true) + sampler.GetLength(0b0000) +
        x_derivatives.GetLength(derivatives_mask, address_components > 1) +
        y_derivatives.GetLength(derivatives_mask, address_components > 1);
    shader_code_.reserve(shader_code_.size() + 1 + (sample_controls ? 1 : 0) +
                         operands_length);
    shader_code_.push_back(DxbcOpcodeToken(
        DxbcOpcode::kSampleD, operands_length, false, sample_controls ? 1 : 0));
    if (sample_controls) {
      shader_code_.push_back(sample_controls);
    }
    dest.Write(shader_code_);
    address.Write(shader_code_, false, address_mask);
    resource.Write(shader_code_, false, dest_write_mask, true);
    sampler.Write(shader_code_, false, 0b0000);
    x_derivatives.Write(shader_code_, false, derivatives_mask,
                        address_components > 1);
    y_derivatives.Write(shader_code_, false, derivatives_mask,
                        address_components > 1);
    ++stat_.instruction_count;
    ++stat_.texture_gradient_instructions;
  }
  void DxbcOpSqRt(const DxbcDest& dest, const DxbcSrc& src,
                  bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kSqRt, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpSwitch(const DxbcSrc& src) {
    DxbcEmitFlowOp(DxbcOpcode::kSwitch, src);
    ++stat_.dynamic_flow_control_count;
  }
  void DxbcOpSinCos(const DxbcDest& dest_sin, const DxbcDest& dest_cos,
                    const DxbcSrc& src, bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kSinCos, 0b0, dest_sin, dest_cos, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpULT(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kULT, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUGE(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kUGE, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUMul(const DxbcDest& dest_hi, const DxbcDest& dest_lo,
                  const DxbcSrc& src0, const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kUMul, 0b11, dest_hi, dest_lo, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUMAd(const DxbcDest& dest, const DxbcSrc& mul0,
                  const DxbcSrc& mul1, const DxbcSrc& add) {
    DxbcEmitAluOp(DxbcOpcode::kUMAd, 0b111, dest, mul0, mul1, add);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUMax(const DxbcDest& dest, const DxbcSrc& src0,
                  const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kUMax, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUMin(const DxbcDest& dest, const DxbcSrc& src0,
                  const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kUMin, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUShR(const DxbcDest& dest, const DxbcSrc& value,
                  const DxbcSrc& shift) {
    DxbcEmitAluOp(DxbcOpcode::kUShR, 0b11, dest, value, shift);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUToF(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kUToF, 0b1, dest, src);
    ++stat_.conversion_instruction_count;
  }
  void DxbcOpXOr(const DxbcDest& dest, const DxbcSrc& src0,
                 const DxbcSrc& src1) {
    DxbcEmitAluOp(DxbcOpcode::kXOr, 0b11, dest, src0, src1);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpLOD(const DxbcDest& dest, const DxbcSrc& address,
                 uint32_t address_components, const DxbcSrc& resource,
                 const DxbcSrc& sampler) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t address_mask = (1 << address_components) - 1;
    uint32_t operands_length =
        dest.GetLength() + address.GetLength(address_mask) +
        resource.GetLength(dest_write_mask) + sampler.GetLength(0b0000);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(DxbcOpcodeToken(DxbcOpcode::kLOD, operands_length));
    dest.Write(shader_code_);
    address.Write(shader_code_, false, address_mask);
    resource.Write(shader_code_, false, dest_write_mask);
    sampler.Write(shader_code_, false, 0b0000);
    ++stat_.instruction_count;
    ++stat_.lod_instructions;
  }
  void DxbcOpDerivRTXCoarse(const DxbcDest& dest, const DxbcSrc& src,
                            bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kDerivRTXCoarse, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpDerivRTXFine(const DxbcDest& dest, const DxbcSrc& src,
                          bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kDerivRTXFine, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpDerivRTYCoarse(const DxbcDest& dest, const DxbcSrc& src,
                            bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kDerivRTYCoarse, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpDerivRTYFine(const DxbcDest& dest, const DxbcSrc& src,
                          bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kDerivRTYFine, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpRcp(const DxbcDest& dest, const DxbcSrc& src,
                 bool saturate = false) {
    DxbcEmitAluOp(DxbcOpcode::kRcp, 0b0, dest, src, saturate);
    ++stat_.float_instruction_count;
  }
  void DxbcOpF32ToF16(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kF32ToF16, 0b0, dest, src);
    ++stat_.conversion_instruction_count;
  }
  void DxbcOpF16ToF32(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kF16ToF32, 0b1, dest, src);
    ++stat_.conversion_instruction_count;
  }
  void DxbcOpFirstBitHi(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kFirstBitHi, 0b1, dest, src);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpUBFE(const DxbcDest& dest, const DxbcSrc& width,
                  const DxbcSrc& offset, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kUBFE, 0b111, dest, width, offset, src);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpIBFE(const DxbcDest& dest, const DxbcSrc& width,
                  const DxbcSrc& offset, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kIBFE, 0b111, dest, width, offset, src);
    ++stat_.int_instruction_count;
  }
  void DxbcOpBFI(const DxbcDest& dest, const DxbcSrc& width,
                 const DxbcSrc& offset, const DxbcSrc& from,
                 const DxbcSrc& to) {
    DxbcEmitAluOp(DxbcOpcode::kBFI, 0b1111, dest, width, offset, from, to);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpBFRev(const DxbcDest& dest, const DxbcSrc& src) {
    DxbcEmitAluOp(DxbcOpcode::kBFRev, 0b1, dest, src);
    ++stat_.uint_instruction_count;
  }
  void DxbcOpLdUAVTyped(const DxbcDest& dest, const DxbcSrc& address,
                        uint32_t address_components, const DxbcSrc& uav) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t address_mask = (1 << address_components) - 1;
    uint32_t operands_length = dest.GetLength() +
                               address.GetLength(address_mask, true) +
                               uav.GetLength(dest_write_mask, true);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kLdUAVTyped, operands_length));
    dest.Write(shader_code_);
    address.Write(shader_code_, true, address_mask, true);
    uav.Write(shader_code_, false, dest_write_mask, true);
    ++stat_.instruction_count;
    ++stat_.texture_load_instructions;
  }
  void DxbcOpStoreUAVTyped(const DxbcDest& dest, const DxbcSrc& address,
                           uint32_t address_components, const DxbcSrc& value) {
    uint32_t dest_write_mask = dest.GetMask();
    // Typed UAV writes don't support write masking.
    assert_true(dest_write_mask == 0b1111);
    uint32_t address_mask = (1 << address_components) - 1;
    uint32_t operands_length = dest.GetLength() +
                               address.GetLength(address_mask, true) +
                               value.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kStoreUAVTyped, operands_length));
    dest.Write(shader_code_);
    address.Write(shader_code_, true, address_mask, true);
    value.Write(shader_code_, false, dest_write_mask);
    ++stat_.instruction_count;
    ++stat_.c_texture_store_instructions;
  }
  void DxbcOpLdRaw(const DxbcDest& dest, const DxbcSrc& byte_offset,
                   const DxbcSrc& src) {
    // For Load, FXC emits code for writing to any component of the destination,
    // with xxxx swizzle of the source SRV/UAV.
    // For Load2/Load3/Load4, it's xy/xyz/xyzw write mask and xyxx/xyzx/xyzw
    // swizzle.
    uint32_t dest_write_mask = dest.GetMask();
    assert_true(dest_write_mask == 0b0001 || dest_write_mask == 0b0010 ||
                dest_write_mask == 0b0100 || dest_write_mask == 0b1000 ||
                dest_write_mask == 0b0011 || dest_write_mask == 0b0111 ||
                dest_write_mask == 0b1111);
    uint32_t component_count = xe::bit_count(dest_write_mask);
    assert_true((src.swizzle_ & ((1 << (component_count * 2)) - 1)) ==
                (DxbcSrc::kXYZW & ((1 << (component_count * 2)) - 1)));
    uint32_t src_mask = (1 << component_count) - 1;
    uint32_t operands_length = dest.GetLength() +
                               byte_offset.GetLength(0b0000) +
                               src.GetLength(src_mask, true);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kLdRaw, operands_length));
    dest.Write(shader_code_);
    byte_offset.Write(shader_code_, true, 0b0000);
    src.Write(shader_code_, true, src_mask, true);
    ++stat_.instruction_count;
    ++stat_.texture_load_instructions;
  }
  void DxbcOpStoreRaw(const DxbcDest& dest, const DxbcSrc& byte_offset,
                      const DxbcSrc& value) {
    uint32_t dest_write_mask = dest.GetMask();
    assert_true(dest_write_mask == 0b0001 || dest_write_mask == 0b0011 ||
                dest_write_mask == 0b0111 || dest_write_mask == 0b1111);
    uint32_t operands_length = dest.GetLength() +
                               byte_offset.GetLength(0b0000) +
                               value.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kStoreRaw, operands_length));
    dest.Write(shader_code_);
    byte_offset.Write(shader_code_, true, 0b0000);
    value.Write(shader_code_, true, dest_write_mask);
    ++stat_.instruction_count;
    ++stat_.c_texture_store_instructions;
  }
  void DxbcOpEvalSampleIndex(const DxbcDest& dest, const DxbcSrc& value,
                             const DxbcSrc& sample_index) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t operands_length = dest.GetLength() +
                               value.GetLength(dest_write_mask) +
                               sample_index.GetLength(0b0000);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kEvalSampleIndex, operands_length));
    dest.Write(shader_code_);
    value.Write(shader_code_, false, dest_write_mask);
    sample_index.Write(shader_code_, true, 0b0000);
    ++stat_.instruction_count;
  }
  void DxbcOpEvalCentroid(const DxbcDest& dest, const DxbcSrc& value) {
    uint32_t dest_write_mask = dest.GetMask();
    uint32_t operands_length =
        dest.GetLength() + value.GetLength(dest_write_mask);
    shader_code_.reserve(shader_code_.size() + 1 + operands_length);
    shader_code_.push_back(
        DxbcOpcodeToken(DxbcOpcode::kEvalCentroid, operands_length));
    dest.Write(shader_code_);
    value.Write(shader_code_, false, dest_write_mask);
    ++stat_.instruction_count;
  }

  enum : uint32_t {
    kSysConst_Flags_Index = 0,
    kSysConst_Flags_Vec = 0,
    kSysConst_Flags_Comp = 0,
    kSysConst_TessellationFactorRange_Index = kSysConst_Flags_Index + 1,
    kSysConst_TessellationFactorRange_Vec = kSysConst_Flags_Vec,
    kSysConst_TessellationFactorRange_Comp = 1,
    kSysConst_LineLoopClosingIndex_Index =
        kSysConst_TessellationFactorRange_Index + 1,
    kSysConst_LineLoopClosingIndex_Vec = kSysConst_Flags_Vec,
    kSysConst_LineLoopClosingIndex_Comp = 3,

    kSysConst_VertexIndexEndian_Index =
        kSysConst_LineLoopClosingIndex_Index + 1,
    kSysConst_VertexIndexEndian_Vec = kSysConst_LineLoopClosingIndex_Vec + 1,
    kSysConst_VertexIndexEndian_Comp = 0,
    kSysConst_VertexBaseIndex_Index = kSysConst_VertexIndexEndian_Index + 1,
    kSysConst_VertexBaseIndex_Vec = kSysConst_VertexIndexEndian_Vec,
    kSysConst_VertexBaseIndex_Comp = 1,
    kSysConst_PointSize_Index = kSysConst_VertexBaseIndex_Index + 1,
    kSysConst_PointSize_Vec = kSysConst_VertexIndexEndian_Vec,
    kSysConst_PointSize_Comp = 2,

    kSysConst_PointSizeMinMax_Index = kSysConst_PointSize_Index + 1,
    kSysConst_PointSizeMinMax_Vec = kSysConst_PointSize_Vec + 1,
    kSysConst_PointSizeMinMax_Comp = 0,
    kSysConst_PointScreenToNDC_Index = kSysConst_PointSizeMinMax_Index + 1,
    kSysConst_PointScreenToNDC_Vec = kSysConst_PointSizeMinMax_Vec,
    kSysConst_PointScreenToNDC_Comp = 2,

    kSysConst_UserClipPlanes_Index = kSysConst_PointScreenToNDC_Index + 1,
    // 6 vectors.
    kSysConst_UserClipPlanes_Vec = kSysConst_PointScreenToNDC_Vec + 1,

    kSysConst_NDCScale_Index = kSysConst_UserClipPlanes_Index + 1,
    kSysConst_NDCScale_Vec = kSysConst_UserClipPlanes_Vec + 6,
    kSysConst_NDCScale_Comp = 0,
    kSysConst_InterpolatorSamplingPattern_Index = kSysConst_NDCScale_Index + 1,
    kSysConst_InterpolatorSamplingPattern_Vec = kSysConst_NDCScale_Vec,
    kSysConst_InterpolatorSamplingPattern_Comp = 3,

    kSysConst_NDCOffset_Index = kSysConst_InterpolatorSamplingPattern_Index + 1,
    kSysConst_NDCOffset_Vec = kSysConst_InterpolatorSamplingPattern_Vec + 1,
    kSysConst_NDCOffset_Comp = 0,
    kSysConst_PSParamGen_Index = kSysConst_NDCOffset_Index + 1,
    kSysConst_PSParamGen_Vec = kSysConst_NDCOffset_Vec,
    kSysConst_PSParamGen_Comp = 3,

    kSysConst_TextureSwizzledSigns_Index = kSysConst_PSParamGen_Index + 1,
    // 2 vectors.
    kSysConst_TextureSwizzledSigns_Vec = kSysConst_PSParamGen_Vec + 1,

    kSysConst_SampleCountLog2_Index = kSysConst_TextureSwizzledSigns_Index + 1,
    kSysConst_SampleCountLog2_Vec = kSysConst_TextureSwizzledSigns_Vec + 2,
    kSysConst_SampleCountLog2_Comp = 0,
    kSysConst_AlphaTestReference_Index = kSysConst_SampleCountLog2_Index + 1,
    kSysConst_AlphaTestReference_Vec = kSysConst_SampleCountLog2_Vec,
    kSysConst_AlphaTestReference_Comp = 2,
    kSysConst_AlphaToMask_Index = kSysConst_AlphaTestReference_Index + 1,
    kSysConst_AlphaToMask_Vec = kSysConst_SampleCountLog2_Vec,
    kSysConst_AlphaToMask_Comp = 3,

    kSysConst_ColorExpBias_Index = kSysConst_AlphaToMask_Index + 1,
    kSysConst_ColorExpBias_Vec = kSysConst_AlphaToMask_Vec + 1,

    kSysConst_ColorOutputMap_Index = kSysConst_ColorExpBias_Index + 1,
    kSysConst_ColorOutputMap_Vec = kSysConst_ColorExpBias_Vec + 1,

    kSysConst_EdramResolutionSquareScale_Index =
        kSysConst_ColorOutputMap_Index + 1,
    kSysConst_EdramResolutionSquareScale_Vec = kSysConst_ColorOutputMap_Vec + 1,
    kSysConst_EdramResolutionSquareScale_Comp = 0,
    kSysConst_EdramPitchTiles_Index =
        kSysConst_EdramResolutionSquareScale_Index + 1,
    kSysConst_EdramPitchTiles_Vec = kSysConst_EdramResolutionSquareScale_Vec,
    kSysConst_EdramPitchTiles_Comp = 1,
    kSysConst_EdramDepthRange_Index = kSysConst_EdramPitchTiles_Index + 1,
    kSysConst_EdramDepthRange_Vec = kSysConst_EdramResolutionSquareScale_Vec,
    kSysConst_EdramDepthRangeScale_Comp = 2,
    kSysConst_EdramDepthRangeOffset_Comp = 3,

    kSysConst_EdramPolyOffsetFront_Index = kSysConst_EdramDepthRange_Index + 1,
    kSysConst_EdramPolyOffsetFront_Vec = kSysConst_EdramDepthRange_Vec + 1,
    kSysConst_EdramPolyOffsetFrontScale_Comp = 0,
    kSysConst_EdramPolyOffsetFrontOffset_Comp = 1,
    kSysConst_EdramPolyOffsetBack_Index =
        kSysConst_EdramPolyOffsetFront_Index + 1,
    kSysConst_EdramPolyOffsetBack_Vec = kSysConst_EdramPolyOffsetFront_Vec,
    kSysConst_EdramPolyOffsetBackScale_Comp = 2,
    kSysConst_EdramPolyOffsetBackOffset_Comp = 3,

    kSysConst_EdramDepthBaseDwords_Index =
        kSysConst_EdramPolyOffsetBack_Index + 1,
    kSysConst_EdramDepthBaseDwords_Vec = kSysConst_EdramPolyOffsetBack_Vec + 1,
    kSysConst_EdramDepthBaseDwords_Comp = 0,

    kSysConst_EdramStencil_Index = kSysConst_EdramDepthBaseDwords_Index + 1,
    // 2 vectors.
    kSysConst_EdramStencil_Vec = kSysConst_EdramDepthBaseDwords_Vec + 1,
    kSysConst_EdramStencil_Front_Vec = kSysConst_EdramStencil_Vec,
    kSysConst_EdramStencil_Back_Vec,
    kSysConst_EdramStencil_Reference_Comp = 0,
    kSysConst_EdramStencil_ReadMask_Comp,
    kSysConst_EdramStencil_WriteMask_Comp,
    kSysConst_EdramStencil_FuncOps_Comp,

    kSysConst_EdramRTBaseDwordsScaled_Index = kSysConst_EdramStencil_Index + 1,
    kSysConst_EdramRTBaseDwordsScaled_Vec = kSysConst_EdramStencil_Vec + 2,

    kSysConst_EdramRTFormatFlags_Index =
        kSysConst_EdramRTBaseDwordsScaled_Index + 1,
    kSysConst_EdramRTFormatFlags_Vec =
        kSysConst_EdramRTBaseDwordsScaled_Vec + 1,

    kSysConst_EdramRTClamp_Index = kSysConst_EdramRTFormatFlags_Index + 1,
    // 4 vectors.
    kSysConst_EdramRTClamp_Vec = kSysConst_EdramRTFormatFlags_Vec + 1,

    kSysConst_EdramRTKeepMask_Index = kSysConst_EdramRTClamp_Index + 1,
    // 2 vectors (render targets 01 and 23).
    kSysConst_EdramRTKeepMask_Vec = kSysConst_EdramRTClamp_Vec + 4,

    kSysConst_EdramRTBlendFactorsOps_Index =
        kSysConst_EdramRTKeepMask_Index + 1,
    kSysConst_EdramRTBlendFactorsOps_Vec = kSysConst_EdramRTKeepMask_Vec + 2,

    kSysConst_EdramBlendConstant_Index =
        kSysConst_EdramRTBlendFactorsOps_Index + 1,
    kSysConst_EdramBlendConstant_Vec = kSysConst_EdramRTBlendFactorsOps_Vec + 1,

    kSysConst_Count = kSysConst_EdramBlendConstant_Index + 1
  };
  static_assert(kSysConst_Count <= 64,
                "Too many system constants, can't use uint64_t for usage bits");

  static constexpr uint32_t kPointParametersTexCoord = xenos::kMaxInterpolators;
  static constexpr uint32_t kClipSpaceZWTexCoord = kPointParametersTexCoord + 1;

  enum class InOutRegister : uint32_t {
    // IF ANY OF THESE ARE CHANGED, WriteInputSignature and WriteOutputSignature
    // MUST BE UPDATED!
    kVSInVertexIndex = 0,

    kDSInControlPointIndex = 0,

    kVSDSOutInterpolators = 0,
    kVSDSOutPointParameters = kVSDSOutInterpolators + xenos::kMaxInterpolators,
    kVSDSOutClipSpaceZW,
    kVSDSOutPosition,
    // Clip and cull distances must be tightly packed in Direct3D!
    kVSDSOutClipDistance0123,
    kVSDSOutClipDistance45AndCullDistance,
    // TODO(Triang3l): Use SV_CullDistance instead for
    // PA_CL_CLIP_CNTL::UCP_CULL_ONLY_ENA, but can't have more than 8 clip and
    // cull distances in total. Currently only using SV_CullDistance for vertex
    // kill.

    kPSInInterpolators = 0,
    kPSInPointParameters = kPSInInterpolators + xenos::kMaxInterpolators,
    kPSInClipSpaceZW,
    kPSInPosition,
    kPSInFrontFace,
  };

  static constexpr uint32_t kSwizzleXYZW = 0b11100100;
  static constexpr uint32_t kSwizzleXXXX = 0b00000000;
  static constexpr uint32_t kSwizzleYYYY = 0b01010101;
  static constexpr uint32_t kSwizzleZZZZ = 0b10101010;
  static constexpr uint32_t kSwizzleWWWW = 0b11111111;

  // Operand encoding, with 32-bit immediate indices by default. None of the
  // arguments must be shifted when calling.
  static constexpr uint32_t EncodeZeroComponentOperand(
      uint32_t type, uint32_t index_dimension,
      uint32_t index_representation_0 = 0, uint32_t index_representation_1 = 0,
      uint32_t index_representation_2 = 0) {
    // D3D10_SB_OPERAND_0_COMPONENT.
    return 0 | (type << 12) | (index_dimension << 20) |
           (index_representation_0 << 22) | (index_representation_1 << 25) |
           (index_representation_0 << 28);
  }
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

  Modification GetDxbcShaderModification() const {
    return Modification(modification());
  }

  bool IsDxbcVertexShader() const {
    return is_vertex_shader() &&
           GetDxbcShaderModification().host_vertex_shader_type ==
               Shader::HostVertexShaderType::kVertex;
  }
  bool IsDxbcDomainShader() const {
    return is_vertex_shader() &&
           GetDxbcShaderModification().host_vertex_shader_type !=
               Shader::HostVertexShaderType::kVertex;
  }

  // Whether to use switch-case rather than if (pc >= label) for control flow.
  bool UseSwitchForControlFlow() const;

  // Allocates new consecutive r# registers for internal use and returns the
  // index of the first.
  uint32_t PushSystemTemp(uint32_t zero_mask = 0, uint32_t count = 1);
  // Frees the last allocated internal r# registers for later reuse.
  void PopSystemTemp(uint32_t count = 1);

  // Converts one scalar to or from PWL gamma, using 1 temporary scalar.
  // The target may be the same as any of the source, the piece temporary or the
  // accumulator, but not two or three of these.
  // The piece and the accumulator can't be the same as source or as each other.
  void ConvertPWLGamma(bool to_gamma, int32_t source_temp,
                       uint32_t source_temp_component, uint32_t target_temp,
                       uint32_t target_temp_component, uint32_t piece_temp,
                       uint32_t piece_temp_component, uint32_t accumulator_temp,
                       uint32_t accumulator_temp_component);

  // Converts the depth value externally clamped to the representable [0, 2)
  // range to 20e4 floating point, with zeros in bits 24:31, rounding to the
  // nearest even. Source and destination may be the same, temporary must be
  // different than both.
  void PreClampedDepthTo20e4(uint32_t d24_temp, uint32_t d24_temp_component,
                             uint32_t d32_temp, uint32_t d32_temp_component,
                             uint32_t temp_temp, uint32_t temp_temp_component);
  bool IsDepthStencilSystemTempUsed() const {
    // See system_temp_depth_stencil_ documentation for explanation of cases.
    if (edram_rov_used_) {
      return writes_depth() || ROV_IsDepthStencilEarly();
    }
    return writes_depth() && DSV_IsWritingFloat24Depth();
  }
  // Whether the current non-ROV pixel shader should convert the depth to 20e4.
  bool DSV_IsWritingFloat24Depth() const {
    if (edram_rov_used_) {
      return false;
    }
    Modification::DepthStencilMode depth_stencil_mode =
        GetDxbcShaderModification().depth_stencil_mode;
    return depth_stencil_mode ==
               Modification::DepthStencilMode::kFloat24Truncating ||
           depth_stencil_mode ==
               Modification::DepthStencilMode::kFloat24Rounding;
  }
  // Whether it's possible and worth skipping running the translated shader for
  // 2x2 quads.
  bool ROV_IsDepthStencilEarly() const {
    return !is_depth_only_pixel_shader_ && !writes_depth() &&
           memexport_stream_constants().empty();
  }
  // Converts the depth value to 24-bit (storing the result in bits 0:23 and
  // zeros in 24:31, not creating room for stencil - since this may be involved
  // in comparisons) according to the format specified in the system constants.
  // Source and destination may be the same, temporary must be different than
  // both.
  void ROV_DepthTo24Bit(uint32_t d24_temp, uint32_t d24_temp_component,
                        uint32_t d32_temp, uint32_t d32_temp_component,
                        uint32_t temp_temp, uint32_t temp_temp_component);
  // Does all the depth/stencil-related things, including or not including
  // writing based on whether it's late, or on whether it's safe to do it early.
  // Updates system_temp_rov_params_ result and coverage if allowed and safe,
  // updates system_temp_depth_stencil_, and if early and the coverage is empty
  // for all pixels in the 2x2 quad and safe to return early (stencil is
  // unchanged or known that it's safe not to await kills/alphatest/AtoC),
  // returns from the shader.
  void ROV_DepthStencilTest();
  // Unpacks a 32bpp or a 64bpp color in packed_temp.packed_temp_components to
  // color_temp, using 2 temporary VGPRs.
  void ROV_UnpackColor(uint32_t rt_index, uint32_t packed_temp,
                       uint32_t packed_temp_components, uint32_t color_temp,
                       uint32_t temp1, uint32_t temp1_component, uint32_t temp2,
                       uint32_t temp2_component);
  // Packs a float32x4 color value to 32bpp or a 64bpp in color_temp to
  // packed_temp.packed_temp_components, using 2 temporary VGPR. color_temp and
  // packed_temp may be the same if packed_temp_components is 0. If the format
  // is 32bpp, will still write the high part to break register dependency.
  void ROV_PackPreClampedColor(uint32_t rt_index, uint32_t color_temp,
                               uint32_t packed_temp,
                               uint32_t packed_temp_components, uint32_t temp1,
                               uint32_t temp1_component, uint32_t temp2,
                               uint32_t temp2_component);
  // Emits a sequence of `case` labels for color blend factors, generating the
  // factor from src_temp.rgb and dst_temp.rgb to factor_temp.rgb. factor_temp
  // can be the same as src_temp or dst_temp.
  void ROV_HandleColorBlendFactorCases(uint32_t src_temp, uint32_t dst_temp,
                                       uint32_t factor_temp);
  // Emits a sequence of `case` labels for alpha blend factors, generating the
  // factor from src_temp.a and dst_temp.a to factor_temp.factor_component.
  // factor_temp can be the same as src_temp or dst_temp.
  void ROV_HandleAlphaBlendFactorCases(uint32_t src_temp, uint32_t dst_temp,
                                       uint32_t factor_temp,
                                       uint32_t factor_component);

  // Writing the prologue.
  void StartVertexShader_LoadVertexIndex();
  void StartVertexOrDomainShader();
  void StartDomainShader();
  void StartPixelShader_LoadROVParameters();
  void StartPixelShader();

  // Writing the epilogue.
  // ExportToMemory modifies the values of eA/eM# for simplicity, don't call
  // multiple times.
  void ExportToMemory_PackFixed32(const uint32_t* eM_temps, uint32_t eM_count,
                                  const uint32_t bits[4],
                                  const DxbcSrc& is_integer,
                                  const DxbcSrc& is_signed);
  void ExportToMemory();
  void CompleteVertexOrDomainShader();
  // Discards the SSAA sample if it's masked out by alpha to coverage.
  void CompletePixelShader_WriteToRTVs_AlphaToMask();
  void CompletePixelShader_WriteToRTVs();
  void CompletePixelShader_DSV_DepthTo24Bit();
  // Masks the sample away from system_temp_rov_params_.x if it's not covered.
  // threshold_offset and temp.temp_component can be the same if needed.
  void CompletePixelShader_ROV_AlphaToMaskSample(
      uint32_t sample_index, float threshold_base, DxbcSrc threshold_offset,
      float threshold_offset_scale, uint32_t temp, uint32_t temp_component);
  // Performs alpha to coverage if necessary, updating the low (coverage) bits
  // of system_temp_rov_params_.x.
  void CompletePixelShader_ROV_AlphaToMask();
  void CompletePixelShader_WriteToROV();
  void CompletePixelShader();

  void CompleteShaderCode();

  // Writes the original instruction disassembly in the output DXBC if enabled,
  // as shader messages, from instruction_disassembly_buffer_.
  void EmitInstructionDisassembly();

  // Converts a shader translator source operand to a DXBC emitter operand, or
  // returns a zero literal operand if it's not going to be referenced. This may
  // allocate a temporary register and emit instructions if the operand can't be
  // used directly with most DXBC instructions (like, if it's an indexable GPR),
  // in this case, temp_pushed_out will be set to true, and PopSystemTemp must
  // be done when the operand is not needed anymore.
  DxbcSrc LoadOperand(const InstructionOperand& operand,
                      uint32_t needed_components, bool& temp_pushed_out);
  // Writes the specified source (src must be usable as a vector `mov` source,
  // including to x#) to an instruction storage target.
  // can_store_memexport_address is for safety, to allow only proper MADs with a
  // stream constant to write to eA.
  void StoreResult(const InstructionResult& result, const DxbcSrc& src,
                   bool can_store_memexport_address = false);

  // The nesting of `if` instructions is the following:
  // - pc checks (labels).
  // - exec predicate/bool constant check.
  // - Instruction-level predicate checks.
  // As an optimization, where possible, the DXBC translator tries to merge
  // multiple execs into one, not creating endif/if doing nothing, if the
  // execution condition is the same. This can't be done across labels
  // (obviously) and in case `setp` is done in a predicated exec - in this case,
  // the predicate value in the current exec may not match the predicate value
  // in the next exec.
  // Instruction-level predicate checks are also merged, and until a `setp` is
  // done, if the instruction has the same predicate condition as the exec it is
  // in, no instruction-level predicate `if` is created as well. One exception
  // to the usual way of instruction-level predicate handling is made for
  // instructions involving derivative computation, such as texture fetches with
  // computed LOD. The part involving derivatives is executed disregarding the
  // predication, but the result storing is predicated (this is handled in
  // texture fetch instruction implementation):
  // https://docs.microsoft.com/en-us/windows/desktop/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-output-color

  // Updates the current flow control condition (to be called in the beginning
  // of exec and in jumps), closing the previous conditionals if needed.
  // However, if the condition is not different, the instruction-level predicate
  // `if` also won't be closed - this must be checked separately if needed (for
  // example, in jumps). Also emits the last disassembly written to
  // instruction_disassembly_buffer_ after closing the previous conditional and
  // before opening a new one.
  void UpdateExecConditionalsAndEmitDisassembly(
      ParsedExecInstruction::Type type, uint32_t bool_constant_index,
      bool condition);
  // Closes `if`s opened by exec and instructions within them (but not by
  // labels) and updates the state accordingly.
  void CloseExecConditionals();
  // Opens or reopens the predicate check conditional for the instruction, and
  // emits the last disassembly written to instruction_disassembly_buffer_ after
  // closing the previous predicate conditional and before opening a new one.
  // This should be called before processing a non-control-flow instruction.
  void UpdateInstructionPredicationAndEmitDisassembly(bool predicated,
                                                      bool condition);
  // Closes the instruction-level predicate `if` if it's open, useful if a flow
  // control instruction needs to do some code which needs to respect the exec's
  // conditional, but can't itself be predicated.
  void CloseInstructionPredication();
  void JumpToLabel(uint32_t address);

  uint32_t FindOrAddTextureBinding(uint32_t fetch_constant,
                                   xenos::FetchOpDimension dimension,
                                   bool is_signed);
  uint32_t FindOrAddSamplerBinding(uint32_t fetch_constant,
                                   xenos::TextureFilter mag_filter,
                                   xenos::TextureFilter min_filter,
                                   xenos::TextureFilter mip_filter,
                                   xenos::AnisoFilter aniso_filter);
  // Returns the number of texture SRV and sampler offsets that need to be
  // passed via a constant buffer to the shader.
  uint32_t GetBindlessResourceCount() const {
    return uint32_t(texture_bindings_.size() + sampler_bindings_.size());
  }
  // Marks fetch constants as used by the DXBC shader and returns DxbcSrc
  // for the words 01 (pair 0), 23 (pair 1) or 45 (pair 2) of the texture fetch
  // constant.
  DxbcSrc RequestTextureFetchConstantWordPair(uint32_t fetch_constant_index,
                                              uint32_t pair_index) {
    if (cbuffer_index_fetch_constants_ == kBindingIndexUnallocated) {
      cbuffer_index_fetch_constants_ = cbuffer_count_++;
    }
    uint32_t total_pair_index = fetch_constant_index * 3 + pair_index;
    return DxbcSrc::CB(cbuffer_index_fetch_constants_,
                       uint32_t(CbufferRegister::kFetchConstants),
                       total_pair_index >> 1,
                       (total_pair_index & 1) ? 0b10101110 : 0b00000100);
  }
  DxbcSrc RequestTextureFetchConstantWord(uint32_t fetch_constant_index,
                                          uint32_t word_index) {
    return RequestTextureFetchConstantWordPair(fetch_constant_index,
                                               word_index >> 1)
        .SelectFromSwizzled(word_index & 1);
  }

  void ProcessVectorAluOperation(const ParsedAluInstruction& instr,
                                 uint32_t& result_swizzle,
                                 bool& predicate_written);
  void ProcessScalarAluOperation(const ParsedAluInstruction& instr,
                                 bool& predicate_written);

  // Appends a string to a DWORD stream, returns the DWORD-aligned length.
  static uint32_t AppendString(std::vector<uint32_t>& dest, const char* source);
  // Returns the length of a string as if it was appended to a DWORD stream, in
  // bytes.
  static uint32_t GetStringLength(const char* source) {
    return uint32_t(xe::align(std::strlen(source) + 1, sizeof(uint32_t)));
  }

  void WriteResourceDefinitions();
  void WriteInputSignature();
  void WritePatchConstantSignature();
  void WriteOutputSignature();
  void WriteShaderCode();

  // Executable instructions - generated during translation.
  std::vector<uint32_t> shader_code_;
  // Complete shader object, with all the needed chunks and dcl_ instructions -
  // generated in the end of translation.
  std::vector<uint32_t> shader_object_;

  // Buffer for instruction disassembly comments.
  StringBuffer instruction_disassembly_buffer_;

  // Whether to write comments with the original Xenos instructions to the
  // output.
  bool emit_source_map_;

  // Vendor ID of the GPU manufacturer, for toggling unsupported features.
  uint32_t vendor_id_;

  // Whether textures and samplers should be bindless.
  bool bindless_resources_used_;

  // Whether the output merger should be emulated in pixel shaders.
  bool edram_rov_used_;

  // Is currently writing the empty depth-only pixel shader, for
  // CompleteTranslation.
  bool is_depth_only_pixel_shader_;

  // Data types used in constants buffers. Listed in dependency order.
  enum class RdefTypeIndex {
    kFloat,
    kFloat2,
    kFloat3,
    kFloat4,
    kInt,
    kUint,
    kUint2,
    kUint4,
    // Render target clamping ranges.
    kFloat4Array4,
    // User clip planes.
    kFloat4Array6,
    // Float constants - size written dynamically.
    kFloat4ConstantArray,
    // Bool constants, texture signedness, front/back stencil, render target
    // keep masks.
    kUint4Array2,
    // Loop constants.
    kUint4Array8,
    // Fetch constants.
    kUint4Array48,
    // Descriptor indices - size written dynamically.
    kUint4DescriptorIndexArray,

    kCount,
    kUnknown = kCount
  };

  struct RdefStructMember {
    const char* name;
    RdefTypeIndex type;
    uint32_t offset;
  };

  struct RdefType {
    // Name ignored for arrays.
    const char* name;
    DxbcRdefVariableClass variable_class;
    DxbcRdefVariableType variable_type;
    uint32_t row_count;
    uint32_t column_count;
    // 0 for primitive types, 1 for structures, array size for arrays.
    uint32_t element_count;
    uint32_t struct_member_count;
    RdefTypeIndex array_element_type;
    const RdefStructMember* struct_members;
  };
  static const RdefType rdef_types_[size_t(RdefTypeIndex::kCount)];

  static constexpr uint32_t kBindingIndexUnallocated = UINT32_MAX;

  // Number of constant buffer bindings used in this shader - also used for
  // generation of indices of constant buffers that are optional.
  uint32_t cbuffer_count_;
  uint32_t cbuffer_index_system_constants_;
  uint32_t cbuffer_index_float_constants_;
  uint32_t cbuffer_index_bool_loop_constants_;
  uint32_t cbuffer_index_fetch_constants_;
  uint32_t cbuffer_index_descriptor_indices_;

  struct SystemConstantRdef {
    const char* name;
    RdefTypeIndex type;
    uint32_t size;
    uint32_t padding_after;
  };
  static const SystemConstantRdef system_constant_rdef_[kSysConst_Count];
  // Mask of system constants (1 << kSysConst_#_Index) used in the shader, so
  // the remaining ones can be marked as unused in RDEF.
  uint64_t system_constants_used_;

  // Mask of domain location actually used in the domain shader.
  uint32_t in_domain_location_used_;
  // Whether the primitive ID has been used in the domain shader.
  bool in_primitive_id_used_;
  // Whether InOutRegister::kDSInControlPointIndex has been used in the shader.
  bool in_control_point_index_used_;
  // Mask of the pixel/sample position actually used in the pixel shader.
  uint32_t in_position_used_;
  // Whether the faceness has been used in the pixel shader.
  bool in_front_face_used_;

  // Number of currently allocated Xenia internal r# registers.
  uint32_t system_temp_count_current_;
  // Total maximum number of temporary registers ever used during this
  // translation (for the declaration).
  uint32_t system_temp_count_max_;

  // Position in vertex shaders (because viewport and W transformations can be
  // applied in the end of the shader).
  uint32_t system_temp_position_;
  // Special exports in vertex shaders.
  uint32_t system_temp_point_size_edge_flag_kill_vertex_;
  // ROV only - 4 persistent VGPRs when writing to color targets, 2 VGPRs when
  // not:
  // X - Bit masks:
  // 0:3 - Per-sample coverage at the current stage of the shader's execution.
  //       Affected by things like SV_Coverage, early or late depth/stencil
  //       (always resets bits for failing, no matter if need to defer writing),
  //       alpha to coverage.
  // 4:7 - Depth write deferred mask - when early depth/stencil resulted in a
  //       different value for the sample (like different stencil if the test
  //       failed), but can't write it before running the shader because it's
  //       not known if the sample will be discarded by the shader, alphatest or
  //       AtoC.
  // Early depth/stencil rejection of the pixel is possible when both 0:3 and
  // 4:7 are zero.
  // 8:11 - Whether color buffers have been written to, if not written on the
  //        taken execution path, don't export according to Direct3D 9 register
  //        documentation (some games rely on this behavior).
  // Y - Absolute resolution-scaled EDRAM offset for depth/stencil, in dwords.
  // Z - Base-relative resolution-scaled EDRAM offset for 32bpp color data, in
  //     dwords.
  // W - Base-relative resolution-scaled EDRAM offset for 64bpp color data, in
  //     dwords.
  uint32_t system_temp_rov_params_;
  // Two purposes:
  // - When writing to oDepth, and either using ROV or converting the depth to
  //   float24: X also used to hold the depth written by the shader,
  //   later used as a temporary during depth/stencil testing.
  // - Otherwise, when using ROV output with ROV_IsDepthStencilEarly being true:
  //   New per-sample depth/stencil values, generated during early depth/stencil
  //   test (actual writing checks coverage bits).
  uint32_t system_temp_depth_stencil_;
  // Up to 4 color outputs in pixel shaders (because of exponent bias, alpha
  // test and remapping, and also for ROV writing).
  uint32_t system_temps_color_[4];

  // Bits containing whether each eM# has been written, for up to 16 streams, or
  // UINT32_MAX if memexport is not used. 8 bits (5 used) for each stream, with
  // 4 `alloc export`s per component.
  uint32_t system_temp_memexport_written_;
  // eA in each `alloc export`, or UINT32_MAX if not used.
  uint32_t system_temps_memexport_address_[kMaxMemExports];
  // eM# in each `alloc export`, or UINT32_MAX if not used.
  uint32_t system_temps_memexport_data_[kMaxMemExports][5];

  // Vector ALU or fetch result/scratch (since Xenos write masks can contain
  // swizzles).
  uint32_t system_temp_result_;
  // Temporary register ID for previous scalar result, program counter,
  // predicate and absolute address register.
  uint32_t system_temp_ps_pc_p0_a0_;
  // Loop index stack - .x is the active loop, shifted right to .yzw on push.
  uint32_t system_temp_aL_;
  // Loop counter stack, .x is the active loop. Represents number of times
  // remaining to loop.
  uint32_t system_temp_loop_count_;
  // Explicitly set texture gradients and LOD.
  uint32_t system_temp_grad_h_lod_;
  uint32_t system_temp_grad_v_;

  // The bool constant number containing the condition for the currently
  // processed exec (or the last - unless a label has reset this), or
  // kCfExecBoolConstantNone if it's not checked.
  uint32_t cf_exec_bool_constant_;
  static constexpr uint32_t kCfExecBoolConstantNone = UINT32_MAX;
  // The expected bool constant value in the current exec if
  // cf_exec_bool_constant_ is not kCfExecBoolConstantNone.
  bool cf_exec_bool_constant_condition_;
  // Whether the currently processed exec is executed if a predicate is
  // set/unset.
  bool cf_exec_predicated_;
  // The expected predicated condition if cf_exec_predicated_ is true.
  bool cf_exec_predicate_condition_;
  // Whether an `if` for instruction-level predicate check is currently open.
  bool cf_instruction_predicate_if_open_;
  // The expected predicate condition for the current or the last instruction if
  // cf_exec_instruction_predicated_ is true.
  bool cf_instruction_predicate_condition_;
  // Whether there was a `setp` in the current exec before the current
  // instruction, thus instruction-level predicate value can be different than
  // the exec-level predicate value, and can't merge two execs with the same
  // predicate condition anymore.
  bool cf_exec_predicate_written_;

  // Number of SRV resources used in this shader - also used for generation of
  // indices of SRV resources that are optional.
  uint32_t srv_count_;
  uint32_t srv_index_shared_memory_;
  uint32_t srv_index_bindless_textures_2d_;
  uint32_t srv_index_bindless_textures_3d_;
  uint32_t srv_index_bindless_textures_cube_;

  // The first binding is at t[SRVMainRegister::kBindfulTexturesStart] of space
  // SRVSpace::kMain.
  std::vector<TextureBinding> texture_bindings_;
  std::unordered_map<uint32_t, uint32_t>
      texture_bindings_for_bindful_srv_indices_;

  // Number of UAV resources used in this shader - also used for generation of
  // indices of UAV resources that are optional.
  uint32_t uav_count_;
  uint32_t uav_index_shared_memory_;
  uint32_t uav_index_edram_;

  std::vector<SamplerBinding> sampler_bindings_;

  // Number of `alloc export`s encountered so far in the translation. The index
  // of the current eA/eM# temp register set is this minus 1, if it's not 0.
  uint32_t memexport_alloc_current_count_;

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
    // Unknown in Wine, but confirmed by testing.
    uint32_t lod_instructions;
    uint32_t unknown_28;
    uint32_t unknown_29;
    uint32_t c_control_points;
    uint32_t hs_output_primitive;
    uint32_t hs_partitioning;
    DxbcTessellatorDomain tessellator_domain;
    // Unknown in Wine.
    uint32_t c_barrier_instructions;
    // Unknown in Wine.
    uint32_t c_interlocked_instructions;
    // Unknown in Wine, but confirmed by testing.
    uint32_t c_texture_store_instructions;
  };
  Statistics stat_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DXBC_SHADER_TRANSLATOR_H_
