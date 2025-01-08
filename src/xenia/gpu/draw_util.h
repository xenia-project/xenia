/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_DRAW_UTIL_H_
#define XENIA_GPU_DRAW_UTIL_H_

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {
namespace draw_util {

constexpr bool IsPrimitiveLine(bool vgt_output_path_is_tessellation_enable,
                               xenos::PrimitiveType type) {
  if (vgt_output_path_is_tessellation_enable &&
      type == xenos::PrimitiveType::kLinePatch) {
    // For patch primitive types, the major mode is always explicit, so just
    // checking if VGT_OUTPUT_PATH_CNTL::path_select is kTessellationEnable is
    // enough.
    return true;
  }
  switch (type) {
    case xenos::PrimitiveType::kLineList:
    case xenos::PrimitiveType::kLineStrip:
    case xenos::PrimitiveType::kLineLoop:
    case xenos::PrimitiveType::k2DLineStrip:
      return true;
    default:
      break;
  }
  return false;
}

inline bool IsPrimitiveLine(const RegisterFile& regs) {
  return IsPrimitiveLine(regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select ==
                             xenos::VGTOutputPath::kTessellationEnable,
                         regs.Get<reg::VGT_DRAW_INITIATOR>().prim_type);
}

constexpr uint32_t EncodeIsPrimitivePolygonalTable() {
  unsigned result = 0;
#define TRUEFOR(x) \
  result |= 1U << static_cast<uint32_t>(xenos::PrimitiveType::x)

  TRUEFOR(kTriangleList);
  TRUEFOR(kTriangleFan);
  TRUEFOR(kTriangleStrip);
  TRUEFOR(kTriangleWithWFlags);
  TRUEFOR(kQuadList);
  TRUEFOR(kQuadStrip);
  TRUEFOR(kPolygon);
#undef TRUEFOR
  // TODO(Triang3l): Investigate how kRectangleList should be treated - possibly
  // actually drawn as two polygons on the console, however, the current
  // geometry shader doesn't care about the winding order - allowing backface
  // culling for rectangles currently breaks 4D53082D.
  return result;
}

// Polygonal primitive types (not including points and lines) are rasterized as
// triangles, have front and back faces, and also support face culling and fill
// modes (polymode_front_ptype, polymode_back_ptype). Other primitive types are
// always "front" (but don't support front face and back face culling, according
// to OpenGL and Vulkan specifications - even if glCullFace is
// GL_FRONT_AND_BACK, points and lines are still drawn), and may in some cases
// use the "para" registers instead of "front" or "back" (for "parallelogram" -
// like poly_offset_para_enable).
XE_FORCEINLINE
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
  // chrispy: expensive jumptable, use bit table instead

  constexpr uint32_t primitive_polygonal_table =
      EncodeIsPrimitivePolygonalTable();

  return (primitive_polygonal_table & (1U << static_cast<uint32_t>(type))) != 0;
}
XE_FORCEINLINE
static bool IsPrimitivePolygonal(const RegisterFile& regs) {
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

reg::RB_DEPTHCONTROL GetNormalizedDepthControl(const RegisterFile& regs);

// Direct3D 9 and Xenos constant polygon offset is an absolute floating-point
// value.
// It's possibly treated just as an absolute offset by the Xenos too - the
// PA_SU_POLY_OFFSET_DB_FMT_CNTL::POLY_OFFSET_DB_IS_FLOAT_FMT switch was added
// later in the R6xx, though this needs verification.
// 5454082B, for example, for float24, sets the bias to 0.000002 - or slightly
// above 2^-19.
// The total polygon offset formula specified by Direct3D 9 is:
// `offset = slope * slope factor + constant offset`
//
// Direct3D 10, Metal, OpenGL and Vulkan, however, take the constant polygon
// offset factor as a relative value, with the formula being:
// `offset = slope * slope factor +
//           maximum resolvable difference * constant factor`
// where the maximum resolvable difference is:
// - For a fixed-point depth buffer, the minimum representable non-zero value in
//   the depth buffer, that is 1 / (2^24 - 1) for unorm24 (on Vulkan though it's
//   allowed to be up to 2 / 2^24 in this case).
// - For a floating-point depth buffer, it's:
//   2 ^ (exponent of the maximum Z in the primitive - the number of explicitly
//        stored mantissa bits)
//   (23 explicitly stored bits for float32 - and 20 explicitly stored bits for
//    float24).
//
// While the polygon offset is a fixed-function feature in the pipeline, and the
// formula can't be toggled between absolute and relative, it's important that
// Xenia translates the guest absolute depth bias into the host relative depth
// bias in a way that the values that separate coplanar geometry on the guest
// qualitatively also still correctly separate them on the host.
//
// It also should be taken into account that on Xenia, float32 depth values may
// be snapped to float24 directly in the translated pixel shaders (to prevent
// data loss if after reuploading a depth buffer to the EDRAM there's no way to
// recover the full-precision value, that results in the inability to perform
// more rendering passes for the same geometry), and not only to the nearest
// value, but also just truncating them (so in case of data loss, the "greater
// or equal" depth test function still works).
//
// Because of this, the depth bias may be lost if Xenia translates it into a too
// small value, and the conversion of the depth is done in the pixel shader.
// Specifically, Xenia should not simply convert a value that separates coplanar
// primitives as float24 just into something that still separates them as
// float32. Essentially, if conversion to float24 is done in the pixel shader,
// Xenia should make sure the polygon offset on the host is calculated as if the
// host had float24 depth too, not float32.

// Applies to both native host unorm24, and unorm24 emulated as host float32.
// For native unorm24, this is exactly the inverse of the minimum representable
// non-zero value.
// For unorm24 emulated as float32, the minimum representable non-zero value for
// a primitive in the [0.5, 1) range (the worst case that forward depth reaches
// very quickly, at nearly `2 * near clipping plane distance`) is 2 ^ (-1 - 23),
// or 2^-24, and this factor is almost 2^24.
constexpr float kD3D10PolygonOffsetFactorUnorm24 =
    float((UINT32_C(1) << 24) - 1);

// For a host floating-point depth buffer, the integer value of the depth bias
// is roughly how many ULPs primitives should be separated by.
//
// Float24, however, has 3 mantissa bits fewer than float32 - so one float24 ULP
// corresponds to 8 float32 ULPs - which means each conceptual "layer" of the
// guest value should correspond to a polygon offset of 8. So, after the guest
// absolute value is converted to "layers", it should be multiplied by 8 before
// being used on the host with a float32 depth buffer.
//
// The scale for converting the guest absolute depth bias to the "layers" needs
// to be determined for the worst case - specifically, the [0.5, 1) range (1 is
// a single value, so there's no need to take it into consideration). In this
// range, Z values have the exponent of -1. Therefore, for float24, the absolute
// offset is obtained from the "layer index" in this range as (disregarding the
// slope term):
// offset = 2 ^ (-1 - 20) * constant factor
// Thus, to obtain the constant factor from the absolute offset in the range
// with the lowest absolute precision, the offset needs to be multiplied by
// 2^21.
//
// Finally, the 0...0.5 range may be used on the host to represent the 0...1
// guest depth range to be able to copy all possible encodings, which are
// [0, 2), via a [0, 1] depth output variable, during EDRAM contents
// reinterpretation. This is done by scaling the viewport depth bounds by 0.5.
// However, there's no need to do anything to handle this scenario in the
// polygon offset - it's calculated after applying the viewport transformation,
// and the maximum Z value in the primitive will have an exponent lowered by 1,
// thus the result will also have an exponent lowered by 1 - exactly what's
// needed for remapping 0...1 to 0...0.5.
constexpr float kD3D10PolygonOffsetFactorFloat24 =
    float(UINT32_C(1) << (21 + 3));

inline int32_t GetD3D10IntegerPolygonOffset(
    xenos::DepthRenderTargetFormat depth_format, float polygon_offset) {
  bool is_float24 = depth_format == xenos::DepthRenderTargetFormat::kD24FS8;
  // Using `ceil` because more offset is better, especially if flooring would
  // result in 0 - conceptually, if the offset is used at all, primitives need
  // to be separated in the depth buffer.
  int32_t polygon_offset_int = int32_t(
      std::ceil(std::abs(polygon_offset) *
                (is_float24 ? kD3D10PolygonOffsetFactorFloat24 * (1.0f / 8.0f)
                            : kD3D10PolygonOffsetFactorUnorm24)));
  // For float24, the conversion may be done in the translated pixel shaders,
  // including via truncation rather than rounding to the nearest. So, making
  // the integer bias always in the increments of 2^3 (2 ^ the difference in the
  // mantissa bit count between float32 and float24), and because of that, doing
  // `ceil` before changing the units from float24 ULPs to float32 ULPs.
  if (is_float24) {
    polygon_offset_int <<= 3;
  }
  return polygon_offset < 0 ? -polygon_offset_int : polygon_offset_int;
}

// For hosts not supporting separate front and back polygon offsets, returns the
// polygon offset for the face which likely needs the offset the most (and that
// will not be culled). The values returned will have the units of the original
// registers (the scale is for 1/16 subpixels, multiply by
// xenos::kPolygonOffsetScaleSubpixelUnit outside if the value for pixels is
// needed).
void GetPreferredFacePolygonOffset(const RegisterFile& regs,
                                   bool primitive_polygonal, float& scale_out,
                                   float& offset_out);

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
static_assert(sizeof(xenos::DepthRenderTargetFormat) == sizeof(uint32_t),
              "Change in depthrendertargetformat throws off "
              "getviewportinfoargs by a bit");
struct GetViewportInfoArgs {
  union alignas(64) {
    struct {
      // group 1
      uint32_t x_max;
      uint32_t y_max;
      union {
        struct {
          uint32_t origin_bottom_left : 1;
          uint32_t allow_reverse_z : 1;
          uint32_t convert_z_to_float24 : 1;
          uint32_t full_float24_in_0_to_1 : 1;
          uint32_t pixel_shader_writes_depth : 1;
          xenos::DepthRenderTargetFormat depth_format : 1;
        };
        uint32_t packed_portions;
      };
      reg::RB_DEPTHCONTROL normalized_depth_control;
      // group 2
      reg::PA_CL_CLIP_CNTL pa_cl_clip_cntl;
      reg::PA_CL_VTE_CNTL pa_cl_vte_cntl;
      reg::PA_SU_SC_MODE_CNTL pa_su_sc_mode_cntl;
      reg::PA_SU_VTX_CNTL pa_su_vtx_cntl;
      // group 3
      reg::PA_SC_WINDOW_OFFSET pa_sc_window_offset;
      float PA_CL_VPORT_XSCALE;
      float PA_CL_VPORT_YSCALE;
      float PA_CL_VPORT_ZSCALE;

      float PA_CL_VPORT_XOFFSET;
      float PA_CL_VPORT_YOFFSET;
      float PA_CL_VPORT_ZOFFSET;
      uint32_t padding_set_to_0;
    };
#if XE_ARCH_AMD64 == 1
    struct {
      __m128i first4;   // x_max, y_max, packed_portions,
                        // normalized_depth_control
      __m128i second4;  // pa_cl_clip_cntl, pa_cl_vte_cntl, pa_su_sc_mode_cntl,
                        // pa_su_vtx_cntl
      __m128i third4;   // pa_sc_window_offset, PA_CL_VPORT_XSCALE,
                        // PA_CL_VPORT_YSCALE, PA_CL_VPORT_ZSCALE
      __m128i last4;    // PA_CL_VPORT_XOFFSET, PA_CL_VPORT_YOFFSET,
                        // PA_CL_VPORT_ZOFFSET, padding_set_to_0
    };
#endif
  };

  // everything that follows here does not need to be compared
  uint32_t draw_resolution_scale_x;
  uint32_t draw_resolution_scale_y;
  divisors::MagicDiv draw_resolution_scale_x_divisor;
  divisors::MagicDiv draw_resolution_scale_y_divisor;
  void Setup(uint32_t _draw_resolution_scale_x,
             uint32_t _draw_resolution_scale_y,
             divisors::MagicDiv _draw_resolution_scale_x_divisor,
             divisors::MagicDiv _draw_resolution_scale_y_divisor,
             bool _origin_bottom_left, uint32_t _x_max, uint32_t _y_max,
             bool _allow_reverse_z,
             reg::RB_DEPTHCONTROL _normalized_depth_control,
             bool _convert_z_to_float24, bool _full_float24_in_0_to_1,
             bool _pixel_shader_writes_depth) {
    packed_portions = 0;
    padding_set_to_0 = 0;  // important to zero this
    draw_resolution_scale_x = _draw_resolution_scale_x;
    draw_resolution_scale_y = _draw_resolution_scale_y;
    draw_resolution_scale_x_divisor = _draw_resolution_scale_x_divisor;
    draw_resolution_scale_y_divisor = _draw_resolution_scale_y_divisor;
    origin_bottom_left = _origin_bottom_left;
    x_max = _x_max;
    y_max = _y_max;
    allow_reverse_z = _allow_reverse_z;
    normalized_depth_control = _normalized_depth_control;
    convert_z_to_float24 = _convert_z_to_float24;
    full_float24_in_0_to_1 = _full_float24_in_0_to_1;
    pixel_shader_writes_depth = _pixel_shader_writes_depth;
  }

  void SetupRegisterValues(const RegisterFile& regs) {
    pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
    pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
    pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    pa_su_vtx_cntl = regs.Get<reg::PA_SU_VTX_CNTL>();
    PA_CL_VPORT_XSCALE = regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE);
    PA_CL_VPORT_YSCALE = regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE);
    PA_CL_VPORT_ZSCALE = regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_ZSCALE);
    PA_CL_VPORT_XOFFSET = regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET);
    PA_CL_VPORT_YOFFSET = regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET);
    PA_CL_VPORT_ZOFFSET = regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
    pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
    depth_format = regs.Get<reg::RB_DEPTH_INFO>().depth_format;
  }
  XE_FORCEINLINE
  bool operator==(const GetViewportInfoArgs& prev) const {
#if XE_ARCH_AMD64 == 0
    bool result = true;

    auto accum_eq = [&result](auto x, auto y) { result &= (x == y); };

#define EQC(field) accum_eq(field, prev.field)
    EQC(x_max);
    EQC(y_max);
    EQC(packed_portions);
    EQC(normalized_depth_control.value);
    EQC(pa_cl_clip_cntl.value);
    EQC(pa_cl_vte_cntl.value);

    EQC(pa_su_sc_mode_cntl.value);
    EQC(pa_su_vtx_cntl.value);
    EQC(PA_CL_VPORT_XSCALE);
    EQC(PA_CL_VPORT_YSCALE);
    EQC(PA_CL_VPORT_ZSCALE);
    EQC(PA_CL_VPORT_XOFFSET);
    EQC(PA_CL_VPORT_YOFFSET);
    EQC(PA_CL_VPORT_ZOFFSET);
    EQC(pa_sc_window_offset.value);

#undef EQC
    return result;
#else
    __m128i mask1 = _mm_cmpeq_epi32(first4, prev.first4);
    __m128i mask2 = _mm_cmpeq_epi32(second4, prev.second4);

    __m128i mask3 = _mm_cmpeq_epi32(third4, prev.third4);
    __m128i unified1 = _mm_and_si128(mask1, mask2);
    __m128i mask4 = _mm_cmpeq_epi32(last4, prev.last4);

    __m128i unified2 = _mm_and_si128(unified1, mask3);

    __m128i unified3 = _mm_and_si128(unified2, mask4);

    return _mm_movemask_epi8(unified3) == 0xFFFF;

#endif
  }
};

