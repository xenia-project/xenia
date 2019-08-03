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

#include <cstring>
#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/shader_translator.h"

DECLARE_bool(dxbc_source_map);

namespace xe {
namespace gpu {

// Generates shader model 5_1 byte code (for Direct3D 12).
class DxbcShaderTranslator : public ShaderTranslator {
 public:
  DxbcShaderTranslator(uint32_t vendor_id, bool edram_rov_used);
  ~DxbcShaderTranslator() override;

  // Constant buffer bindings in space 0.
  enum class CbufferRegister {
    kSystemConstants,
    kFloatConstants,
    kBoolLoopConstants,
    kFetchConstants,
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
    kSysFlag_ReverseZ_Shift,
    kSysFlag_AlphaPassIfLess_Shift,
    kSysFlag_AlphaPassIfEqual_Shift,
    kSysFlag_AlphaPassIfGreater_Shift,
    kSysFlag_AlphaToCoverage_Shift,
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
    // anyway and don't run the shader (to check if the sample may be discarded
    // some way). This, however, also results in depth/stencil testing done
    // entirely early even when it passes to prevent writing in divergent places
    // in the shader. When the shader can kill, this must be set only for
    // RB_DEPTHCONTROL EARLY_Z_ENABLE, not for alpha test/alpha to coverage
    // disabled.
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
    kSysFlag_ReverseZ = 1u << kSysFlag_ReverseZ_Shift,
    kSysFlag_AlphaPassIfLess = 1u << kSysFlag_AlphaPassIfLess_Shift,
    kSysFlag_AlphaPassIfEqual = 1u << kSysFlag_AlphaPassIfEqual_Shift,
    kSysFlag_AlphaPassIfGreater = 1u << kSysFlag_AlphaPassIfGreater_Shift,
    kSysFlag_AlphaToCoverage = 1u << kSysFlag_AlphaToCoverage_Shift,
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
    uint32_t line_loop_closing_index;
    uint32_t vertex_index_endian_and_edge_factors;
    int32_t vertex_base_index;

    float user_clip_planes[6][4];

    float ndc_scale[3];
    uint32_t pixel_pos_reg;

    float ndc_offset[3];
    float pixel_half_pixel_offset;

    float point_size[2];
    float point_size_min_max[2];

    // Inverse scale of the host viewport (but not supersampled), with signs
    // pre-applied.
    float point_screen_to_ndc[2];
    // Log2 of X and Y sample size. For SSAA with RTV/DSV, this is used to get
    // VPOS to pass to the game's shader. For MSAA with ROV, this is used for
    // EDRAM address calculation.
    uint32_t sample_count_log2[2];

    float alpha_test_reference;
    uint32_t edram_resolution_square_scale;
    uint32_t edram_pitch_tiles;
    uint32_t edram_depth_base_dwords;

    float color_exp_bias[4];

    uint32_t color_output_map[4];

    union {
      struct {
        float tessellation_factor_range_min;
        float tessellation_factor_range_max;
      };
      float tessellation_factor_range[2];
    };
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

  // 192 textures at most because there are 32 fetch constants, and textures can
  // be 2D array, 3D or cube, and also signed and unsigned.
  static constexpr uint32_t kMaxTextureSRVIndexBits = 8;
  static constexpr uint32_t kMaxTextureSRVs =
      (1 << kMaxTextureSRVIndexBits) - 1;
  struct TextureSRV {
    uint32_t fetch_constant;
    TextureDimension dimension;
    bool is_signed;
    // Whether this SRV must be bound even if it's signed and all components are
    // unsigned and vice versa (for kGetTextureComputedLod).
    bool is_sign_required;
    std::string name;
  };
  // The first binding returned is at t1 because t0 is shared memory.
  const TextureSRV* GetTextureSRVs(uint32_t& count_out) const {
    count_out = uint32_t(texture_srvs_.size());
    return texture_srvs_.data();
  }

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
    uint32_t fetch_constant;
    TextureFilter mag_filter;
    TextureFilter min_filter;
    TextureFilter mip_filter;
    AnisoFilter aniso_filter;
    std::string name;
  };
  const SamplerBinding* GetSamplerBindings(uint32_t& count_out) const {
    count_out = uint32_t(sampler_bindings_.size());
    return sampler_bindings_.data();
  }

