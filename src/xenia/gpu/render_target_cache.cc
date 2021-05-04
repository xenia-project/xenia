/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/render_target_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"

DEFINE_bool(
    depth_transfer_not_equal_test, true,
    "When transferring data between depth render targets, use the \"not "
    "equal\" test to avoid writing rewriting depth via shader depth output if "
    "it's the same as the one currently in the depth buffer in case of round "
    "trips of the data.\n"
    "Settings this to true may make transfer round trips more friendly to "
    "depth compression depending on how the GPU implements it (as arbitrary "
    "depth output may result in it being disabled completely), which is "
    "beneficial to subsequent rendering, while setting this to false may "
    "reduce bandwidth usage during transfers as the previous depth won't need "
    "to be read.",
    "GPU");
DEFINE_string(
    depth_float24_conversion, "",
    "Method for converting 32-bit Z values to 20e4 floating point when using "
    "host depth buffers without native 20e4 support (when not using rasterizer-"
    "ordered views / fragment shader interlocks to perform depth testing "
    "manually).\n"
    "Use: [any, on_copy, truncate, round]\n"
    " on_copy:\n"
    "  Do depth testing at host precision, converting when copying between "
    "color and depth buffers (or between depth buffers of different formats) "
    "to support reinterpretation, but keeps the last host depth buffer used "
    "for each EDRAM range and reloads the host precision value if it's still "
    "up to date after the EDRAM range was used with a different pixel format.\n"
    "  + Highest performance, allows early depth test and writing.\n"
    "  + Host MSAA is possible with pixel-rate shading where supported.\n"
    "  - EDRAM > RAM > EDRAM depth buffer round trip done in certain games "
    "(such as GTA IV) destroys precision irreparably, causing artifacts if "
    "another rendering pass is done after the EDRAM reupload.\n"
    " truncate:\n"
    "  Convert to 20e4 directly in pixel shaders, always rounding down.\n"
    "  + Average performance, conservative early depth test is possible.\n"
    "  + No precision loss when anything changes in the storage of the depth "
    "buffer, EDRAM > RAM > EDRAM copying preserves precision.\n"
    "  - Rounding mode is incorrect, sometimes giving results smaller than "
    "they should be - may cause inaccuracy especially in edge cases when the "
    "game wants to write an exact value.\n"
    "  - Host MSAA is only possible at SSAA speed, with per-sample shading.\n"
    " round:\n"
    "  Convert to 20e4 directly in pixel shaders, correctly rounding to the "
    "nearest even.\n"
    "  + Highest accuracy.\n"
    "  - Significantly limited performance, early depth test is not possible.\n"
    "  - Host MSAA is only possible at SSAA speed, with per-sample shading.\n"
    " Any other value:\n"
    "  Choose what is considered the most optimal (currently \"on_copy\").",
    "GPU");
DEFINE_int32(
    draw_resolution_scale, 1,
    "Integer pixel width and height scale used for scaling the rendering "
    "resolution opaquely to the game.\n"
    "1, 2 and 3 may be supported, but support of anything above 1 depends on "
    "the device properties, such as whether it supports sparse binding / tiled "
    "resources, the number of virtual address bits per resource, and other "
    "factors.\n"
    "Various effects and parts of game rendering pipelines may work "
    "incorrectly as pixels become ambiguous from the game's perspective and "
    "because half-pixel offset (which normally doesn't affect coverage when "
    "MSAA isn't used) becomes full-pixel.",
    "GPU");
DEFINE_bool(
    draw_resolution_scaled_texture_offsets, true,
    "Apply offsets from texture fetch instructions taking resolution scale "
    "into account for render-to-texture, for more correct shadow filtering, "
    "bloom, etc., in some cases.",
    "GPU");
// Disabled by default because of full-screen effects that occur when game
// shaders assume piecewise linear, much more severe than blending-related
// issues.
DEFINE_bool(
    gamma_render_target_as_srgb, false,
    "When the host can't write piecewise linear gamma directly with correct "
    "blending, use sRGB output on the host for conceptually correct blending "
    "in linear color space (to prevent issues such as bright squares around "
    "bullet holes and overly dark lighting in Halo 3) while having slightly "
    "different precision distribution in the render target and severely "
    "incorrect values if the game accesses the resulting colors directly as "
    "raw data (the whole screen in The Orange Box, for instance, since when "
    "the first loading bar appears).",
    "GPU");
DEFINE_bool(
    mrt_edram_used_range_clamp_to_min, true,
    "With host render targets, if multiple render targets are bound, estimate "
    "the EDRAM range modified in any of them to be not bigger than the "
    "distance between any two render targets in the EDRAM, rather than "
    "allowing the last one claim the rest of the EDRAM.\n"
    "Has effect primarily on draws without viewport clipping.\n"
    "Setting this to false results in higher accuracy in rare cases, but may "
    "increase the amount of copying that needs to be done sometimes.",
    "GPU");
DEFINE_bool(
    native_2x_msaa, true,
    "Use host 2x MSAA when available. Can be disabled for scalability testing "
    "on host GPU APIs where 2x is not mandatory, in this case, 2 samples of 4x "
    "MSAA will be used instead (with similar or worse quality and higher "
    "memory usage).",
    "GPU");
DEFINE_bool(
    native_stencil_value_output, true,
    "Use pixel shader stencil reference output where available for purposes "
    "like copying between render targets. Can be disabled for scalability "
    "testing, in this case, much more expensive drawing of 8 quads will be "
    "done.",
    "GPU");
DEFINE_bool(
    snorm16_render_target_full_range, true,
    "When the host can only support 16_16 and 16_16_16_16 render targets as "
    "-1...1, remap -32...32 to -1...1 to use the full possible range of "
    "values, at the expense of multiplicative blending correctness.",
    "GPU");

