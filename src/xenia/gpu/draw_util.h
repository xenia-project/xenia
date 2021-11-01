/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_DRAW_UTIL_H_
#define XENIA_GPU_DRAW_UTIL_H_

#include <cstdint>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {
namespace draw_util {

// For estimating coverage extents from vertices. This may give results that are
// different than what the host GPU will actually draw (this is the reference
// conversion with 1/2 ULP accuracy, but Direct3D 11 permits 0.6 ULP tolerance
// in floating point to fixed point conversion), but is enough to tie-break
// vertices at pixel centers (due to the half-pixel offset applied to integer
// coordinates incorrectly, for instance) with some error tolerance near 0.5,
// for use with the top-left rasterization rule later.
int32_t FloatToD3D11Fixed16p8(float f32);

// Polygonal primitive types (not including points and lines) are rasterized as
// triangles, have front and back faces, and also support face culling and fill
// modes (polymode_front_ptype, polymode_back_ptype). Other primitive types are
// always "front" (but don't support front face and back face culling, according
// to OpenGL and Vulkan specifications - even if glCullFace is
// GL_FRONT_AND_BACK, points and lines are still drawn), and may in some cases
// use the "para" registers instead of "front" or "back" (for "parallelogram" -
// like poly_offset_para_enable).
constexpr bool IsPrimitivePolygonal(bool vgt_output_path_is_tessellation_enable,
                                    xenos::PrimitiveType type) {
  if (vgt_output_path_is_tessellation_enable &&
      (type == xenos::PrimitiveType::kTrianglePatch ||
       type == xenos::PrimitiveType::kQuadPatch)) {
    // For patch primitive types, the major mode is always explicit, so just
    // checking if VGT_OUTPUT_PATH_CNTL::path_select is kTessellationEnable is
    // enough.
    return true;
  }
  switch (type) {
    case xenos::PrimitiveType::kTriangleList:
    case xenos::PrimitiveType::kTriangleFan:
    case xenos::PrimitiveType::kTriangleStrip:
    case xenos::PrimitiveType::kTriangleWithWFlags:
    case xenos::PrimitiveType::kQuadList:
    case xenos::PrimitiveType::kQuadStrip:
    case xenos::PrimitiveType::kPolygon:
      return true;
    default:
      break;
  }
  // TODO(Triang3l): Investigate how kRectangleList should be treated - possibly
  // actually drawn as two polygons on the console, however, the current
  // geometry shader doesn't care about the winding order - allowing backface
  // culling for rectangles currently breaks 4D53082D.
  return false;
}

inline bool IsPrimitivePolygonal(const RegisterFile& regs) {
  return IsPrimitivePolygonal(
      regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select ==
          xenos::VGTOutputPath::kTessellationEnable,
      regs.Get<reg::VGT_DRAW_INITIATOR>().prim_type);
}

// Whether with the current state, any samples to rasterize (for any reason, not
// only to write something to a render target, but also to do sample counting or
// pixel shader memexport) can be generated. Finally dropping draw calls can
// only be done if the vertex shader doesn't memexport. Checks mostly special
// cases (for both the guest and usual host implementations), not everything
// like whether viewport / scissor are empty (until this truly matters in any
// game, of course).
bool IsRasterizationPotentiallyDone(const RegisterFile& regs,
                                    bool primitive_polygonal);

// Direct3D 10.1+ standard sample positions, also used in Vulkan, for
// calculations related to host MSAA, in 1/16th of a pixel.
extern const int8_t kD3D10StandardSamplePositions2x[2][2];
extern const int8_t kD3D10StandardSamplePositions4x[4][2];

inline reg::RB_DEPTHCONTROL GetDepthControlForCurrentEdramMode(
    const RegisterFile& regs) {
  xenos::ModeControl edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode != xenos::ModeControl::kColorDepth &&
      edram_mode != xenos::ModeControl::kDepth) {
    // Both depth and stencil disabled (EDRAM depth and stencil ignored).
    reg::RB_DEPTHCONTROL disabled;
    disabled.value = 0;
    return disabled;
  }
  return regs.Get<reg::RB_DEPTHCONTROL>();
}