// Converts the guest viewport (or fakes one if drawing without a viewport) to
// a viewport, plus values to multiply-add the returned position by, usable on
// host graphics APIs such as Direct3D 11+ and Vulkan, also forcing it to the
// Direct3D clip space with 0...W Z rather than -W...W.
void GetHostViewportInfo(GetViewportInfoArgs* XE_RESTRICT args,
                         ViewportInfo& viewport_info_out);

struct alignas(16) Scissor {
  // Offset from render target UV = 0 to +UV.
  uint32_t offset[2];
  // Extent can be zero.
  uint32_t extent[2];
};

void GetScissor(const RegisterFile& XE_RESTRICT regs,
                Scissor& XE_RESTRICT scissor_out,
                bool clamp_to_surface_pitch = true);

// Returns the color component write mask for the draw command taking into
// account which color targets are written to by the pixel shader, as well as
// components that don't exist in the formats of the render targets (render
// targets with only non-existent components written are skipped, but
// non-existent components are forced to written if some existing components of
// the render target are actually used to make sure the host driver doesn't try
// to take a slow path involving reading and mixing if there are any disabled
// components even if they don't actually exist).
uint32_t GetNormalizedColorMask(const RegisterFile& regs,
                                uint32_t pixel_shader_writes_color_targets);