namespace xe {
namespace gpu {

uint32_t RenderTargetCache::Transfer::GetRangeRectangles(
    uint32_t start_tiles, uint32_t end_tiles, uint32_t base_tiles,
    uint32_t pitch_tiles, xenos::MsaaSamples msaa_samples, bool is_64bpp,
    Rectangle* rectangles_out, const Rectangle* cutout) {
  assert_true(start_tiles <= end_tiles);
  assert_true(base_tiles <= start_tiles);
  assert_not_zero(pitch_tiles);
  if (start_tiles == end_tiles) {
    return 0;
  }
  uint32_t tile_width =
      xenos::kEdramTileWidthSamples >>
      (uint32_t(msaa_samples >= xenos::MsaaSamples::k4X) + uint32_t(is_64bpp));
  uint32_t tile_height = xenos::kEdramTileHeightSamples >>
                         uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  // If the first and / or the last rows have the same X spans as the middle
  // part, merge them with it.
  uint32_t rectangle_count = 0;
  uint32_t local_start = start_tiles - base_tiles;
  uint32_t local_end = end_tiles - base_tiles;
  // Inclusive.
  uint32_t rows_start = local_start / pitch_tiles;
  // Exclusive.
  uint32_t rows_end = (local_end + (pitch_tiles - 1)) / pitch_tiles;
  uint32_t row_first_start = local_start - rows_start * pitch_tiles;
  uint32_t row_last_end = pitch_tiles - (rows_end * pitch_tiles - local_end);
  uint32_t rows = rows_end - rows_start;
  if (rows == 1 || row_first_start) {
    Rectangle rectangle_first;
    rectangle_first.x_pixels = row_first_start * tile_width;
    rectangle_first.y_pixels = rows_start * tile_height;
    rectangle_first.width_pixels =
        ((rows == 1 ? row_last_end : pitch_tiles) - row_first_start) *
        tile_width;
    rectangle_first.height_pixels = tile_height;
    rectangle_count += AddRectangle(
        rectangle_first,
        rectangles_out ? rectangles_out + rectangle_count : nullptr, cutout);
    if (rows == 1) {
      return rectangle_count;
    }
  }
  uint32_t mid_rows_start = rows_start + 1;
  uint32_t mid_rows = rows - 2;
  if (!row_first_start) {
    --mid_rows_start;
    ++mid_rows;
  }
  if (row_last_end == pitch_tiles) {
    ++mid_rows;
  }
  if (mid_rows) {
    Rectangle rectangle_mid;
    rectangle_mid.x_pixels = 0;
    rectangle_mid.y_pixels = mid_rows_start * tile_height;
    rectangle_mid.width_pixels = pitch_tiles * tile_width;
    rectangle_mid.height_pixels = mid_rows * tile_height;
    rectangle_count += AddRectangle(
        rectangle_mid,
        rectangles_out ? rectangles_out + rectangle_count : nullptr, cutout);
  }
  if (row_last_end != pitch_tiles) {
    Rectangle rectangle_last;
    rectangle_last.x_pixels = 0;
    rectangle_last.y_pixels = (rows_end - 1) * tile_height;
    rectangle_last.width_pixels = row_last_end * tile_width;
    rectangle_last.height_pixels = tile_height;
    rectangle_count += AddRectangle(
        rectangle_last,
        rectangles_out ? rectangles_out + rectangle_count : nullptr, cutout);
  }
  assert_true(rectangle_count <= (cutout ? kMaxRectanglesWithCutout
                                         : kMaxRectanglesWithoutCutout));
  return rectangle_count;
}

uint32_t RenderTargetCache::Transfer::AddRectangle(const Rectangle& rectangle,
                                                   Rectangle* rectangles_out,
                                                   const Rectangle* cutout) {
  uint32_t rectangle_right = rectangle.x_pixels + rectangle.width_pixels;
  uint32_t rectangle_bottom = rectangle.y_pixels + rectangle.height_pixels;
  // If nothing to cut out (no region specified, or no intersection - if the
  // cutout region is in the middle on Y, but completely to the left / right on
  // X, don't split), add the whole rectangle.
  if (!cutout || !cutout->width_pixels || !cutout->height_pixels ||
      cutout->x_pixels >= rectangle_right ||
      cutout->x_pixels + cutout->width_pixels <= rectangle.x_pixels ||
      cutout->y_pixels >= rectangle_bottom ||
      cutout->y_pixels + cutout->height_pixels <= rectangle.y_pixels) {
    if (rectangles_out) {
      rectangles_out[0] = rectangle;
    }
    return 1;
  }
  uint32_t rectangle_count = 0;
  uint32_t cutout_right = cutout->x_pixels + cutout->width_pixels;
  uint32_t cutout_bottom = cutout->y_pixels + cutout->height_pixels;
  // Upper part after cutout.
  if (cutout->y_pixels > rectangle.y_pixels) {
    // The completely outside case has already been checked.
    assert_true(cutout->y_pixels < rectangle_bottom);
    if (rectangles_out) {
      Rectangle& rectangle_upper = rectangles_out[rectangle_count];
      rectangle_upper.x_pixels = rectangle.x_pixels;
      rectangle_upper.y_pixels = rectangle.y_pixels;
      rectangle_upper.width_pixels = rectangle.width_pixels;
      // cutout->y_pixels is already known to be < rectangle_bottom, no need for
      // min(cutout->y_pixels - rectangle.y_pixels, rectangle.height_pixels).
      rectangle_upper.height_pixels = cutout->y_pixels - rectangle.y_pixels;
    }
    ++rectangle_count;
  }
  // Middle part after cutout.
  uint32_t middle_top = std::max(cutout->y_pixels, rectangle.y_pixels);
  uint32_t middle_height =
      std::min(cutout_bottom, rectangle_bottom) - middle_top;
  // Middle left.
  if (cutout->x_pixels > rectangle.x_pixels) {
    assert_true(cutout->x_pixels < rectangle_right);
    if (rectangles_out) {
      Rectangle& rectangle_middle_left = rectangles_out[rectangle_count];
      rectangle_middle_left.x_pixels = rectangle.x_pixels;
      rectangle_middle_left.y_pixels = middle_top;
      rectangle_middle_left.width_pixels =
          cutout->x_pixels - rectangle.x_pixels;
      rectangle_middle_left.height_pixels = middle_height;
    }
    ++rectangle_count;
  }
  // Middle right.
  if (cutout_right < rectangle_right) {
    assert_true(cutout_right > rectangle.x_pixels);
    if (rectangles_out) {
      Rectangle& rectangle_middle_right = rectangles_out[rectangle_count];
      rectangle_middle_right.x_pixels = cutout_right;
      rectangle_middle_right.y_pixels = middle_top;
      rectangle_middle_right.width_pixels = rectangle_right - cutout_right;
      rectangle_middle_right.height_pixels = middle_height;
    }
    ++rectangle_count;
  }
  // Lower part after cutout.
  if (cutout_bottom < rectangle_bottom) {
    assert_true(cutout_bottom > rectangle.y_pixels);
    if (rectangles_out) {
      Rectangle& rectangle_upper = rectangles_out[rectangle_count];
      rectangle_upper.x_pixels = rectangle.x_pixels;
      rectangle_upper.y_pixels = cutout_bottom;
      rectangle_upper.width_pixels = rectangle.width_pixels;
      rectangle_upper.height_pixels = rectangle_bottom - cutout_bottom;
    }
    ++rectangle_count;
  }
  assert_true(rectangle_count <= kMaxCutoutBorderRectangles);
  return rectangle_count;
}

RenderTargetCache::~RenderTargetCache() { ShutdownCommon(); }

void RenderTargetCache::InitializeCommon() {
  assert_true(ownership_ranges_.empty());
  ownership_ranges_.emplace(
      std::piecewise_construct, std::forward_as_tuple(uint32_t(0)),
      std::forward_as_tuple(xenos::kEdramTileCount, RenderTargetKey(),
                            RenderTargetKey(), RenderTargetKey()));
}

void RenderTargetCache::ShutdownCommon() {
  ownership_ranges_.clear();

  for (const auto& render_target_pair : render_targets_) {
    if (render_target_pair.second) {
      delete render_target_pair.second;
    }
  }
  render_targets_.clear();
}

void RenderTargetCache::ClearCache() {
  // Keep only render targets currently owning any EDRAM data.
  if (!render_targets_.empty()) {
    std::unordered_set<RenderTargetKey, RenderTargetKey::Hasher>
        used_render_targets;
    for (const auto& ownership_range_pair : ownership_ranges_) {
      const OwnershipRange& ownership_range = ownership_range_pair.second;
      if (!ownership_range.render_target.IsEmpty()) {
        used_render_targets.emplace(ownership_range.render_target);
      }
      if (!ownership_range.host_depth_render_target_unorm24.IsEmpty()) {
        used_render_targets.emplace(
            ownership_range.host_depth_render_target_unorm24);
      }
      if (!ownership_range.host_depth_render_target_float24.IsEmpty()) {
        used_render_targets.emplace(
            ownership_range.host_depth_render_target_float24);
      }
    }
    if (render_targets_.size() != used_render_targets.size()) {
      typename decltype(render_targets_)::iterator it_next;
      for (auto it = render_targets_.begin(); it != render_targets_.end();
           it = it_next) {
        it_next = std::next(it);
        if (!it->second) {
          render_targets_.erase(it);
          continue;
        }
        if (used_render_targets.find(it->second->key()) ==
            used_render_targets.end()) {
          delete it->second;
          render_targets_.erase(it);
        }
      }
    }
  }
}

void RenderTargetCache::BeginFrame() { ResetAccumulatedRenderTargets(); }

bool RenderTargetCache::Update(bool is_rasterization_done,
                               uint32_t shader_writes_color_targets) {
  const RegisterFile& regs = register_file();
  bool interlock_barrier_only = GetPath() == Path::kPixelShaderInterlock;

  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  xenos::MsaaSamples msaa_samples = rb_surface_info.msaa_samples;
  assert_true(msaa_samples <= xenos::MsaaSamples::k4X);
  if (msaa_samples > xenos::MsaaSamples::k4X) {
    // Safety check because a lot of code assumes up to 4x.
    assert_always();
    XELOGE("{}x MSAA requested by the guest, Xenos only supports up to 4x",
           uint32_t(1) << uint32_t(msaa_samples));
    return false;
  }
  uint32_t msaa_samples_x_log2 =
      uint32_t(msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t pitch_pixels = rb_surface_info.surface_pitch;
  // surface_pitch 0 should be handled in disabling rasterization (hopefully
  // it's safe to assume that).
  assert_true(pitch_pixels || !is_rasterization_done);
  if (!pitch_pixels) {
    is_rasterization_done = false;
  } else if (pitch_pixels > xenos::kTexture2DCubeMaxWidthHeight) {
    XELOGE(
        "Surface pitch {} larger than the maximum texture width {} specified "
        "by the guest",
        pitch_pixels, xenos::kTexture2DCubeMaxWidthHeight);
    return false;
  }
  uint32_t pitch_tiles_at_32bpp = ((pitch_pixels << msaa_samples_x_log2) +
                                   (xenos::kEdramTileWidthSamples - 1)) /
                                  xenos::kEdramTileWidthSamples;
  if (!interlock_barrier_only) {
    uint32_t pitch_pixels_tile_aligned_scaled =
        pitch_tiles_at_32bpp *
        (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2) *
        GetResolutionScale();
    uint32_t max_render_target_width = GetMaxRenderTargetWidth();
    if (pitch_pixels_tile_aligned_scaled > max_render_target_width) {
      // TODO(Triang3l): If really needed for some game on some device, clamp
      // the pitch and generate multiple ranges (each for every row of tiles)
      // with gaps for padding. Very few PowerVR GPUs have 4096, not 8192, as
      // the limit, though with 8192 (on Mali) the actual limit for Xenia is
      // 8160 because tile padding is stored - but 8192 should be extremely rare
      // anyway.
      XELOGE(
          "Surface pitch aligned to EDRAM tiles and resolution-scaled {} "
          "larger than the maximum host render target width {}",
          pitch_pixels_tile_aligned_scaled, max_render_target_width);
      return false;
    }
  }

  uint32_t rts_remaining;
  uint32_t rt_index;

  // Get used render targets.
  // [0] is depth / stencil where relevant, [1...4] is color.
  // Depth / stencil testing / writing is before color in the pipeline.
  uint32_t depth_and_color_rts_used_bits = 0;
  // depth_and_color_rts_used_bits -> EDRAM base.
  uint32_t edram_bases[1 + xenos::kMaxColorRenderTargets];
  uint32_t host_relevant_formats[1 + xenos::kMaxColorRenderTargets];
  uint32_t rts_are_64bpp = 0;
  uint32_t color_rts_are_gamma = 0;
  if (is_rasterization_done) {
    auto rb_depthcontrol = regs.Get<reg::RB_DEPTHCONTROL>();
    if (rb_depthcontrol.z_enable || rb_depthcontrol.stencil_enable) {
      depth_and_color_rts_used_bits |= 1;
      auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
      // std::min for safety, to avoid negative numbers in case it's completely
      // wrong.
      edram_bases[0] =
          std::min(rb_depth_info.depth_base, xenos::kEdramTileCount);
      // With pixel shader interlock, always the same addressing disregarding
      // the format.
      host_relevant_formats[0] =
          interlock_barrier_only ? 0 : uint32_t(rb_depth_info.depth_format);
    }
    if (regs.Get<reg::RB_MODECONTROL>().edram_mode ==
        xenos::ModeControl::kColorDepth) {
      uint32_t rb_color_mask = regs[XE_GPU_REG_RB_COLOR_MASK].u32;
      rts_remaining = shader_writes_color_targets;
      while (xe::bit_scan_forward(rts_remaining, &rt_index)) {
        rts_remaining &= ~(uint32_t(1) << rt_index);
        auto color_info = regs.Get<reg::RB_COLOR_INFO>(
            reg::RB_COLOR_INFO::rt_register_indices[rt_index]);
        xenos::ColorRenderTargetFormat color_format =
            regs.Get<reg::RB_COLOR_INFO>(
                    reg::RB_COLOR_INFO::rt_register_indices[rt_index])
                .color_format;
        if ((rb_color_mask >> (rt_index * 4)) &
            ((uint32_t(1) << xenos::GetColorRenderTargetFormatComponentCount(
                  color_format)) -
             1)) {
          uint32_t rt_bit_index = 1 + rt_index;
          depth_and_color_rts_used_bits |= uint32_t(1) << rt_bit_index;
          edram_bases[rt_bit_index] =
              std::min(color_info.color_base, xenos::kEdramTileCount);
          bool is_64bpp = xenos::IsColorRenderTargetFormat64bpp(color_format);
          if (is_64bpp) {
            rts_are_64bpp |= uint32_t(1) << rt_bit_index;
          }
          if (color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
            color_rts_are_gamma |= uint32_t(1) << rt_index;
          }
          xenos::ColorRenderTargetFormat color_host_relevant_format;
          if (interlock_barrier_only) {
            // Only changes in mapping between coordinates and addresses are
            // interesting (along with access overlap between draw calls), thus
            // only pixel size is relevant.
            color_host_relevant_format =
                is_64bpp ? xenos::ColorRenderTargetFormat::k_16_16_16_16
                         : xenos::ColorRenderTargetFormat::k_8_8_8_8;
          } else {
            color_host_relevant_format = GetHostRelevantColorFormat(
                xenos::GetStorageColorFormat(color_format));
          }
          host_relevant_formats[rt_bit_index] =
              uint32_t(color_host_relevant_format);
        }
      }
    }
  }
  // Eliminate other bound render targets if their EDRAM base conflicts with
  // another render target - it's an error in most host implementations to bind
  // the same render target into multiple slots, also the behavior would be
  // unpredictable if that happens.
  // Depth is considered the least important as it's earlier in the pipeline
  // (issues caused by color and depth render target collisions haven't been
  // found yet), but render targets with smaller index are considered more
  // important - specifically, because of the usage in the lighting pass of
  // Halo 3, which can be checked in the vertical look calibration sequence in
  // the beginning of the game: if render target 0 is removed in favor of 1, the
  // UNSC servicemen and the world will be too dark, like fully in shadow -
  // especially prominent on the helmet. This happens because the shader picks
  // between two render targets to write dynamically (though with a static, bool
  // constant condition), but all other state is set up in a way that implies
  // the same render target being bound twice. On Direct3D 9, if you don't write
  // to a color pixel shader output on the control flow that was taken, the
  // render target will not be written to. However, this has been relaxed in
  // Direct3D 10, where if the shader declares an output, it's assumed to be
  // always written (or with an undefined value otherwise).
  rts_remaining = depth_and_color_rts_used_bits & ~(uint32_t(1));
  while (xe::bit_scan_forward(rts_remaining, &rt_index)) {
    rts_remaining &= ~(uint32_t(1) << rt_index);
    uint32_t edram_base = edram_bases[rt_index];
    uint32_t rts_other_remaining =
        depth_and_color_rts_used_bits &
        (~((uint32_t(1) << (rt_index + 1)) - 1) | uint32_t(1));
    uint32_t rt_other_index;
    while (xe::bit_scan_forward(rts_other_remaining, &rt_other_index)) {
      rts_other_remaining &= ~(uint32_t(1) << rt_other_index);
      if (edram_bases[rt_other_index] == edram_base) {
        depth_and_color_rts_used_bits &= ~(uint32_t(1) << rt_other_index);
      }
    }
  }

  // Clear ownership transfers before adding any.
  if (!interlock_barrier_only) {
    for (size_t i = 0; i < xe::countof(last_update_transfers_); ++i) {
      last_update_transfers_[i].clear();
    }
  }

  if (!depth_and_color_rts_used_bits) {
    // Nothing to bind, don't waste time on things like memexport-only draws -
    // just check if old bindings can still be used.
    std::memset(last_update_used_render_targets_, 0,
                sizeof(last_update_used_render_targets_));
    if (are_accumulated_render_targets_valid_) {
      for (size_t i = 0;
           i < xe::countof(last_update_accumulated_render_targets_); ++i) {
        const RenderTarget* render_target =
            last_update_accumulated_render_targets_[i];
        if (!render_target) {
          continue;
        }
        RenderTargetKey rt_key = render_target->key();
        if (rt_key.pitch_tiles_at_32bpp != pitch_tiles_at_32bpp ||
            rt_key.msaa_samples != msaa_samples) {
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      }
    }
    if (!are_accumulated_render_targets_valid_) {
      std::memset(last_update_accumulated_render_targets_, 0,
                  sizeof(last_update_accumulated_render_targets_));
      last_update_accumulated_color_targets_are_gamma_ = 0;
    }
    return true;
  }

  // Estimate height used by render targets (for color for writes, for depth /
  // stencil for both reads and writes) from various sources.
  uint32_t height_used =
      GetRenderTargetHeight(pitch_tiles_at_32bpp, msaa_samples);
  int32_t window_y_offset =
      regs.Get<reg::PA_SC_WINDOW_OFFSET>().window_y_offset;
  if (!regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable) {
    auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
    float viewport_bottom = 0.0f;
    // First calculate all the integer.0 or integer.5 offsetting exactly at full
    // precision.
    if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
      viewport_bottom += float(window_y_offset);
    }
    if (cvars::half_pixel_offset &&
        !regs.Get<reg::PA_SU_VTX_CNTL>().pix_center) {
      viewport_bottom += 0.5f;
    }
    // Then apply the floating-point viewport offset.
    if (pa_cl_vte_cntl.vport_y_offset_ena) {
      viewport_bottom += regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32;
    }
    viewport_bottom += pa_cl_vte_cntl.vport_y_scale_ena
                           ? std::abs(regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32)
                           : 1.0f;
    // Using floor, or, rather, truncation (because maxing with zero anyway)
    // similar to how viewport scissoring behaves on real AMD, Intel and Nvidia
    // GPUs on Direct3D 12, also like in draw_util::GetHostViewportInfo.
    // fmax to drop NaN and < 0, min as float (height_used is well below 2^24)
    // to safely drop very large values.
    height_used = uint32_t(
        std::min(std::fmax(viewport_bottom, 0.0f), float(height_used)));
  }
  int32_t scissor_bottom =
      int32_t(regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>().br_y);
  if (!regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>().window_offset_disable) {
    scissor_bottom += window_y_offset;
  }
  scissor_bottom =
      std::min(scissor_bottom, regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>().br_y);
  height_used =
      std::min(height_used, uint32_t(std::max(scissor_bottom, int32_t(0))));

  // Sorted by EDRAM base and then by index in the pipeline - for simplicity,
  // treat render targets placed closer to the end of the EDRAM as truncating
  // the previous one (and in case multiple render targets are placed at the
  // same EDRAM base, though normally this shouldn't happen, treat the color
  // ones as more important than the depth one, which may be not needed and just
  // a leftover if the draw, for instance, has depth / stencil happening to be
  // always passing and never writing with the current state, and also because
  // depth testing has to happen before the color is written). Overall it's
  // normal for estimated EDRAM ranges of render targets to intersect if drawing
  // without a viewport (as there's nothing to clamp the estimated height) and
  // multiple render targets are bound.
  std::pair<uint32_t, uint32_t>
      edram_bases_sorted[1 + xenos::kMaxColorRenderTargets];
  uint32_t edram_bases_sorted_count = 0;
  rts_remaining = depth_and_color_rts_used_bits;
  while (xe::bit_scan_forward(rts_remaining, &rt_index)) {
    rts_remaining &= ~(uint32_t(1) << rt_index);
    edram_bases_sorted[edram_bases_sorted_count++] =
        std::make_pair(edram_bases[rt_index], rt_index);
  }
  std::sort(edram_bases_sorted, edram_bases_sorted + edram_bases_sorted_count);
  uint32_t rt_max_distance_tiles = xenos::kEdramTileCount;
  if (cvars::mrt_edram_used_range_clamp_to_min) {
    for (uint32_t i = 1; i < edram_bases_sorted_count; ++i) {
      rt_max_distance_tiles =
          std::min(rt_max_distance_tiles, edram_bases_sorted[i].first -
                                              edram_bases_sorted[i - 1].first);
    }
  }

  // Make sure all the needed render targets are created, and gather lengths of
  // ranges used by each render target.
  RenderTargetKey rt_keys[1 + xenos::kMaxColorRenderTargets];
  RenderTarget* rts[1 + xenos::kMaxColorRenderTargets];
  uint32_t rt_lengths_tiles[1 + xenos::kMaxColorRenderTargets];
  uint32_t length_used_tiles_at_32bpp =
      ((height_used << uint32_t(msaa_samples >= xenos::MsaaSamples::k2X)) +
       (xenos::kEdramTileHeightSamples - 1)) /
      xenos::kEdramTileHeightSamples * pitch_tiles_at_32bpp;
  for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
    const std::pair<uint32_t, uint32_t>& rt_base_index = edram_bases_sorted[i];
    uint32_t rt_base = rt_base_index.first;
    uint32_t rt_bit_index = rt_base_index.second;
    RenderTargetKey& rt_key = rt_keys[rt_bit_index];
    rt_key.base_tiles = rt_base;
    rt_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    rt_key.msaa_samples = msaa_samples;
    rt_key.is_depth = rt_bit_index == 0;
    rt_key.host_relevant_format = host_relevant_formats[rt_bit_index];
    if (!interlock_barrier_only) {
      RenderTarget* render_target = GetOrCreateRenderTarget(rt_key);
      if (!render_target) {
        return false;
      }
      rts[rt_bit_index] = render_target;
    }
    rt_lengths_tiles[i] = std::min(
        std::min(
            length_used_tiles_at_32bpp << ((rts_are_64bpp >> rt_bit_index) & 1),
            rt_max_distance_tiles),
        ((i + 1 < edram_bases_sorted_count) ? edram_bases_sorted[i + 1].first
                                            : xenos::kEdramTileCount) -
            rt_base);
  }

  if (interlock_barrier_only) {
    // Because a full pixel shader interlock barrier may clear the ownership map
    // (since it flushes all previous writes, and there's no need for another
    // barrier if an overlap is encountered later between pre-barrier and
    // post-barrier usages), check if any overlap requiring a barrier happens,
    // and then insert the barrier if needed.
    bool interlock_barrier_needed = false;
    for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
      const std::pair<uint32_t, uint32_t>& rt_base_index =
          edram_bases_sorted[i];
      if (WouldOwnershipChangeRequireTransfers(rt_keys[rt_base_index.second],
                                               rt_base_index.first,
                                               rt_lengths_tiles[i])) {
        interlock_barrier_needed = true;
        break;
      }
    }
    if (interlock_barrier_needed) {
      RequestPixelShaderInterlockBarrier();
    }
  }