constexpr float GetD3D10PolygonOffsetFactor(
    xenos::DepthRenderTargetFormat depth_format, bool float24_as_0_to_0_5) {
  if (depth_format == xenos::DepthRenderTargetFormat::kD24S8) {
    return float(1 << 24);
  }
  // 20 explicit + 1 implicit (1.) mantissa bits.
  // 2^20 is not enough for 415607E6 retail version's training mission shooting
  // range floor (with the number 1) on Direct3D 12. Tested on Nvidia GeForce
  // GTX 1070, the exact formula (taking into account the 0...1 to 0...0.5
  // remapping described below) used for testing is
  // `int(ceil(offset * 2^20 * 0.5)) * sign(offset)`. With 2^20 * 0.5, there
  // are various kinds of stripes dependending on the view angle in that
  // location. With 2^21 * 0.5, the issue is not present.
  constexpr float kFloat24Scale = float(1 << 21);
  // 0...0.5 range may be used on the host to represent the 0...1 guest depth
  // range to be able to copy all possible encodings, which are [0, 2), via a
  // [0, 1] depth output variable, during EDRAM contents reinterpretation.
  // This is done by scaling the viewport depth bounds by 0.5. However, the
  // depth bias is applied after the viewport. This adjustment is only needed
  // for the constant bias - for slope-scaled, the derivatives of Z are
  // calculated after the viewport as well, and will already include the 0.5
  // scaling from the viewport.
  return float24_as_0_to_0_5 ? kFloat24Scale * 0.5f : kFloat24Scale;
}

inline bool DoesCoverageDependOnAlpha(reg::RB_COLORCONTROL rb_colorcontrol) {
  return (rb_colorcontrol.alpha_test_enable &&
          rb_colorcontrol.alpha_func != xenos::CompareFunction::kAlways) ||
         rb_colorcontrol.alpha_to_mask_enable;
}

// Whether the pixel shader can be disabled on the host to speed up depth
// pre-passes and shadowmaps. The shader must have its ucode analyzed. If
// IsRasterizationPotentiallyDone, this shouldn't be called, and assumed false
// instead. Helps reject the pixel shader in some cases - memexport draws in
// 4D5307E6, and also most of some 1-point draws not covering anything done for
// some reason in different games with a leftover pixel shader from the previous
// draw, but with SQ_PROGRAM_CNTL destroyed, reducing the number of
// unpredictable unneeded translations of random shaders with different host
// modification bits, such as register count and depth format-related (though
// shaders with side effects on depth or memory export will still be preserved).
bool IsPixelShaderNeededWithRasterization(const Shader& shader,
                                          const RegisterFile& regs);

struct ViewportInfo {
  // Offset from render target UV = 0 to +UV.
  // For simplicity of cropping to the maximum size on the host; to match the
  // Direct3D 12 clipping / scissoring behavior with a fractional viewport, to
  // floor(TopLeftXY) ... floor(TopLeftXY + WidthHeight), on the real AMD, Intel
  // and Nvidia hardware (not WARP); as well as to hide the differences between
  // 0 and 8+ viewportSubPixelBits on Vulkan, and to prevent any numerical error
  // in bound checking in host APIs, viewport bounds are returned as integers.
  // Also they're returned as non-negative, also to make it easier to crop (so
  // Vulkan maxViewportDimensions and viewportBoundsRange don't have to be
  // handled separately - maxViewportDimensions is greater than or equal to the
  // largest framebuffer image size, so it's safe, and viewportBoundsRange is
  // always bigger than maxViewportDimensions. All fractional offsetting,
  // including the half-pixel offset, and cropping are handled via ndc_scale and
  // ndc_offset.
  uint32_t xy_offset[2];
  // Extent can be zero for an empty viewport - host APIs not supporting empty
  // viewports need to use an empty scissor rectangle.
  uint32_t xy_extent[2];
  float z_min;
  float z_max;
  // The scale is applied before the offset (like using multiply-add).
  float ndc_scale[3];
  float ndc_offset[3];
};
// Converts the guest viewport (or fakes one if drawing without a viewport) to
// a viewport, plus values to multiply-add the returned position by, usable on
// host graphics APIs such as Direct3D 11+ and Vulkan, also forcing it to the
// Direct3D clip space with 0...W Z rather than -W...W.
void GetHostViewportInfo(const RegisterFile& regs, uint32_t resolution_scale,
                         bool origin_bottom_left, uint32_t x_max,
                         uint32_t y_max, bool allow_reverse_z,
                         bool convert_z_to_float24, bool full_float24_in_0_to_1,
                         bool pixel_shader_writes_depth,
                         ViewportInfo& viewport_info_out);

struct Scissor {
  // Offset from render target UV = 0 to +UV.
  uint32_t offset[2];
  // Extent can be zero.
  uint32_t extent[2];
};
void GetScissor(const RegisterFile& regs, Scissor& scissor_out,
                bool clamp_to_surface_pitch = true);