// Never an identity conversion - can always write conditional move instructions
// to shaders that will be no-ops for conversion from guest to host samples.
// While we don't know the exact guest sample pattern, due to the way
// multisampled render targets are stored in the memory (like 1x2 single-sampled
// pixels with 2x MSAA, or like 2x2 single-sampled pixels with 4x), assuming
// that the sample 0 is the top sample, and the sample 1 is the bottom one.
inline uint32_t GetD3D10SampleIndexForGuest2xMSAA(
    uint32_t guest_sample_index, bool native_2x_msaa_supported) {
  assert(guest_sample_index <= 1);
  if (native_2x_msaa_supported) {
    // On Direct3D 10.1 with native 2x MSAA, the top-left sample is 1, and the
    // bottom-right sample is 0.
    return guest_sample_index ? 0 : 1;
  }
  // When native 2x MSAA is not supported, using the top-left (0) and the
  // bottom-right (3) samples of the guaranteed 4x MSAA.
  return guest_sample_index ? 3 : 0;
}

struct MemExportRange {
  uint32_t base_address_dwords;
  uint32_t size_bytes;

  explicit MemExportRange(uint32_t base_address_dwords, uint32_t size_bytes)
      : base_address_dwords(base_address_dwords), size_bytes(size_bytes) {}
};

