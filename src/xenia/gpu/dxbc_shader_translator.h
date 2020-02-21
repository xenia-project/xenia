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
#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/shader_translator.h"

DECLARE_bool(dxbc_source_map);

namespace xe {
namespace gpu {

// Generates shader model 5_1 byte code (for Direct3D 12).
//
// IMPORTANT CONTRIBUTION NOTES:
//
// Not all DXBC instructions accept all kinds of operands equally!
// Refer to Shader Model 4 and 5 Assembly on MSDN to see if the needed
// swizzle/selection, absolute/negate modifiers and saturation are supported by
// the instruction.
// https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm
// Before adding anything that behaves in a way that doesn't follow patterns
// already used in Xenia, try to write the same logic in HLSL, compile it with
// FXC and see the resulting assembly *and preferably binary bytecode* as some
// instructions may, for example, require selection rather than swizzling for
// certain operands. For bytecode structure, see d3d12TokenizedProgramFormat.hpp
// from the Windows Driver Kit.
//
// Avoid using uninitialized register components - such as registers written to
// in "if" and not in "else", but then used outside unconditionally or with a
// different condition (or even with the same condition, but in a different "if"
// block). This will cause crashes on AMD drivers, and will also limit
// optimization possibilities as this may result in false dependencies. Always
// mov l(0, 0, 0, 0) to such components before potential branching -
// PushSystemTemp accepts a zero mask for this purpose.
//
// Clamping must be done first to the lower bound (using max), then to the upper
// bound (using min), to match the saturate modifier behavior, which results in
// 0 for NaN.
class DxbcShaderTranslator : public ShaderTranslator {
 public:
  DxbcShaderTranslator(uint32_t vendor_id, bool edram_rov_used,
                       bool force_emit_source_map = false);
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
    kSysFlag_KillIfAnyVertexKilled_Shift,
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
    kSysFlag_KillIfAnyVertexKilled = 1u << kSysFlag_KillIfAnyVertexKilled_Shift,
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
  // D3D10_SB_OPERAND_TYPE
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
    kInputDomainPoint = 28,
    kUnorderedAccessView = 30,
    kInputCoverageMask = 35,
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
      kImmediate32 = 0,
      kRelative = 2,
      kImmediate32PlusRelative = 3,
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
            operand_token |= uint32_t(index_2d_.GetRepresentation()) << 28;
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
    DxbcDest Mask(uint32_t write_mask) const {
      return DxbcDest(type_, write_mask, index_1d_, index_2d_, index_3d_);
    }
    DxbcDest MaskMasked(uint32_t write_mask) const {
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

    DxbcSrc WithModifiers(bool absolute, bool negate) const {
      DxbcSrc new_src(*this);
      new_src.absolute_ = absolute;
      new_src.negate_ = negate;
      return new_src;
    }
    DxbcSrc WithAbs(bool absolute) const {
      return WithModifiers(absolute, negate_);
    }
    DxbcSrc WithNeg(bool negate) const {
      return WithModifiers(absolute_, negate);
    }
    DxbcSrc Abs() const { return WithModifiers(true, false); }
    DxbcSrc operator-() const { return WithModifiers(absolute_, !negate_); }
    DxbcSrc Swizzle(uint32_t swizzle) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = swizzle;
      return new_src;
    }
    DxbcSrc SwizzleSwizzled(uint32_t swizzle) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = 0;
      for (uint32_t i = 0; i < 4; ++i) {
        new_src.swizzle_ |= ((swizzle_ >> (((swizzle >> (i * 2)) & 3) * 2)) & 3)
                            << (i * 2);
      }
      return new_src;
    }
    DxbcSrc Select(uint32_t component) const {
      DxbcSrc new_src(*this);
      new_src.swizzle_ = component * 0b01010101;
      return new_src;
    }
    DxbcSrc SelectFromSwizzled(uint32_t component) const {
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
    static uint32_t GetModifiedImmediate(uint32_t value, bool is_integer,
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

  // D3D10_SB_OPCODE_TYPE
  enum class DxbcOpcode : uint32_t {
    kAdd = 0,
    kAnd = 1,
    kBreak = 2,
    kCall = 4,
    kCallC = 5,
    kCase = 6,
    kDefault = 10,
    kDiscard = 13,
    kDiv = 14,
    kElse = 18,
    kEndIf = 21,
    kEndLoop = 22,
    kEndSwitch = 23,
    kEq = 24,
    kFToI = 27,
    kFToU = 28,
    kGE = 29,
    kIAdd = 30,
    kIf = 31,
    kIEq = 32,
    kILT = 34,
    kIMAd = 35,
    kIMax = 36,
    kIMin = 37,
    kINE = 39,
    kIShL = 41,
    kIToF = 43,
    kLabel = 44,
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
    kSwitch = 76,
    kULT = 79,
    kUGE = 80,
    kUMul = 81,
    kUMAd = 82,
    kUMax = 83,
    kUMin = 84,
    kUShR = 85,
    kUToF = 86,
    kXOr = 87,
    kDerivRTXCoarse = 122,
    kDerivRTXFine = 123,
    kDerivRTYCoarse = 124,
    kDerivRTYFine = 125,
    kF32ToF16 = 130,
    kF16ToF32 = 131,
    kFirstBitHi = 135,
    kUBFE = 138,
    kIBFE = 139,
    kBFI = 140,
    kLdUAVTyped = 163,
    kStoreUAVTyped = 164,
    kEvalSampleIndex = 204,
  };

  static uint32_t DxbcOpcodeToken(DxbcOpcode opcode, uint32_t operands_length,
                                  bool saturate = false) {
    return uint32_t(opcode) | (saturate ? (1 << 13) : 0) |
           ((1 + operands_length) << 24);
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
  void DxbcOpSwitch(const DxbcSrc& src) {
    DxbcEmitFlowOp(DxbcOpcode::kSwitch, src);
    ++stat_.dynamic_flow_control_count;
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
    // Clip and cull distances must be tightly packed in Direct3D!
    kVSOutClipDistance0123,
    kVSOutClipDistance45AndCullDistance,
    // TODO(Triang3l): Use SV_CullDistance instead for
    // PA_CL_CLIP_CNTL::UCP_CULL_ONLY_ENA, but can't have more than 8 clip and
    // cull distances in total. Currently only using SV_CullDistance for vertex
    // kill.

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
  // packed_temp may be the same if packed_temp_components is 0. If the format
  // is 32bpp, will still the high part to break register dependency.
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
  // Discards the SSAA sample if it fails alpha to coverage.
  void CompletePixelShader_WriteToRTVs_AlphaToCoverage();
  void CompletePixelShader_WriteToRTVs();
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

  // Whether to write comments with the original Xenos instructions to the
  // output.
  bool emit_source_map_;

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
    // Bool constants, front/back stencil, render target keep masks.
    kUint4Array2,
    // Loop constants.
    kUint4Array8,
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
  // ROV only - new depth/stencil data. 4 VGPRs when not writing to oDepth, 1
  // VGPR when writing to oDepth.
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
