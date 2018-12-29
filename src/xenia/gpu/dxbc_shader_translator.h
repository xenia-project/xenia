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

#include <gflags/gflags.h>

#include <cstring>
#include <string>
#include <vector>

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

  enum class VertexShaderType { kVertex, kTriangleDomain, kQuadDomain };
  // Sets the type (shader model and input layout) of the next vertex shader
  // that will be converted.
  void SetVertexShaderType(VertexShaderType type) {
    vertex_shader_type_ = type;
  }

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
    kSysFlag_ReverseZ_Shift,
    kSysFlag_DepthStencil_Shift,
    kSysFlag_DepthFloat24_Shift,
    // Depth/stencil testing not done if DepthStencilRead is disabled, but
    // writing may still be done.
    kSysFlag_DepthPassIfLess_Shift,
    kSysFlag_DepthPassIfEqual_Shift,
    kSysFlag_DepthPassIfGreater_Shift,
    // 1 to write new depth to the depth buffer, 0 to keep the old one if the
    // depth test passes.
    kSysFlag_DepthWriteMask_Shift,
    kSysFlag_StencilTest_Shift,
    // This doesn't include depth/stencil masks - only reflects the fact that
    // the new value must be written.
    kSysFlag_DepthStencilWrite_Shift,
    kSysFlag_Color0Gamma_Shift,
    kSysFlag_Color1Gamma_Shift,
    kSysFlag_Color2Gamma_Shift,
    kSysFlag_Color3Gamma_Shift,

    kSysFlag_SharedMemoryIsUAV = 1u << kSysFlag_SharedMemoryIsUAV_Shift,
    kSysFlag_XYDividedByW = 1u << kSysFlag_XYDividedByW_Shift,
    kSysFlag_ZDividedByW = 1u << kSysFlag_ZDividedByW_Shift,
    kSysFlag_WNotReciprocal = 1u << kSysFlag_WNotReciprocal_Shift,
    kSysFlag_ReverseZ = 1u << kSysFlag_ReverseZ_Shift,
    kSysFlag_DepthStencil = 1u << kSysFlag_DepthStencil_Shift,
    kSysFlag_DepthFloat24 = 1u << kSysFlag_DepthFloat24_Shift,
    kSysFlag_DepthPassIfLess = 1u << kSysFlag_DepthPassIfLess_Shift,
    kSysFlag_DepthPassIfEqual = 1u << kSysFlag_DepthPassIfEqual_Shift,
    kSysFlag_DepthPassIfGreater = 1u << kSysFlag_DepthPassIfGreater_Shift,
    kSysFlag_DepthWriteMask = 1u << kSysFlag_DepthWriteMask_Shift,
    kSysFlag_StencilTest = 1u << kSysFlag_StencilTest_Shift,
    kSysFlag_DepthStencilWrite = 1u << kSysFlag_DepthStencilWrite_Shift,
    kSysFlag_Color0Gamma = 1u << kSysFlag_Color0Gamma_Shift,
    kSysFlag_Color1Gamma = 1u << kSysFlag_Color1Gamma_Shift,
    kSysFlag_Color2Gamma = 1u << kSysFlag_Color2Gamma_Shift,
    kSysFlag_Color3Gamma = 1u << kSysFlag_Color3Gamma_Shift,
  };

  enum : uint32_t {
    kStencilOp_Flag_CurrentMask_Shift,
    // 0, 1 or 3 expanded to 0 or 1 or 0xFF - the value to add.
    kStencilOp_Flag_Add_Shift,
    kStencilOp_Flag_Saturate_Shift = kStencilOp_Flag_Add_Shift + 2,
    kStencilOp_Flag_Invert_Shift,
    kStencilOp_Flag_NewMask_Shift,

    kStencilOp_Flag_CurrentMask = 1u << kStencilOp_Flag_CurrentMask_Shift,
    kStencilOp_Flag_Increment = 1u << kStencilOp_Flag_Add_Shift,
    kStencilOp_Flag_Decrement = 3u << kStencilOp_Flag_Add_Shift,
    kStencilOp_Flag_Saturate = 1u << kStencilOp_Flag_Saturate_Shift,
    kStencilOp_Flag_Invert = 1u << kStencilOp_Flag_Invert_Shift,
    kStencilOp_Flag_NewMask = 1u << kStencilOp_Flag_NewMask_Shift,

    kStencilOp_Keep = kStencilOp_Flag_CurrentMask,
    kStencilOp_Zero = 0,
    kStencilOp_Replace = kStencilOp_Flag_NewMask,
    kStencilOp_IncrementSaturate = kStencilOp_Flag_CurrentMask |
                                   kStencilOp_Flag_Increment |
                                   kStencilOp_Flag_Saturate,
    kStencilOp_DecrementSaturate = kStencilOp_Flag_CurrentMask |
                                   kStencilOp_Flag_Decrement |
                                   kStencilOp_Flag_Saturate,
    kStencilOp_Invert = kStencilOp_Flag_CurrentMask | kStencilOp_Flag_Invert,
    kStencilOp_Increment =
        kStencilOp_Flag_CurrentMask | kStencilOp_Flag_Increment,
    kStencilOp_Decrement =
        kStencilOp_Flag_CurrentMask | kStencilOp_Flag_Decrement,
  };

  enum : uint32_t {
    // Whether the render target needs to be merged with another (if the write
    // mask is not 1111, or 11 for 16_16, or 1 for 32_FLOAT, or blending is
    // enabled and it's not no-op).
    kRTFlag_WriteR_Shift,
    kRTFlag_WriteG_Shift,
    kRTFlag_WriteB_Shift,
    kRTFlag_WriteA_Shift,
    kRTFlag_Blend_Shift,
    // Whether the component does not exist in the render target format.
    kRTFlag_FormatUnusedR_Shift,
    kRTFlag_FormatUnusedG_Shift,
    kRTFlag_FormatUnusedB_Shift,
    kRTFlag_FormatUnusedA_Shift,
    // Whether the format is fixed-point and needs to be converted to integer
    // (k_8_8_8_8, k_2_10_10_10, k_16_16, k_16_16_16_16).
    kRTFlag_FormatFixed_Shift,
    // Whether the format is k_2_10_10_10_FLOAT and 7e3 conversion is needed.
    kRTFlag_FormatFloat10_Shift,
    // Whether the format is k_16_16_FLOAT or k_16_16_16_16_FLOAT and
    // f16tof32/f32tof16 is needed.
    kRTFlag_FormatFloat16_Shift,

    kRTFlag_WriteR = 1u << kRTFlag_WriteR_Shift,
    kRTFlag_WriteG = 1u << kRTFlag_WriteG_Shift,
    kRTFlag_WriteB = 1u << kRTFlag_WriteB_Shift,
    kRTFlag_WriteA = 1u << kRTFlag_WriteA_Shift,
    kRTFlag_Blend = 1u << kRTFlag_Blend_Shift,
    kRTFlag_FormatUnusedR = 1u << kRTFlag_FormatUnusedR_Shift,
    kRTFlag_FormatUnusedG = 1u << kRTFlag_FormatUnusedG_Shift,
    kRTFlag_FormatUnusedB = 1u << kRTFlag_FormatUnusedB_Shift,
    kRTFlag_FormatUnusedA = 1u << kRTFlag_FormatUnusedA_Shift,
    kRTFlag_FormatFixed = 1u << kRTFlag_FormatFixed_Shift,
    kRTFlag_FormatFloat10 = 1u << kRTFlag_FormatFloat10_Shift,
    kRTFlag_FormatFloat16 = 1u << kRTFlag_FormatFloat16_Shift,
  };

  enum : uint32_t {
    // X/Z of the blend constant for the render target.

    // For ONE_MINUS modes, enable both One and the needed factor with _Neg.
    kBlendX_Src_One_Shift = 0,
    kBlendX_Src_One = 1u << kBlendX_Src_One_Shift,
    kBlendX_Src_SrcColor_Shift = 1,
    kBlendX_Src_SrcColor_Pos = 1u << kBlendX_Src_SrcColor_Shift,
    kBlendX_Src_SrcColor_Neg = 3u << kBlendX_Src_SrcColor_Shift,
    kBlendX_Src_SrcAlpha_Shift = 3,
    kBlendX_Src_SrcAlpha_Pos = 1u << kBlendX_Src_SrcAlpha_Shift,
    kBlendX_Src_SrcAlpha_Neg = 3u << kBlendX_Src_SrcAlpha_Shift,
    kBlendX_Src_DestColor_Shift = 5,
    kBlendX_Src_DestColor_Pos = 1u << kBlendX_Src_DestColor_Shift,
    kBlendX_Src_DestColor_Neg = 3u << kBlendX_Src_DestColor_Shift,
    kBlendX_Src_DestAlpha_Shift = 7,
    kBlendX_Src_DestAlpha_Pos = 1u << kBlendX_Src_DestAlpha_Shift,
    kBlendX_Src_DestAlpha_Neg = 3u << kBlendX_Src_DestAlpha_Shift,
    kBlendX_Src_SrcAlphaSaturate_Shift = 9,
    kBlendX_Src_SrcAlphaSaturate = 1u << kBlendX_Src_SrcAlphaSaturate_Shift,

    kBlendX_SrcAlpha_One_Shift = 10,
    kBlendX_SrcAlpha_One = 1u << kBlendX_SrcAlpha_One_Shift,
    kBlendX_SrcAlpha_SrcAlpha_Shift = 11,
    kBlendX_SrcAlpha_SrcAlpha_Pos = 1u << kBlendX_SrcAlpha_SrcAlpha_Shift,
    kBlendX_SrcAlpha_SrcAlpha_Neg = 3u << kBlendX_SrcAlpha_SrcAlpha_Shift,
    kBlendX_SrcAlpha_DestAlpha_Shift = 13,
    kBlendX_SrcAlpha_DestAlpha_Pos = 1u << kBlendX_SrcAlpha_DestAlpha_Shift,
    kBlendX_SrcAlpha_DestAlpha_Neg = 3u << kBlendX_SrcAlpha_DestAlpha_Shift,

    // For ONE_MINUS modes, enable both One and the needed factor with _Neg.
    kBlendX_Dest_One_Shift = 15,
    kBlendX_Dest_One = 1u << kBlendX_Dest_One_Shift,
    kBlendX_Dest_SrcColor_Shift = 16,
    kBlendX_Dest_SrcColor_Pos = 1u << kBlendX_Dest_SrcColor_Shift,
    kBlendX_Dest_SrcColor_Neg = 3u << kBlendX_Dest_SrcColor_Shift,
    kBlendX_Dest_SrcAlpha_Shift = 18,
    kBlendX_Dest_SrcAlpha_Pos = 1u << kBlendX_Dest_SrcAlpha_Shift,
    kBlendX_Dest_SrcAlpha_Neg = 3u << kBlendX_Dest_SrcAlpha_Shift,
    kBlendX_Dest_DestColor_Shift = 20,
    kBlendX_Dest_DestColor_Pos = 1u << kBlendX_Dest_DestColor_Shift,
    kBlendX_Dest_DestColor_Neg = 3u << kBlendX_Dest_DestColor_Shift,
    kBlendX_Dest_DestAlpha_Shift = 22,
    kBlendX_Dest_DestAlpha_Pos = 1u << kBlendX_Dest_DestAlpha_Shift,
    kBlendX_Dest_DestAlpha_Neg = 3u << kBlendX_Dest_DestAlpha_Shift,
    kBlendX_Dest_SrcAlphaSaturate_Shift = 24,
    kBlendX_Dest_SrcAlphaSaturate = 1u << kBlendX_Dest_SrcAlphaSaturate_Shift,

    kBlendX_DestAlpha_One_Shift = 25,
    kBlendX_DestAlpha_One = 1u << kBlendX_DestAlpha_One_Shift,
    kBlendX_DestAlpha_SrcAlpha_Shift = 26,
    kBlendX_DestAlpha_SrcAlpha_Pos = 1u << kBlendX_DestAlpha_SrcAlpha_Shift,
    kBlendX_DestAlpha_SrcAlpha_Neg = 3u << kBlendX_DestAlpha_SrcAlpha_Shift,
    kBlendX_DestAlpha_DestAlpha_Shift = 28,
    kBlendX_DestAlpha_DestAlpha_Pos = 1u << kBlendX_DestAlpha_DestAlpha_Shift,
    kBlendX_DestAlpha_DestAlpha_Neg = 3u << kBlendX_DestAlpha_DestAlpha_Shift,

    // Y/W of the blend constant for the render target.

    kBlendY_Src_ConstantColor_Shift = 0,
    kBlendY_Src_ConstantColor_Pos = 1u << kBlendY_Src_ConstantColor_Shift,
    kBlendY_Src_ConstantColor_Neg = 3u << kBlendY_Src_ConstantColor_Shift,
    kBlendY_Src_ConstantAlpha_Shift = 2,
    kBlendY_Src_ConstantAlpha_Pos = 1u << kBlendY_Src_ConstantAlpha_Shift,
    kBlendY_Src_ConstantAlpha_Neg = 3u << kBlendY_Src_ConstantAlpha_Shift,

    kBlendY_SrcAlpha_ConstantAlpha_Shift = 4,
    kBlendY_SrcAlpha_ConstantAlpha_Pos =
        1u << kBlendY_SrcAlpha_ConstantAlpha_Shift,
    kBlendY_SrcAlpha_ConstantAlpha_Neg =
        3u << kBlendY_SrcAlpha_ConstantAlpha_Shift,

    kBlendY_Dest_ConstantColor_Shift = 6,
    kBlendY_Dest_ConstantColor_Pos = 1u << kBlendY_Dest_ConstantColor_Shift,
    kBlendY_Dest_ConstantColor_Neg = 3u << kBlendY_Dest_ConstantColor_Shift,
    kBlendY_Dest_ConstantAlpha_Shift = 8,
    kBlendY_Dest_ConstantAlpha_Pos = 1u << kBlendY_Dest_ConstantAlpha_Shift,
    kBlendY_Dest_ConstantAlpha_Neg = 3u << kBlendY_Dest_ConstantAlpha_Shift,

    kBlendY_DestAlpha_ConstantAlpha_Shift = 10,
    kBlendY_DestAlpha_ConstantAlpha_Pos =
        1u << kBlendY_DestAlpha_ConstantAlpha_Shift,
    kBlendY_DestAlpha_ConstantAlpha_Neg =
        3u << kBlendY_DestAlpha_ConstantAlpha_Shift,

    // For addition/subtraction/inverse subtraction, but must be positive for
    // min/max.
    kBlendY_Src_OpSign_Shift = 12,
    kBlendY_Src_OpSign_Pos = 1u << kBlendY_Src_OpSign_Shift,
    kBlendY_Src_OpSign_Neg = 3u << kBlendY_Src_OpSign_Shift,
    kBlendY_SrcAlpha_OpSign_Shift = 14,
    kBlendY_SrcAlpha_OpSign_Pos = 1u << kBlendY_SrcAlpha_OpSign_Shift,
    kBlendY_SrcAlpha_OpSign_Neg = 3u << kBlendY_SrcAlpha_OpSign_Shift,
    kBlendY_Dest_OpSign_Shift = 16,
    kBlendY_Dest_OpSign_Pos = 1u << kBlendY_Dest_OpSign_Shift,
    kBlendY_Dest_OpSign_Neg = 3u << kBlendY_Dest_OpSign_Shift,
    kBlendY_DestAlpha_OpSign_Shift = 18,
    kBlendY_DestAlpha_OpSign_Pos = 1u << kBlendY_DestAlpha_OpSign_Shift,
    kBlendY_DestAlpha_OpSign_Neg = 3u << kBlendY_DestAlpha_OpSign_Shift,

    kBlendY_Color_OpMin_Shift = 20,
    kBlendY_Color_OpMin = 1u << kBlendY_Color_OpMin_Shift,
    kBlendY_Color_OpMax_Shift = 21,
    kBlendY_Color_OpMax = 1u << kBlendY_Color_OpMax_Shift,
    kBlendY_Alpha_OpMin_Shift = 22,
    kBlendY_Alpha_OpMin = 1u << kBlendY_Alpha_OpMin_Shift,
    kBlendY_Alpha_OpMax_Shift = 23,
    kBlendY_Alpha_OpMax = 1u << kBlendY_Alpha_OpMax_Shift,
  };

  // IF SYSTEM CONSTANTS ARE CHANGED OR ADDED, THE FOLLOWING MUST BE UPDATED:
  // - kSysConst enum (indices, registers and first components).
  // - system_constant_rdef_.
  // - d3d12/shaders/xenos_draw.hlsli (for geometry shaders).
  struct SystemConstants {
    // vec4 0
    uint32_t flags;
    uint32_t vertex_index_endian_and_edge_factors;
    int32_t vertex_base_index;
    uint32_t pixel_pos_reg;

    // vec4 1
    float ndc_scale[3];
    float pixel_half_pixel_offset;

    // vec4 2
    float ndc_offset[3];
    // 0 - disabled, 1 - passes if in range, -1 - fails if in range.
    int32_t alpha_test;

    // vec4 3
    float point_size[2];
    float point_size_min_max[2];

    // vec3 4
    // Inverse scale of the host viewport (but not supersampled), with signs
    // pre-applied.
    float point_screen_to_ndc[2];
    // Log2 of X and Y sample size. For SSAA with RTV/DSV, this is used to get
    // VPOS to pass to the game's shader. For MSAA with ROV, this is used for
    // EDRAM address calculation.
    uint32_t sample_count_log2[2];

    // vec4 5
    // The range is floats as uints so it's easier to pass infinity.
    uint32_t alpha_test_range[2];
    uint32_t edram_pitch_tiles;
    uint32_t edram_depth_base_dwords;

    // vec4 6
    float color_exp_bias[4];

    // vec4 7
    uint32_t color_output_map[4];

    // vec4 8
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

    // vec4 9
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

    // vec4 10
    uint32_t edram_resolution_scale_log2;
    uint32_t edram_stencil_reference;
    uint32_t edram_stencil_read_mask;
    uint32_t edram_stencil_write_mask;

    // vec4 11
    union {
      struct {
        // kStencilOp, separated into sub-operations - not the Xenos enum.
        uint32_t edram_stencil_front_fail;
        uint32_t edram_stencil_front_depth_fail;
        uint32_t edram_stencil_front_pass;
        uint32_t edram_stencil_front_comparison;
      };
      uint32_t edram_stencil_front[4];
    };

    // vec4 12
    union {
      struct {
        // kStencilOp, separated into sub-operations - not the Xenos enum.
        uint32_t edram_stencil_back_fail;
        uint32_t edram_stencil_back_depth_fail;
        uint32_t edram_stencil_back_pass;
        uint32_t edram_stencil_back_comparison;
      };
      uint32_t edram_stencil_back[4];
    };

    // vec4 13
    uint32_t edram_base_dwords[4];

    // vec4 14
    // Binding and format info flags.
    uint32_t edram_rt_flags[4];

    // vec4 15
    // Format info - widths of components in the lower 32 bits (for ibfe/bfi),
    // packed as 8:8:8:8 for each render target.
    uint32_t edram_rt_pack_width_low[4];

    // vec4 16
    // Format info - offsets of components in the lower 32 bits (for ibfe/bfi),
    // packed as 8:8:8:8 for each render target.
    uint32_t edram_rt_pack_offset_low[4];

    // vec4 17
    // Format info - widths of components in the upper 32 bits (for ibfe/bfi),
    // packed as 8:8:8:8 for each render target.
    uint32_t edram_rt_pack_width_high[4];

    // vec4 18
    // Format info - offsets of components in the upper 32 bits (for ibfe/bfi),
    // packed as 8:8:8:8 for each render target.
    uint32_t edram_rt_pack_offset_high[4];

    // vec4 19:20
    // Format info - mask of color and alpha after unpacking, but before float
    // conversion. Primarily to differentiate between signed and unsigned
    // formats because ibfe is used for both since k_16_16 and k_16_16_16_16 are
    // signed.
    uint32_t edram_load_mask_rt01_rt23[2][4];

    // vec4 21:22
    // Format info - scale to apply to the color and the alpha of each render
    // target after unpacking and converting.
    float edram_load_scale_rt01_rt23[2][4];

    // vec4 23:24
    // Render target blending options.
    uint32_t edram_blend_rt01_rt23[2][4];

    // vec4 25
    // The constant blend factor for the respective modes.
    float edram_blend_constant[4];

    // vec4 26:27
    // Format info - minimum color and alpha values (as float, before
    // conversion) writable to the each render target. Integer so it's easier to
    // write infinity.
    uint32_t edram_store_min_rt01_rt23[2][4];

    // vec4 28:29
    // Format info - maximum color and alpha values (as float, before
    // conversion) writable to the each render target. Integer so it's easier to
    // write infinity.
    uint32_t edram_store_max_rt01_rt23[2][4];

    // vec4 30:31
    // Format info - scale to apply to the color and the alpha of each render
    // target before converting and packing.
    float edram_store_scale_rt01_rt23[2][4];
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

  // Returns the bits that need to be added to the RT flags constant - needs to
  // be done externally, not in SetColorFormatConstants, because the flags
  // contain other state.
  static uint32_t GetColorFormatRTFlags(ColorRenderTargetFormat format);
  static void SetColorFormatSystemConstants(SystemConstants& constants,
                                            uint32_t rt_index,
                                            ColorRenderTargetFormat format);
  // Returns whether blending should be done at all (not 1 * src + 0 * dest).
  static bool GetBlendConstants(uint32_t blend_control, uint32_t& blend_x_out,
                                uint32_t& blend_y_out);

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
    kSysConst_VertexIndexEndianAndEdgeFactors_Index = kSysConst_Flags_Index + 1,
    kSysConst_VertexIndexEndianAndEdgeFactors_Vec = kSysConst_Flags_Vec,
    kSysConst_VertexIndexEndianAndEdgeFactors_Comp = 1,
    kSysConst_VertexBaseIndex_Index =
        kSysConst_VertexIndexEndianAndEdgeFactors_Index + 1,
    kSysConst_VertexBaseIndex_Vec = kSysConst_Flags_Vec,
    kSysConst_VertexBaseIndex_Comp = 2,
    kSysConst_PixelPosReg_Index = kSysConst_VertexBaseIndex_Index + 1,
    kSysConst_PixelPosReg_Vec = kSysConst_Flags_Vec,
    kSysConst_PixelPosReg_Comp = 3,

    kSysConst_NDCScale_Index = kSysConst_PixelPosReg_Index + 1,
    kSysConst_NDCScale_Vec = kSysConst_Flags_Vec + 1,
    kSysConst_NDCScale_Comp = 0,
    kSysConst_PixelHalfPixelOffset_Index = kSysConst_NDCScale_Index + 1,
    kSysConst_PixelHalfPixelOffset_Vec = kSysConst_NDCScale_Vec,
    kSysConst_PixelHalfPixelOffset_Comp = 3,

    kSysConst_NDCOffset_Index = kSysConst_PixelHalfPixelOffset_Index + 1,
    kSysConst_NDCOffset_Vec = kSysConst_NDCScale_Vec + 1,
    kSysConst_NDCOffset_Comp = 0,
    kSysConst_AlphaTest_Index = kSysConst_NDCOffset_Index + 1,
    kSysConst_AlphaTest_Vec = kSysConst_NDCOffset_Vec,
    kSysConst_AlphaTest_Comp = 3,

    kSysConst_PointSize_Index = kSysConst_AlphaTest_Index + 1,
    kSysConst_PointSize_Vec = kSysConst_NDCOffset_Vec + 1,
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

    kSysConst_AlphaTestRange_Index = kSysConst_SampleCountLog2_Index + 1,
    kSysConst_AlphaTestRange_Vec = kSysConst_SampleCountLog2_Vec + 1,
    kSysConst_AlphaTestRange_Comp = 0,
    kSysConst_EDRAMPitchTiles_Index = kSysConst_AlphaTestRange_Index + 1,
    kSysConst_EDRAMPitchTiles_Vec = kSysConst_AlphaTestRange_Vec,
    kSysConst_EDRAMPitchTiles_Comp = 2,
    kSysConst_EDRAMDepthBaseDwords_Index = kSysConst_EDRAMPitchTiles_Index + 1,
    kSysConst_EDRAMDepthBaseDwords_Vec = kSysConst_AlphaTestRange_Vec,
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

    kSysConst_EDRAMResolutionScaleLog2_Index =
        kSysConst_EDRAMPolyOffsetBack_Index + 1,
    kSysConst_EDRAMResolutionScaleLog2_Vec =
        kSysConst_EDRAMPolyOffsetBack_Vec + 1,
    kSysConst_EDRAMResolutionScaleLog2_Comp = 0,
    kSysConst_EDRAMStencilReference_Index =
        kSysConst_EDRAMResolutionScaleLog2_Index + 1,
    kSysConst_EDRAMStencilReference_Vec =
        kSysConst_EDRAMResolutionScaleLog2_Vec,
    kSysConst_EDRAMStencilReference_Comp = 1,
    kSysConst_EDRAMStencilReadMask_Index =
        kSysConst_EDRAMStencilReference_Index + 1,
    kSysConst_EDRAMStencilReadMask_Vec = kSysConst_EDRAMResolutionScaleLog2_Vec,
    kSysConst_EDRAMStencilReadMask_Comp = 2,
    kSysConst_EDRAMStencilWriteMask_Index =
        kSysConst_EDRAMStencilReadMask_Index + 1,
    kSysConst_EDRAMStencilWriteMask_Vec =
        kSysConst_EDRAMResolutionScaleLog2_Vec,
    kSysConst_EDRAMStencilWriteMask_Comp = 3,

    kSysConst_EDRAMStencilFront_Index =
        kSysConst_EDRAMStencilWriteMask_Index + 1,
    kSysConst_EDRAMStencilFront_Vec = kSysConst_EDRAMStencilWriteMask_Vec + 1,

    kSysConst_EDRAMStencilBack_Index = kSysConst_EDRAMStencilFront_Index + 1,
    kSysConst_EDRAMStencilBack_Vec = kSysConst_EDRAMStencilFront_Vec + 1,

    // Components of stencil front and back.
    kSysConst_EDRAMStencilSide_Fail_Comp = 0,
    kSysConst_EDRAMStencilSide_DepthFail_Comp = 1,
    kSysConst_EDRAMStencilSide_Pass_Comp = 2,
    kSysConst_EDRAMStencilSide_Comparison_Comp = 3,

    kSysConst_EDRAMBaseDwords_Index = kSysConst_EDRAMStencilBack_Index + 1,
    kSysConst_EDRAMBaseDwords_Vec = kSysConst_EDRAMStencilBack_Vec + 1,

    kSysConst_EDRAMRTFlags_Index = kSysConst_EDRAMBaseDwords_Index + 1,
    kSysConst_EDRAMRTFlags_Vec = kSysConst_EDRAMBaseDwords_Vec + 1,

    kSysConst_EDRAMRTPackWidthLow_Index = kSysConst_EDRAMRTFlags_Index + 1,
    kSysConst_EDRAMRTPackWidthLow_Vec = kSysConst_EDRAMRTFlags_Vec + 1,

    kSysConst_EDRAMRTPackOffsetLow_Index =
        kSysConst_EDRAMRTPackWidthLow_Index + 1,
    kSysConst_EDRAMRTPackOffsetLow_Vec = kSysConst_EDRAMRTPackWidthLow_Vec + 1,

    kSysConst_EDRAMRTPackWidthHigh_Index =
        kSysConst_EDRAMRTPackOffsetLow_Index + 1,
    kSysConst_EDRAMRTPackWidthHigh_Vec = kSysConst_EDRAMRTPackOffsetLow_Vec + 1,

    kSysConst_EDRAMRTPackOffsetHigh_Index =
        kSysConst_EDRAMRTPackWidthHigh_Index + 1,
    kSysConst_EDRAMRTPackOffsetHigh_Vec =
        kSysConst_EDRAMRTPackWidthHigh_Vec + 1,

    kSysConst_EDRAMLoadMaskRT01_Index =
        kSysConst_EDRAMRTPackOffsetHigh_Index + 1,
    kSysConst_EDRAMLoadMaskRT01_Vec = kSysConst_EDRAMRTPackOffsetHigh_Vec + 1,

    kSysConst_EDRAMLoadMaskRT23_Index = kSysConst_EDRAMLoadMaskRT01_Index + 1,
    kSysConst_EDRAMLoadMaskRT23_Vec = kSysConst_EDRAMLoadMaskRT01_Vec + 1,

    kSysConst_EDRAMLoadScaleRT01_Index = kSysConst_EDRAMLoadMaskRT23_Index + 1,
    kSysConst_EDRAMLoadScaleRT01_Vec = kSysConst_EDRAMLoadMaskRT23_Vec + 1,

    kSysConst_EDRAMLoadScaleRT23_Index = kSysConst_EDRAMLoadScaleRT01_Index + 1,
    kSysConst_EDRAMLoadScaleRT23_Vec = kSysConst_EDRAMLoadScaleRT01_Vec + 1,

    kSysConst_EDRAMBlendRT01_Index = kSysConst_EDRAMLoadScaleRT23_Index + 1,
    kSysConst_EDRAMBlendRT01_Vec = kSysConst_EDRAMLoadScaleRT23_Vec + 1,

    kSysConst_EDRAMBlendRT23_Index = kSysConst_EDRAMBlendRT01_Index + 1,
    kSysConst_EDRAMBlendRT23_Vec = kSysConst_EDRAMBlendRT01_Vec + 1,

    kSysConst_EDRAMBlendConstant_Index = kSysConst_EDRAMBlendRT23_Index + 1,
    kSysConst_EDRAMBlendConstant_Vec = kSysConst_EDRAMBlendRT23_Vec + 1,

    kSysConst_EDRAMStoreMinRT01_Index = kSysConst_EDRAMBlendConstant_Index + 1,
    kSysConst_EDRAMStoreMinRT01_Vec = kSysConst_EDRAMBlendConstant_Vec + 1,

    kSysConst_EDRAMStoreMinRT23_Index = kSysConst_EDRAMStoreMinRT01_Index + 1,
    kSysConst_EDRAMStoreMinRT23_Vec = kSysConst_EDRAMStoreMinRT01_Vec + 1,

    kSysConst_EDRAMStoreMaxRT01_Index = kSysConst_EDRAMStoreMinRT23_Index + 1,
    kSysConst_EDRAMStoreMaxRT01_Vec = kSysConst_EDRAMStoreMinRT23_Vec + 1,

    kSysConst_EDRAMStoreMaxRT23_Index = kSysConst_EDRAMStoreMaxRT01_Index + 1,
    kSysConst_EDRAMStoreMaxRT23_Vec = kSysConst_EDRAMStoreMaxRT01_Vec + 1,

    kSysConst_EDRAMStoreScaleRT01_Index = kSysConst_EDRAMStoreMaxRT23_Index + 1,
    kSysConst_EDRAMStoreScaleRT01_Vec = kSysConst_EDRAMStoreMaxRT23_Vec + 1,

    kSysConst_EDRAMStoreScaleRT23_Index =
        kSysConst_EDRAMStoreScaleRT01_Index + 1,
    kSysConst_EDRAMStoreScaleRT23_Vec = kSysConst_EDRAMStoreScaleRT01_Vec + 1,

    kSysConst_Count = kSysConst_EDRAMStoreScaleRT23_Index + 1
  };

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
           vertex_shader_type_ == VertexShaderType::kVertex;
  }
  inline bool IsDxbcDomainShader() const {
    return IsDxbcVertexOrDomainShader() &&
           (vertex_shader_type_ == VertexShaderType::kTriangleDomain ||
            vertex_shader_type_ == VertexShaderType::kQuadDomain);
  }
  inline bool IsDxbcPixelShader() const {
    return is_depth_only_pixel_shader_ || is_pixel_shader();
  }

  // Whether to use switch-case rather than if (pc >= label) for control flow.
  bool UseSwitchForControlFlow() const;

  // Allocates new consecutive r# registers for internal use and returns the
  // index of the first.
  uint32_t PushSystemTemp(bool zero = false, uint32_t count = 1);
  // Frees the last allocated internal r# registers for later reuse.
  void PopSystemTemp(uint32_t count = 1);

  // Writing the prologue.
  void StartVertexShader_LoadVertexIndex();
  void StartVertexOrDomainShader();
  void StartDomainShader();
  void StartPixelShader();

  // Writing the epilogue.
  // ExportToMemory modifies the values of eA/eM# for simplicity, don't call
  // multiple times.
  void ExportToMemory();
  void CompleteVertexOrDomainShader();
  // Converts four depth values to 24-bit unorm or float, depending on the flag
  // value.
  void CompletePixelShader_DepthTo24Bit(uint32_t depths_temp);
  // This just converts the color output value from/to gamma space, not checking
  // any conditions.
  void CompletePixelShader_GammaCorrect(uint32_t color_temp, bool to_gamma);
  void CompletePixelShader_WriteToRTVs();
  inline uint32_t GetEDRAMUAVIndex() const {
    // xe_edram is U1 when there's xe_shared_memory_uav which is U0, but when
    // there's no xe_shared_memory_uav, it's U0.
    return is_depth_only_pixel_shader_ ? 0 : 1;
  }
  // Performs depth/stencil testing. After the test, coverage_out_temp will
  // contain non-zero values for samples that passed the depth/stencil test and
  // are included in SV_Coverage, and zeros for those who didn't.
  //
  // edram_dword_offset_temp.x must contain the address of the first
  // depth/stencil sample - .yzw will be overwritten by this function with the
  // addresses for the other samples if depth/stencil is enabled.
  void CompletePixelShader_WriteToROV_DepthStencil(
      uint32_t edram_dword_offset_temp, uint32_t coverage_out_temp);
  // Extracts widths and offsets of the components in the lower or the upper
  // dword of a pixel from the format constants, for use as ibfe and bfi
  // operands later.
  void CompletePixelShader_WriteToROV_ExtractPackLayout(uint32_t rt_index,
                                                        bool high,
                                                        uint32_t width_temp,
                                                        uint32_t offset_temp);
  // Components of rt_format_flags_temp.
  enum : uint32_t {
    kROVRTFormatFlagTemp_ColorFixed,
    kROVRTFormatFlagTemp_AlphaFixed,
    kROVRTFormatFlagTemp_Float10,
    kROVRTFormatFlagTemp_Float16,

    kROVRTFormatFlagTemp_Fixed_Swizzle =
        kROVRTFormatFlagTemp_ColorFixed * 0b00010101 +
        kROVRTFormatFlagTemp_AlphaFixed * 0b01000000,
  };
  void CompletePixelShader_WriteToROV_UnpackColor(
      uint32_t data_low_temp, uint32_t data_high_temp, uint32_t data_component,
      uint32_t rt_index, uint32_t rt_format_flags_temp, uint32_t target_temp);
  // Clamps the color to the range representable by the render target's format.
  // Will also remove NaN since min and max return the non-NaN value.
  // color_in_temp and color_out_temp may be the same.
  void CompletePixelShader_WriteToROV_ClampColor(uint32_t rt_index,
                                                 uint32_t color_in_temp,
                                                 uint32_t color_out_temp);
  // Extracts 0.0 or plus/minus 1.0 from a blend constant. For example, it can
  // be used to extract one scale for color and alpha into XY, and another scale
  // for color and alpha into ZW. constant_swizzle is a bit mask indicating
  // which part of the blend constant for the render target to extract the scale
  // from, 0b00000000 for X/Z only, 0b01010101 for Y/W only, 0b00000001 for X/Z
  // in the first component, Y/W in the rest (XY changed to ZW automatically
  // according to the render target index - don't set the higher bit).
  void CompletePixelShader_WriteToROV_ExtractBlendScales(
      uint32_t rt_index, uint32_t constant_swizzle, bool is_signed,
      uint32_t shift_x, uint32_t shift_y, uint32_t shift_z, uint32_t shift_w,
      uint32_t target_temp, uint32_t write_mask = 0b1111);
  void CompletePixelShader_WriteToROV_ApplyZeroBlendScale(
      uint32_t scale_temp, uint32_t scale_swizzle, uint32_t factor_in_temp,
      uint32_t factor_swizzle, uint32_t factor_out_temp,
      uint32_t write_mask = 0b1111);
  void CompletePixelShader_WriteToROV_Blend(uint32_t rt_index,
                                            uint32_t rt_format_flags_temp,
                                            uint32_t src_color_and_output_temp,
                                            uint32_t dest_color_temp);
  // Assumes the incoming color is already clamped to the range representable by
  // the RT format.
  void CompletePixelShader_WriteToROV_PackColor(
      uint32_t data_low_temp, uint32_t data_high_temp, uint32_t data_component,
      uint32_t rt_index, uint32_t rt_format_flags_temp,
      uint32_t source_and_scratch_temp);
  void CompletePixelShader_WriteToROV();
  void CompletePixelShader();
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

  void ProcessVectorAluInstruction(const ParsedAluInstruction& instr);
  void ProcessScalarAluInstruction(const ParsedAluInstruction& instr);

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

  VertexShaderType vertex_shader_type_ = VertexShaderType::kVertex;
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
    // Float constants - size written dynamically.
    kFloat4ConstantArray,
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
    uint32_t offset;
    uint32_t size;
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

  // Number of currently allocated Xenia internal r# registers.
  uint32_t system_temp_count_current_;
  // Total maximum number of temporary registers ever used during this
  // translation (for the declaration).
  uint32_t system_temp_count_max_;

  // Position in vertex shaders (because viewport and W transformations can be
  // applied in the end of the shader).
  uint32_t system_temp_position_;

  // 4 color outputs in pixel shaders (because of exponent bias, alpha test and
  // remapping, and also for ROV writing).
  uint32_t system_temps_color_;
  // Whether the color output has been written in the execution path (ROV only).
  uint32_t system_temp_color_written_;
  // Depth value (ROV only). The meaning depends on whether the shader writes to
  // depth.
  // If depth is written to:
  // - X - the value that was written to oDepth.
  // If not:
  // - X - clip space Z / clip space W from the respective pixel shader input.
  // - Y - depth X derivative (for polygon offset).
  // - Z - depth Y derivative.
  uint32_t system_temp_depth_;

  // Bits containing whether each eM# has been written, for up to 16 streams, or
  // UINT32_MAX if memexport is not used. 8 bits (5 used) for each stream, with
  // 4 `alloc export`s per component.
  uint32_t system_temp_memexport_written_;
  // eA in each `alloc export`, or UINT32_MAX if not used.
  uint32_t system_temps_memexport_address_[kMaxMemExports];
  // eM# in each `alloc export`, or UINT32_MAX if not used.
  uint32_t system_temps_memexport_data_[kMaxMemExports][5];

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