// Scales, and shift amounts of the upper 32 bits of the 32x32=64-bit
// multiplication result, for fast division and multiplication by
// EDRAM-tile-related amounts.
constexpr uint32_t kDivideScale3 = 0xAAAAAAABu;
constexpr uint32_t kDivideUpperShift3 = 1;
constexpr uint32_t kDivideScale5 = 0xCCCCCCCDu;
constexpr uint32_t kDivideUpperShift5 = 2;
constexpr uint32_t kDivideScale15 = 0x88888889u;
constexpr uint32_t kDivideUpperShift15 = 3;

// To avoid passing values that the shader won't understand (even though
// Direct3D 9 shouldn't pass them anyway).
xenos::CopySampleSelect SanitizeCopySampleSelect(
    xenos::CopySampleSelect copy_sample_select, xenos::MsaaSamples msaa_samples,
    bool is_depth);

// Packed structures are small and can be passed to the shaders in root/push
// constants.

union ResolveEdramPackedInfo {
  uint32_t packed;
  struct {
    // With 32bpp/64bpp taken into account.
    uint32_t pitch_tiles : xenos::kEdramPitchTilesBits;
    xenos::MsaaSamples msaa_samples : xenos::kMsaaSamplesBits;
    uint32_t is_depth : 1;
    // With offset to the 160x32 region that local_x/y_div_8 are relative to.
    uint32_t base_tiles : xenos::kEdramBaseTilesBits;
    uint32_t format : xenos::kRenderTargetFormatBits;
    uint32_t format_is_64bpp : 1;
    // Whether to take the value of column/row 1 for column/row 0, to reduce
    // the impact of the half-pixel offset with resolution scaling.
    uint32_t duplicate_second_pixel : 1;
  };
  ResolveEdramPackedInfo() : packed(0) {
    static_assert_size(*this, sizeof(packed));
  }
};
static_assert(sizeof(ResolveEdramPackedInfo) <= sizeof(uint32_t),
              "ResolveEdramPackedInfo must be packable in uint32_t");

union ResolveAddressPackedInfo {
  uint32_t packed;
  struct {
    // 160x32 is divisible by both the EDRAM tile size (80x16 samples, but for
    // simplicity, this is in pixels) and the texture tile size (32x32), so
    // the X and Y offsets can be packed in a very small number of bits (also
    // taking 8x8 granularity into account) if the offset of the 160x32 region
    // itself, and the offset of the texture tile, are pre-added to the bases.

    // In the EDRAM source, the whole offset is relative to the base.
    // In the texture, & 31 of the offset is relative to the base (the base is
    // adjusted to 32x32 tiles).

    // 0...19 for 0...152.
    uint32_t local_x_div_8 : 5;
    // 0...3 for 0...24.
    uint32_t local_y_div_8 : 2;
    // May be zero if the original rectangle was somehow specified in a
    // totally broken way - in this case, the resolve must be dropped.
    uint32_t width_div_8 : xenos::kResolveSizeBits -
                           xenos::kResolveAlignmentPixelsLog2;
    uint32_t height_div_8 : xenos::kResolveSizeBits -
                            xenos::kResolveAlignmentPixelsLog2;

    xenos::CopySampleSelect copy_sample_select : 3;
  };
  ResolveAddressPackedInfo() : packed(0) {
    static_assert_size(*this, sizeof(packed));
  }
};
static_assert(sizeof(ResolveAddressPackedInfo) <= sizeof(uint32_t),
              "ResolveAddressPackedInfo must be packable in uint32_t");

// Returns tiles actually covered by a resolve area. Row length used is width of
// the area in tiles, but the pitch between rows is edram_info.pitch_tiles.
void GetResolveEdramTileSpan(ResolveEdramPackedInfo edram_info,
                             ResolveAddressPackedInfo address_info,
                             uint32_t& base_out, uint32_t& row_length_used_out,
                             uint32_t& rows_out);

union ResolveCopyDestPitchPackedInfo {
  uint32_t packed;
  struct {
    // 0...16384/32.
    uint32_t pitch_aligned_div_32 : xenos::kTexture2DCubeMaxWidthHeightLog2 +
                                    2 - xenos::kTextureTileWidthHeightLog2;
    uint32_t height_aligned_div_32 : xenos::kTexture2DCubeMaxWidthHeightLog2 +
                                     2 - xenos::kTextureTileWidthHeightLog2;
  };
  ResolveCopyDestPitchPackedInfo() : packed(0) {
    static_assert_size(*this, sizeof(packed));
  }
};