  // Unordered access view bindings in space 0.
  enum class UAVRegister {
    kSharedMemory,
    kEDRAM,
  };

  // Creates a copy of the shader with early depth/stencil testing forced,
  // overriding that alpha testing is used in the shader.
  static std::vector<uint8_t> ForceEarlyDepthStencil(const uint8_t* shader);

  // Returns the format with internal flags for passing via the
  // edram_rt_format_flags system constant.
  static constexpr uint32_t ROV_AddColorFormatFlags(
      ColorRenderTargetFormat format) {
    uint32_t format_flags = uint32_t(format);
    if (format == ColorRenderTargetFormat::k_16_16_16_16 ||
        format == ColorRenderTargetFormat::k_16_16_16_16_FLOAT ||
        format == ColorRenderTargetFormat::k_32_32_FLOAT) {
      format_flags |= kRTFormatFlag_64bpp;
    }
    if (format == ColorRenderTargetFormat::k_8_8_8_8 ||
        format == ColorRenderTargetFormat::k_8_8_8_8_GAMMA ||
        format == ColorRenderTargetFormat::k_2_10_10_10 ||
        format == ColorRenderTargetFormat::k_16_16 ||
        format == ColorRenderTargetFormat::k_16_16_16_16 ||
        format == ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10) {
      format_flags |=
          kRTFormatFlag_FixedPointColor | kRTFormatFlag_FixedPointAlpha;
    } else if (format == ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
               format ==
                   ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16) {
      format_flags |= kRTFormatFlag_FixedPointAlpha;
    }
    return format_flags;
  }
  // Returns the bits that need to be added to the RT flags constant - needs to
  // be done externally, not in SetColorFormatConstants, because the flags
  // contain other state.
  static void ROV_GetColorFormatSystemConstants(
      ColorRenderTargetFormat format, uint32_t write_mask, float& clamp_rgb_low,
      float& clamp_alpha_low, float& clamp_rgb_high, float& clamp_alpha_high,
      uint32_t& keep_mask_low, uint32_t& keep_mask_high);

  // Creates a special pixel shader without color outputs - this resets the
  // state of the translator.
  std::vector<uint8_t> CreateDepthOnlyPixelShader();

 protected:
  void Reset() override;

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
  void ProcessAllocInstruction(const ParsedAllocInstruction& instr) override;

