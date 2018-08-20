/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/render_target_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/bin/edram_load_color_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_load_color_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_load_color_7e3_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_load_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_load_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_store_color_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_store_color_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_store_color_7e3_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_store_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/bin/edram_store_depth_unorm_cs.h"

const RenderTargetCache::EDRAMLoadStoreModeInfo
    RenderTargetCache::edram_load_store_mode_info_[size_t(
        RenderTargetCache::EDRAMLoadStoreMode::kCount)] = {
        {edram_load_color_32bpp_cs, sizeof(edram_load_color_32bpp_cs),
         L"EDRAM Load 32bpp Color", edram_store_color_32bpp_cs,
         sizeof(edram_store_color_32bpp_cs), L"EDRAM Store 32bpp Color"},
        {edram_load_color_64bpp_cs, sizeof(edram_load_color_64bpp_cs),
         L"EDRAM Load 64bpp Color", edram_store_color_64bpp_cs,
         sizeof(edram_store_color_64bpp_cs), L"EDRAM Store 64bpp Color"},
        {edram_load_color_7e3_cs, sizeof(edram_load_color_7e3_cs),
         L"EDRAM Load 7e3 Color", edram_store_color_7e3_cs,
         sizeof(edram_store_color_7e3_cs), L"EDRAM Store 7e3 Color"},
        {edram_load_depth_unorm_cs, sizeof(edram_load_depth_unorm_cs),
         L"EDRAM Load UNorm Depth", edram_store_depth_unorm_cs,
         sizeof(edram_store_depth_unorm_cs), L"EDRAM Store UNorm Depth"},
        {edram_load_depth_float_cs, sizeof(edram_load_depth_float_cs),
         L"EDRAM Load Float Depth", edram_store_depth_float_cs,
         sizeof(edram_store_depth_float_cs), L"EDRAM Store Float Depth"},
};

RenderTargetCache::RenderTargetCache(D3D12CommandProcessor* command_processor,
                                     RegisterFile* register_file)
    : command_processor_(command_processor), register_file_(register_file) {}

RenderTargetCache::~RenderTargetCache() { Shutdown(); }