// For backends with Shader Model 5-like compute, host shaders to use to perform
// copying in resolve operations.
enum class ResolveCopyShaderIndex {
  kFast32bpp1x2xMSAA,
  kFast32bpp4xMSAA,
  kFast32bpp2xRes,
  kFast32bpp3xRes1x2xMSAA,
  kFast32bpp3xRes4xMSAA,
  kFast64bpp1x2xMSAA,
  kFast64bpp4xMSAA,
  kFast64bpp2xRes,
  kFast64bpp3xRes,

  kFull8bpp,
  kFull8bpp2xRes,
  kFull8bpp3xRes,
  kFull16bpp,
  kFull16bpp2xRes,
  kFull16bppFrom32bpp3xRes,
  kFull16bppFrom64bpp3xRes,
  kFull32bpp,
  kFull32bpp2xRes,
  kFull32bppFrom32bpp3xRes,
  kFull32bppFrom64bpp3xRes,
  kFull64bpp,
  kFull64bpp2xRes,
  kFull64bppFrom32bpp3xRes,
  kFull64bppFrom64bpp3xRes,
  kFull128bpp,
  kFull128bpp2xRes,
  kFull128bppFrom32bpp3xRes,
  kFull128bppFrom64bpp3xRes,

  kCount,
  kUnknown = kCount,
};

struct ResolveCopyShaderInfo {
  // Debug name of the pipeline state object with this shader.
  const char* debug_name;
  // Only need to load this shader if the emulator resolution scale == this
  // value.
  uint32_t resolution_scale;
  // Whether the EDRAM source needs be bound as a raw buffer (ByteAddressBuffer
  // in Direct3D) since it can load different numbers of 32-bit values at once
  // on some hardware. If the host API doesn't support raw buffers, a typed
  // buffer with source_bpe_log2-byte elements needs to be used instead.
  bool source_is_raw;
  // Log2 of bytes per element of the type of the EDRAM buffer bound to the
  // shader (at least 2).
  uint32_t source_bpe_log2;
  // Log2 of bytes per element of the type of the destination buffer bound to
  // the shader (at least 2 because of Nvidia's 128 megatexel limit that
  // prevents binding the entire shared memory buffer with smaller element
  // sizes).
  uint32_t dest_bpe_log2;
  // Log2 of number of pixels in a single thread group along X and Y. 64 threads
  // per group preferred (GCN lane count).
  uint32_t group_size_x_log2, group_size_y_log2;
};

extern const ResolveCopyShaderInfo
    resolve_copy_shader_info[size_t(ResolveCopyShaderIndex::kCount)];

struct ResolveCopyShaderConstants {
  // When the destination base is not needed (not binding the entire shared
  // memory buffer - with resoluion scaling, for instance), only the
  // DestRelative part may be passed to the shader to use less constants.
  struct DestRelative {
    ResolveEdramPackedInfo edram_info;
    ResolveAddressPackedInfo address_info;
    reg::RB_COPY_DEST_INFO dest_info;
    ResolveCopyDestPitchPackedInfo dest_pitch_aligned;
  };
  DestRelative dest_relative;
  uint32_t dest_base;
};

struct ResolveClearShaderConstants {
  // rt_specific is different for color and depth, the rest is the same and can
  // be preserved in the root bindings when going from depth to color.
  struct RenderTargetSpecific {
    uint32_t clear_value[2];
    ResolveEdramPackedInfo edram_info;
  };
  RenderTargetSpecific rt_specific;
  ResolveAddressPackedInfo address_info;
};

struct ResolveInfo {
  reg::RB_COPY_CONTROL rb_copy_control;

  // depth_edram_info / depth_original_base and color_edram_info /
  // color_original_base are set up if copying or clearing color and depth
  // respectively, according to RB_COPY_CONTROL.
  ResolveEdramPackedInfo depth_edram_info;
  ResolveEdramPackedInfo color_edram_info;
  // Original bases, without adjustment to a 160x32 region for packed offsets,
  // for locating host render targets to perform clears if host render targets
  // are used for EDRAM emulation - the same as the base that the render target
  // will likely used for drawing next, to prevent unneeded tile ownership
  // transfers between clears and first usage if clearing a subregion.
  uint32_t depth_original_base;
  uint32_t color_original_base;

  ResolveAddressPackedInfo address;

  reg::RB_COPY_DEST_INFO copy_dest_info;
  ResolveCopyDestPitchPackedInfo copy_dest_pitch_aligned;