  void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) override;
  void ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  enum : uint32_t {
    kSysConst_Flags_Index = 0,
    kSysConst_Flags_Vec = 0,
    kSysConst_Flags_Comp = 0,
    kSysConst_LineLoopClosingIndex_Index = kSysConst_Flags_Index + 1,
    kSysConst_LineLoopClosingIndex_Vec = kSysConst_Flags_Vec,
    kSysConst_LineLoopClosingIndex_Comp = 1,
    kSysConst_VertexIndexEndianAndEdgeFactors_Index =
        kSysConst_LineLoopClosingIndex_Index + 1,
    kSysConst_VertexIndexEndianAndEdgeFactors_Vec = kSysConst_Flags_Vec,
    kSysConst_VertexIndexEndianAndEdgeFactors_Comp = 2,
    kSysConst_VertexBaseIndex_Index =
        kSysConst_VertexIndexEndianAndEdgeFactors_Index + 1,
    kSysConst_VertexBaseIndex_Vec = kSysConst_Flags_Vec,
    kSysConst_VertexBaseIndex_Comp = 3,

    kSysConst_UserClipPlanes_Index = kSysConst_VertexBaseIndex_Index + 1,
    // 6 vectors.
    kSysConst_UserClipPlanes_Vec = kSysConst_VertexBaseIndex_Vec + 1,

    kSysConst_NDCScale_Index = kSysConst_UserClipPlanes_Index + 1,
    kSysConst_NDCScale_Vec = kSysConst_UserClipPlanes_Vec + 6,
    kSysConst_NDCScale_Comp = 0,
    kSysConst_PixelPosReg_Index = kSysConst_NDCScale_Index + 1,
    kSysConst_PixelPosReg_Vec = kSysConst_NDCScale_Vec,
    kSysConst_PixelPosReg_Comp = 3,

    kSysConst_NDCOffset_Index = kSysConst_PixelPosReg_Index + 1,
    kSysConst_NDCOffset_Vec = kSysConst_PixelPosReg_Vec + 1,
    kSysConst_NDCOffset_Comp = 0,
    kSysConst_PixelHalfPixelOffset_Index = kSysConst_NDCOffset_Index + 1,
    kSysConst_PixelHalfPixelOffset_Vec = kSysConst_NDCOffset_Vec,
    kSysConst_PixelHalfPixelOffset_Comp = 3,

    kSysConst_PointSize_Index = kSysConst_PixelHalfPixelOffset_Index + 1,
    kSysConst_PointSize_Vec = kSysConst_PixelHalfPixelOffset_Vec + 1,
    kSysConst_PointSize_Comp = 0,
    kSysConst_PointSizeMinMax_Index = kSysConst_PointSize_Index + 1,
    kSysConst_PointSizeMinMax_Vec = kSysConst_PointSize_Vec,
    kSysConst_PointSizeMinMax_Comp = 2,

    kSysConst_PointScreenToNDC_Index = kSysConst_PointSizeMinMax_Index + 1,
    kSysConst_PointScreenToNDC_Vec = kSysConst_PointSizeMinMax_Vec + 1,
    kSysConst_PointScreenToNDC_Comp = 0,
    kSysConst_SampleCountLog2_Index = kSysConst_PointScreenToNDC_Index + 1,
    kSysConst_SampleCountLog2_Vec = kSysConst_PointScreenToNDC_Vec,
    kSysConst_SampleCountLog2_Comp = 2,

    kSysConst_AlphaTestReference_Index = kSysConst_SampleCountLog2_Index + 1,
    kSysConst_AlphaTestReference_Vec = kSysConst_SampleCountLog2_Vec + 1,
    kSysConst_AlphaTestReference_Comp = 0,
    kSysConst_EDRAMResolutionSquareScale_Index =
        kSysConst_AlphaTestReference_Index + 1,
    kSysConst_EDRAMResolutionSquareScale_Vec = kSysConst_AlphaTestReference_Vec,
    kSysConst_EDRAMResolutionSquareScale_Comp = 1,
    kSysConst_EDRAMPitchTiles_Index =
        kSysConst_EDRAMResolutionSquareScale_Index + 1,
    kSysConst_EDRAMPitchTiles_Vec = kSysConst_AlphaTestReference_Vec,
    kSysConst_EDRAMPitchTiles_Comp = 2,
    kSysConst_EDRAMDepthBaseDwords_Index = kSysConst_EDRAMPitchTiles_Index + 1,
    kSysConst_EDRAMDepthBaseDwords_Vec = kSysConst_AlphaTestReference_Vec,
    kSysConst_EDRAMDepthBaseDwords_Comp = 3,

    kSysConst_ColorExpBias_Index = kSysConst_EDRAMDepthBaseDwords_Index + 1,
    kSysConst_ColorExpBias_Vec = kSysConst_EDRAMDepthBaseDwords_Vec + 1,

    kSysConst_ColorOutputMap_Index = kSysConst_ColorExpBias_Index + 1,
    kSysConst_ColorOutputMap_Vec = kSysConst_ColorExpBias_Vec + 1,

    kSysConst_TessellationFactorRange_Index =
        kSysConst_ColorOutputMap_Index + 1,
    kSysConst_TessellationFactorRange_Vec = kSysConst_ColorOutputMap_Vec + 1,
    kSysConst_TessellationFactorRange_Comp = 0,
    kSysConst_EDRAMDepthRange_Index =
        kSysConst_TessellationFactorRange_Index + 1,
    kSysConst_EDRAMDepthRange_Vec = kSysConst_TessellationFactorRange_Vec,
    kSysConst_EDRAMDepthRangeScale_Comp = 2,
    kSysConst_EDRAMDepthRangeOffset_Comp = 3,

    kSysConst_EDRAMPolyOffsetFront_Index = kSysConst_EDRAMDepthRange_Index + 1,
    kSysConst_EDRAMPolyOffsetFront_Vec = kSysConst_EDRAMDepthRange_Vec + 1,
    kSysConst_EDRAMPolyOffsetFrontScale_Comp = 0,
    kSysConst_EDRAMPolyOffsetFrontOffset_Comp = 1,
    kSysConst_EDRAMPolyOffsetBack_Index =
        kSysConst_EDRAMPolyOffsetFront_Index + 1,
    kSysConst_EDRAMPolyOffsetBack_Vec = kSysConst_EDRAMPolyOffsetFront_Vec,
    kSysConst_EDRAMPolyOffsetBackScale_Comp = 2,
    kSysConst_EDRAMPolyOffsetBackOffset_Comp = 3,

    kSysConst_EDRAMStencil_Index = kSysConst_EDRAMPolyOffsetBack_Index + 1,
    // 2 vectors.
    kSysConst_EDRAMStencil_Vec = kSysConst_EDRAMPolyOffsetFront_Vec + 1,
    kSysConst_EDRAMStencil_Front_Vec = kSysConst_EDRAMStencil_Vec,
    kSysConst_EDRAMStencil_Back_Vec,
    kSysConst_EDRAMStencil_Reference_Comp = 0,
    kSysConst_EDRAMStencil_ReadMask_Comp,
    kSysConst_EDRAMStencil_WriteMask_Comp,
    kSysConst_EDRAMStencil_FuncOps_Comp,

    kSysConst_EDRAMRTBaseDwordsScaled_Index = kSysConst_EDRAMStencil_Index + 1,
    kSysConst_EDRAMRTBaseDwordsScaled_Vec = kSysConst_EDRAMStencil_Vec + 2,

    kSysConst_EDRAMRTFormatFlags_Index =
        kSysConst_EDRAMRTBaseDwordsScaled_Index + 1,
    kSysConst_EDRAMRTFormatFlags_Vec =
        kSysConst_EDRAMRTBaseDwordsScaled_Vec + 1,

    kSysConst_EDRAMRTClamp_Index = kSysConst_EDRAMRTFormatFlags_Index + 1,
    // 4 vectors.
    kSysConst_EDRAMRTClamp_Vec = kSysConst_EDRAMRTFormatFlags_Vec + 1,

    kSysConst_EDRAMRTKeepMask_Index = kSysConst_EDRAMRTClamp_Index + 1,
    // 2 vectors (render targets 01 and 23).
    kSysConst_EDRAMRTKeepMask_Vec = kSysConst_EDRAMRTClamp_Vec + 4,

    kSysConst_EDRAMRTBlendFactorsOps_Index =
        kSysConst_EDRAMRTKeepMask_Index + 1,
    kSysConst_EDRAMRTBlendFactorsOps_Vec = kSysConst_EDRAMRTKeepMask_Vec + 2,

    kSysConst_EDRAMBlendConstant_Index =
        kSysConst_EDRAMRTBlendFactorsOps_Index + 1,
    kSysConst_EDRAMBlendConstant_Vec = kSysConst_EDRAMRTBlendFactorsOps_Vec + 1,

    kSysConst_Count = kSysConst_EDRAMBlendConstant_Index + 1
  };
  static_assert(kSysConst_Count <= 64,
                "Too many system constants, can't use uint64_t for usage bits");