// Gathers memory ranges involved in memexports in the shader with the float
// constants from the registers, adding them to ranges_out.
void AddMemExportRanges(const RegisterFile& regs, const Shader& shader,
                        std::vector<MemExportRange>& ranges_out);

// To avoid passing values that the shader won't understand (even though
// Direct3D 9 shouldn't pass them anyway).
XE_NOINLINE
XE_NOALIAS
xenos::CopySampleSelect SanitizeCopySampleSelect(
    xenos::CopySampleSelect copy_sample_select, xenos::MsaaSamples msaa_samples,
    bool is_depth);

// Packed structures are small and can be passed to the shaders in root/push
// constants.

union ResolveEdramInfo {
  uint32_t packed;
  struct {
    // With 32bpp/64bpp taken into account.
    uint32_t pitch_tiles : xenos::kEdramPitchTilesBits;
    xenos::MsaaSamples msaa_samples : xenos::kMsaaSamplesBits;
    uint32_t is_depth : 1;
    // With offset to the region that edram_offset_x/y_div_8 are relative to.
    uint32_t base_tiles : xenos::kEdramBaseTilesBits;
    uint32_t format : xenos::kRenderTargetFormatBits;
    uint32_t format_is_64bpp : 1;
    // Whether to fill the half-pixel offset gap on the left and the top sides
    // of the resolve region with the contents of the first surely covered
    // column / row with resolution scaling.
    uint32_t fill_half_pixel_offset : 1;
  };
  ResolveEdramInfo() : packed(0) { static_assert_size(*this, sizeof(packed)); }
};