bool RenderTargetCache::Initialize() {
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();

  // Create the buffer for reinterpreting EDRAM contents.
  D3D12_RESOURCE_DESC edram_buffer_desc;
  edram_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  edram_buffer_desc.Alignment = 0;
  // First 10 MB is guest pixel data, second 10 MB is 32-bit depth when using
  // D24FS8 so loads/stores don't corrupt multipass rendering.
  edram_buffer_desc.Width = 2 * 2048 * 5120;
  edram_buffer_desc.Height = 1;
  edram_buffer_desc.DepthOrArraySize = 1;
  edram_buffer_desc.MipLevels = 1;
  edram_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
  edram_buffer_desc.SampleDesc.Count = 1;
  edram_buffer_desc.SampleDesc.Quality = 0;
  edram_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  edram_buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12_HEAP_PROPERTIES edram_buffer_heap_properties = {};
  edram_buffer_heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  // The first operation will be a clear.
  edram_buffer_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  if (FAILED(device->CreateCommittedResource(
          &edram_buffer_heap_properties, D3D12_HEAP_FLAG_NONE,
          &edram_buffer_desc, edram_buffer_state_, nullptr,
          IID_PPV_ARGS(&edram_buffer_)))) {
    XELOGE("Failed to create the EDRAM buffer");
    return false;
  }
  edram_buffer_cleared_ = false;

  // Create the root signature for EDRAM buffer load/store.
  D3D12_ROOT_PARAMETER root_parameters[2];
  // Parameter 0 is constants (changed for each render target binding).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameters[0].Constants.ShaderRegister = 0;
  root_parameters[0].Constants.RegisterSpace = 0;
  root_parameters[0].Constants.Num32BitValues =
      sizeof(EDRAMLoadStoreRootConstants) / sizeof(uint32_t);
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is source and target.
  D3D12_DESCRIPTOR_RANGE root_load_store_ranges[2];
  root_load_store_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  root_load_store_ranges[0].NumDescriptors = 1;
  root_load_store_ranges[0].BaseShaderRegister = 0;
  root_load_store_ranges[0].RegisterSpace = 0;
  root_load_store_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  root_load_store_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  root_load_store_ranges[1].NumDescriptors = 1;
  root_load_store_ranges[1].BaseShaderRegister = 0;
  root_load_store_ranges[1].RegisterSpace = 0;
  root_load_store_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[1].DescriptorTable.NumDescriptorRanges = 2;
  root_parameters[1].DescriptorTable.pDescriptorRanges = root_load_store_ranges;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.NumParameters = UINT(xe::countof(root_parameters));
  root_signature_desc.pParameters = root_parameters;
  root_signature_desc.NumStaticSamplers = 0;
  root_signature_desc.pStaticSamplers = nullptr;
  root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ID3DBlob* root_signature_blob;
  ID3DBlob* root_signature_error_blob = nullptr;
  if (FAILED(D3D12SerializeRootSignature(
          &root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1,
          &root_signature_blob, &root_signature_error_blob))) {
    XELOGE("Failed to serialize the EDRAM buffer load/store root signature");
    if (root_signature_error_blob != nullptr) {
      XELOGE("%s", reinterpret_cast<const char*>(
                       root_signature_error_blob->GetBufferPointer()));
      root_signature_error_blob->Release();
    }
    Shutdown();
    return false;
  }
  if (root_signature_error_blob != nullptr) {
    root_signature_error_blob->Release();
  }
  if (FAILED(device->CreateRootSignature(
          0, root_signature_blob->GetBufferPointer(),
          root_signature_blob->GetBufferSize(),
          IID_PPV_ARGS(&edram_load_store_root_signature_)))) {
    XELOGE("Failed to create the EDRAM buffer load/store root signature");
    root_signature_blob->Release();
    Shutdown();
    return false;
  }
  root_signature_blob->Release();

  // Create the load/store pipelines.
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc;
  pipeline_desc.pRootSignature = edram_load_store_root_signature_;
  pipeline_desc.NodeMask = 0;
  pipeline_desc.CachedPSO.pCachedBlob = nullptr;
  pipeline_desc.CachedPSO.CachedBlobSizeInBytes = 0;
  pipeline_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  for (uint32_t i = 0; i < uint32_t(EDRAMLoadStoreMode::kCount); ++i) {
    // Load.
    const EDRAMLoadStoreModeInfo& mode_info = edram_load_store_mode_info_[i];
    pipeline_desc.CS.pShaderBytecode = mode_info.load_shader;
    pipeline_desc.CS.BytecodeLength = mode_info.load_shader_size;
    if (FAILED(device->CreateComputePipelineState(
            &pipeline_desc, IID_PPV_ARGS(&edram_load_pipelines_[i])))) {
      XELOGE("Failed to create EDRAM load pipeline for mode %u", i);
      Shutdown();
      return false;
    }
    edram_load_pipelines_[i]->SetName(mode_info.load_pipeline_name);
    // Store.
    pipeline_desc.CS.pShaderBytecode = mode_info.store_shader;
    pipeline_desc.CS.BytecodeLength = mode_info.store_shader_size;
    if (FAILED(device->CreateComputePipelineState(
            &pipeline_desc, IID_PPV_ARGS(&edram_store_pipelines_[i])))) {
      XELOGE("Failed to create EDRAM store pipeline for mode %u", i);
      Shutdown();
      return false;
    }
    edram_store_pipelines_[i]->SetName(mode_info.store_pipeline_name);
  }

  return true;
}

void RenderTargetCache::Shutdown() {
  ClearCache();

  for (uint32_t i = 0; i < uint32_t(EDRAMLoadStoreMode::kCount); ++i) {
    if (edram_load_pipelines_[i] != nullptr) {
      edram_load_pipelines_[i]->Release();
      edram_load_pipelines_[i] = nullptr;
    }
    if (edram_store_pipelines_[i] != nullptr) {
      edram_store_pipelines_[i]->Release();
      edram_store_pipelines_[i] = nullptr;
    }
  }
  if (edram_load_store_root_signature_ != nullptr) {
    edram_load_store_root_signature_->Release();
    edram_load_store_root_signature_ = nullptr;
  }

  if (edram_buffer_ != nullptr) {
    edram_buffer_->Release();
    edram_buffer_ = nullptr;
  }
}

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