  static constexpr uint32_t kInterpolatorCount = 16;
  static constexpr uint32_t kPointParametersTexCoord = kInterpolatorCount;
  static constexpr uint32_t kClipSpaceZWTexCoord = kPointParametersTexCoord + 1;

  enum class InOutRegister : uint32_t {
    // IF ANY OF THESE ARE CHANGED, WriteInputSignature and WriteOutputSignature
    // MUST BE UPDATED!
    kVSInVertexIndex = 0,

    kVSOutInterpolators = 0,
    kVSOutPointParameters = kVSOutInterpolators + kInterpolatorCount,
    kVSOutClipSpaceZW,
    kVSOutPosition,
    kVSOutClipDistance0123,
    kVSOutClipDistance45,

    kPSInInterpolators = 0,
    kPSInPointParameters = kPSInInterpolators + kInterpolatorCount,
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

  // Use these instead of is_vertex_shader/is_pixel_shader because they don't
  // take is_depth_only_pixel_shader_ into account.
  inline bool IsDxbcVertexOrDomainShader() const {
    return !is_depth_only_pixel_shader_ && is_vertex_shader();
  }
  inline bool IsDxbcVertexShader() const {
    return IsDxbcVertexOrDomainShader() &&
           patch_primitive_type() == PrimitiveType::kNone;
  }
  inline bool IsDxbcDomainShader() const {
    return IsDxbcVertexOrDomainShader() &&
           patch_primitive_type() != PrimitiveType::kNone;
  }
  inline bool IsDxbcPixelShader() const {
    return is_depth_only_pixel_shader_ || is_pixel_shader();
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

  inline uint32_t ROV_GetEDRAMUAVIndex() const {
    // xe_edram is U1 when there's xe_shared_memory_uav which is U0, but when
    // there's no xe_shared_memory_uav, it's U0.
    return is_depth_only_pixel_shader_ ? 0 : 1;
  }
  // Whether it's possible and worth skipping running the translated shader for
  // 2x2 quads.
  bool ROV_IsDepthStencilEarly() const {
    return !is_depth_only_pixel_shader_ && !writes_depth();
  }
  // Does all the depth/stencil-related things, including or not including
  // writing based on whether it's late, or on whether it's safe to do it early.
  // Updates system_temp_rov_params_ result and coverage if allowed and safe,
  // updates system_temp_rov_depth_stencil_, and if early and the coverage is
  // empty for all pixels in the 2x2 quad and safe to return early (stencil is
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
  // packed_temp may be the same if packed_temp_components is 0.
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
  void ExportToMemory();
  void CompleteVertexOrDomainShader();
  // Applies the exponent bias from the constant to one color output.
  void CompletePixelShader_ApplyColorExpBias(uint32_t rt_index);
  // Discards the SSAA sample if it fails alpha to coverage.
  void CompletePixelShader_WriteToRTVs_AlphaToCoverage();
  void CompletePixelShader_WriteToRTVs();
  // Returns if coverage in system_temp_rov_params_.x is empty.
  void CompletePixelShader_ROV_CheckAnyCovered(
      bool check_deferred_stencil_write, uint32_t temp,
      uint32_t temp_component);
  // Masks the sample away from system_temp_rov_params_.x if it's not covered.
  void CompletePixelShader_ROV_AlphaToCoverageSample(uint32_t sample_index,
                                                     float threshold,
                                                     uint32_t temp,
                                                     uint32_t temp_component);
  // Performs alpha to coverage if necessary, updating the low (coverage) bits
  // of system_temp_.
  void CompletePixelShader_ROV_AlphaToCoverage();
  void CompletePixelShader_WriteToROV();
  void CompletePixelShader();

  // Writes a function that converts depth to 24 bits, putting it in 0:23, not
  // creating space for stencil (ROV only).
  // Input:
  // - system_temps_subroutine_[0].x - Z/W + polygon offset at sample.
  // Output:
  // - system_temps_subroutine_[0].x - 24-bit depth.
  // Local temps:
  // - system_temps_subroutine_[0].y.
  void CompleteShaderCode_ROV_DepthTo24BitSubroutine();
  // Writes a function that does early (or both early and late, when not
  // separating) depth/stencil testing for one sample (ROV only).
  // Input:
  // - system_temps_subroutine_[0].x - depth converted to 24 bits in bits 0:23.
  // - system_temp_rov_params_.y - depth sample EDRAM address.
  // Output:
  // - system_temps_subroutine_[0].x - resulting packed depth/stencil.
  // - system_temps_subroutine_[0].y - test result, bit 0 if test FAILED (so
  //   coverage can be updated with XOR), and if depth/stencil is early, also
  //   bit 4 if the pixel shader still needs to be done to check for
  //   kills/alphatest/AtoC before writing the new stencil.
  // Local temps:
  // - system_temps_subroutine_[0].zw.
  // - system_temps_subroutine_[1].xy.
  void CompleteShaderCode_ROV_DepthStencilSampleSubroutine();
  // Writes a function that does loading, blending, write masking and storing
  // for one color sample of the specified render target.
  // Input:
  // - system_temps_subroutine_[0].xy:
  //   - If not blending, packed source color (will be masked by the function).
  //   - If blending, used as a temporary.
  // - system_temp_rov_params_.zw - color sample 32bpp and 64bpp EDRAM
  //   addresses.
  // - system_temps_color_[rt_index]:
  //   - If blending (blend control is 0x00010001), source color clamped to the
  //     render target's representable range if it's fixed-point, unclamped
  //     source color if it's floating-point, not modified.
  //   - If not blending, ignored.
  // Local temps:
  // - system_temps_subroutine_[0].zw.
  // - system_temps_subroutine_[1].xyzw.
  // - system_temps_subroutine_[2].xyz.
  // - system_temps_subroutine_[3].xyz.
  void CompleteShaderCode_ROV_ColorSampleSubroutine(uint32_t rt_index);
  void CompleteShaderCode();

  // Writes the original instruction disassembly in the output DXBC if enabled,
  // as shader messages, from instruction_disassembly_buffer_.
  void EmitInstructionDisassembly();

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
    // If the operand is dynamically indexed directly when it's used as an
    // operand in DXBC instructions.
    InstructionStorageAddressingMode addressing_mode;

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
                                   bool negate = false,
                                   bool absolute = false) const;
  // Writes the operand access tokens to the instruction (either for a scalar if
  // select_component is <= 3, or for a vector).
  void UseDxbcSourceOperand(const DxbcSourceOperand& operand,
                            uint32_t additional_swizzle = kSwizzleXYZW,
                            uint32_t select_component = 4, bool negate = false,
                            bool absolute = false);
  void UnloadDxbcSourceOperand(const DxbcSourceOperand& operand);