union ResolveCoordinateInfo {
  uint32_t packed;
  struct {
    // In pixels relatively to the origin of the EDRAM base tile.
    // 0...9 for 0...72.
    uint32_t edram_offset_x_div_8 : 4;
    // 0...1 for 0...8.
    uint32_t edram_offset_y_div_8 : 1;

    // In pixels.
    // May be zero if the original rectangle was somehow specified in a
    // totally broken way - in this case, the resolve must be dropped.
    uint32_t width_div_8 : xenos::kResolveSizeBits -
                           xenos::kResolveAlignmentPixelsLog2;

    // 1 to 7.
    uint32_t draw_resolution_scale_x : 3;
    uint32_t draw_resolution_scale_y : 3;
  };
  ResolveCoordinateInfo() : packed(0) {
    static_assert_size(*this, sizeof(packed));
  }
};

// Returns tiles actually covered by a resolve area. Row length used is width of
// the area in tiles, but the pitch between rows is edram_info.pitch_tiles.
void GetResolveEdramTileSpan(ResolveEdramInfo edram_info,
                             ResolveCoordinateInfo coordinate_info,
                             uint32_t height_div_8, uint32_t& base_out,
                             uint32_t& row_length_used_out, uint32_t& rows_out);

union ResolveCopyDestCoordinateInfo {
  uint32_t packed;
  struct {
    // 0...16384/32.
    uint32_t pitch_aligned_div_32 : xenos::kTexture2DCubeMaxWidthHeightLog2 +
                                    2 - xenos::kTextureTileWidthHeightLog2;
    uint32_t height_aligned_div_32 : xenos::kTexture2DCubeMaxWidthHeightLog2 +
                                     2 - xenos::kTextureTileWidthHeightLog2;