  // From now on ownership transfers should succeed for simplicity and
  // consistency, even if they fail in the implementation (just ignore that and
  // draw with whatever contents currently are in the render target in this
  // case).

  for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
    const std::pair<uint32_t, uint32_t>& rt_base_index = edram_bases_sorted[i];
    uint32_t rt_bit_index = rt_base_index.second;
    ChangeOwnership(
        rt_keys[rt_bit_index], rt_base_index.first, rt_lengths_tiles[i],
        interlock_barrier_only ? nullptr
                               : &last_update_transfers_[rt_bit_index]);
  }

  if (interlock_barrier_only) {
    // No copying transfers or render target bindings - only needed the barrier.
    return true;
  }

  // If everything succeeded, update the used render targets.
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    last_update_used_render_targets_[i] =
        (depth_and_color_rts_used_bits & (uint32_t(1) << i)) ? rts[i] : nullptr;
  }
  if (are_accumulated_render_targets_valid_) {
    // Check if the only re-enabling a previously bound render target.
    for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      RenderTarget* current_rt =
          (depth_and_color_rts_used_bits & (uint32_t(1) << i)) ? rts[i]
                                                               : nullptr;
      const RenderTarget* accumulated_rt =
          last_update_accumulated_render_targets_[i];
      if (!accumulated_rt) {
        if (current_rt) {
          // Binding a totally new render target - won't keep the existing
          // render pass anyway, no much need to try to re-enable previously
          // disabled render targets in other slots as well, even though that
          // would be valid.
          are_accumulated_render_targets_valid_ = false;
          break;
        }
        // Append the new render target.
        last_update_accumulated_render_targets_[i] = current_rt;
        continue;
      }
      if (current_rt) {
        if (current_rt != accumulated_rt) {
          // Changing a render target in a slot.
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      } else {
        RenderTargetKey accumulated_rt_key = accumulated_rt->key();
        if (accumulated_rt_key.pitch_tiles_at_32bpp != pitch_tiles_at_32bpp ||
            accumulated_rt_key.msaa_samples != msaa_samples) {
          // The previously bound render target is incompatible with the
          // current surface info.
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      }
    }
    // Make sure the same render target isn't bound into two different slots
    // over time.
    for (uint32_t i = 1; are_accumulated_render_targets_valid_ &&
                         i < 1 + xenos::kMaxColorRenderTargets;
         ++i) {
      const RenderTarget* render_target =
          last_update_accumulated_render_targets_[i];
      if (!render_target) {
        continue;
      }
      for (uint32_t j = 0; j < i; ++j) {
        if (last_update_accumulated_render_targets_[j] == render_target) {
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      }
    }
  }
  if (!are_accumulated_render_targets_valid_) {
    std::memcpy(last_update_accumulated_render_targets_,
                last_update_used_render_targets_,
                sizeof(last_update_accumulated_render_targets_));
    last_update_accumulated_color_targets_are_gamma_ = 0;
    are_accumulated_render_targets_valid_ = true;
  }
  // Only update color space of render targets that actually matter here, don't
  // disable gamma emulation (which may require ending the render pass) on the
  // host, for example, if making a depth-only draw between color draws with a
  // gamma target.
  uint32_t color_rts_used_bits = depth_and_color_rts_used_bits >> 1;
  // Ignore any render targets dropped before in this function for any reason.
  color_rts_are_gamma &= color_rts_used_bits;
  last_update_accumulated_color_targets_are_gamma_ =
      (last_update_accumulated_color_targets_are_gamma_ &
       ~color_rts_used_bits) |
      color_rts_are_gamma;

  return true;
}

uint32_t RenderTargetCache::GetLastUpdateBoundRenderTargets(
    uint32_t* depth_and_color_formats_out) const {
  if (GetPath() != Path::kHostRenderTargets) {
    if (depth_and_color_formats_out) {
      std::memset(depth_and_color_formats_out, 0,
                  sizeof(uint32_t) * (1 + xenos::kMaxColorRenderTargets));
    }
    return 0;
  }
  uint32_t rts_used = 0;
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    const RenderTarget* render_target =
        last_update_accumulated_render_targets_[i];
    if (!render_target) {
      if (depth_and_color_formats_out) {
        depth_and_color_formats_out[i] = 0;
      }
      continue;
    }
    rts_used |= uint32_t(1) << i;
    if (depth_and_color_formats_out) {
      depth_and_color_formats_out[i] =
          render_target->key().host_relevant_format;
    }
  }
  return rts_used;
}