  // Writes xyzw or xxxx of the specified r# to the destination.
  // can_store_memexport_address is for safety, to allow only proper MADs with
  // a stream constant to write to eA.
  void StoreResult(const InstructionResult& result, uint32_t reg,
                   bool replicate_x, bool can_store_memexport_address = false);

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
  // example, in jumps). If emit_disassembly is true, this function emits the
  // last disassembly written to instruction_disassembly_buffer_ after closing
  // the previous conditional and before opening a new one.
  void UpdateExecConditionals(ParsedExecInstruction::Type type,
                              uint32_t bool_constant_index, bool condition,
                              bool emit_disassembly);
  // Closes `if`s opened by exec and instructions within them (but not by
  // labels) and updates the state accordingly.
  void CloseExecConditionals();
  // Opens or reopens the predicate check conditional for the instruction. If
  // emit_disassembly is true, this function emits the last disassembly written
  // to instruction_disassembly_buffer_ after closing the previous predicate
  // conditional and before opening a new one.
  void UpdateInstructionPredication(bool predicated, bool condition,
                                    bool emit_disassembly);
  // Closes the instruction-level predicate `if` if it's open, useful if a flow
  // control instruction needs to do some code which needs to respect the exec's
  // conditional, but can't itself be predicated.
  void CloseInstructionPredication();
  void JumpToLabel(uint32_t address);