    // Up to the maximum period of the texture tiled address function (128x128
    // for 2D 1bpb).
    uint32_t offset_x_div_8 : 7 - xenos::kResolveAlignmentPixelsLog2;
    uint32_t offset_y_div_8 : 7 - xenos::kResolveAlignmentPixelsLog2;

    xenos::CopySampleSelect copy_sample_select : 3;
  };
  ResolveCopyDestCoordinateInfo() : packed(0) {
    static_assert_size(*this, sizeof(packed));
  }
};

// For backends with Shader Model 5-like compute, host shaders to use to perform
// copying in resolve operations.
enum class ResolveCopyShaderIndex {
  kFast32bpp1x2xMSAA,
  kFast32bpp4xMSAA,
  kFast64bpp1x2xMSAA,
  kFast64bpp4xMSAA,

  kFull8bpp,
  kFull16bpp,
  kFull32bpp,
  kFull64bpp,
  kFull128bpp,

  kCount,
  kUnknown = kCount,
};

struct ResolveCopyShaderInfo {
  // Debug name of the pipeline state object with this shader.
  const char* debug_name;
  // Whether the EDRAM source needs be bound as a raw buffer (ByteAddressBuffer
  // in Direct3D) since it can load different numbers of 32-bit values at once
  // on some hardware. If the host API doesn't support raw buffers, a typed
  // buffer with source_bpe_log2-byte elements needs to be used instead.
  bool source_is_raw;
  // Log2 of bytes per element of the type of the EDRAM buffer bound to the
  // shader (at least 2).
  uint32_t source_bpe_log2;
  // Log2 of bytes per element of the type of the destination buffer bound to
  // the shader (at least 2 because of the 128 megatexel minimum requirement on
  // Direct3D 10+ - D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP - that
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
    ResolveEdramInfo edram_info;
    ResolveCoordinateInfo coordinate_info;
    reg::RB_COPY_DEST_INFO dest_info;
    ResolveCopyDestCoordinateInfo dest_coordinate_info;
  };
  DestRelative dest_relative;
  uint32_t dest_base;
};

struct ResolveClearShaderConstants {
  // rt_specific is different for color and depth, the rest is the same and can
  // be preserved in the root bindings when going from depth to color.
  struct RenderTargetSpecific {
    uint32_t clear_value[2];
    ResolveEdramInfo edram_info;
  };
  RenderTargetSpecific rt_specific;
  ResolveCoordinateInfo coordinate_info;
};

struct ResolveInfo {
  reg::RB_COPY_CONTROL rb_copy_control;