void RenderTargetCache::BeginFrame() {
  ClearBindings();

  // TODO(Triang3l): Clear the EDRAM buffer if this is the first frame for a
  // stable D24F==D32F comparison.
}

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

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

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
    formats[i] = (rb_color_info[i] >> 16) & 0xF;
    formats_are_64bpp[i] =
        IsColorFormat64bpp(ColorRenderTargetFormat(formats[i]));
  }
  uint32_t rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  uint32_t rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  // 0x1 = stencil test, 0x2 = depth test, 0x4 = depth write.
  enabled[4] = (rb_depthcontrol & (0x1 | 0x2 | 0x4)) != 0;
  edram_bases[4] = std::min(rb_depth_info & 0xFFF, 2048u);
  formats[4] = (rb_depth_info >> 16) & 0x1;
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
                                : std::abs(viewport_scale_y);
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
      uint32_t edram_dirty_rows_1;
      if (binding_1.is_bound) {
        if (enabled[i]) {
          continue;
        }
        // Checking if now overlapping a previously used render target.
        // binding_1 is the previously used render target.
        edram_dirty_rows_1 = binding_1.edram_dirty_rows;
      } else {
        if (!(render_targets_to_attach & (1 << i))) {
          continue;
        }
        // Checking if the new render target is overlapping any bound one.
        // binding_1 is the new render target.
        edram_dirty_rows_1 = edram_dirty_rows;
      }
      for (uint32_t j = 0; j < 5; ++j) {
        const RenderTargetBinding& binding_2 = current_bindings_[j];
        if (!binding_2.is_bound) {
          continue;
        }
        uint32_t edram_dirty_rows_2;
        if (binding_1.is_bound) {
          if (!enabled[j]) {
            continue;
          }
          // Checking if now overlapping a previously used render target.
          // binding_2 is a currently used render target.
          edram_dirty_rows_2 = edram_dirty_rows;
        } else {
          // Checking if the new render target is overlapping any bound one.
          // binding_2 is another bound render target.
          edram_dirty_rows_2 = binding_2.edram_dirty_rows;
        }
        // Do a full update if there is overlap.
        if (edram_bases[i] <
                edram_bases[j] + edram_dirty_rows_2 * edram_row_tiles[j] &&
            edram_bases[j] <
                edram_bases[i] + edram_dirty_rows_1 * edram_row_tiles[i]) {
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
      StoreRenderTargetsToEDRAM();

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
      binding.edram_dirty_rows = 0;
      binding.format = formats[i];
      binding.render_target = nullptr;

      RenderTargetKey key;
      key.width_ss_div_80 = edram_row_tiles_32bpp;
      key.height_ss_div_16 = current_edram_max_rows_;
      key.is_depth = i == 4 ? 1 : 0;
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

    // Load the contents of the new render targets from the EDRAM buffer (will
    // change the state of the render targets to copy destination).
    RenderTarget* load_render_targets[5];
    uint32_t load_edram_bases[5];
    uint32_t load_render_target_count = 0;
    for (uint32_t i = 0; i < 5; ++i) {
      if (!(render_targets_to_attach & (1 << i))) {
        continue;
      }
      RenderTarget* render_target = current_bindings_[i].render_target;
      if (render_target == nullptr) {
        continue;
      }
      load_render_targets[load_render_target_count] = render_target;
      load_edram_bases[load_render_target_count] = edram_bases[i];
      ++load_render_target_count;
    }
    if (load_render_target_count != 0) {
      LoadRenderTargetsFromEDRAM(load_render_target_count, load_render_targets,
                                 load_edram_bases);
    }

    // Transition the render targets to the appropriate state if needed,
    // compress the list of the render target because null RTV descriptors are
    // broken in Direct3D 12 and bind the render targets to the command list.
    barrier_count = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[4];
    uint32_t rtv_count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      const RenderTargetBinding& binding = current_bindings_[i];
      RenderTarget* render_target = binding.render_target;
      if (!binding.is_bound || render_target == nullptr) {
        continue;
      }
      if (render_target->state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = render_target->resource;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = render_target->state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        render_target->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      rtv_handles[rtv_count] = render_target->handle;
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
    RenderTarget* depth_render_target = depth_binding.render_target;
    current_pipeline_render_targets_[4].guest_render_target = 4;
    if (depth_binding.is_bound && depth_render_target != nullptr) {
      if (depth_render_target->state != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = depth_render_target->resource;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = depth_render_target->state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depth_render_target->state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      }
      dsv_handle = &depth_binding.render_target->handle;
      current_pipeline_render_targets_[4].format =
          GetDepthDXGIFormat(DepthRenderTargetFormat(formats[4]));
    } else {
      dsv_handle = nullptr;
      current_pipeline_render_targets_[4].format = DXGI_FORMAT_UNKNOWN;
    }
    if (barrier_count != 0) {
      command_list->ResourceBarrier(barrier_count, barriers);
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
    binding.edram_dirty_rows =
        std::max(binding.edram_dirty_rows, edram_dirty_rows);
  }

  return true;
}

bool RenderTargetCache::Resolve(SharedMemory* shared_memory, Memory* memory) {
  // Save the currently bound render targets to the EDRAM buffer that will be
  // used as the resolve source and clear bindings to allow render target
  // resources to be reused as source textures for format conversion, resolving
  // samples, to let format conversion bind other render targets, and so after a
  // clear new data will be loaded.
  StoreRenderTargetsToEDRAM();
  ClearBindings();

  auto& regs = *register_file_;

  // Get the render target properties.
  uint32_t rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = std::min(rb_surface_info & 0x3FFF, 2560u);
  if (surface_pitch == 0) {
    // Nothing to copy or clear.
    return true;
  }
  MsaaSamples msaa_samples = MsaaSamples((rb_surface_info >> 16) & 0x3);
  uint32_t rb_copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  uint32_t surface_index = rb_copy_control & 0x7;
  if (surface_index > 4) {
    assert_always();
    return false;
  }
  uint32_t surface_is_depth = surface_index == 4;
  uint32_t surface_edram_base;
  uint32_t surface_format;
  bool surface_format_64bpp;
  if (surface_is_depth) {
    uint32_t rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
    surface_edram_base = rb_depth_info & 0xFFF;
    surface_format = (rb_depth_info >> 16) & 0x1;
    surface_format_64bpp = false;
  } else {
    uint32_t rb_color_info;
    switch (surface_index) {
      case 1:
        rb_color_info = regs[XE_GPU_REG_RB_COLOR1_INFO].u32;
        break;
      case 2:
        rb_color_info = regs[XE_GPU_REG_RB_COLOR2_INFO].u32;
        break;
      case 3:
        rb_color_info = regs[XE_GPU_REG_RB_COLOR3_INFO].u32;
        break;
      default:
        rb_color_info = regs[XE_GPU_REG_RB_COLOR_INFO].u32;
        break;
    }
    surface_edram_base = rb_color_info & 0xFFF;
    surface_format = (rb_color_info >> 16) & 0xF;
    surface_format_64bpp =
        IsColorFormat64bpp(ColorRenderTargetFormat(surface_format));
  }
  if (surface_edram_base >= 2048) {
    // The surface is totally outside of EDRAM - shouldn't happen.
    return false;
  }
  // Calculate the maximum number of rows to clamp the source rectangle.
  uint32_t surface_pitch_ss =
      surface_pitch * (msaa_samples >= MsaaSamples::k4X ? 2 : 1);
  uint32_t surface_pitch_tiles =
      (surface_pitch_ss + 79) / 80 * (surface_format_64bpp ? 2 : 1);
  uint32_t surface_edram_max_rows =
      (2048 - surface_edram_base) / surface_pitch_tiles;
  if (surface_edram_max_rows == 0) {
    // The surface is too close to the end of EDRAM.
    return true;
  }
  uint32_t surface_max_height =
      surface_edram_max_rows * (msaa_samples >= MsaaSamples::k2X ? 8 : 16);

  // Get the resolve region since both copying and clearing need it.
  // HACK: Vertices to use are always in vf0.
  auto fetch_group = reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(
      &regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]);
  const auto& fetch = fetch_group->vertex_fetch_0;
  assert_true(fetch.type == 3);
  assert_true(fetch.endian == 2);
  assert_true(fetch.size == 6);
  const uint8_t* src_vertex_address =
      memory->TranslatePhysical(fetch.address << 2);
  float src_vertices[6];
  // Most vertices have a negative half pixel offset applied, which we reverse.
  float src_vertex_offset =
      (regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32 & 0x1) ? 0.0f : 0.5f;
  for (uint32_t i = 0; i < 6; ++i) {
    src_vertices[i] =
        xenos::GpuSwap(xe::load<float>(src_vertex_address + i * sizeof(float)),
                       Endian(fetch.endian)) +
        src_vertex_offset;
  }
  // Xenos only supports rectangle copies (luckily).
  D3D12_RECT src_rect;
  src_rect.left = LONG(
      std::min(std::min(src_vertices[0], src_vertices[2]), src_vertices[4]));
  src_rect.right = LONG(
      std::max(std::max(src_vertices[0], src_vertices[2]), src_vertices[4]));
  src_rect.top = LONG(
      std::min(std::min(src_vertices[1], src_vertices[3]), src_vertices[5]));
  src_rect.bottom = LONG(
      std::max(std::max(src_vertices[1], src_vertices[3]), src_vertices[5]));
  if (regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & (1 << 16)) {
    uint32_t pa_sc_window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
    int16_t window_offset_x = pa_sc_window_offset & 0x7FFF;
    int16_t window_offset_y = (pa_sc_window_offset >> 16) & 0x7FFF;
    if (window_offset_x & 0x4000) {
      window_offset_x |= 0x8000;
    }
    if (window_offset_y & 0x4000) {
      window_offset_y |= 0x8000;
    }
    src_rect.left += window_offset_x;
    src_rect.right += window_offset_x;
    src_rect.top += window_offset_y;
    src_rect.bottom += window_offset_y;
  }
  src_rect.right = std::min(src_rect.right, LONG(surface_pitch));
  src_rect.bottom = std::min(src_rect.bottom, LONG(surface_max_height));
  if (src_rect.right <= 0 || src_rect.bottom <= 0 ||
      src_rect.right <= src_rect.left || src_rect.bottom <= src_rect.top) {
    // Totally off screen or empty - nothing to copy.
    return true;
  }
  src_rect.left = std::max(src_rect.left, LONG(0));
  src_rect.top = std::max(src_rect.top, LONG(0));

  XELOGGPU(
      "Resolving (%d,%d)->(%d,%d) of RT %u (pitch %u, %u sample%s, format "
      "%u) at %u",
      src_rect.left, src_rect.top, src_rect.right, src_rect.bottom,
      surface_index, surface_pitch, 1 << uint32_t(msaa_samples),
      msaa_samples != MsaaSamples::k1X ? "s" : "", surface_format,
      surface_edram_base);

  // TODO(Triang3l): Copy and clear.
  return true;
}

void RenderTargetCache::EndFrame() {
  StoreRenderTargetsToEDRAM();
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

  // Get the layout for copying to the EDRAM buffer.
  RenderTarget* render_target = new RenderTarget;
  render_target->resource = resource;
  render_target->state = state;
  render_target->handle = descriptor_handle;
  render_target->key = key;
  render_target->heap_page_first = heap_page_first;
  render_target->heap_page_count = heap_page_count;
  UINT64 copy_buffer_size;
  device->GetCopyableFootprints(&resource_desc, 0, key.is_depth ? 2 : 1, 0,
                                render_target->footprints, nullptr, nullptr,
                                &copy_buffer_size);
  render_target->copy_buffer_size = uint32_t(copy_buffer_size);
  render_targets_.insert(std::make_pair(key.value, render_target));
  XELOGGPU(
      "Created %ux%u %s render target with format %u at heap 4 MB pages %u:%u",
      uint32_t(resource_desc.Width), resource_desc.Height,
      key.is_depth ? "depth" : "color", key.format, heap_page_first,
      heap_page_first + heap_page_count - 1);
  return render_target;
}

RenderTargetCache::EDRAMLoadStoreMode RenderTargetCache::GetLoadStoreMode(
    bool is_depth, uint32_t format) {
  if (is_depth) {
    return DepthRenderTargetFormat(format) == DepthRenderTargetFormat::kD24FS8
               ? EDRAMLoadStoreMode::kDepthFloat
               : EDRAMLoadStoreMode::kDepthUnorm;
  }
  ColorRenderTargetFormat color_format = ColorRenderTargetFormat(format);
  if (color_format == ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
      color_format ==
          ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16) {
    return EDRAMLoadStoreMode::kColor7e3;
  }
  return IsColorFormat64bpp(color_format) ? EDRAMLoadStoreMode::kColor64bpp
                                          : EDRAMLoadStoreMode::kColor32bpp;
}

void RenderTargetCache::StoreRenderTargetsToEDRAM() {
  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return;
  }

  uint32_t store_bindings[5];
  uint32_t store_binding_count = 0;

  // 6 for 5 render targets + the EDRAM buffer.
  D3D12_RESOURCE_BARRIER barriers[6];
  uint32_t barrier_count;

  // Extract only the render targets that need to be stored, transition them to
  // copy sources and calculate copy buffer size.
  uint32_t copy_buffer_size = 0;
  barrier_count = 0;
  for (uint32_t i = 0; i < 5; ++i) {
    const RenderTargetBinding& binding = current_bindings_[i];
    RenderTarget* render_target = binding.render_target;
    if (!binding.is_bound || render_target == nullptr ||
        binding.edram_dirty_rows < 0) {
      continue;
    }
    store_bindings[store_binding_count] = i;
    copy_buffer_size =
        std::max(copy_buffer_size, render_target->copy_buffer_size);
    ++store_binding_count;
    if (render_target->state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
      D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = render_target->resource;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore = render_target->state;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      render_target->state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
  }
  if (store_binding_count == 0) {
    return;
  }
  if (edram_buffer_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    // Also transition the EDRAM buffer to UAV.
    D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = edram_buffer_;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = edram_buffer_state_;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    edram_buffer_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if (barrier_count != 0) {
    command_list->ResourceBarrier(barrier_count, barriers);
  }

  // Allocate descriptors for the buffers.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    return;
  }

  // Get the buffer for copying.
  D3D12_RESOURCE_STATES copy_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* copy_buffer = command_processor_->RequestScratchGPUBuffer(
      copy_buffer_size, copy_buffer_state);
  if (copy_buffer == nullptr) {
    return;
  }

  // Prepare for storing.
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  auto descriptor_size_view = provider->GetDescriptorSizeView();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
  srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Buffer.FirstElement = 0;
  srv_desc.Buffer.NumElements = copy_buffer_size >> 2;
  srv_desc.Buffer.StructureByteStride = 0;
  srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(copy_buffer, &srv_desc,
                                   descriptor_cpu_start);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
  uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = 2 * 2048 * 1280;
  uav_desc.Buffer.StructureByteStride = 0;
  uav_desc.Buffer.CounterOffsetInBytes = 0;
  uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu_handle;
  uav_cpu_handle.ptr = descriptor_cpu_start.ptr + descriptor_size_view;
  device->CreateUnorderedAccessView(edram_buffer_, nullptr, &uav_desc,
                                    uav_cpu_handle);
  command_list->SetComputeRootSignature(edram_load_store_root_signature_);
  command_list->SetComputeRootDescriptorTable(1, descriptor_gpu_start);

  // Sort the bindings in ascending order of EDRAM base so data in the render
  // targets placed farther in EDRAM isn't lost in case of overlap.
  std::sort(
      store_bindings, store_bindings + store_binding_count,
      [this](uint32_t a, uint32_t b) {
        if (current_bindings_[a].edram_base < current_bindings_[b].edram_base) {
          return true;
        }
        return a < b;
      });

  // Calculate the dispatch width.
  uint32_t surface_pitch_ss =
      current_surface_pitch_ *
      (current_msaa_samples_ >= MsaaSamples::k4X ? 2 : 1);
  uint32_t surface_pitch_tiles = (surface_pitch_ss + 79) / 80;
  assert_true(surface_pitch_tiles != 0);

  // Store each render target.
  for (uint32_t i = 0; i < store_binding_count; ++i) {
    const RenderTargetBinding& binding = current_bindings_[store_bindings[i]];
    const RenderTarget* render_target = binding.render_target;
    bool is_64bpp = false;

    // Copy from the render target planes and set up the layout.
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = render_target->resource;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_source.SubresourceIndex = 0;
    location_dest.pResource = copy_buffer;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_dest.PlacedFootprint = render_target->footprints[0];
    // TODO(Triang3l): Box for color render targets.
    command_list->CopyTextureRegion(&location_dest, 0, 0, 0, &location_source,
                                    nullptr);
    EDRAMLoadStoreRootConstants root_constants;
    root_constants.base_tiles = binding.edram_base;
    root_constants.pitch_tiles = surface_pitch_tiles;
    if (!render_target->key.is_depth &&
        IsColorFormat64bpp(
            ColorRenderTargetFormat(render_target->key.format))) {
      root_constants.pitch_tiles *= 2;
    }
    root_constants.rt_color_depth_pitch =
        location_dest.PlacedFootprint.Footprint.RowPitch;
    if (render_target->key.is_depth) {
      location_source.SubresourceIndex = 1;
      location_dest.PlacedFootprint = render_target->footprints[1];
      command_list->CopyTextureRegion(&location_dest, 0, 0, 0, &location_source,
                                      nullptr);
      root_constants.rt_stencil_offset =
          uint32_t(location_dest.PlacedFootprint.Offset);
      root_constants.rt_stencil_pitch =
          location_dest.PlacedFootprint.Footprint.RowPitch;
    }

    // Transition the copy buffer to SRV.
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = copy_buffer;
    barriers[0].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    copy_buffer_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_list->ResourceBarrier(1, barriers);

    // Store the data.
    command_list->SetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    EDRAMLoadStoreMode mode = GetLoadStoreMode(render_target->key.is_depth,
                                               render_target->key.format);
    command_processor_->SetPipeline(edram_store_pipelines_[size_t(mode)]);
    command_list->Dispatch(root_constants.pitch_tiles, binding.edram_dirty_rows,
                           1);

    // Commit the UAV write and prepare for copying again.
    barrier_count = 1;
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].UAV.pResource = edram_buffer_;
    if (i + 1 < store_binding_count) {
      barrier_count = 2;
      barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barriers[1].Transition.pResource = copy_buffer;
      barriers[1].Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barriers[1].Transition.StateBefore =
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      copy_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    command_list->ResourceBarrier(barrier_count, barriers);
  }

  command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
}

void RenderTargetCache::LoadRenderTargetsFromEDRAM(
    uint32_t render_target_count, RenderTarget* const* render_targets,
    const uint32_t* edram_bases) {
  assert_true(render_target_count <= 5);
  if (render_target_count == 0 || render_target_count > 5) {
    return;
  }

  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return;
  }

  // 6 for 5 render targets + the EDRAM buffer.
  D3D12_RESOURCE_BARRIER barriers[6];
  uint32_t barrier_count;

  // Transition the render targets to copy destinations and calculate copy
  // buffer size.
  uint32_t copy_buffer_size = 0;
  barrier_count = 0;
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* render_target = render_targets[i];
    copy_buffer_size =
        std::max(copy_buffer_size, render_target->copy_buffer_size);
    if (render_target->state != D3D12_RESOURCE_STATE_COPY_DEST) {
      D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = render_target->resource;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore = render_target->state;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      render_target->state = D3D12_RESOURCE_STATE_COPY_DEST;
    }
  }
  if (edram_buffer_state_ != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
    // Also transition the EDRAM buffer to SRV.
    D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count++];
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = edram_buffer_;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = edram_buffer_state_;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    edram_buffer_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  if (barrier_count != 0) {
    command_list->ResourceBarrier(barrier_count, barriers);
  }

  // Allocate descriptors for the buffers.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    return;
  }

  // Get the buffer for copying.
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_->RequestScratchGPUBuffer(
      copy_buffer_size, copy_buffer_state);
  if (copy_buffer == nullptr) {
    return;
  }

  // Prepare for loading.
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  auto descriptor_size_view = provider->GetDescriptorSizeView();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
  srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Buffer.FirstElement = 0;
  srv_desc.Buffer.NumElements = 2 * 2048 * 1280;
  srv_desc.Buffer.StructureByteStride = 0;
  srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(edram_buffer_, &srv_desc,
                                   descriptor_cpu_start);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
  uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = copy_buffer_size >> 2;
  uav_desc.Buffer.StructureByteStride = 0;
  uav_desc.Buffer.CounterOffsetInBytes = 0;
  uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu_handle;
  uav_cpu_handle.ptr = descriptor_cpu_start.ptr + descriptor_size_view;
  device->CreateUnorderedAccessView(copy_buffer, nullptr, &uav_desc,
                                    uav_cpu_handle);
  command_list->SetComputeRootSignature(edram_load_store_root_signature_);
  command_list->SetComputeRootDescriptorTable(1, descriptor_gpu_start);

  // Load each render target.
  for (uint32_t i = 0; i < render_target_count; ++i) {
    if (edram_bases[i] >= 2048) {
      // Something is wrong with the resolve.
      return;
    }
    const RenderTarget* render_target = render_targets[i];

    // Set up the layout.
    EDRAMLoadStoreRootConstants root_constants;
    root_constants.base_tiles = edram_bases[i];
    root_constants.pitch_tiles = render_target->key.width_ss_div_80;
    if (!render_target->key.is_depth &&
        IsColorFormat64bpp(
            ColorRenderTargetFormat(render_target->key.format))) {
      root_constants.pitch_tiles *= 2;
    }
    root_constants.rt_color_depth_pitch =
        render_target->footprints[0].Footprint.RowPitch;
    if (render_target->key.is_depth) {
      root_constants.rt_stencil_offset =
          uint32_t(render_target->footprints[1].Offset);
      root_constants.rt_stencil_pitch =
          render_target->footprints[1].Footprint.RowPitch;
    }

    // Validate the height in case the resolve is somehow too large (shouldn't
    // happen though, but who knows what games do).
    uint32_t edram_rows =
        std::min(render_target->key.height_ss_div_16,
                 (2048u - edram_bases[i]) / root_constants.pitch_tiles);
    if (edram_rows == 0) {
      continue;
    }

    // Transition the copy buffer back to UAV if it's not the first load.
    if (copy_buffer_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
      barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barriers[0].Transition.pResource = copy_buffer;
      barriers[0].Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barriers[0].Transition.StateBefore = copy_buffer_state;
      barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      copy_buffer_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      command_list->ResourceBarrier(1, barriers);
    }

    // Load the data.
    command_list->SetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    EDRAMLoadStoreMode mode = GetLoadStoreMode(render_target->key.is_depth,
                                               render_target->key.format);
    command_processor_->SetPipeline(edram_load_pipelines_[size_t(mode)]);
    command_list->Dispatch(root_constants.pitch_tiles, edram_rows, 1);

    // Commit the UAV write and transition the copy buffer to copy source.
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].UAV.pResource = copy_buffer;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].Transition.pResource = copy_buffer;
    barriers[1].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list->ResourceBarrier(2, barriers);

    // Copy to the render target planes.
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = copy_buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = render_target->footprints[0];
    location_dest.pResource = render_target->resource;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    command_list->CopyTextureRegion(&location_dest, 0, 0, 0, &location_source,
                                    nullptr);
    if (render_target->key.is_depth) {
      location_source.PlacedFootprint = render_target->footprints[1];
      location_dest.SubresourceIndex = 1;
      command_list->CopyTextureRegion(&location_dest, 0, 0, 0, &location_source,
                                      nullptr);
    }
  }

  command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