RenderTargetCache::DepthFloat24Conversion
RenderTargetCache::GetConfigDepthFloat24Conversion() {
  if (cvars::depth_float24_conversion == "truncate") {
    return DepthFloat24Conversion::kOnOutputTruncating;
  }
  if (cvars::depth_float24_conversion == "round") {
    return DepthFloat24Conversion::kOnOutputRounding;
  }
  return DepthFloat24Conversion::kOnCopy;
}

uint32_t RenderTargetCache::GetRenderTargetHeight(
    uint32_t pitch_tiles_at_32bpp, xenos::MsaaSamples msaa_samples) const {
  assert_not_zero(pitch_tiles_at_32bpp);
  // Down to the end of EDRAM.
  uint32_t tile_rows = (xenos::kEdramTileCount + (pitch_tiles_at_32bpp - 1)) /
                       pitch_tiles_at_32bpp;
  // Clamp to the guest limit (tile padding should exceed it) and to the host
  // limit (tile padding mustn't exceed it).
  static_assert(
      !(xenos::kTexture2DCubeMaxWidthHeight % xenos::kEdramTileHeightSamples),
      "Maximum guest render target height is assumed to always be a multiple "
      "of an EDRAM tile height");
  uint32_t resolution_scale = GetResolutionScale();
  uint32_t max_height_scaled =
      std::min(xenos::kTexture2DCubeMaxWidthHeight * resolution_scale,
               GetMaxRenderTargetHeight());
  uint32_t msaa_samples_y_log2 =
      uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  uint32_t tile_height_samples_scaled =
      xenos::kEdramTileHeightSamples * resolution_scale;
  tile_rows = std::min(tile_rows, (max_height_scaled << msaa_samples_y_log2) /
                                      tile_height_samples_scaled);
  assert_not_zero(tile_rows);
  return tile_rows * (xenos::kEdramTileHeightSamples >> msaa_samples_y_log2);
}

