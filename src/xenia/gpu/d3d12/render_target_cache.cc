/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/render_target_cache.h"

#include <cmath>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"

namespace xe {
namespace gpu {
namespace d3d12 {

RenderTargetCache::RenderTargetCache(D3D12CommandProcessor* command_processor,
                                     RegisterFile* register_file)
    : command_processor_(command_processor), register_file_(register_file) {}

RenderTargetCache::~RenderTargetCache() { Shutdown(); }

bool RenderTargetCache::Initialize() { return true; }

void RenderTargetCache::Shutdown() { ClearCache(); }

void RenderTargetCache::ClearCache() {
  for (auto render_target_pair : render_targets_) {
    RenderTarget* render_target = render_target_pair.second;
    if (render_target->resource != nullptr) {
      render_target->resource->Release();
    }
    delete render_target;
  }
  render_targets_.clear();

  for (uint32_t i = 0; i < xe::countof(heaps_); ++i) {
    if (heaps_[i] != nullptr) {
      heaps_[i]->Release();
      heaps_[i] = nullptr;
    }
  }
}

void RenderTargetCache::BeginFrame() { ClearBindings(); }

void RenderTargetCache::UpdateRenderTargets() {
  // There are two kinds of render target binding updates in this implementation
  // in case something has been changed - full and partial.
  //
  // A full update involves flushing all the currently bound render targets that
  // have been modified to the EDRAM buffer, allocating all the newly bound
  // render targets in the heaps, loading them from the EDRAM buffer and binding
  // them.
  //
  // ("Bound" here means ever used since the last full update - and in this case
  // it's bound to the Direct3D 12 command list.)
  //
  // However, Banjo-Kazooie interleaves color/depth and depth-only writes every
  // draw call, and doing a full update whenever the color mask is changed is
  // too expensive. So, we shouldn't do a full update if the game only toggles
  // color writes and depth testing. Instead, we're only adding or re-enabling
  // render targets if color writes are being enabled (adding involves loading
  // the contents from the EDRAM, while re-enabling does nothing on the D3D
  // side).
  //
  // There are cases when simply toggling render targets may still require EDRAM
  // stores and thus a full update. Here's an example situation:
  // Draw 1:
  // - 32bpp RT0 0-10 MB
  // - 32bpp RT1 3-10 MB
  // - 1280x720 viewport
  // Draw 2:
  // - 32bpp RT0 0-10 MB
  // - Inactive RT1
  // - 1280x1440 viewport
  // Draw 3:
  // - 32bpp RT0 0-10 MB
  // - 32bpp RT1 3-10 MB
  // - 1280x720 viewport
  // In this case, before draw 2, RT1 must be written to the EDRAM buffer, and
  // RT0 must be loaded, and also before draw 3 RT1 must receive the changes
  // made to the lower part of RT0. So, before draws 2 and 3, full updates must
  // be done.
  //
  // Full updates are better for memory usage than partial updates though, as
  // the render targets are re-allocated in the heaps, which means that they can
  // be allocated more tightly, preventing too many 32 MB heaps from being
  // created.
  //
  // To summarize, a full update happens if:
  // - Starting a new frame.
  // - Drawing after resolving.
  // - Surface pitch changed.
  // - Sample count changed.
  // - EDRAM base of a currently used RT changed.
  // - Format of a currently used RT changed.
  // - Current viewport contains unsaved data from previously used render
  //   targets.
  // - New render target overlaps unsaved data from other bound render targets.
  //
  // "Previously used" and "new" in the last 2 conditions is important so if the
  // game has render targets aliased in the same draw call, there won't be a
  // full update every draw.
  //
  // A partial update happens if:
  // - New render target is added, but doesn't overlap unsaved data from other
  //   currently or previously used render targets.
  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return;
  }

  auto& regs = *register_file_;
  uint32_t rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = std::min(rb_surface_info & 0x3FFF, 2560u);
  if (surface_pitch == 0) {
    assert_always();
    return;
  }
  MsaaSamples msaa_samples = MsaaSamples((rb_surface_info >> 16) & 0x3);
  uint32_t msaa_samples_x = msaa_samples >= MsaaSamples::k4X ? 2 : 1;
  uint32_t msaa_samples_y = msaa_samples >= MsaaSamples::k2X ? 2 : 1;

