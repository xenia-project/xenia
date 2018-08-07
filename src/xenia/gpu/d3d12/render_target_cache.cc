/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/render_target_cache.h"

#include <cstring>

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

void RenderTargetCache::UpdateRenderTargets(/* const D3D12_VIEWPORT& viewport,
                                            const D3D12_RECT& scissor */) {
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
  // A partial update happens if:
  // - New render target is added, but doesn't overlap unsaved data from other
  //   currently or previously used render targets.
  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return;
  }

  auto& regs = *register_file_;
  uint32_t rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = rb_surface_info & 0x3FFF;
  MsaaSamples msaa_samples = MsaaSamples((rb_surface_info >> 16) & 0x3);
  uint32_t rb_color_mask = regs[XE_GPU_REG_RB_COLOR_MASK].u32;
  if (xenos::ModeControl(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7) !=
      xenos::ModeControl::kColorDepth) {
    rb_color_mask = 0;
  }
  bool color_enabled[4] = {
      (rb_color_mask & 0xF) != 0, (rb_color_mask & 0xF0) != 0,
      (rb_color_mask & 0xF00) != 0, (rb_color_mask & 0xF000) != 0};
  uint32_t rb_color_info[4] = {
      regs[XE_GPU_REG_RB_COLOR_INFO].u32, regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
      regs[XE_GPU_REG_RB_COLOR2_INFO].u32, regs[XE_GPU_REG_RB_COLOR3_INFO].u32};
  uint32_t rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  uint32_t rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  // 0x1 = stencil test enabled, 0x2 = depth test enabled, 0x4 = depth write enabled.
  bool depth_enabled = (rb_depthcontrol & (0x1 | 0x2 | 0x4)) != 0;

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

  // Check the following full update conditions:
  // - EDRAM base of a currently used RT changed.
  // - Format of a currently used RT changed.
  // TODO(Triang3l): Check the following full update conditions here:
  // - Current viewport contains unsaved data from previously used render
  //   targets.
  uint32_t render_targets_to_attach = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!color_enabled[i]) {
      continue;
    }
    RenderTargetBinding& binding = current_bindings_[i];
    if (binding.is_bound) {
      // TODO(Triang3l): If was inactive, check if overlapping unsaved data now.
      if ((rb_color_info[i] & 0xFFF) != binding.edram_base ||
          ColorRenderTargetFormat((rb_color_info[i] >> 12) & 0xF) !=
              binding.color_format) {
        full_update = true;
      }
    } else {
      render_targets_to_attach |= 1 << i;
    }
  }
  if (depth_enabled) {
    RenderTargetBinding& binding = current_bindings_[4];
    if (binding.is_bound) {
      // TODO(Triang3l): If was inactive, check if overlapping unsaved data now.
      if ((rb_depth_info & 0xFFF) != binding.edram_base ||
          DepthRenderTargetFormat((rb_depth_info >> 12) & 0x1) !=
              binding.depth_format) {
        full_update = true;
      }
    } else {
      render_targets_to_attach |= 1 << 4;
    }
  }

  // TODO(Triang3l): Check the following full update condition here:
  // - New render target overlaps unsaved data from other bound render targets.

  // If no need to attach any new render targets, update activation state, dirty
  // regions and exit early.
  if (!full_update && !render_targets_to_attach) {
    for (uint32_t i = 0; i < 4; ++i) {
      current_bindings_[i].is_active = color_enabled[i];
    }
    current_bindings_[4].is_active = depth_enabled;
    // TODO(Triang3l): Update dirty regions.
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
    for (uint32_t i = 0; i < 4; ++i) {
      if (color_enabled[i]) {
        render_targets_to_attach |= 1 << i;
      }
    }
    if (depth_enabled) {
      render_targets_to_attach |= 1 << 4;
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
  XELOGGPU("RT Cache: %s update! Pitch %u, samples %u, RTs to attach %u.",
           full_update ? "Full" : "Partial", surface_pitch, msaa_samples,
           render_targets_to_attach);

  // Allocate the new render targets.
  // TODO(Triang3l): Actually allocate them.
  // TODO(Triang3l): Load the contents from the EDRAM.
  // TODO(Triang3l): Bind the render targets to the command list.

  // Write the new bindings.
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(render_targets_to_attach & (1 << i))) {
      continue;
    }
    RenderTargetBinding& binding = current_bindings_[i];
    binding.is_bound = true;
    binding.is_active = true;
    binding.edram_base = rb_color_info[i] & 0xFFF;
    binding.color_format =
        ColorRenderTargetFormat((rb_color_info[i] >> 12) & 0xF);
  }
  if (render_targets_to_attach & (1 << 4)) {
    RenderTargetBinding& binding = current_bindings_[4];
    binding.is_bound = true;
    binding.is_active = true;
    binding.edram_base = rb_depth_info & 0xFFF;
    binding.depth_format = DepthRenderTargetFormat((rb_depth_info >> 12) & 0x1);
  }
}

void RenderTargetCache::EndFrame() {
  WriteRenderTargetsToEDRAM();
  ClearBindings();
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