void RenderTargetCache::GetResolveCopyRectanglesToDump(
    uint32_t base, uint32_t row_length, uint32_t rows, uint32_t pitch,
    std::vector<ResolveCopyDumpRectangle>& rectangles_out) const {
  rectangles_out.clear();
  assert_true(row_length <= pitch);
  row_length = std::min(row_length, pitch);
  if (!row_length || !rows) {
    return;
  }
  uint32_t resolve_area_end = base + (rows - 1) * pitch + row_length;
  // Collect render targets owning ranges within the specified rectangle. The
  // first render target in the range may be before the lower_bound, only being
  // in the range with its tail.
  auto it = ownership_ranges_.lower_bound(base);
  if (it != ownership_ranges_.cbegin()) {
    auto it_pre = std::prev(it);
    if (it_pre->second.end_tiles > base) {
      it = it_pre;
    }
  }
  for (; it != ownership_ranges_.cend(); ++it) {
    uint32_t range_global_start = std::max(it->first, base);
    if (range_global_start >= resolve_area_end) {
      break;
    }
    RenderTargetKey rt_key = it->second.render_target;
    if (rt_key.IsEmpty()) {
      continue;
    }
    // Merge with other render target ranges with the same current ownership,
    // but different depth ownership, since it's not relevant to resolving.
    while (it != ownership_ranges_.cend()) {
      auto it_next = std::next(it);
      if (it_next == ownership_ranges_.cend() ||
          it_next->first >= resolve_area_end ||
          it_next->second.render_target != rt_key) {
        break;
      }
      it = it_next;
    }

    uint32_t range_local_start = std::max(range_global_start, base) - base;
    uint32_t range_local_end =
        std::min(it->second.end_tiles, resolve_area_end) - base;
    assert_true(range_local_start < range_local_end);

    uint32_t rows_start = range_local_start / pitch;
    uint32_t rows_end = (range_local_end + (pitch - 1)) / pitch;
    uint32_t row_first_start = range_local_start - rows_start * pitch;
    if (row_first_start >= row_length) {
      // The first row starts within the pitch padding.
      if (rows_start + 1 < rows_end) {
        // Multiple rows - start at the second.
        ++rows_start;
        row_first_start = 0;
      } else {
        // Single row - nothing to dump.
        continue;
      }
    }

    auto it_rt = render_targets_.find(rt_key);
    assert_true(it_rt != render_targets_.cend());
    assert_not_null(it_rt->second);
    // Don't include pitch padding in the last row.
    rectangles_out.emplace_back(
        it_rt->second, rows_start, rows_end - rows_start, row_first_start,
        std::min(pitch - (rows_end * pitch - range_local_end), row_length));
  }
}