  // Extract color/depth info in an unified way.
  bool enabled[5];
  uint32_t edram_bases[5];
  uint32_t formats[5];
  bool formats_are_64bpp[5];
  uint32_t rb_color_mask = regs[XE_GPU_REG_RB_COLOR_MASK].u32;
  if (xenos::ModeControl(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7) !=
      xenos::ModeControl::kColorDepth) {
    rb_color_mask = 0;
  }
  uint32_t rb_color_info[4] = {
      regs[XE_GPU_REG_RB_COLOR_INFO].u32, regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
      regs[XE_GPU_REG_RB_COLOR2_INFO].u32, regs[XE_GPU_REG_RB_COLOR3_INFO].u32};
  for (uint32_t i = 0; i < 4; ++i) {
    enabled[i] = (rb_color_mask & (0xF << (i * 4))) != 0;
    edram_bases[i] = std::min(rb_color_info[i] & 0xFFF, 2048u);
    formats[i] = (rb_color_info[i] >> 12) & 0xF;
    formats_are_64bpp[i] =
        IsColorFormat64bpp(ColorRenderTargetFormat(formats[i]));
  }
  uint32_t rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  uint32_t rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  // 0x1 = stencil test, 0x2 = depth test, 0x4 = depth write.
  enabled[4] = (rb_depthcontrol & (0x1 | 0x2 | 0x4)) != 0;
  edram_bases[4] = std::min(rb_depth_info & 0xFFF, 2048u);
  formats[4] = (rb_depth_info >> 12) & 0x1;
  formats_are_64bpp[4] = false;
  // Don't mark depth regions as dirty if not writing the depth.
  bool depth_readonly = (rb_depthcontrol & (0x1 | 0x4)) == 0;

  bool full_update = false;

  // Check the following full update conditions:
  // - Starting a new frame.
  // - Drawing after resolving.
  // - Surface pitch changed.
  // - Sample count changed.
  // Draws are skipped if the surface pitch is 0, so a full update can be forced
  // in the beginning of the frame or after resolves by setting the current
  // pitch to 0.
  if (current_surface_pitch_ != surface_pitch ||
      current_msaa_samples_ != msaa_samples) {
    full_update = true;
  }

  // Get the maximum height of each render target in EDRAM rows to help
  // clamp the dirty region heights.
  uint32_t edram_row_tiles_32bpp = (surface_pitch * msaa_samples_x + 79) / 80;
  uint32_t edram_row_tiles[5];
  uint32_t edram_max_rows[5];
  for (uint32_t i = 0; i < 5; ++i) {
    edram_row_tiles[i] = edram_row_tiles_32bpp * (formats_are_64bpp[i] ? 2 : 1);
    edram_max_rows[i] = (2048 - edram_bases[i]) / edram_row_tiles[i];
  }

  // Get EDRAM usage of the current draw so dirty regions can be calculated.
  // See D3D12CommandProcessor::UpdateFixedFunctionState for more info.
  int16_t window_offset_y =
      (regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32 >> 16) & 0x7FFF;
  if (window_offset_y & 0x4000) {
    window_offset_y |= 0x8000;
  }
  uint32_t pa_cl_vte_cntl = regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
  float viewport_scale_y = (pa_cl_vte_cntl & (1 << 2))
                               ? regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32
                               : 1280.0f;
  float viewport_offset_y = (pa_cl_vte_cntl & (1 << 3))
                                ? regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32
                                : viewport_scale_y;
  if (regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & (1 << 16)) {
    viewport_offset_y += float(window_offset_y);
  }
  uint32_t viewport_bottom = uint32_t(std::max(
      0.0f, std::ceil(viewport_offset_y + std::abs(viewport_scale_y))));
  uint32_t scissor_bottom =
      (regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32 >> 16) & 0x7FFF;
  if (!(regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32 & (1u << 31))) {
    scissor_bottom = std::max(int32_t(scissor_bottom) + window_offset_y, 0);
  }
  uint32_t dirty_bottom =
      std::min(std::min(viewport_bottom, scissor_bottom), 2560u);
  uint32_t edram_rows = (dirty_bottom * msaa_samples_y + 15) >> 4;

  // Check the following full update conditions:
  // - EDRAM base of a currently used RT changed.
  // - Format of a currently used RT changed.
  // Also build a list of render targets to attach in a partial update.
  uint32_t render_targets_to_attach = 0;
  if (!full_update) {
    for (uint32_t i = 0; i < 5; ++i) {
      if (!enabled[i]) {
        continue;
      }
      const RenderTargetBinding& binding = current_bindings_[i];
      if (binding.is_bound) {
        if (binding.edram_base != edram_bases[i] ||
            binding.format != formats[i]) {
          full_update = true;
          break;
        }
      } else {
        render_targets_to_attach |= 1 << i;
      }
    }
  }