  // Memory range that will potentially be modified by copying, with
  // address.local_x/y_div_8 & 31 being the origin relative to it.
  uint32_t copy_dest_base;
  // May be zero if something is wrong with the destination, in this case,
  // clearing may still be done, but copying must be dropped.
  uint32_t copy_dest_length;

  // The clear shaders always write to a uint4 view of EDRAM.
  uint32_t rb_depth_clear;
  uint32_t rb_color_clear;
  uint32_t rb_color_clear_lo;

  bool IsCopyingDepth() const {
    return rb_copy_control.copy_src_select >= xenos::kMaxColorRenderTargets;
  }

  // See GetResolveEdramTileSpan documentation for explanation.
  void GetCopyEdramTileSpan(uint32_t& base_out, uint32_t& row_length_used_out,
                            uint32_t& rows_out, uint32_t& pitch_out) const {
    ResolveEdramPackedInfo edram_info =
        IsCopyingDepth() ? depth_edram_info : color_edram_info;
    GetResolveEdramTileSpan(edram_info, address, base_out, row_length_used_out,
                            rows_out);
    pitch_out = edram_info.pitch_tiles;
  }

  ResolveCopyShaderIndex GetCopyShader(
      uint32_t resolution_scale, ResolveCopyShaderConstants& constants_out,
      uint32_t& group_count_x_out, uint32_t& group_count_y_out) const;

  bool IsClearingDepth() const {
    return rb_copy_control.depth_clear_enable != 0;
  }

  bool IsClearingColor() const {
    return !IsCopyingDepth() && rb_copy_control.color_clear_enable != 0;
  }

  void GetDepthClearShaderConstants(
      ResolveClearShaderConstants& constants_out) const {
    assert_true(IsClearingDepth());
    constants_out.rt_specific.clear_value[0] = rb_depth_clear;
    constants_out.rt_specific.clear_value[1] = rb_depth_clear;
    constants_out.rt_specific.edram_info = depth_edram_info;
    constants_out.address_info = address;
  }

  void GetColorClearShaderConstants(
      ResolveClearShaderConstants& constants_out) const {
    assert_true(IsClearingColor());
    // Not doing -32...32 to -1...1 clamping here as a hack for k_16_16 and
    // k_16_16_16_16 blending emulation when using host render targets as it
    // would be inconsistent with the usual way of clearing with a depth quad.
    // TODO(Triang3l): Check which 32-bit portion is in which register.
    constants_out.rt_specific.clear_value[0] = rb_color_clear;
    constants_out.rt_specific.clear_value[1] = rb_color_clear_lo;
    constants_out.rt_specific.edram_info = color_edram_info;
    constants_out.address_info = address;
  }

  std::pair<uint32_t, uint32_t> GetClearShaderGroupCount() const {
    // 8 guest MSAA samples per invocation.
    uint32_t width_samples_div_8 = address.width_div_8;
    uint32_t height_samples_div_8 = address.height_div_8;
    xenos::MsaaSamples samples = IsCopyingDepth()
                                     ? depth_edram_info.msaa_samples
                                     : color_edram_info.msaa_samples;
    if (samples >= xenos::MsaaSamples::k2X) {
      height_samples_div_8 <<= 1;
      if (samples >= xenos::MsaaSamples::k4X) {
        width_samples_div_8 <<= 1;
      }
    }
    return std::make_pair((width_samples_div_8 + uint32_t(7)) >> 3,
                          height_samples_div_8);
  }
};

// Returns false if there was an error obtaining the info making it totally
// invalid. fixed_16_truncated_to_minus_1_to_1 is false if 16_16 and 16_16_16_16
// color render target formats are properly emulated as -32...32, true if
// emulated as snorm, with range limited to -1...1, but with correct blending
// within that range.
bool GetResolveInfo(const RegisterFile& regs, const Memory& memory,
                    TraceWriter& trace_writer, uint32_t resolution_scale,
                    bool fixed_16_truncated_to_minus_1_to_1,
                    ResolveInfo& info_out);

// Taking user configuration - stretching or letterboxing, overscan region to
// crop to fill while maintaining the aspect ratio - into account, returns the
// area where the frame should be presented in the host window.
void GetPresentArea(uint32_t source_width, uint32_t source_height,
                    uint32_t window_width, uint32_t window_height,
                    int32_t& target_x_out, int32_t& target_y_out,
                    uint32_t& target_width_out, uint32_t& target_height_out);

}  // namespace draw_util
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DRAW_UTIL_H_