bool RenderTargetCache::PrepareHostRenderTargetsResolveClear(
    const draw_util::ResolveInfo& resolve_info,
    Transfer::Rectangle& clear_rectangle_out,
    RenderTarget*& depth_render_target_out,
    std::vector<Transfer>& depth_transfers_out,
    RenderTarget*& color_render_target_out,
    std::vector<Transfer>& color_transfers_out) {
  assert_true(GetPath() == Path::kHostRenderTargets);

  uint32_t pitch_tiles_at_32bpp;
  uint32_t base_offset_tiles_at_32bpp;
  xenos::MsaaSamples msaa_samples;
  if (resolve_info.IsClearingDepth()) {
    pitch_tiles_at_32bpp = resolve_info.depth_edram_info.pitch_tiles;
    base_offset_tiles_at_32bpp = resolve_info.depth_edram_info.base_tiles -
                                 resolve_info.depth_original_base;
    msaa_samples = resolve_info.depth_edram_info.msaa_samples;
  } else if (resolve_info.IsClearingColor()) {
    pitch_tiles_at_32bpp = resolve_info.color_edram_info.pitch_tiles;
    base_offset_tiles_at_32bpp = resolve_info.color_edram_info.base_tiles -
                                 resolve_info.color_original_base;
    if (resolve_info.color_edram_info.format_is_64bpp) {
      assert_zero(pitch_tiles_at_32bpp & 1);
      pitch_tiles_at_32bpp >>= 1;
      assert_zero(base_offset_tiles_at_32bpp & 1);
      base_offset_tiles_at_32bpp >>= 1;
    }
    msaa_samples = resolve_info.color_edram_info.msaa_samples;
  } else {
    return false;
  }
  assert_true(msaa_samples <= xenos::MsaaSamples::k4X);
  if (!pitch_tiles_at_32bpp) {
    return false;
  }
  uint32_t msaa_samples_x_log2 =
      uint32_t(msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t msaa_samples_y_log2 =
      uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  if (pitch_tiles_at_32bpp >
      ((xenos::kTexture2DCubeMaxWidthHeight << msaa_samples_x_log2) +
       (xenos::kEdramTileWidthSamples - 1)) /
          xenos::kEdramTileWidthSamples) {
    XELOGE(
        "Surface pitch in 80-sample groups {} at {}x MSAA larger than the "
        "maximum texture width {} specified by the guest in a resolve",
        pitch_tiles_at_32bpp, uint32_t(1) << uint32_t(msaa_samples),
        xenos::kTexture2DCubeMaxWidthHeight);
    return false;
  }
  uint32_t pitch_pixels =
      pitch_tiles_at_32bpp *
      (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2);
  uint32_t pitch_pixels_scaled = pitch_pixels * GetResolutionScale();
  uint32_t max_render_target_width = GetMaxRenderTargetWidth();
  if (pitch_pixels_scaled > max_render_target_width) {
    // TODO(Triang3l): If really needed for some game on some device, clamp the
    // pitch the same way as explained in the comment in Update.
    XELOGE(
        "Surface pitch aligned to EDRAM tiles and resolution-scaled {} larger "
        "than the maximum host render target width {} in a resolve",
        pitch_pixels_scaled, max_render_target_width);
    return false;
  }

  uint32_t render_target_height_pixels =
      GetRenderTargetHeight(pitch_tiles_at_32bpp, msaa_samples);
  uint32_t base_offset_rows_at_32bpp =
      base_offset_tiles_at_32bpp / pitch_tiles_at_32bpp;
  Transfer::Rectangle clear_rectangle;
  clear_rectangle.x_pixels =
      std::min((base_offset_tiles_at_32bpp -
                base_offset_rows_at_32bpp * pitch_tiles_at_32bpp) *
                       (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2) +
                   resolve_info.address.local_x_div_8 * uint32_t(8),
               pitch_pixels);
  clear_rectangle.y_pixels =
      std::min(base_offset_rows_at_32bpp *
                       (xenos::kEdramTileHeightSamples >> msaa_samples_y_log2) +
                   resolve_info.address.local_y_div_8 * uint32_t(8),
               render_target_height_pixels);
  clear_rectangle.width_pixels =
      std::min(resolve_info.address.width_div_8 * uint32_t(8),
               pitch_pixels - clear_rectangle.x_pixels);
  clear_rectangle.height_pixels =
      std::min(resolve_info.address.height_div_8 * uint32_t(8),
               render_target_height_pixels - clear_rectangle.y_pixels);
  if (!clear_rectangle.width_pixels || !clear_rectangle.height_pixels) {
    // Outside the pitch / height (or initially specified as 0).
    return false;
  }

  // Change ownership of the tiles containing the area to be cleared, so the
  // up-to-date host render target for the cleared range will be the cleared
  // one.
  uint32_t clear_start_tiles_at_32bpp =
      ((clear_rectangle.y_pixels << msaa_samples_y_log2) /
       xenos::kEdramTileHeightSamples) *
          pitch_tiles_at_32bpp +
      (clear_rectangle.x_pixels << msaa_samples_x_log2) /
          xenos::kEdramTileWidthSamples;
  uint32_t clear_length_tiles_at_32bpp =
      (((clear_rectangle.y_pixels + clear_rectangle.height_pixels - 1)
        << msaa_samples_y_log2) /
       xenos::kEdramTileHeightSamples) *
          pitch_tiles_at_32bpp +
      ((clear_rectangle.x_pixels + clear_rectangle.width_pixels - 1)
       << msaa_samples_x_log2) /
          xenos::kEdramTileWidthSamples +
      1 - clear_start_tiles_at_32bpp;
  uint32_t depth_clear_start_tiles =
      resolve_info.IsClearingDepth()
          ? std::min(
                resolve_info.depth_original_base + clear_start_tiles_at_32bpp,
                xenos::kEdramTileCount)
          : xenos::kEdramTileCount;
  uint32_t color_clear_start_tiles =
      resolve_info.IsClearingColor()
          ? std::min(resolve_info.color_original_base +
                         (clear_start_tiles_at_32bpp
                          << resolve_info.color_edram_info.format_is_64bpp),
                     xenos::kEdramTileCount)
          : xenos::kEdramTileCount;
  uint32_t depth_clear_end_tiles =
      std::min(depth_clear_start_tiles + clear_length_tiles_at_32bpp,
               xenos::kEdramTileCount);
  uint32_t color_clear_end_tiles =
      std::min(color_clear_start_tiles +
                   (clear_length_tiles_at_32bpp
                    << resolve_info.color_edram_info.format_is_64bpp),
               xenos::kEdramTileCount);
  // Prevent overlap.
  if (depth_clear_start_tiles < color_clear_start_tiles) {
    depth_clear_end_tiles =
        std::min(depth_clear_end_tiles, color_clear_start_tiles);
  } else {
    color_clear_end_tiles =
        std::min(color_clear_end_tiles, depth_clear_start_tiles);
  }

  RenderTargetKey depth_render_target_key;
  RenderTarget* depth_render_target = nullptr;
  if (depth_clear_start_tiles < depth_clear_end_tiles) {
    depth_render_target_key.base_tiles = resolve_info.depth_original_base;
    depth_render_target_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    depth_render_target_key.msaa_samples = msaa_samples;
    depth_render_target_key.is_depth = 1;
    depth_render_target_key.host_relevant_format =
        resolve_info.depth_edram_info.format;
    depth_render_target = GetOrCreateRenderTarget(depth_render_target_key);
    if (!depth_render_target) {
      depth_render_target_key = RenderTargetKey();
      depth_clear_start_tiles = depth_clear_end_tiles;
    }
  }
  RenderTargetKey color_render_target_key;
  RenderTarget* color_render_target = nullptr;
  if (color_clear_start_tiles < color_clear_end_tiles) {
    color_render_target_key.base_tiles = resolve_info.color_original_base;
    color_render_target_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    color_render_target_key.msaa_samples = msaa_samples;
    color_render_target_key.is_depth = 0;
    color_render_target_key.host_relevant_format =
        uint32_t(GetHostRelevantColorFormat(xenos::ColorRenderTargetFormat(
            resolve_info.color_edram_info.format)));
    color_render_target = GetOrCreateRenderTarget(color_render_target_key);
    if (!color_render_target) {
      color_render_target_key = RenderTargetKey();
      color_clear_start_tiles = color_clear_end_tiles;
    }
  }
  if (depth_clear_start_tiles >= depth_clear_end_tiles &&
      color_clear_start_tiles >= color_clear_end_tiles) {
    // The region turned out to be outside the EDRAM, or there's complete
    // overlap, shouldn't be happening. Or failed to create both render targets.
    return false;
  }

  clear_rectangle_out = clear_rectangle;
  depth_render_target_out = depth_render_target;
  depth_transfers_out.clear();
  if (depth_render_target) {
    ChangeOwnership(depth_render_target_key, depth_clear_start_tiles,
                    depth_clear_end_tiles - depth_clear_start_tiles,
                    &depth_transfers_out, &clear_rectangle);
  }
  color_render_target_out = color_render_target;
  color_transfers_out.clear();
  if (color_render_target) {
    ChangeOwnership(color_render_target_key, color_clear_start_tiles,
                    color_clear_end_tiles - color_clear_start_tiles,
                    &color_transfers_out, &clear_rectangle);
  }
  return true;
}

RenderTargetCache::RenderTarget*
RenderTargetCache::PrepareFullEdram1280xRenderTargetForSnapshotRestoration(
    xenos::ColorRenderTargetFormat color_format) {
  assert_true(GetPath() == Path::kHostRenderTargets);
  uint32_t resolution_scale = GetResolutionScale();
  constexpr uint32_t kPitchTilesAt32bpp = 16;
  constexpr uint32_t kWidth =
      kPitchTilesAt32bpp * xenos::kEdramTileWidthSamples;
  if (kWidth * resolution_scale > GetMaxRenderTargetWidth()) {
    return nullptr;
  }
  // Same render target height is used for 32bpp and 64bpp to allow mixing them.
  constexpr uint32_t kHeightTileRows =
      xenos::kEdramTileCount / kPitchTilesAt32bpp;
  static_assert(
      kPitchTilesAt32bpp * kHeightTileRows == xenos::kEdramTileCount,
      "Using width of the render target for EDRAM snapshot restoration that is "
      "expected to result in the last row being fully utilized.");
  constexpr uint32_t kHeight = kHeightTileRows * xenos::kEdramTileHeightSamples;
  static_assert(
      kHeight <= xenos::kTexture2DCubeMaxWidthHeight,
      "Using width of the render target for EDRAM snapshot restoration that is "
      "expect to fully cover the EDRAM without exceeding the maximum guest "
      "render target height.");
  if (kHeight * resolution_scale > GetMaxRenderTargetHeight()) {
    return nullptr;
  }
  RenderTargetKey render_target_key;
  render_target_key.pitch_tiles_at_32bpp = kPitchTilesAt32bpp;
  render_target_key.host_relevant_format = uint32_t(
      GetHostRelevantColorFormat(xenos::GetStorageColorFormat(color_format)));
  RenderTarget* render_target = GetOrCreateRenderTarget(render_target_key);
  if (!render_target) {
    return nullptr;
  }
  // Change ownership, but don't transfer the contents - they will be replaced
  // anyway.
  ownership_ranges_.clear();
  ownership_ranges_.emplace(
      std::piecewise_construct, std::forward_as_tuple(uint32_t(0)),
      std::forward_as_tuple(xenos::kEdramTileCount, render_target_key,
                            RenderTargetKey(), RenderTargetKey()));
  return render_target;
}

void RenderTargetCache::PixelShaderInterlockFullEdramBarrierPlaced() {
  assert_true(GetPath() == Path::kPixelShaderInterlock);
  // Clear ownership - any overlap of data written before the barrier is safe.
  OwnershipRange empty_range(xenos::kEdramTileCount, RenderTargetKey(),
                             RenderTargetKey(), RenderTargetKey());
  if (ownership_ranges_.size() == 1) {
    // Do not reallocate map elements if not needed (either nothing drawn since
    // the last barrier, or all of the EDRAM is owned by one render target).
    // The ownership map contains no gaps - the first element should always be
    // at 0.
    assert_true(!ownership_ranges_.begin()->first);
    OwnershipRange& all_edram_range = ownership_ranges_.begin()->second;
    assert_true(all_edram_range.end_tiles == xenos::kEdramTileCount);
    all_edram_range = empty_range;
    return;
  }
  ownership_ranges_.clear();
  ownership_ranges_.emplace(0, empty_range);
}

RenderTargetCache::RenderTarget* RenderTargetCache::GetOrCreateRenderTarget(
    RenderTargetKey key) {
  assert_true(GetPath() == Path::kHostRenderTargets);
  auto it_rt = render_targets_.find(key);
  RenderTarget* render_target;
  if (it_rt != render_targets_.end()) {
    render_target = it_rt->second;
  } else {
    render_target = CreateRenderTarget(key);
    uint32_t width = key.GetWidth();
    uint32_t height =
        GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples);
    if (render_target) {
      XELOGGPU(
          "Created a {}x{} {}xMSAA {} render target with guest format {} at "
          "EDRAM base {}",
          width, height, uint32_t(1) << uint32_t(key.msaa_samples),
          key.is_depth ? "depth" : "color", key.host_relevant_format,
          key.base_tiles);
    } else {
      XELOGE(
          "Failed to create a {}x{} {}xMSAA {} render target with guest format "
          "{} at EDRAM base {}",
          width, height, uint32_t(1) << uint32_t(key.msaa_samples),
          key.is_depth ? "depth" : "color", key.host_relevant_format,
          key.base_tiles);
    }
    // Insert even if failed to create, not to try to create again.
    render_targets_.emplace(key, render_target);
  }
  return render_target;
}