  // depth_edram_info / depth_original_base and color_edram_info /
  // color_original_base are set up if copying or clearing color and depth
  // respectively, according to RB_COPY_CONTROL.
  ResolveEdramInfo depth_edram_info;
  ResolveEdramInfo color_edram_info;
  // Original bases, without adjustment to a 160x32 region for packed offsets,
  // for locating host render targets to perform clears if host render targets
  // are used for EDRAM emulation - the same as the base that the render target
  // will likely be used for drawing next, to prevent unneeded tile ownership
  // transfers between clears and first usage if clearing a subregion.
  uint32_t depth_original_base;
  uint32_t color_original_base;

  ResolveCoordinateInfo coordinate_info;
  // Like coordinate_info.width_div_8, but not needed for shaders.
  // In pixels.
  // May be zero if the original rectangle was somehow specified in a totally
  // broken way - in this case, the resolve must be dropped.
  uint32_t height_div_8;

  reg::RB_COPY_DEST_INFO copy_dest_info;
  ResolveCopyDestCoordinateInfo copy_dest_coordinate_info;

  // The address of the texture or the location within the texture that
  // copy_dest_coordinate_info.offset_x/y_div_8 - the origin of the copy
  // destination - is relative to.
  uint32_t copy_dest_base;
  // Memory range that will potentially be modified by copying to the texture.
  // copy_dest_extent_length may be zero if something is wrong with the
  // destination, in this case, clearing may still be done, but copying must be
  // dropped.
  uint32_t copy_dest_extent_start;
  uint32_t copy_dest_extent_length;

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
    ResolveEdramInfo edram_info =
        IsCopyingDepth() ? depth_edram_info : color_edram_info;
    GetResolveEdramTileSpan(edram_info, coordinate_info, height_div_8, base_out,
                            row_length_used_out, rows_out);
    pitch_out = edram_info.pitch_tiles;
  }

  ResolveCopyShaderIndex GetCopyShader(
      uint32_t draw_resolution_scale_x, uint32_t draw_resolution_scale_y,
      ResolveCopyShaderConstants& constants_out, uint32_t& group_count_x_out,
      uint32_t& group_count_y_out) const;

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
    constants_out.coordinate_info = coordinate_info;
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
    constants_out.coordinate_info = coordinate_info;
  }

  std::pair<uint32_t, uint32_t> GetClearShaderGroupCount(
      uint32_t draw_resolution_scale_x,
      uint32_t draw_resolution_scale_y) const {
    // 8 guest MSAA samples per invocation.
    uint32_t width_samples_div_8 = coordinate_info.width_div_8;
    uint32_t height_samples_div_8 = height_div_8;
    xenos::MsaaSamples samples = IsCopyingDepth()
                                     ? depth_edram_info.msaa_samples
                                     : color_edram_info.msaa_samples;
    if (samples >= xenos::MsaaSamples::k2X) {
      height_samples_div_8 <<= 1;
      if (samples >= xenos::MsaaSamples::k4X) {
        width_samples_div_8 <<= 1;
      }
    }
    width_samples_div_8 *= draw_resolution_scale_x;
    height_samples_div_8 *= draw_resolution_scale_y;
    return std::make_pair((width_samples_div_8 + uint32_t(7)) >> 3,
                          height_samples_div_8);
  }
};

// Returns false if there was an error obtaining the info making it totally
// invalid. fixed_rg[ba]16_truncated_to_minus_1_to_1 is false if 16_16[_16_16]
// color render target formats are properly emulated as -32...32, true if
// emulated as snorm, with range limited to -1...1, but with correct blending
// within that range.
bool GetResolveInfo(const RegisterFile& regs, const Memory& memory,
                    TraceWriter& trace_writer, uint32_t draw_resolution_scale_x,
                    uint32_t draw_resolution_scale_y,
                    bool fixed_rg16_truncated_to_minus_1_to_1,
                    bool fixed_rgba16_truncated_to_minus_1_to_1,
                    ResolveInfo& info_out);

}  // namespace draw_util
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DRAW_UTIL_H_