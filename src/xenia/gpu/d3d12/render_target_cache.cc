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

  while (descriptor_heaps_depth_ != nullptr) {
    auto heap = descriptor_heaps_depth_;
    heap->heap->Release();
    descriptor_heaps_depth_ = heap->previous;
    delete heap;
  }
  while (descriptor_heaps_color_ != nullptr) {
    auto heap = descriptor_heaps_color_;
    heap->heap->Release();
    descriptor_heaps_color_ = heap->previous;
    delete heap;
  }

  for (uint32_t i = 0; i < xe::countof(heaps_); ++i) {
    if (heaps_[i] != nullptr) {
      heaps_[i]->Release();
      heaps_[i] = nullptr;
    }
  }
}

void RenderTargetCache::BeginFrame() { ClearBindings(); }

bool RenderTargetCache::UpdateRenderTargets() {
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
  // Direct3D 12 also requires all render targets to have the same size, so the
  // height is calculated from the EDRAM space available to the last render
  // target available in it. However, to make toggling render targets like in
  // the Banjo-Kazooie case possible, the height may be decreased only in full
  // updates.
  // TODO(Triang3l): Check if it's safe to calculate the smallest EDRAM region
  // without aliasing and use it for the height. This won't work if games
  // actually alias active render targets for some reason.
  //
  // To summarize, a full update happens if:
  // - Starting a new frame.
  // - Drawing after resolving.
  // - Surface pitch changed.
  // - Sample count changed.
  // - Render target is disabled and another render target got more space than
  //   is currently available in the textures.
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
  //   currently or previously used render targets, and it doesn't require a
  //   bigger size.
  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return false;
  }

  auto& regs = *register_file_;
  uint32_t rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = std::min(rb_surface_info & 0x3FFF, 2560u);
  if (surface_pitch == 0) {
    return false;
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
  uint32_t edram_max_rows = UINT32_MAX;
  for (uint32_t i = 0; i < 5; ++i) {
    edram_row_tiles[i] = edram_row_tiles_32bpp * (formats_are_64bpp[i] ? 2 : 1);
    if (enabled[i]) {
      // Direct3D 12 doesn't allow render targets with different sizes, so
      // calculate the height from the render target closest to the end of
      // EDRAM.
      edram_max_rows = std::min(edram_max_rows,
                                (2048 - edram_bases[i]) / edram_row_tiles[i]);
    }
  }
  if (edram_max_rows == 0 || edram_max_rows == UINT32_MAX) {
    // Some render target is totally in the end of EDRAM, or nothing is drawn.
    return false;
  }
  // Don't create render targets larger than x2560.
  edram_max_rows = std::min(edram_max_rows, 160u * msaa_samples_y);
  // Check the following full update conditions:
  // - Render target is disabled and another render target got more space than
  //   is currently available in the textures.
  if (edram_max_rows > current_edram_max_rows_) {
    full_update = true;
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
  uint32_t edram_dirty_rows =
      std::min((dirty_bottom * msaa_samples_y + 15) >> 4, edram_max_rows);

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
        edram_length_1 = edram_dirty_rows * edram_row_tiles[i];
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
          edram_length_2 = edram_dirty_rows * edram_row_tiles[i];
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

  // Need to change the bindings.
  if (full_update || render_targets_to_attach) {
    uint32_t heap_usage[5] = {};
    if (full_update) {
      // Export the currently bound render targets before we ruin the bindings.
      WriteRenderTargetsToEDRAM();

      ClearBindings();
      current_surface_pitch_ = surface_pitch;
      current_msaa_samples_ = msaa_samples;
      current_edram_max_rows_ = edram_max_rows;

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

    auto device =
        command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();

    D3D12_RESOURCE_BARRIER barriers[5];
    uint32_t barrier_count = 0;

    // Allocate new render targets and add them to the bindings list.
    for (uint32_t i = 0; i < 5; ++i) {
      if (!(render_targets_to_attach & (1 << i))) {
        continue;
      }
      RenderTargetBinding& binding = current_bindings_[i];
      binding.is_bound = true;
      binding.edram_base = edram_bases[i];
      binding.edram_dirty_length = 0;
      binding.format = formats[i];
      binding.render_target = nullptr;

      RenderTargetKey key;
      key.width_ss_div_80 = edram_row_tiles_32bpp;
      key.height_ss_div_16 = current_edram_max_rows_;
      key.is_depth = i == 4;
      key.format = formats[i];
      D3D12_RESOURCE_DESC resource_desc;
      if (!GetResourceDesc(key, resource_desc)) {
        // Invalid format.
        continue;
      }

      // Calculate the number of 4 MB pages of 32 MB heaps this RT will use.
      D3D12_RESOURCE_ALLOCATION_INFO allocation_info =
          device->GetResourceAllocationInfo(0, 1, &resource_desc);
      if (allocation_info.SizeInBytes == 0 ||
          allocation_info.SizeInBytes > (32 << 20)) {
        assert_always();
        continue;
      }
      uint32_t heap_page_count =
          (uint32_t(allocation_info.SizeInBytes) + ((4 << 20) - 1)) >> 22;

      // Find the heap page range for this render target.
      uint32_t heap_page_first = UINT32_MAX;
      for (uint32_t j = 0; j < 5; ++j) {
        if (heap_usage[j] + heap_page_count <= 8) {
          heap_page_first = j * 8 + heap_usage[j];
          break;
        }
      }
      if (heap_page_first == UINT32_MAX) {
        assert_always();
        continue;
      }

      // Get the render target.
      binding.render_target = FindOrCreateRenderTarget(key, heap_page_first);
      if (binding.render_target == nullptr) {
        continue;
      }
      heap_usage[heap_page_first >> 3] += heap_page_count;

      // Inform Direct3D that we're reusing the heap for this render target.
      D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Aliasing.pResourceBefore = nullptr;
      barrier.Aliasing.pResourceAfter = binding.render_target->resource;
    }

    if (barrier_count != 0) {
      command_list->ResourceBarrier(barrier_count, barriers);
    }

    barrier_count = 0;

    // Load the contents of the new render targets from the EDRAM buffer and
    // switch their state to RTV/DSV.
    for (uint32_t i = 0; i < 5; ++i) {
      if (!(render_targets_to_attach & (1 << i))) {
        continue;
      }
      RenderTarget* render_target = current_bindings_[i].render_target;
      if (render_target == nullptr) {
        continue;
      }

      // TODO(Triang3l): Load the contents from the EDRAM buffer.

      // After loading from the EDRAM buffer (which may make this render target
      // a copy destination), switch it to RTV/DSV if needed.
      D3D12_RESOURCE_STATES state = i == 4 ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                           : D3D12_RESOURCE_STATE_RENDER_TARGET;
      if (render_target->state != state) {
        D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = render_target->resource;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = render_target->state;
        barrier.Transition.StateAfter = state;
        render_target->state = state;
      }
    }

    if (barrier_count != 0) {
      command_list->ResourceBarrier(barrier_count, barriers);
    }

    // Compress the list of the render target because null RTV descriptors are
    // broken in Direct3D 12 and bind the render targets to the command list.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[4];
    uint32_t rtv_count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      const RenderTargetBinding& binding = current_bindings_[i];
      if (!binding.is_bound || binding.render_target == nullptr) {
        continue;
      }
      rtv_handles[rtv_count] = binding.render_target->handle;
      current_pipeline_render_targets_[rtv_count].guest_render_target = i;
      current_pipeline_render_targets_[rtv_count].format =
          GetColorDXGIFormat(ColorRenderTargetFormat(formats[i]));
      ++rtv_count;
    }
    for (uint32_t i = rtv_count; i < 4; ++i) {
      current_pipeline_render_targets_[i].guest_render_target = i;
      current_pipeline_render_targets_[i].format = DXGI_FORMAT_UNKNOWN;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle;
    const RenderTargetBinding& depth_binding = current_bindings_[4];
    current_pipeline_render_targets_[4].guest_render_target = 4;
    if (depth_binding.is_bound && depth_binding.render_target != nullptr) {
      dsv_handle = &depth_binding.render_target->handle;
      current_pipeline_render_targets_[4].format =
          GetDepthDXGIFormat(DepthRenderTargetFormat(formats[4]));
    } else {
      dsv_handle = nullptr;
      current_pipeline_render_targets_[4].format = DXGI_FORMAT_UNKNOWN;
    }
    command_list->OMSetRenderTargets(rtv_count, rtv_handles, FALSE, dsv_handle);
  }

  // Update the dirty regions.
  for (uint32_t i = 0; i < 5; ++i) {
    if (!enabled[i] || (i == 4 && depth_readonly)) {
      continue;
    }
    RenderTargetBinding& binding = current_bindings_[i];
    if (binding.render_target == nullptr) {
      // Nothing to store to the EDRAM buffer if there was an error.
      continue;
    }
    binding.edram_dirty_length = std::max(
        binding.edram_dirty_length, edram_dirty_rows * edram_row_tiles[i]);
  }

  return true;
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
  current_edram_max_rows_ = 0;
  std::memset(current_bindings_, 0, sizeof(current_bindings_));
}

bool RenderTargetCache::GetResourceDesc(RenderTargetKey key,
                                        D3D12_RESOURCE_DESC& desc) {
  if (key.width_ss_div_80 == 0 || key.height_ss_div_16 == 0) {
    return false;
  }
  DXGI_FORMAT dxgi_format =
      key.is_depth ? GetDepthDXGIFormat(DepthRenderTargetFormat(key.format))
                   : GetColorDXGIFormat(ColorRenderTargetFormat(key.format));
  if (dxgi_format == DXGI_FORMAT_UNKNOWN) {
    return false;
  }
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  // TODO(Triang3l): If real MSAA is added, alignment must be 4 MB.
  desc.Alignment = 0;
  desc.Width = key.width_ss_div_80 * 80;
  desc.Height = key.height_ss_div_16 * 16;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = dxgi_format;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = key.is_depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                            : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return true;
}

RenderTargetCache::RenderTarget* RenderTargetCache::FindOrCreateRenderTarget(
    RenderTargetKey key, uint32_t heap_page_first) {
  assert_true(heap_page_first <= 8 * 5);

  // Try to find an existing render target.
  auto found_range = render_targets_.equal_range(key.value);
  for (auto iter = found_range.first; iter != found_range.second; ++iter) {
    RenderTarget* found_render_target = iter->second;
    if (found_render_target->heap_page_first == heap_page_first) {
      return found_render_target;
    }
  }

  D3D12_RESOURCE_DESC resource_desc;
  if (!GetResourceDesc(key, resource_desc)) {
    return nullptr;
  }

  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Get the number of heap pages needed for the render target.
  D3D12_RESOURCE_ALLOCATION_INFO allocation_info =
      device->GetResourceAllocationInfo(0, 1, &resource_desc);
  uint32_t heap_page_count =
      (uint32_t(allocation_info.SizeInBytes) + ((4 << 20) - 1)) >> 22;
  if (heap_page_count == 0 || (heap_page_first & 7) + heap_page_count > 8) {
    assert_always();
    return nullptr;
  }

  // Create a new descriptor heap if needed, and get a place for the descriptor.
  auto& descriptor_heap =
      key.is_depth ? descriptor_heaps_depth_ : descriptor_heaps_color_;
  if (descriptor_heap == nullptr ||
      descriptor_heap->descriptors_used >= kRenderTargetDescriptorHeapSize) {
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc;
    descriptor_heap_desc.Type = key.is_depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV
                                             : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptor_heap_desc.NumDescriptors = kRenderTargetDescriptorHeapSize;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    descriptor_heap_desc.NodeMask = 0;
    ID3D12DescriptorHeap* new_d3d_descriptor_heap;
    if (FAILED(device->CreateDescriptorHeap(
            &descriptor_heap_desc, IID_PPV_ARGS(&new_d3d_descriptor_heap)))) {
      XELOGE("Failed to create a heap for %u %s buffer descriptors",
             kRenderTargetDescriptorHeapSize, key.is_depth ? "depth" : "color");
      return nullptr;
    }
    RenderTargetDescriptorHeap* new_descriptor_heap =
        new RenderTargetDescriptorHeap;
    new_descriptor_heap->heap = new_d3d_descriptor_heap;
    new_descriptor_heap->start_handle =
        new_d3d_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    new_descriptor_heap->descriptors_used = 0;
    new_descriptor_heap->previous = descriptor_heap;
    descriptor_heap = new_descriptor_heap;
  }

  // Create the memory heap if it doesn't exist yet.
  ID3D12Heap* heap = heaps_[heap_page_first >> 3];
  if (heap == nullptr) {
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = 32 << 20;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    // TODO(Triang3l): If real MSAA is added, alignment must be 4 MB.
    heap_desc.Alignment = 0;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    if (FAILED(device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)))) {
      XELOGE("Failed to create a 32 MB heap for render targets");
      return nullptr;
    }
    heaps_[heap_page_first >> 3] = heap;
  }

  // The first action likely to be done is EDRAM buffer load.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* resource;
  if (FAILED(device->CreatePlacedResource(heap, (heap_page_first & 7) << 22,
                                          &resource_desc, state, nullptr,
                                          IID_PPV_ARGS(&resource)))) {
    XELOGE(
        "Failed to create a placed resource for %ux%u %s render target with "
        "format %u at heap 4 MB pages %u:%u",
        uint32_t(resource_desc.Width), resource_desc.Height,
        key.is_depth ? "depth" : "color", key.format, heap_page_first,
        heap_page_first + heap_page_count - 1);
    return nullptr;
  }

  // Create the descriptor for the render target.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle;
  if (key.is_depth) {
    descriptor_handle.ptr =
        descriptor_heap->start_handle.ptr +
        descriptor_heap->descriptors_used * provider->GetDescriptorSizeDSV();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Format = resource_desc.Format;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.Texture2D.MipSlice = 0;
    device->CreateDepthStencilView(resource, &dsv_desc, descriptor_handle);
  } else {
    descriptor_handle.ptr =
        descriptor_heap->start_handle.ptr +
        descriptor_heap->descriptors_used * provider->GetDescriptorSizeRTV();
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    rtv_desc.Format = resource_desc.Format;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    device->CreateRenderTargetView(resource, &rtv_desc, descriptor_handle);
  }
  ++descriptor_heap->descriptors_used;

  RenderTarget* render_target = new RenderTarget;
  render_target->resource = resource;
  render_target->state = state;
  render_target->handle = descriptor_handle;
  render_target->key = key;
  render_target->heap_page_first = heap_page_first;
  render_target->heap_page_count = heap_page_count;
  render_targets_.insert(std::make_pair(key.value, render_target));
  return render_target;
}

void RenderTargetCache::WriteRenderTargetsToEDRAM() {}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