bool RenderTargetCache::WouldOwnershipChangeRequireTransfers(
    RenderTargetKey dest, uint32_t start_tiles, uint32_t length_tiles) const {
  assert_true(start_tiles >= dest.base_tiles);
  assert_true(length_tiles <= (xenos::kEdramTileCount - start_tiles));
  if (length_tiles == 0) {
    return false;
  }
  bool host_depth_encoding_different =
      dest.is_depth && GetPath() == Path::kHostRenderTargets &&
      IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  // The map contains consecutive ranges, merged if the adjacent ones are the
  // same. Find the range starting at >= the start. A portion of the range
  // preceding it may be intersecting the render target's range (or even fully
  // contain it).
  uint32_t end_tiles = start_tiles + length_tiles;
  auto it = ownership_ranges_.lower_bound(start_tiles);
  if (it != ownership_ranges_.begin()) {
    auto it_pre = std::prev(it);
    if (it_pre->second.end_tiles > start_tiles) {
      it = it_pre;
    }
  }
  for (; it != ownership_ranges_.end(); ++it) {
    if (it->first >= end_tiles) {
      // Outside the touched range already.
      break;
    }
    if (it->second.IsOwnedBy(dest, host_depth_encoding_different)) {
      // Already owned by the needed render target - no need to transfer
      // anything.
      continue;
    }
    RenderTargetKey transfer_source = it->second.render_target;
    // Only perform the transfer when actually changing the latest owner, not
    // just the latest host depth owner - the transfer source is expected to
    // be different than the destination.
    if (!transfer_source.IsEmpty() && transfer_source != dest) {
      return true;
    }
  }
  return false;
}