  // Emits copde for endian swapping of the data located in pv.
  void SwapVertexData(uint32_t vfetch_index, uint32_t write_mask);

  // Returns T#/t# index (they are the same in this translator).
  uint32_t FindOrAddTextureSRV(uint32_t fetch_constant,
                               TextureDimension dimension, bool is_signed,
                               bool is_sign_required = false);
  // Returns S#/s# index (they are the same in this translator).
  uint32_t FindOrAddSamplerBinding(uint32_t fetch_constant,
                                   TextureFilter mag_filter,
                                   TextureFilter min_filter,
                                   TextureFilter mip_filter,
                                   AnisoFilter aniso_filter);
  // Converts (S, T, face index) in the specified temporary register to a 3D
  // cubemap coordinate.
  void ArrayCoordToCubeDirection(uint32_t reg);

  bool ProcessVectorAluOperation(const ParsedAluInstruction& instr,
                                 bool& replicate_result_x,
                                 bool& predicate_written);
  bool ProcessScalarAluOperation(const ParsedAluInstruction& instr,
                                 bool& predicate_written);

  // Appends a string to a DWORD stream, returns the DWORD-aligned length.
  static uint32_t AppendString(std::vector<uint32_t>& dest, const char* source);
  // Returns the length of a string as if it was appended to a DWORD stream, in
  // bytes.
  static inline uint32_t GetStringLength(const char* source) {
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

  // Vendor ID of the GPU manufacturer, for toggling unsupported features.
  uint32_t vendor_id_;

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
    // Front/back stencil, render target keep masks.
    kUint4Array2,
    // Bool constants.
    kUint4Array8,
    // Loop constants.
    kUint4Array32,
    // Fetch constants.
    kUint4Array48,

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

  // Number of constant buffer bindings used in this shader - also used for
  // generation of indices of constant buffers that are optional.
  uint32_t cbuffer_count_;
  static constexpr uint32_t kCbufferIndexUnallocated = UINT32_MAX;
  uint32_t cbuffer_index_system_constants_;
  uint32_t cbuffer_index_float_constants_;
  uint32_t cbuffer_index_bool_loop_constants_;
  uint32_t cbuffer_index_fetch_constants_;

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

  // Whether constants are dynamically indexed and need to be marked as such in
  // dcl_constantBuffer.
  bool float_constants_dynamic_indexed_;
  bool bool_loop_constants_dynamic_indexed_;

  // Offsets of float constant indices in shader_code_, for remapping in
  // CompleteTranslation (initially, at these offsets, guest float constant
  // indices are written).
  std::vector<uint32_t> float_constant_index_offsets_;

  // Subroutine labels. D3D10_SB_OPCODE_LABEL is not counted as an instruction
  // in STAT.
  uint32_t label_rov_depth_to_24bit_;
  uint32_t label_rov_depth_stencil_sample_;
  uint32_t label_rov_color_sample_[4];

  // Number of currently allocated Xenia internal r# registers.
  uint32_t system_temp_count_current_;
  // Total maximum number of temporary registers ever used during this
  // translation (for the declaration).
  uint32_t system_temp_count_max_;

  // Registers for the needed count of non-main-subroutine-local variables.
  // This includes arguments.
  uint32_t system_temps_subroutine_;
  // Number of registers allocated for subroutines other than main.
  uint32_t system_temps_subroutine_count_;

  // Position in vertex shaders (because viewport and W transformations can be
  // applied in the end of the shader).
  uint32_t system_temp_position_;
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
  // ROV only - new depth/stencil data. 4 VGPRs.
  // When not writing to oDepth: New per-sample depth/stencil values, generated
  // during early depth/stencil test (actual writing checks coverage bits).
  // When writing to oDepth: X also used to hold the depth written by the
  // shader, later used as a temporary during depth/stencil testing.
  uint32_t system_temp_rov_depth_stencil_;
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

  // Vector ALU result or fetch scratch (since Xenos write masks can contain
  // swizzles).
  uint32_t system_temp_pv_;
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

  std::vector<TextureSRV> texture_srvs_;
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
    uint32_t tessellator_domain;
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