  // Check the following full update conditions here:
  // - Current viewport contains unsaved data from previously used render
  //   targets.
  // - New render target overlaps unsaved data from other bound render
  //   targets.
  if (!full_update) {
    for (uint32_t i = 0; i < 5; ++i) {
      const RenderTargetBinding& binding_1 = current_bindings_[i];
      uint32_t edram_length_1;
      if (binding_1.is_bound) {
        if (enabled[i]) {
          continue;
        }
        // Checking if now overlapping a previously used render target.
        // binding_1 is the previously used render target.
        edram_length_1 = binding_1.edram_dirty_length;
      } else {
        if (!(render_targets_to_attach & (1 << i))) {
          continue;
        }
        // Checking if the new render target is overlapping any bound one.
        // binding_1 is the new render target.
        edram_length_1 =
            std::min(edram_rows, edram_max_rows[i]) * edram_row_tiles[i];
      }
      for (uint32_t j = 0; j < 5; ++j) {
        const RenderTargetBinding& binding_2 = current_bindings_[j];
        if (!binding_2.is_bound) {
          continue;
        }
        uint32_t edram_length_2;
        if (binding_1.is_bound) {
          if (!enabled[j]) {
            continue;
          }
          // Checking if now overlapping a previously used render target.
          // binding_2 is a currently used render target.
          edram_length_2 =
              std::min(edram_rows, edram_max_rows[j]) * edram_row_tiles[i];
        } else {
          // Checking if the new render target is overlapping any bound one.
          // binding_2 is another bound render target.
          edram_length_2 = binding_2.edram_dirty_length;
        }
        // Do a full update if there is overlap.
        if (edram_bases[i] < edram_bases[j] + edram_length_2 &&
            edram_bases[i] + edram_length_1 > edram_bases[j]) {
          XELOGGPU("RT Cache: Overlap between %u (%u:%u) and %u (%u:%u)", i,
                   edram_bases[i], edram_bases[i] + edram_length_1 - 1, j,
                   edram_bases[j], edram_bases[j] + edram_length_2 - 1);
          full_update = true;
          break;
        }
      }
      if (full_update) {
        break;
      }
    }
  }

  // If no need to attach any new render targets, update dirty regions and exit.
  if (!full_update && !render_targets_to_attach) {
    for (uint32_t i = 0; i < 5; ++i) {
      if (!enabled[i] || (i == 4 && depth_readonly)) {
        continue;
      }
      RenderTargetBinding& binding = current_bindings_[i];
      binding.edram_dirty_length = std::max(
          binding.edram_dirty_length,
          std::min(edram_rows, edram_max_rows[i]) * edram_row_tiles[i]);
    }
    return;
  }

  // From this point, the function MUST NOT FAIL, otherwise bindings will be
  // left in an incomplete state.

  uint32_t heap_usage[5] = {};
  if (full_update) {
    // Export the currently bound render targets before we ruin the bindings.
    WriteRenderTargetsToEDRAM();

    ClearBindings();
    current_surface_pitch_ = surface_pitch;
    current_msaa_samples_ = msaa_samples;

    // If updating fully, need to reattach all the render targets and allocate
    // from scratch.
    for (uint32_t i = 0; i < 5; ++i) {
      if (enabled[i]) {
        render_targets_to_attach |= 1 << i;
      }
    }
  } else {
    // If updating partially, only need to attach new render targets.
    for (uint32_t i = 0; i < 5; ++i) {
      const RenderTargetBinding& binding = current_bindings_[i];
      if (!binding.is_bound) {
        continue;
      }
      const RenderTarget* render_target = binding.render_target;
      if (render_target != nullptr) {
        // There are no holes between 4 MB pages in each heap.
        heap_usage[render_target->heap_page_first >> 3] +=
            render_target->heap_page_count;
        continue;
      }
    }
  }
  XELOGGPU("RT Cache: %s update - pitch %u, samples %u, RTs to attach %u",
           full_update ? "Full" : "Partial", surface_pitch, msaa_samples,
           render_targets_to_attach);

  // Allocate the new render targets.
  // TODO(Triang3l): Actually allocate them.
  // TODO(Triang3l): Load the contents from the EDRAM.
  // TODO(Triang3l): Bind the render targets to the command list.

  // Write the new bindings and update the dirty regions.
  for (uint32_t i = 0; i < 5; ++i) {
    if (!enabled[i]) {
      continue;
    }
    RenderTargetBinding& binding = current_bindings_[i];
    if (render_targets_to_attach & (1 << i)) {
      binding.is_bound = true;
      binding.edram_base = edram_bases[i];
      binding.edram_dirty_length = 0;
      binding.format = formats[i];
    }
    if (!(i == 4 && depth_readonly)) {
      binding.edram_dirty_length = std::max(
          binding.edram_dirty_length,
          std::min(edram_rows, edram_max_rows[i]) * edram_row_tiles[i]);
    }
  }
}

void RenderTargetCache::EndFrame() {
  WriteRenderTargetsToEDRAM();
  ClearBindings();
}

DXGI_FORMAT RenderTargetCache::GetColorDXGIFormat(
    ColorRenderTargetFormat format) {
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_AS_16_16_16_16:
      return DXGI_FORMAT_R10G10B10A2_UNORM;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ColorRenderTargetFormat::k_16_16:
      return DXGI_FORMAT_R16G16_UNORM;
    case ColorRenderTargetFormat::k_16_16_16_16:
      return DXGI_FORMAT_R16G16B16A16_UNORM;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
      return DXGI_FORMAT_R16G16_FLOAT;
    case ColorRenderTargetFormat::k_32_FLOAT:
      return DXGI_FORMAT_R32_FLOAT;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      return DXGI_FORMAT_R32G32_FLOAT;
    default:
      break;
  }
  return DXGI_FORMAT_UNKNOWN;
}

void RenderTargetCache::ClearBindings() {
  current_surface_pitch_ = 0;
  current_msaa_samples_ = MsaaSamples::k1X;
  std::memset(current_bindings_, 0, sizeof(current_bindings_));
}

void RenderTargetCache::WriteRenderTargetsToEDRAM() {}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
