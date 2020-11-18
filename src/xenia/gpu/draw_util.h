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

struct ViewportInfo {
  // The returned viewport will always be in the positive quarter-plane for
  // simplicity of clamping to the maximum size supported by the host, negative
  // offset will be applied via ndc_offset.
  float left;
  float top;
  float width;
  float height;
  float z_min;
  float z_max;
  float ndc_scale[3];
  float ndc_offset[3];
};
// Converts the guest viewport (or fakes one if drawing without a viewport) to
// a viewport, plus values to multiply-add the returned position by, usable on
// host graphics APIs such as Direct3D 11+ and Vulkan, also forcing it to the
// Direct3D clip space with 0...W Z rather than -W...W.
void GetHostViewportInfo(const RegisterFile& regs, float pixel_size_x,
                         float pixel_size_y, bool origin_bottom_left,
                         float x_max, float y_max, bool allow_reverse_z,
                         ViewportInfo& viewport_info_out);

struct Scissor {
  uint32_t left;
  uint32_t top;
  uint32_t width;
  uint32_t height;
};
void GetScissor(const RegisterFile& regs, Scissor& scissor_out);

// To avoid passing values that the shader won't understand (even though
// Direct3D 9 shouldn't pass them anyway).
xenos::CopySampleSelect SanitizeCopySampleSelect(
    xenos::CopySampleSelect copy_sample_select, xenos::MsaaSamples msaa_samples,
    bool is_depth);

// Packed structures are small and can be passed to the shaders in root/push
// constants.

union ResolveEdramPackedInfo {
  struct {
    // With offset to the 160x32 region that local_x/y_div_8 are relative to,
    // and with 32bpp/64bpp taken into account.
    uint32_t pitch_tiles : xenos::kEdramPitchTilesBits;
    xenos::MsaaSamples msaa_samples : xenos::kMsaaSamplesBits;
    uint32_t is_depth : 1;
    uint32_t base_tiles : xenos::kEdramBaseTilesBits;
    uint32_t format : xenos::kRenderTargetFormatBits;
    uint32_t format_is_64bpp : 1;
    // Whether to take the value of column/row 1 for column/row 0, to reduce
    // the impact of the half-pixel offset with resolution scaling.
    uint32_t duplicate_second_pixel : 1;
  };
  uint32_t packed;
};
static_assert(sizeof(ResolveEdramPackedInfo) <= sizeof(uint32_t),
              "ResolveEdramPackedInfo must be packable in uint32_t");

union ResolveAddressPackedInfo {
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
  uint32_t packed;
};
static_assert(sizeof(ResolveAddressPackedInfo) <= sizeof(uint32_t),
              "ResolveAddressPackedInfo must be packable in uint32_t");

// For backends with Shader Model 5-like compute, host shaders to use to perform
// copying in resolve operations.
enum class ResolveCopyShaderIndex {
  kFast32bpp1x2xMSAA,
  kFast32bpp4xMSAA,
  kFast32bpp2xRes,
  kFast64bpp1x2xMSAA,
  kFast64bpp4xMSAA,
  kFast64bpp2xRes,

  kFull8bpp,
  kFull8bpp2xRes,
  kFull16bpp,
  kFull16bpp2xRes,
  kFull32bpp,
  kFull32bpp2xRes,
  kFull64bpp,
  kFull64bpp2xRes,
  kFull128bpp,
  kFull128bpp2xRes,

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
    reg::RB_COPY_DEST_PITCH dest_pitch;
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

  // color_edram_info and depth_edram_info are set up if copying or clearing
  // color and depth respectively, according to RB_COPY_CONTROL.
  ResolveEdramPackedInfo color_edram_info;
  ResolveEdramPackedInfo depth_edram_info;

  ResolveAddressPackedInfo address;

  reg::RB_COPY_DEST_INFO rb_copy_dest_info;
  reg::RB_COPY_DEST_PITCH rb_copy_dest_pitch;

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
      bool has_float32_copy, ResolveClearShaderConstants& constants_out) const {
    assert_true(IsClearingDepth());
    constants_out.rt_specific.clear_value[0] = rb_depth_clear;
    if (has_float32_copy) {
      float depth32;
      uint32_t depth24 = rb_depth_clear >> 8;
      if (xenos::DepthRenderTargetFormat(depth_edram_info.format) ==
          xenos::DepthRenderTargetFormat::kD24S8) {
        depth32 = depth24 * float(1.0f / 16777215.0f);
      } else {
        depth32 = xenos::Float20e4To32(depth24);
      }
      constants_out.rt_specific.clear_value[1] =
          *reinterpret_cast<const uint32_t*>(&depth32);
    } else {
      constants_out.rt_specific.clear_value[1] = rb_depth_clear;
    }
    constants_out.rt_specific.edram_info = depth_edram_info;
    constants_out.address_info = address;
  }

  void GetColorClearShaderConstants(
      ResolveClearShaderConstants& constants_out) const {
    assert_true(IsClearingColor());
    // Not doing -32...32 to -1...1 clamping here as a hack for k_16_16 and
    // k_16_16_16_16 blending emulation when using traditional host render
    // targets as it would be inconsistent with the usual way of clearing with a
    // quad.
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
// invalid. edram_16_as_minus_1_to_1 is false if 16_16 and 16_16_16_16 color
// render target formats are properly emulated as -32...32, true if emulated as
// snorm, with range limited to -1...1, but with correct blending within that
// range.
bool GetResolveInfo(const RegisterFile& regs, const Memory& memory,
                    TraceWriter& trace_writer, uint32_t resolution_scale,
                    bool edram_16_as_minus_1_to_1, ResolveInfo& info_out);

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