void RenderTargetCache::ChangeOwnership(
    RenderTargetKey dest, uint32_t start_tiles, uint32_t length_tiles,
    std::vector<Transfer>* transfers_append_out,
    const Transfer::Rectangle* resolve_clear_cutout) {
  assert_true(start_tiles >= dest.base_tiles);
  assert_true(length_tiles <= (xenos::kEdramTileCount - start_tiles));
  if (length_tiles == 0) {
    return;
  }
  uint32_t dest_pitch_tiles = dest.GetPitchTiles();
  bool dest_is_64bpp = dest.Is64bpp();

  bool host_depth_encoding_different =
      dest.is_depth && GetPath() == Path::kHostRenderTargets &&
      IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  // The map contains consecutive ranges, merged if the adjacent ones are the
  // same. Find the range starting at >= the start. A portion of the range
  // preceding it may be intersecting the render target's range (or even fully
  // contain it) - split it into the untouched head and the claimed tail if
  // needed.
  uint32_t end_tiles = start_tiles + length_tiles;
  auto it = ownership_ranges_.lower_bound(start_tiles);
  if (it != ownership_ranges_.begin()) {
    auto it_pre = std::prev(it);
    if (it_pre->second.end_tiles > start_tiles &&
        !it_pre->second.IsOwnedBy(dest, host_depth_encoding_different)) {
      // Different render target overlapping the range - split the head.
      ownership_ranges_.emplace(start_tiles, it_pre->second);
      it_pre->second.end_tiles = start_tiles;
      // Let the next loop do the transfer and needed merging and splitting
      // starting from the added tail.
      it = std::next(it_pre);
    }
  }
  while (it != ownership_ranges_.end()) {
    if (it->first >= end_tiles) {
      // Outside the touched range already.
      break;
    }
    if (it->second.IsOwnedBy(dest, host_depth_encoding_different)) {
      // Already owned by the needed render target - no need to transfer
      // anything.
      ++it;
      continue;
    }
    // Take over the current range. Handle the tail - may be outside the range
    // (split in this case) or within it.
    if (it->second.end_tiles > end_tiles) {
      // Split the tail.
      ownership_ranges_.emplace(end_tiles, it->second);
      it->second.end_tiles = end_tiles;
    }
    if (transfers_append_out) {
      RenderTargetKey transfer_source = it->second.render_target;
      // Only perform the copying when actually changing the latest owner, not
      // just the latest host depth owner - the transfer source is expected to
      // be different than the destination.
      if (!transfer_source.IsEmpty() && transfer_source != dest) {
        uint32_t transfer_end_tiles = std::min(it->second.end_tiles, end_tiles);
        if (!resolve_clear_cutout ||
            Transfer::GetRangeRectangles(it->first, transfer_end_tiles,
                                         dest.base_tiles, dest_pitch_tiles,
                                         dest.msaa_samples, dest_is_64bpp,
                                         nullptr, resolve_clear_cutout)) {
          RenderTargetKey transfer_host_depth_source =
              host_depth_encoding_different
                  ? it->second
                        .host_depth_render_targets[dest.host_relevant_format]
                  : RenderTargetKey();
          if (transfer_host_depth_source == transfer_source) {
            // Same render target, don't provide a separate host depth source.
            transfer_host_depth_source = RenderTargetKey();
          }
          if (!transfers_append_out->empty() &&
              transfers_append_out->back().end_tiles == it->first &&
              transfers_append_out->back().source->key() == transfer_source &&
              ((transfers_append_out->back().host_depth_source == nullptr) ==
               transfer_host_depth_source.IsEmpty()) &&
              (transfer_host_depth_source.IsEmpty() ||
               transfers_append_out->back().host_depth_source->key() ==
                   transfer_host_depth_source)) {
            // Extend the last transfer if, for example, transferring color, but
            // host depth is different.
            transfers_append_out->back().end_tiles = transfer_end_tiles;
          } else {
            auto transfer_source_rt_it = render_targets_.find(transfer_source);
            if (transfer_source_rt_it != render_targets_.end()) {
              assert_not_null(transfer_source_rt_it->second);
              auto transfer_host_depth_source_rt_it =
                  !transfer_host_depth_source.IsEmpty()
                      ? render_targets_.find(transfer_host_depth_source)
                      : render_targets_.end();
              if (transfer_host_depth_source.IsEmpty() ||
                  transfer_host_depth_source_rt_it != render_targets_.end()) {
                assert_false(transfer_host_depth_source_rt_it !=
                                 render_targets_.end() &&
                             !transfer_host_depth_source_rt_it->second);
                transfers_append_out->emplace_back(
                    it->first, transfer_end_tiles,
                    transfer_source_rt_it->second,
                    transfer_host_depth_source_rt_it != render_targets_.end()
                        ? transfer_host_depth_source_rt_it->second
                        : nullptr);
              }
            }
          }
        }
      }
    }
    // Claim the current range.
    it->second.render_target = dest;
    if (host_depth_encoding_different) {
      it->second.host_depth_render_targets[dest.host_relevant_format] = dest;
    }
    // Check if can merge with the next range after claiming.
    std::map<uint32_t, OwnershipRange>::iterator it_next;
    if (it != ownership_ranges_.end()) {
      it_next = std::next(it);
      if (it_next != ownership_ranges_.end() &&
          it_next->second.AreOwnersSame(it->second)) {
        // Merge with the next range.
        it->second.end_tiles = it_next->second.end_tiles;
        auto it_after = std::next(it_next);
        ownership_ranges_.erase(it_next);
        it_next = it_after;
      }
    } else {
      it_next = ownership_ranges_.end();
    }
    // Check if can merge with the previous range after claiming and merging
    // with the next (thus obtaining the correct end pointer).
    if (it != ownership_ranges_.begin()) {
      auto it_prev = std::prev(it);
      if (it_prev->second.AreOwnersSame(it->second)) {
        it_prev->second.end_tiles = it->second.end_tiles;
        ownership_ranges_.erase(it);
      }
    }
    it = it_next;
  }
}

}  // namespace gpu
}  // namespace xe
