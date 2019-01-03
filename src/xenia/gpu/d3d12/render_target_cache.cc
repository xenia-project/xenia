/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/render_target_cache.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/ui/d3d12/d3d12_util.h"

DEFINE_bool(d3d12_resolution_scale_resolve_edge_clamp, true,
            "When using resolution scale, apply the hack that duplicates the "
            "right/lower subpixel in the left and top sides of render target "
            "resolve areas to eliminate the gap caused by half-pixel offset "
            "(this is necessary for certain games like GTA IV to work).");
DECLARE_bool(d3d12_half_pixel_offset);

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/edram_clear_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_clear_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_clear_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_32bpp_2x_resolve_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_64bpp_2x_resolve_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_7e3_2x_resolve_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_7e3_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_color_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_color_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_color_7e3_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_tile_sample_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_tile_sample_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_ps.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_vs.h"

#if 0
constexpr uint32_t RenderTargetCache::kHeap4MBPages;
#endif
constexpr uint32_t RenderTargetCache::kRenderTargetDescriptorHeapSize;

const RenderTargetCache::EDRAMLoadStoreModeInfo
    RenderTargetCache::edram_load_store_mode_info_[size_t(
        RenderTargetCache::EDRAMLoadStoreMode::kCount)] = {
        {edram_load_color_32bpp_cs, sizeof(edram_load_color_32bpp_cs),
         L"EDRAM Load 32bpp Color", edram_store_color_32bpp_cs,
         sizeof(edram_store_color_32bpp_cs), L"EDRAM Store 32bpp Color",
         edram_load_color_32bpp_2x_resolve_cs,
         sizeof(edram_load_color_32bpp_2x_resolve_cs),
         L"EDRAM Load 32bpp Color for 2x Resolve"},
        {edram_load_color_64bpp_cs, sizeof(edram_load_color_64bpp_cs),
         L"EDRAM Load 64bpp Color", edram_store_color_64bpp_cs,
         sizeof(edram_store_color_64bpp_cs), L"EDRAM Store 64bpp Color",
         edram_load_color_64bpp_2x_resolve_cs,
         sizeof(edram_load_color_64bpp_2x_resolve_cs),
         L"EDRAM Load 64bpp Color for 2x Resolve"},
        {edram_load_color_7e3_cs, sizeof(edram_load_color_7e3_cs),
         L"EDRAM Load 7e3 Color", edram_store_color_7e3_cs,
         sizeof(edram_store_color_7e3_cs), L"EDRAM Store 7e3 Color",
         edram_load_color_7e3_2x_resolve_cs,
         sizeof(edram_load_color_7e3_2x_resolve_cs),
         L"EDRAM Load 7e3 Color for 2x Resolve"},
        {edram_load_depth_unorm_cs, sizeof(edram_load_depth_unorm_cs),
         L"EDRAM Load UNorm Depth", edram_store_depth_unorm_cs,
         sizeof(edram_store_depth_unorm_cs), L"EDRAM Store UNorm Depth",
         nullptr, 0, nullptr},
        {edram_load_depth_float_cs, sizeof(edram_load_depth_float_cs),
         L"EDRAM Load Float Depth", edram_store_depth_float_cs,
         sizeof(edram_store_depth_float_cs), L"EDRAM Store Float Depth",
         nullptr, 0, nullptr},
};

RenderTargetCache::RenderTargetCache(D3D12CommandProcessor* command_processor,
                                     RegisterFile* register_file)
    : command_processor_(command_processor), register_file_(register_file) {}

RenderTargetCache::~RenderTargetCache() { Shutdown(); }

bool RenderTargetCache::Initialize(const TextureCache* texture_cache) {
  // EDRAM buffer size depends on this.
  resolution_scale_2x_ = texture_cache->IsResolutionScale2X();
  assert_false(resolution_scale_2x_ &&
               !command_processor_->IsROVUsedForEDRAM());

  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Create the buffer for reinterpreting EDRAM contents.
  D3D12_RESOURCE_DESC edram_buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      edram_buffer_desc, GetEDRAMBufferSize(),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  // The first operation will be a clear.
  edram_buffer_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
          &edram_buffer_desc, edram_buffer_state_, nullptr,
          IID_PPV_ARGS(&edram_buffer_)))) {
    XELOGE("Failed to create the EDRAM buffer");
    return false;
  }
  edram_buffer_cleared_ = false;

  // Create the root signature for EDRAM buffer load/store.
  D3D12_ROOT_PARAMETER load_store_root_parameters[2];
  // Parameter 0 is constants (changed for each render target binding).
  load_store_root_parameters[0].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  load_store_root_parameters[0].Constants.ShaderRegister = 0;
  load_store_root_parameters[0].Constants.RegisterSpace = 0;
  load_store_root_parameters[0].Constants.Num32BitValues =
      sizeof(EDRAMLoadStoreRootConstants) / sizeof(uint32_t);
  load_store_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is source and target.
  D3D12_DESCRIPTOR_RANGE load_store_root_ranges[2];
  load_store_root_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  load_store_root_ranges[0].NumDescriptors = 1;
  load_store_root_ranges[0].BaseShaderRegister = 0;
  load_store_root_ranges[0].RegisterSpace = 0;
  load_store_root_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  load_store_root_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  load_store_root_ranges[1].NumDescriptors = 1;
  load_store_root_ranges[1].BaseShaderRegister = 0;
  load_store_root_ranges[1].RegisterSpace = 0;
  load_store_root_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  load_store_root_parameters[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  load_store_root_parameters[1].DescriptorTable.NumDescriptorRanges = 2;
  load_store_root_parameters[1].DescriptorTable.pDescriptorRanges =
      load_store_root_ranges;
  load_store_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC load_store_root_desc;
  load_store_root_desc.NumParameters =
      UINT(xe::countof(load_store_root_parameters));
  load_store_root_desc.pParameters = load_store_root_parameters;
  load_store_root_desc.NumStaticSamplers = 0;
  load_store_root_desc.pStaticSamplers = nullptr;
  load_store_root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  edram_load_store_root_signature_ =
      ui::d3d12::util::CreateRootSignature(provider, load_store_root_desc);
  if (edram_load_store_root_signature_ == nullptr) {
    XELOGE("Failed to create the EDRAM load/store root signature");
    Shutdown();
    return false;
  }
  // Create the clear root signature (the same, but with the UAV only).
  load_store_root_ranges[1].OffsetInDescriptorsFromTableStart = 0;
  load_store_root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  ++load_store_root_parameters[1].DescriptorTable.pDescriptorRanges;
  edram_clear_root_signature_ =
      ui::d3d12::util::CreateRootSignature(provider, load_store_root_desc);
  if (edram_clear_root_signature_ == nullptr) {
    XELOGE("Failed to create the EDRAM buffer clear root signature");
    Shutdown();
    return false;
  }

  // Create the pipelines.
  // Load and store.
  for (uint32_t i = 0; i < uint32_t(EDRAMLoadStoreMode::kCount); ++i) {
    const EDRAMLoadStoreModeInfo& mode_info = edram_load_store_mode_info_[i];
    edram_load_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.load_shader, mode_info.load_shader_size,
        edram_load_store_root_signature_);
    edram_store_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.store_shader, mode_info.store_shader_size,
        edram_load_store_root_signature_);
    // Load shader for resolution-scaled resolves (host pixels within samples to
    // samples within host pixels) doesn't always exist for each mode - depth is
    // not resolved using drawing, for example.
    bool load_2x_resolve_pipeline_used =
        resolution_scale_2x_ && mode_info.load_2x_resolve_shader != nullptr;
    if (load_2x_resolve_pipeline_used) {
      edram_load_2x_resolve_pipelines_[i] =
          ui::d3d12::util::CreateComputePipeline(
              device, mode_info.load_2x_resolve_shader,
              mode_info.load_2x_resolve_shader_size,
              edram_load_store_root_signature_);
    }
    if (edram_load_pipelines_[i] == nullptr ||
        edram_store_pipelines_[i] == nullptr ||
        (load_2x_resolve_pipeline_used &&
         edram_load_2x_resolve_pipelines_[i] == nullptr)) {
      XELOGE("Failed to create the EDRAM load/store pipelines for mode %u", i);
      Shutdown();
      return false;
    }
    edram_load_pipelines_[i]->SetName(mode_info.load_pipeline_name);
    edram_store_pipelines_[i]->SetName(mode_info.store_pipeline_name);
    if (edram_load_2x_resolve_pipelines_[i] != nullptr) {
      edram_load_pipelines_[i]->SetName(
          mode_info.load_2x_resolve_pipeline_name);
    }
  }
  // Tile single sample into a texture - 32 bits per pixel.
  edram_tile_sample_32bpp_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device, edram_tile_sample_32bpp_cs, sizeof(edram_tile_sample_32bpp_cs),
      edram_load_store_root_signature_);
  if (edram_tile_sample_32bpp_pipeline_ == nullptr) {
    XELOGE("Failed to create the 32bpp EDRAM raw resolve pipeline");
    Shutdown();
    return false;
  }
  edram_tile_sample_32bpp_pipeline_->SetName(L"EDRAM Raw Resolve 32bpp");
  // Tile single sample into a texture - 64 bits per pixel.
  edram_tile_sample_64bpp_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device, edram_tile_sample_64bpp_cs, sizeof(edram_tile_sample_64bpp_cs),
      edram_load_store_root_signature_);
  if (edram_tile_sample_64bpp_pipeline_ == nullptr) {
    XELOGE("Failed to create the 64bpp EDRAM raw resolve pipeline");
    Shutdown();
    return false;
  }
  edram_tile_sample_64bpp_pipeline_->SetName(L"EDRAM Raw Resolve 64bpp");
  // Clear 32-bit color or unorm depth.
  edram_clear_32bpp_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device, edram_clear_32bpp_cs, sizeof(edram_clear_32bpp_cs),
      edram_clear_root_signature_);
  if (edram_clear_32bpp_pipeline_ == nullptr) {
    XELOGE("Failed to create the EDRAM 32bpp clear pipeline");
    Shutdown();
    return false;
  }
  edram_clear_32bpp_pipeline_->SetName(L"EDRAM Clear 32bpp");
  // Clear 64-bit color.
  edram_clear_64bpp_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device, edram_clear_64bpp_cs, sizeof(edram_clear_64bpp_cs),
      edram_clear_root_signature_);
  if (edram_clear_64bpp_pipeline_ == nullptr) {
    XELOGE("Failed to create the EDRAM 64bpp clear pipeline");
    Shutdown();
    return false;
  }
  edram_clear_64bpp_pipeline_->SetName(L"EDRAM Clear 64bpp");
  // Clear float depth.
  edram_clear_depth_float_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device, edram_clear_depth_float_cs, sizeof(edram_clear_depth_float_cs),
      edram_clear_root_signature_);
  if (edram_clear_depth_float_pipeline_ == nullptr) {
    XELOGE("Failed to create the EDRAM float depth clear pipeline");
    Shutdown();
    return false;
  }
  edram_clear_depth_float_pipeline_->SetName(L"EDRAM Clear Float Depth");

  // Create the converting resolve root signature.
  D3D12_ROOT_PARAMETER resolve_root_parameters[2];
  // Parameter 0 is constants.
  resolve_root_parameters[0].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  resolve_root_parameters[0].Constants.ShaderRegister = 0;
  resolve_root_parameters[0].Constants.RegisterSpace = 0;
  resolve_root_parameters[0].Constants.Num32BitValues =
      sizeof(ResolveRootConstants) / sizeof(uint32_t);
  resolve_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Parameter 1 is the source render target.
  D3D12_DESCRIPTOR_RANGE resolve_root_srv_range;
  resolve_root_srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  resolve_root_srv_range.NumDescriptors = 1;
  resolve_root_srv_range.BaseShaderRegister = 0;
  resolve_root_srv_range.RegisterSpace = 0;
  resolve_root_srv_range.OffsetInDescriptorsFromTableStart = 0;
  resolve_root_parameters[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  resolve_root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  resolve_root_parameters[1].DescriptorTable.pDescriptorRanges =
      &resolve_root_srv_range;
  resolve_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Static sampler for resolving AA using bilinear filtering.
  D3D12_STATIC_SAMPLER_DESC resolve_sampler_desc;
  resolve_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  resolve_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  resolve_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  resolve_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  resolve_sampler_desc.MipLODBias = 0.0f;
  resolve_sampler_desc.MaxAnisotropy = 1;
  resolve_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  resolve_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  resolve_sampler_desc.MinLOD = 0.0f;
  resolve_sampler_desc.MaxLOD = 0.0f;
  resolve_sampler_desc.ShaderRegister = 0;
  resolve_sampler_desc.RegisterSpace = 0;
  resolve_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC resolve_root_desc;
  resolve_root_desc.NumParameters = UINT(xe::countof(resolve_root_parameters));
  resolve_root_desc.pParameters = resolve_root_parameters;
  resolve_root_desc.NumStaticSamplers = 1;
  resolve_root_desc.pStaticSamplers = &resolve_sampler_desc;
  resolve_root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
  resolve_root_signature_ =
      ui::d3d12::util::CreateRootSignature(provider, resolve_root_desc);
  if (resolve_root_signature_ == nullptr) {
    XELOGE("Failed to create the converting resolve root signature");
    Shutdown();
    return false;
  }

  return true;
}

void RenderTargetCache::Shutdown() {
  ClearCache();

  for (auto& resolve_pipeline : resolve_pipelines_) {
    resolve_pipeline.pipeline->Release();
  }
  resolve_pipelines_.clear();
  ui::d3d12::util::ReleaseAndNull(resolve_root_signature_);
  ui::d3d12::util::ReleaseAndNull(edram_tile_sample_64bpp_pipeline_);
  ui::d3d12::util::ReleaseAndNull(edram_tile_sample_32bpp_pipeline_);
  ui::d3d12::util::ReleaseAndNull(edram_clear_depth_float_pipeline_);
  ui::d3d12::util::ReleaseAndNull(edram_clear_64bpp_pipeline_);
  ui::d3d12::util::ReleaseAndNull(edram_clear_32bpp_pipeline_);
  for (uint32_t i = 0; i < uint32_t(EDRAMLoadStoreMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(edram_store_pipelines_[i]);
    ui::d3d12::util::ReleaseAndNull(edram_load_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(edram_clear_root_signature_);
  ui::d3d12::util::ReleaseAndNull(edram_load_store_root_signature_);
  ui::d3d12::util::ReleaseAndNull(edram_buffer_);
}

void RenderTargetCache::ClearCache() {
  for (auto resolve_target_pair : resolve_targets_) {
    ResolveTarget* resolve_target = resolve_target_pair.second;
    resolve_target->resource->Release();
    delete resolve_target;
  }
  resolve_targets_.clear();
  COUNT_profile_set("gpu/render_target_cache/resolve_targets", 0);

  for (auto render_target_pair : render_targets_) {
    RenderTarget* render_target = render_target_pair.second;
    render_target->resource->Release();
    delete render_target;
  }
  render_targets_.clear();
  COUNT_profile_set("gpu/render_target_cache/render_targets", 0);

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

#if 0
  for (uint32_t i = 0; i < xe::countof(heaps_); ++i) {
    if (heaps_[i] != nullptr) {
      heaps_[i]->Release();
      heaps_[i] = nullptr;
    }
  }
#endif
}

void RenderTargetCache::BeginFrame() {
  ClearBindings();

  // TODO(Triang3l): Clear the EDRAM buffer if this is the first frame for a
  // stable D24F==D32F comparison.
}

bool RenderTargetCache::UpdateRenderTargets(const D3D12Shader* pixel_shader) {
  if (command_processor_->IsROVUsedForEDRAM()) {
    return true;
  }

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
  auto command_list = command_processor_->GetDeferredCommandList();
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
  uint32_t color_mask = command_processor_->GetCurrentColorMask(pixel_shader);
  uint32_t rb_color_info[4] = {
      regs[XE_GPU_REG_RB_COLOR_INFO].u32, regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
      regs[XE_GPU_REG_RB_COLOR2_INFO].u32, regs[XE_GPU_REG_RB_COLOR3_INFO].u32};
  for (uint32_t i = 0; i < 4; ++i) {
    enabled[i] = (color_mask & (0xF << (i * 4))) != 0;
    edram_bases[i] = std::min(rb_color_info[i] & 0xFFF, 2048u);
    formats[i] = uint32_t(GetBaseColorFormat(
        ColorRenderTargetFormat((rb_color_info[i] >> 16) & 0xF)));
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
#if 0
    uint32_t heap_usage[5] = {};
#endif
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
#if 0
      // If updating partially, only need to attach new render targets.
      for (uint32_t i = 0; i < 5; ++i) {
        const RenderTargetBinding& binding = current_bindings_[i];
        if (!binding.is_bound) {
          continue;
        }
        const RenderTarget* render_target = binding.render_target;
        if (render_target != nullptr) {
          // There are no holes between 4 MB pages in each heap.
          heap_usage[render_target->heap_page_first / kHeap4MBPages] +=
              render_target->heap_page_count;
        }
      }
#endif
    }
    XELOGGPU("RT Cache: %s update - pitch %u, samples %u, RTs to attach %u",
             full_update ? "Full" : "Partial", surface_pitch, msaa_samples,
             render_targets_to_attach);

    auto device =
        command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();

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

#if 0
      // Calculate the number of 4 MB pages of the heaps this RT will use.
      D3D12_RESOURCE_ALLOCATION_INFO allocation_info =
          device->GetResourceAllocationInfo(0, 1, &resource_desc);
      if (allocation_info.SizeInBytes == 0 ||
          allocation_info.SizeInBytes > (kHeap4MBPages << 22)) {
        assert_always();
        continue;
      }
      uint32_t heap_page_count =
          (uint32_t(allocation_info.SizeInBytes) + ((4 << 20) - 1)) >> 22;

      // Find the heap page range for this render target.
      uint32_t heap_page_first = UINT32_MAX;
      for (uint32_t j = 0; j < 5; ++j) {
        if (heap_usage[j] + heap_page_count <= kHeap4MBPages) {
          heap_page_first = j * kHeap4MBPages + heap_usage[j];
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
      heap_usage[heap_page_first / kHeap4MBPages] += heap_page_count;

      // Inform Direct3D that we're reusing the heap for this render target.
      command_processor_->PushAliasingBarrier(nullptr,
                                              binding.render_target->resource);
#else
      // If multiple render targets have the same format, assign different
      // instance numbers to them.
      uint32_t instance = 0;
      if (i != 4) {
        for (uint32_t j = 0; j < i; ++j) {
          const RenderTargetBinding& other_binding = current_bindings_[j];
          if (other_binding.is_bound &&
              other_binding.render_target != nullptr &&
              other_binding.format == formats[i]) {
            ++instance;
          }
        }
      }
      binding.render_target = FindOrCreateRenderTarget(key, instance);
#endif
    }

    // Sample positions when loading depth must match sample positions when
    // drawing.
    command_processor_->SetSamplePositions(msaa_samples);

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
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[4];
    uint32_t rtv_count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      const RenderTargetBinding& binding = current_bindings_[i];
      RenderTarget* render_target = binding.render_target;
      if (!binding.is_bound || render_target == nullptr) {
        continue;
      }
      XELOGGPU("RT Color %u: base %u, format %u", i, edram_bases[i],
               formats[i]);
      command_processor_->PushTransitionBarrier(
          render_target->resource, render_target->state,
          D3D12_RESOURCE_STATE_RENDER_TARGET);
      render_target->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
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
      XELOGGPU("RT Depth: base %u, format %u", edram_bases[4], formats[4]);
      command_processor_->PushTransitionBarrier(
          depth_render_target->resource, depth_render_target->state,
          D3D12_RESOURCE_STATE_DEPTH_WRITE);
      depth_render_target->state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      dsv_handle = &depth_binding.render_target->handle;
      current_pipeline_render_targets_[4].format =
          GetDepthDXGIFormat(DepthRenderTargetFormat(formats[4]));
    } else {
      dsv_handle = nullptr;
      current_pipeline_render_targets_[4].format = DXGI_FORMAT_UNKNOWN;
    }
    command_processor_->SubmitBarriers();
    command_list->D3DOMSetRenderTargets(rtv_count, rtv_handles, FALSE,
                                        dsv_handle);
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

bool RenderTargetCache::Resolve(SharedMemory* shared_memory,
                                TextureCache* texture_cache, Memory* memory) {
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
    return true;
  }
  MsaaSamples msaa_samples = MsaaSamples((rb_surface_info >> 16) & 0x3);
  uint32_t rb_copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  // Depth info is always needed because color resolve may also clear depth.
  uint32_t rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  uint32_t depth_edram_base = rb_depth_info & 0xFFF;
  uint32_t depth_format = (rb_depth_info >> 16) & 0x1;
  uint32_t surface_index = rb_copy_control & 0x7;
  if (surface_index > 4) {
    assert_always();
    return false;
  }
  bool surface_is_depth = surface_index == 4;
  uint32_t surface_edram_base;
  uint32_t surface_format;
  if (surface_is_depth) {
    surface_edram_base = depth_edram_base;
    surface_format = depth_format;
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
    surface_format = uint32_t(GetBaseColorFormat(
        ColorRenderTargetFormat((rb_color_info >> 16) & 0xF)));
  }

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
  float vertices[6];
  // Most vertices have a negative half pixel offset applied, which we reverse.
  float vertex_offset =
      (regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32 & 0x1) ? 0.0f : 0.5f;
  for (uint32_t i = 0; i < 6; ++i) {
    vertices[i] =
        xenos::GpuSwap(xe::load<float>(src_vertex_address + i * sizeof(float)),
                       Endian(fetch.endian)) +
        vertex_offset;
  }
  // Xenos only supports rectangle copies (luckily).
  // The rectangle is for both the source and the destination, according to how
  // it's used in Tales of Vesperia.
  // Window scissor must also be applied - in the jigsaw puzzle in Banjo-Tooie,
  // there are 1280x720 resolve rectangles, but only the scissored 1280x256
  // needs to be copied, otherwise it overflows even beyond the EDRAM, and the
  // depth buffer is visible on the screen. It also ensures the coordinates are
  // not negative (in F.E.A.R., for example, the right tile is resolved with
  // vertices (-640,0)->(640,720), however, the destination texture pointer is
  // adjusted properly to the right half of the texture, and the source render
  // target has a pitch of 800).
  D3D12_RECT rect;
  rect.left = LONG(std::min(std::min(vertices[0], vertices[2]), vertices[4]));
  rect.right = LONG(std::max(std::max(vertices[0], vertices[2]), vertices[4]));
  rect.top = LONG(std::min(std::min(vertices[1], vertices[3]), vertices[5]));
  rect.bottom = LONG(std::max(std::max(vertices[1], vertices[3]), vertices[5]));
  D3D12_RECT scissor;
  uint32_t window_scissor_tl = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32;
  uint32_t window_scissor_br = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32;
  scissor.left = LONG(window_scissor_tl & 0x7FFF);
  scissor.right = LONG(window_scissor_br & 0x7FFF);
  scissor.top = LONG((window_scissor_tl >> 16) & 0x7FFF);
  scissor.bottom = LONG((window_scissor_br >> 16) & 0x7FFF);
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
    rect.left += window_offset_x;
    rect.right += window_offset_x;
    rect.top += window_offset_y;
    rect.bottom += window_offset_y;
    if (!(window_scissor_tl & (1u << 31))) {
      scissor.left = std::max(LONG(scissor.left + window_offset_x), LONG(0));
      scissor.right = std::max(LONG(scissor.right + window_offset_x), LONG(0));
      scissor.top = std::max(LONG(scissor.top + window_offset_y), LONG(0));
      scissor.bottom =
          std::max(LONG(scissor.bottom + window_offset_y), LONG(0));
    }
  }
  rect.left = std::max(rect.left, scissor.left);
  rect.right = std::min(rect.right, scissor.right);
  rect.top = std::max(rect.top, scissor.top);
  rect.bottom = std::min(rect.bottom, scissor.bottom);

  XELOGGPU(
      "Resolve: (%d,%d)->(%d,%d) of RT %u (pitch %u, %u sample%s, format %u) "
      "at %u",
      rect.left, rect.top, rect.right, rect.bottom, surface_index,
      surface_pitch, 1 << uint32_t(msaa_samples),
      msaa_samples != MsaaSamples::k1X ? "s" : "", surface_format,
      surface_edram_base);

  if (rect.left >= rect.right || rect.top >= rect.bottom) {
    // Nothing to copy.
    return true;
  }

  if (command_processor_->IsROVUsedForEDRAM()) {
    // Commit ROV writes.
    command_processor_->PushUAVBarrier(edram_buffer_);
  }

  // GetEDRAMLayout in ResolveCopy and ResolveClear will perform the needed
  // clamping to the source render target size.

  bool result = ResolveCopy(shared_memory, texture_cache, surface_edram_base,
                            surface_pitch, msaa_samples, surface_is_depth,
                            surface_format, rect);
  // Clear the color RT if needed.
  if (!surface_is_depth) {
    result &= ResolveClear(surface_edram_base, surface_pitch, msaa_samples,
                           false, surface_format, rect);
  }
  // Clear the depth RT if needed (may be cleared alongside color).
  result &= ResolveClear(depth_edram_base, surface_pitch, msaa_samples, true,
                         depth_format, rect);
  return result;
}

bool RenderTargetCache::ResolveCopy(SharedMemory* shared_memory,
                                    TextureCache* texture_cache,
                                    uint32_t edram_base, uint32_t surface_pitch,
                                    MsaaSamples msaa_samples, bool is_depth,
                                    uint32_t src_format,
                                    const D3D12_RECT& rect) {
  auto& regs = *register_file_;

  uint32_t rb_copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  xenos::CopyCommand copy_command =
      xenos::CopyCommand((rb_copy_control >> 20) & 0x3);
  if (copy_command != xenos::CopyCommand::kRaw &&
      copy_command != xenos::CopyCommand::kConvert) {
    // TODO(Triang3l): Handle kConstantOne and kNull.
    return false;
  }

  auto command_list = command_processor_->GetDeferredCommandList();

  // Get the destination region and clamp the source region to it.
  uint32_t rb_copy_dest_pitch = regs[XE_GPU_REG_RB_COPY_DEST_PITCH].u32;
  uint32_t dest_pitch = rb_copy_dest_pitch & 0x3FFF;
  uint32_t dest_height = (rb_copy_dest_pitch >> 16) & 0x3FFF;
  if (dest_pitch == 0 || dest_height == 0) {
    // Nothing to copy.
    return true;
  }
  D3D12_RECT copy_rect = rect;
  copy_rect.right = std::min(copy_rect.right, LONG(dest_pitch));
  copy_rect.bottom = std::min(copy_rect.bottom, LONG(dest_height));
  if (copy_rect.left >= copy_rect.right || copy_rect.top >= copy_rect.bottom) {
    // Nothing to copy.
    return true;
  }

  // Get format info.
  uint32_t dest_info = regs[XE_GPU_REG_RB_COPY_DEST_INFO].u32;
  TextureFormat src_texture_format;
  bool src_64bpp;
  if (is_depth) {
    src_texture_format =
        DepthRenderTargetToTextureFormat(DepthRenderTargetFormat(src_format));
    src_64bpp = false;
  } else {
    // Texture formats k_16_16_EDRAM and k_16_16_16_16_EDRAM are not the same as
    // k_16_16 and k_16_16_16_16, but they are emulated with the same DXGI
    // formats as k_16_16 and k_16_16_16_16 on the host, so they are treated as
    // a special case of such formats.
    if (ColorRenderTargetFormat(src_format) ==
        ColorRenderTargetFormat::k_16_16) {
      src_texture_format = TextureFormat::k_16_16;
    } else if (ColorRenderTargetFormat(src_format) ==
               ColorRenderTargetFormat::k_16_16_16_16) {
      src_texture_format = TextureFormat::k_16_16_16_16;
    } else {
      src_texture_format =
          ColorRenderTargetToTextureFormat(ColorRenderTargetFormat(src_format));
    }
    src_64bpp = IsColorFormat64bpp(ColorRenderTargetFormat(src_format));
  }
  assert_true(src_texture_format != TextureFormat::kUnknown);
  src_texture_format = GetBaseFormat(src_texture_format);
  // The destination format is specified as k_8_8_8_8 when resolving depth,
  // apparently there's no format conversion.
  TextureFormat dest_format =
      is_depth ? src_texture_format
               : GetBaseFormat(TextureFormat((dest_info >> 7) & 0x3F));

  // See what samples we need and what we should do with them.
  xenos::CopySampleSelect sample_select =
      xenos::CopySampleSelect((rb_copy_control >> 4) & 0x7);
  if (is_depth && sample_select > xenos::CopySampleSelect::k3) {
    assert_always();
    return false;
  }
  Endian128 dest_endian = Endian128(dest_info & 0x7);
  int32_t dest_exp_bias;
  if (is_depth) {
    dest_exp_bias = 0;
  } else {
    dest_exp_bias = int32_t((dest_info >> 16) << 26) >> 26;
    if (ColorRenderTargetFormat(src_format) ==
            ColorRenderTargetFormat::k_16_16 ||
        ColorRenderTargetFormat(src_format) ==
            ColorRenderTargetFormat::k_16_16_16_16) {
      // On the Xbox 360, k_16_16_EDRAM and k_16_16_16_16_EDRAM internally have
      // -32...32 range, but they're emulated using normalized RG16/RGBA16, so
      // sampling the host render target gives 1/32 of what is actually stored
      // there on the guest side.
      // http://www.students.science.uu.nl/~3220516/advancedgraphics/papers/inferred_lighting.pdf
      dest_exp_bias += 5;
    }
  }
  bool dest_swap = !is_depth && ((dest_info >> 24) & 0x1);

  // Get the destination location.
  uint32_t dest_address = regs[XE_GPU_REG_RB_COPY_DEST_BASE].u32 & 0x1FFFFFFF;
  if (dest_address & 0x3) {
    assert_always();
    // Not 4-aligning may break UAV access significantly, let's hope games don't
    // resolve to 8bpp or 16bpp textures at very odd locations.
    return false;
  }
  // Currently not caring about copy_dest_array because it's untested, and
  // Direct3D 9 would likely offset to the correct slice.

  XELOGGPU(
      "Resolve: Copying samples %u to 0x%.8X (%ux%u), destination format %s, "
      "exponent bias %d, red and blue %sswapped",
      uint32_t(sample_select), dest_address, dest_pitch, dest_height,
      FormatInfo::Get(dest_format)->name, dest_exp_bias,
      dest_swap ? "" : "not ");

  // Validate and clamp the source region, skip parts that don't need to be
  // copied and calculate the number of threads needed for copying/loading.
  // copy_rect will be modified and will become only the source rectangle, for
  // the destination offset, use the original rect from the arguments.
  uint32_t surface_pitch_tiles, row_width_ss_div_80, rows;
  if (!GetEDRAMLayout(surface_pitch, msaa_samples, src_64bpp, edram_base,
                      copy_rect, surface_pitch_tiles, row_width_ss_div_80,
                      rows)) {
    // Nothing to copy.
    return true;
  }

  // There are 2 paths for resolving in this function - they don't necessarily
  // have to map directly to kRaw and kConvert CopyCommands.
  // - Raw - when extracting a single color to a texture of the same format as
  //   the EDRAM surface and exponent bias is not applied, or when resolving a
  //   depth buffer (games read only one sample of it - resolving multiple
  //   samples of a depth buffer is meaningless anyway - and apparently there's
  //   no format conversion as well because k_8_8_8_8 is specified in the
  //   destination format in the register, which is obviously not true, and the
  //   texture is then read as k_24_8 or k_24_8_FLOAT). Swapping red and blue is
  //   possible in this mode.
  // - Conversion - when a simple copy is not enough. The EDRAM region is loaded
  //   to a render target resource, which is then used as a texture in a shader
  //   performing the resolve (by sampling the texture on or between pixels with
  //   bilinear filtering), applying exponent bias and swapping red and blue in
  //   a format-agnostic way, then the resulting color is written to a temporary
  //   RTV of the destination format.
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  uint32_t resolution_scale_log2 = resolution_scale_2x_ ? 1 : 0;
  // Check if we need to apply the hack to remove the gap on the left and top
  // sides of the screen caused by half-pixel offset becoming whole pixel offset
  // with scaled rendering resolution.
  bool resolution_scale_edge_clamp =
      resolution_scale_2x_ && FLAGS_d3d12_resolution_scale_resolve_edge_clamp &&
      FLAGS_d3d12_half_pixel_offset &&
      !(regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32 & 0x1);
  if (sample_select <= xenos::CopySampleSelect::k3 &&
      src_texture_format == dest_format && dest_exp_bias == 0) {
    // *************************************************************************
    // Raw copy
    // *************************************************************************
    XELOGGPU("Resolve: Copying using a compute shader");

    // Calculate the address and the size of the region that specifically
    // is being resolved. Can't just use the texture height for size calculation
    // because it's sometimes bigger than needed (in Red Dead Redemption, an UI
    // texture used for the letterbox bars alpha is located within a 1280x720
    // resolve target, but only 1280x208 is being resolved, and with scaled
    // resolution the UI texture gets ignored).
    dest_address += texture_util::GetTiledOffset2D(
        int(rect.left & ~LONG(31)), int(rect.top & ~LONG(31)), dest_pitch,
        src_64bpp ? 3 : 2);
    uint32_t dest_size = texture_util::GetGuestMipSliceStorageSize(
        xe::align(dest_pitch, 32u),
        xe::align(uint32_t(rect.bottom - (rect.top & ~LONG(31))), 32u), 1, true,
        dest_format, nullptr, false);
    uint32_t dest_offset_x = uint32_t(rect.left) & 31;
    uint32_t dest_offset_y = uint32_t(rect.top) & 31;
    // Make sure we have the memory to write to.
    if (resolution_scale_2x_) {
      if (!texture_cache->EnsureScaledResolveBufferResident(dest_address,
                                                            dest_size)) {
        return false;
      }
    } else {
      if (!shared_memory->MakeTilesResident(dest_address, dest_size)) {
        return false;
      }
    }

    // Write the source and destination descriptors.
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
    D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
    if (command_processor_->RequestViewDescriptors(
            0, 2, 2, descriptor_cpu_start, descriptor_gpu_start) == 0) {
      return false;
    }
    TransitionEDRAMBuffer(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ui::d3d12::util::CreateRawBufferSRV(device, descriptor_cpu_start,
                                        edram_buffer_, GetEDRAMBufferSize());
    if (resolution_scale_2x_) {
      texture_cache->UseScaledResolveBufferForWriting();
      // Can't address more than 512 MB directly on Nvidia - binding only a part
      // of the buffer.
      texture_cache->CreateScaledResolveBufferRawUAV(
          provider->OffsetViewDescriptor(descriptor_cpu_start, 1),
          dest_address >> 12,
          ((dest_address + dest_size - 1) >> 12) - (dest_address >> 12) + 1);
    } else {
      shared_memory->UseForWriting();
      shared_memory->CreateRawUAV(
          provider->OffsetViewDescriptor(descriptor_cpu_start, 1));
    }
    command_processor_->SubmitBarriers();

    // Dispatch the computation.
    command_list->D3DSetComputeRootSignature(edram_load_store_root_signature_);
    EDRAMLoadStoreRootConstants root_constants;
    // Only 5 bits - assuming pre-offset address.
    assert_true(dest_offset_x <= 31 && dest_offset_y <= 31);
    root_constants.tile_sample_dimensions[0] =
        uint32_t(copy_rect.right - copy_rect.left) | (dest_offset_x << 12) |
        (uint32_t(copy_rect.left) << 17);
    root_constants.tile_sample_dimensions[1] =
        uint32_t(copy_rect.bottom - copy_rect.top) | (dest_offset_y << 12) |
        (uint32_t(copy_rect.top) << 17);
    root_constants.tile_sample_dest_base = dest_address;
    if (resolution_scale_2x_) {
      // Can't address more than 512 MB directly on Nvidia - binding only a part
      // of the buffer.
      root_constants.tile_sample_dest_base -= dest_address & ~0xFFFu;
    }
    assert_true(dest_pitch <= 8192);
    root_constants.tile_sample_dest_info = dest_pitch |
                                           (uint32_t(sample_select) << 14) |
                                           (uint32_t(dest_endian) << 16);
    if (dest_swap) {
      switch (ColorRenderTargetFormat(src_format)) {
        case ColorRenderTargetFormat::k_8_8_8_8:
        case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
          root_constants.tile_sample_dest_info |= (8 << 19) | (16 << 24);
          break;
        case ColorRenderTargetFormat::k_2_10_10_10:
        case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
        case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
        case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
          root_constants.tile_sample_dest_info |= (10 << 19) | (20 << 24);
          break;
        case ColorRenderTargetFormat::k_16_16_16_16:
        case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
          root_constants.tile_sample_dest_info |= 1 << 19;
          break;
        default:
          break;
      }
    }
    root_constants.base_samples_2x_depth_pitch =
        edram_base | (resolution_scale_log2 << 13) |
        (resolution_scale_edge_clamp ? (1 << 14) : 0) |
        (is_depth ? (1 << 15) : 0) | (surface_pitch_tiles << 16);
    if (msaa_samples >= MsaaSamples::k2X) {
      root_constants.base_samples_2x_depth_pitch |= 1 << 11;
      if (msaa_samples >= MsaaSamples::k4X) {
        root_constants.base_samples_2x_depth_pitch |= 1 << 12;
      }
    }
    command_list->D3DSetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);
    command_processor_->SetComputePipeline(
        src_64bpp ? edram_tile_sample_64bpp_pipeline_
                  : edram_tile_sample_32bpp_pipeline_);
    // 1 group per destination 80x16 region.
    uint32_t group_count_x = row_width_ss_div_80, group_count_y = rows;
    if (msaa_samples >= MsaaSamples::k2X) {
      group_count_y = (group_count_y + 1) >> 1;
      if (msaa_samples >= MsaaSamples::k4X) {
        group_count_x = (group_count_x + 1) >> 1;
      }
    }
    // With 2x scaling, destination width and height are 2x bigger, and 1 group
    // is 80x16 destination pixels after applying the resolution scale.
    group_count_x <<= resolution_scale_log2;
    group_count_y <<= resolution_scale_log2;
    command_list->D3DDispatch(group_count_x, group_count_y, 1);

    // Commit the write.
    command_processor_->PushUAVBarrier(
        resolution_scale_2x_ ? texture_cache->GetScaledResolveBuffer()
                             : shared_memory->GetBuffer());

    // Invalidate textures and mark the range as scaled if needed.
    texture_cache->MarkRangeAsResolved(dest_address, dest_size);
  } else {
    // *************************************************************************
    // Conversion and AA resolving
    // *************************************************************************
    XELOGGPU("Resolve: Copying via drawing");

    // Get everything we need for the conversion.

    // DXGI format (also checking whether this resolve is possible).
    DXGI_FORMAT dest_dxgi_format =
        texture_cache->GetResolveDXGIFormat(dest_format);
    if (dest_dxgi_format == DXGI_FORMAT_UNKNOWN) {
      XELOGE(
          "No resolve pipeline for destination format %s - tell Xenia "
          "developers!",
          FormatInfo::Get(dest_format)->name);
      return false;
    }
    // Resolve pipeline.
    ID3D12PipelineState* resolve_pipeline =
        GetResolvePipeline(dest_dxgi_format);
    if (resolve_pipeline == nullptr) {
      return false;
    }
    RenderTargetKey render_target_key;
    render_target_key.width_ss_div_80 = row_width_ss_div_80;
    render_target_key.height_ss_div_16 = rows;
    if (resolution_scale_2x_) {
      render_target_key.width_ss_div_80 *= 2;
      render_target_key.height_ss_div_16 *= 2;
    }
    render_target_key.is_depth = false;
    render_target_key.format = src_format;
    // Render target for loading the EDRAM buffer contents as a texture.
    RenderTarget* render_target =
        FindOrCreateRenderTarget(render_target_key, 0);
    if (render_target == nullptr) {
      return false;
    }
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint =
        render_target->footprints[0];
    // Size of the resolved area.
    uint32_t copy_width = copy_rect.right - copy_rect.left;
    uint32_t copy_height = copy_rect.bottom - copy_rect.top;
    // Resolve target for output merger format conversion.
#if 0
    ResolveTarget* resolve_target =
        FindOrCreateResolveTarget(copy_width, copy_height, dest_dxgi_format,
                                  render_target->heap_page_count);
#else
    ResolveTarget* resolve_target =
        FindOrCreateResolveTarget(copy_width, copy_height, dest_dxgi_format);
#endif
    if (resolve_target == nullptr) {
      return false;
    }
    // Descriptors. 2 for EDRAM load, 1 for conversion.
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
    D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
    if (command_processor_->RequestViewDescriptors(
            0, 3, 3, descriptor_cpu_start, descriptor_gpu_start) == 0) {
      return false;
    }
    // Buffer for copying.
    D3D12_RESOURCE_STATES copy_buffer_state =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12Resource* copy_buffer = command_processor_->RequestScratchGPUBuffer(
        std::max(render_target->copy_buffer_size,
                 resolve_target->copy_buffer_size),
        copy_buffer_state);
    if (copy_buffer == nullptr) {
      return false;
    }

    // Load the EDRAM buffer contents to the copy buffer.

    TransitionEDRAMBuffer(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    command_processor_->SubmitBarriers();

    command_list->D3DSetComputeRootSignature(edram_load_store_root_signature_);

    EDRAMLoadStoreRootConstants load_root_constants;
    load_root_constants.rt_color_depth_offset = uint32_t(footprint.Offset);
    load_root_constants.rt_color_depth_pitch =
        uint32_t(footprint.Footprint.RowPitch);
    load_root_constants.base_samples_2x_depth_pitch =
        edram_base | (resolution_scale_log2 << 13) |
        (surface_pitch_tiles << 16);
    if (msaa_samples >= MsaaSamples::k2X) {
      load_root_constants.base_samples_2x_depth_pitch |= 1 << 11;
      if (msaa_samples >= MsaaSamples::k4X) {
        load_root_constants.base_samples_2x_depth_pitch |= 1 << 12;
      }
    }
    command_list->D3DSetComputeRoot32BitConstants(
        0, sizeof(load_root_constants) / sizeof(uint32_t), &load_root_constants,
        0);

    ui::d3d12::util::CreateRawBufferSRV(device, descriptor_cpu_start,
                                        edram_buffer_, GetEDRAMBufferSize());
    ui::d3d12::util::CreateRawBufferUAV(
        device, provider->OffsetViewDescriptor(descriptor_cpu_start, 1),
        copy_buffer, render_target->copy_buffer_size);
    command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);

    EDRAMLoadStoreMode mode = GetLoadStoreMode(false, src_format);
    command_processor_->SetComputePipeline(
        resolution_scale_2x_ ? edram_load_2x_resolve_pipelines_[size_t(mode)]
                             : edram_load_pipelines_[size_t(mode)]);
    // 1 group per 80x16 samples, with both 1x and 2x resolution scales.
    command_list->D3DDispatch(row_width_ss_div_80, rows, 1);
    command_processor_->PushUAVBarrier(copy_buffer);

    // Go to the next descriptor set.

    descriptor_cpu_start =
        provider->OffsetViewDescriptor(descriptor_cpu_start, 2);
    descriptor_gpu_start =
        provider->OffsetViewDescriptor(descriptor_gpu_start, 2);

    // Copy the EDRAM buffer contents to the source texture.

#if 0
    command_processor_->PushAliasingBarrier(nullptr, render_target->resource);
#endif
    command_processor_->PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_processor_->PushTransitionBarrier(render_target->resource,
                                              render_target->state,
                                              D3D12_RESOURCE_STATE_COPY_DEST);
    render_target->state = D3D12_RESOURCE_STATE_COPY_DEST;
    command_processor_->SubmitBarriers();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = copy_buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = render_target->footprints[0];
    location_dest.pResource = render_target->resource;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    command_list->CopyTexture(location_dest, location_source);

    // Do the resolve. Render targets unbound already, safe to call
    // OMSetRenderTargets.

#if 0
    command_processor_->PushAliasingBarrier(nullptr, resolve_target->resource);
#endif
    command_processor_->PushTransitionBarrier(
        render_target->resource, render_target->state,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    render_target->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_processor_->PushTransitionBarrier(
        resolve_target->resource, resolve_target->state,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    resolve_target->state = D3D12_RESOURCE_STATE_RENDER_TARGET;

    command_list->D3DSetGraphicsRootSignature(resolve_root_signature_);

    ResolveRootConstants resolve_root_constants;
    uint32_t samples_x_log2 = msaa_samples >= MsaaSamples::k4X ? 1 : 0;
    uint32_t samples_y_log2 = msaa_samples >= MsaaSamples::k2X ? 1 : 0;
    resolve_root_constants.rect_samples_lw =
        (copy_rect.left << (samples_x_log2 + resolution_scale_log2)) |
        (copy_width << (16 + samples_x_log2 + resolution_scale_log2));
    resolve_root_constants.rect_samples_th =
        (copy_rect.top << (samples_y_log2 + resolution_scale_log2)) |
        (copy_height << (16 + samples_y_log2 + resolution_scale_log2));
    resolve_root_constants.source_size =
        (render_target_key.width_ss_div_80 * 80) |
        (render_target_key.height_ss_div_16 << (4 + 16));
    resolve_root_constants.resolve_info =
        samples_y_log2 | (samples_x_log2 << 1) |
        (resolution_scale_edge_clamp ? (1 << 6) : 0) |
        ((uint32_t(dest_exp_bias) & 0x3F) << 7);
    if (msaa_samples == MsaaSamples::k1X) {
      // No offset.
      resolve_root_constants.resolve_info |= (1 << 2) | (1 << 4);
    } else if (msaa_samples == MsaaSamples::k2X) {
      // -0.5 or +0.5 samples vertical offset if getting only one sample.
      if (sample_select == xenos::CopySampleSelect::k0) {
        resolve_root_constants.resolve_info |= (0 << 2) | (1 << 4);
      } else if (sample_select == xenos::CopySampleSelect::k1) {
        resolve_root_constants.resolve_info |= (2 << 2) | (1 << 4);
      } else {
        resolve_root_constants.resolve_info |= (1 << 2) | (1 << 4);
      }
    } else {
      // -0.5 or +0.5 samples offsets if getting one or two samples.
      switch (sample_select) {
        case xenos::CopySampleSelect::k0:
          resolve_root_constants.resolve_info |= (0 << 2) | (0 << 4);
          break;
        case xenos::CopySampleSelect::k1:
          resolve_root_constants.resolve_info |= (2 << 2) | (0 << 4);
          break;
        case xenos::CopySampleSelect::k2:
          resolve_root_constants.resolve_info |= (0 << 2) | (2 << 4);
          break;
        case xenos::CopySampleSelect::k3:
          resolve_root_constants.resolve_info |= (2 << 2) | (2 << 4);
          break;
        case xenos::CopySampleSelect::k01:
          resolve_root_constants.resolve_info |= (1 << 2) | (0 << 4);
          break;
        case xenos::CopySampleSelect::k23:
          resolve_root_constants.resolve_info |= (1 << 2) | (2 << 4);
          break;
        default:
          resolve_root_constants.resolve_info |= (1 << 2) | (1 << 4);
          break;
      }
    }
    command_list->D3DSetGraphicsRoot32BitConstants(
        0, sizeof(resolve_root_constants) / sizeof(uint32_t),
        &resolve_root_constants, 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC rt_srv_desc;
    rt_srv_desc.Format =
        GetColorDXGIFormat(ColorRenderTargetFormat(src_format));
    rt_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    UINT swizzle = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (dest_swap) {
      switch (ColorRenderTargetFormat(src_format)) {
        case ColorRenderTargetFormat::k_8_8_8_8:
        case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
        case ColorRenderTargetFormat::k_2_10_10_10:
        case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
        case ColorRenderTargetFormat::k_16_16_16_16:
        case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
        case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
        case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
          swizzle = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2, 1, 0, 3);
          break;
        default:
          break;
      }
    }
    if (dest_format == TextureFormat::k_6_5_5) {
      // Green bits of the resolve target used for blue, and blue bits used for
      // green.
      swizzle = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
          D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(0, swizzle),
          D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(2, swizzle),
          D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(1, swizzle),
          D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(3, swizzle));
    }
    rt_srv_desc.Shader4ComponentMapping = swizzle;
    rt_srv_desc.Texture2D.MostDetailedMip = 0;
    rt_srv_desc.Texture2D.MipLevels = 1;
    rt_srv_desc.Texture2D.PlaneSlice = 0;
    rt_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(render_target->resource, &rt_srv_desc,
                                     descriptor_cpu_start);
    command_list->D3DSetGraphicsRootDescriptorTable(1, descriptor_gpu_start);

    command_processor_->SubmitBarriers();
    command_processor_->SetSamplePositions(MsaaSamples::k1X);
    command_processor_->SetExternalGraphicsPipeline(resolve_pipeline);
    command_list->D3DOMSetRenderTargets(1, &resolve_target->rtv_handle, TRUE,
                                        nullptr);
    D3D12_VIEWPORT viewport;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = float(copy_width << resolution_scale_log2);
    viewport.Height = float(copy_height << resolution_scale_log2);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    command_list->RSSetViewport(viewport);
    D3D12_RECT scissor;
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = copy_width << resolution_scale_log2;
    scissor.bottom = copy_height << resolution_scale_log2;
    command_list->RSSetScissorRect(scissor);
    command_list->D3DIASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->D3DDrawInstanced(3, 1, 0, 0);
    if (command_processor_->IsROVUsedForEDRAM()) {
      // Clean up - the ROV path doesn't need render targets bound and has
      // non-zero ForcedSampleCount.
      command_list->D3DOMSetRenderTargets(0, nullptr, FALSE, nullptr);
    }

    // Copy the resolve target to the buffer.

    command_processor_->PushTransitionBarrier(resolve_target->resource,
                                              resolve_target->state,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    resolve_target->state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_processor_->PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                              D3D12_RESOURCE_STATE_COPY_DEST);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
    command_processor_->SubmitBarriers();
    location_source.pResource = resolve_target->resource;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_source.SubresourceIndex = 0;
    location_dest.pResource = copy_buffer;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_dest.PlacedFootprint = resolve_target->footprint;
    command_list->CopyTexture(location_dest, location_source);

    // Tile the resolved texture. The texture cache expects the buffer to be a
    // non-pixel-shader SRV.

    command_processor_->PushTransitionBarrier(
        copy_buffer, copy_buffer_state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    texture_cache->TileResolvedTexture(
        dest_format, dest_address, dest_pitch, uint32_t(rect.left),
        uint32_t(rect.top), copy_width, copy_height, dest_endian, copy_buffer,
        resolve_target->copy_buffer_size, resolve_target->footprint);

    // Done with the copy buffer.

    command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
  }

  return true;
}

bool RenderTargetCache::ResolveClear(uint32_t edram_base,
                                     uint32_t surface_pitch,
                                     MsaaSamples msaa_samples, bool is_depth,
                                     uint32_t format, const D3D12_RECT& rect) {
  auto& regs = *register_file_;

  // Check if clearing is enabled.
  uint32_t rb_copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  if (!(rb_copy_control & (is_depth ? (1 << 9) : (1 << 8)))) {
    return true;
  }

  XELOGGPU("Resolve: Clearing the %s render target",
           is_depth ? "depth" : "color");

  // Calculate the layout.
  bool is_64bpp =
      !is_depth && IsColorFormat64bpp(ColorRenderTargetFormat(format));
  D3D12_RECT clear_rect = rect;
  uint32_t surface_pitch_tiles, row_width_ss_div_80, rows;
  if (!GetEDRAMLayout(surface_pitch, msaa_samples, is_64bpp, edram_base,
                      clear_rect, surface_pitch_tiles, row_width_ss_div_80,
                      rows)) {
    // Nothing to clear.
    return true;
  }
  uint32_t samples_x_log2 = msaa_samples >= MsaaSamples::k4X ? 1 : 0;
  uint32_t samples_y_log2 = msaa_samples >= MsaaSamples::k2X ? 1 : 0;

  // Get everything needed for clearing.
  auto command_list = command_processor_->GetDeferredCommandList();
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 1, 1, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    return false;
  }

  // Submit the clear.
  TransitionEDRAMBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  command_processor_->SubmitBarriers();
  EDRAMLoadStoreRootConstants root_constants;
  root_constants.clear_rect_lt = (clear_rect.left << samples_x_log2) |
                                 (clear_rect.top << (16 + samples_y_log2));
  root_constants.clear_rect_rb = (clear_rect.right << samples_x_log2) |
                                 (clear_rect.bottom << (16 + samples_y_log2));
  root_constants.base_samples_2x_depth_pitch =
      edram_base | (samples_y_log2 << 11) | (samples_x_log2 << 12) |
      (resolution_scale_2x_ ? (1 << 13) : 0) | (is_depth ? (1 << 15) : 0) |
      (surface_pitch_tiles << 16);
  // When ROV is used, there's no 32-bit depth buffer.
  if (!command_processor_->IsROVUsedForEDRAM() && is_depth &&
      DepthRenderTargetFormat(format) == DepthRenderTargetFormat::kD24FS8) {
    root_constants.clear_depth24 = regs[XE_GPU_REG_RB_DEPTH_CLEAR].u32;
    // 20e4 [0,2), based on CFloat24 from d3dref9.dll and on 6e4 in DirectXTex.
    uint32_t depth24 = root_constants.clear_depth24 >> 8;
    if (depth24 == 0) {
      root_constants.clear_depth32 = 0;
    } else {
      uint32_t mantissa = depth24 & 0xFFFFFu, exponent = depth24 >> 20;
      if (exponent == 0) {
        // Normalize the value in the resulting float.
        // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x100000) == 0)
        uint32_t mantissa_lzcnt = xe::lzcnt(mantissa) - (32u - 21u);
        exponent = 1u - mantissa_lzcnt;
        mantissa = (mantissa << mantissa_lzcnt) & 0xFFFFFu;
      }
      root_constants.clear_depth32 =
          ((exponent + 112u) << 23) | (mantissa << 3);
    }
    command_processor_->SetComputePipeline(edram_clear_depth_float_pipeline_);
  } else if (is_64bpp) {
    // TODO(Triang3l): Check which 32-bit portion is in which register.
    root_constants.clear_color_high = regs[XE_GPU_REG_RB_COLOR_CLEAR].u32;
    root_constants.clear_color_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LOW].u32;
    command_processor_->SetComputePipeline(edram_clear_64bpp_pipeline_);
  } else {
    Register reg =
        is_depth ? XE_GPU_REG_RB_DEPTH_CLEAR : XE_GPU_REG_RB_COLOR_CLEAR;
    root_constants.clear_color_high = regs[reg].u32;
    command_processor_->SetComputePipeline(edram_clear_32bpp_pipeline_);
  }
  command_list->D3DSetComputeRootSignature(edram_clear_root_signature_);
  command_list->D3DSetComputeRoot32BitConstants(
      0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
  ui::d3d12::util::CreateRawBufferUAV(device, descriptor_cpu_start,
                                      edram_buffer_, GetEDRAMBufferSize());
  command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);
  // 1 group per 80x16 samples. Resolution scale handled in the shader itself.
  command_list->D3DDispatch(row_width_ss_div_80, rows, 1);
  command_processor_->PushUAVBarrier(edram_buffer_);

  return true;
}

ID3D12PipelineState* RenderTargetCache::GetResolvePipeline(
    DXGI_FORMAT dest_format) {
  // Try to find an existing pipeline.
  for (auto& resolve_pipeline : resolve_pipelines_) {
    if (resolve_pipeline.dest_format == dest_format) {
      return resolve_pipeline.pipeline;
    }
  }
  // Create a new pipeline.
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = resolve_root_signature_;
  pipeline_desc.VS.pShaderBytecode = resolve_vs;
  pipeline_desc.VS.BytecodeLength = sizeof(resolve_vs);
  pipeline_desc.PS.pShaderBytecode = resolve_ps;
  pipeline_desc.PS.BytecodeLength = sizeof(resolve_ps);
  pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = dest_format;
  pipeline_desc.SampleDesc.Count = 1;
  ID3D12PipelineState* pipeline;
  if (FAILED(device->CreateGraphicsPipelineState(&pipeline_desc,
                                                 IID_PPV_ARGS(&pipeline)))) {
    XELOGE("Failed to create the resolve pipeline for DXGI format %u",
           dest_format);
    return nullptr;
  }
  ResolvePipeline new_resolve_pipeline;
  new_resolve_pipeline.pipeline = pipeline;
  new_resolve_pipeline.dest_format = dest_format;
  resolve_pipelines_.push_back(new_resolve_pipeline);
  return pipeline;
}

RenderTargetCache::ResolveTarget* RenderTargetCache::FindOrCreateResolveTarget(
#if 0
    uint32_t width_unscaled, uint32_t height_unscaled, DXGI_FORMAT format,
    uint32_t min_heap_page_first) {
#else
    uint32_t width_unscaled, uint32_t height_unscaled, DXGI_FORMAT format
#endif
) {
#if 0
  assert_true(min_heap_page_first < kHeap4MBPages * 5);
#endif

  if (width_unscaled == 0 || height_unscaled == 0 || width_unscaled > 2160 ||
      height_unscaled > 2160) {
    assert_always();
    return nullptr;
  }
  uint32_t width_scaled = width_unscaled, height_scaled = height_unscaled;
  if (resolution_scale_2x_) {
    width_scaled *= 2;
    height_scaled *= 2;
  }
  ResolveTargetKey key;
  key.width_div_32 = (width_scaled + 31) >> 5;
  key.height_div_32 = (height_scaled + 31) >> 5;
  key.format = format;

  // Try to find an existing target that isn't overlapping the resolve source.
#if 0
  auto found_range = resolve_targets_.equal_range(key.value);
  for (auto iter = found_range.first; iter != found_range.second; ++iter) {
    ResolveTarget* found_resolve_target = iter->second;
    if (found_resolve_target->heap_page_first >= min_heap_page_first) {
      return found_resolve_target;
    }
  }
#else
  auto found_iter = resolve_targets_.find(key.value);
  if (found_iter != resolve_targets_.end()) {
    return found_iter->second;
  }
#endif

  // Ensure the new resolve target can get an RTV descriptor.
  if (!EnsureRTVHeapAvailable(false)) {
    return nullptr;
  }

  // Allocate a new resolve target.
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Alignment = 0;
  resource_desc.Width = key.width_div_32 << 5;
  resource_desc.Height = key.height_div_32 << 5;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = format;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

#if 0
  D3D12_RESOURCE_ALLOCATION_INFO allocation_info =
      device->GetResourceAllocationInfo(0, 1, &resource_desc);
  uint32_t heap_page_count =
      (uint32_t(allocation_info.SizeInBytes) + ((4 << 20) - 1)) >> 22;
  if (heap_page_count == 0 || heap_page_count > kHeap4MBPages) {
    assert_always();
    XELOGE(
        "%ux%u resolve target with DXGI format %u can't fit in a heap, "
        "needs %u bytes - tell Xenia developers to increase the heap size!",
        uint32_t(resource_desc.Width), resource_desc.Height, format,
        uint32_t(allocation_info.SizeInBytes));
    return nullptr;
  }
  if (kHeap4MBPages - (min_heap_page_first % kHeap4MBPages) < heap_page_count) {
    // Go to the next heap if no free space in the current one.
    min_heap_page_first = xe::round_up(min_heap_page_first, kHeap4MBPages);
    assert_true(min_heap_page_first < kHeap4MBPages * 5);
  }
  // Create the memory heap if it doesn't exist yet.
  uint32_t heap_index = min_heap_page_first / kHeap4MBPages;
  if (!MakeHeapResident(heap_index)) {
    return nullptr;
  }
#endif

  // Create it.
  // The first action likely to be done is resolve.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  ID3D12Resource* resource;
#if 0
  if (FAILED(device->CreatePlacedResource(
          heaps_[heap_index], (min_heap_page_first % kHeap4MBPages) << 22,
          &resource_desc, state, nullptr, IID_PPV_ARGS(&resource)))) {
    XELOGE(
        "Failed to create a placed resource for %ux%u resolve target with DXGI "
        "format %u at heap 4 MB pages %u:%u",
        uint32_t(resource_desc.Width), resource_desc.Height, format,
        min_heap_page_first, min_heap_page_first + heap_page_count - 1);
    return nullptr;
  }
#else
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
          &resource_desc, state, nullptr, IID_PPV_ARGS(&resource)))) {
    XELOGE(
        "Failed to create a committed resource for %ux%u resolve target with "
        "DXGI format %u",
        uint32_t(resource_desc.Width), resource_desc.Height, format);
    return nullptr;
  }
#endif

  // Create the RTV.
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
      provider->OffsetRTVDescriptor(descriptor_heaps_color_->start_handle,
                                    descriptor_heaps_color_->descriptors_used);
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
  rtv_desc.Format = format;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_desc.Texture2D.MipSlice = 0;
  rtv_desc.Texture2D.PlaneSlice = 0;
  device->CreateRenderTargetView(resource, &rtv_desc, rtv_handle);
  ++descriptor_heaps_color_->descriptors_used;

  // Add the new resolve target to the cache.
  ResolveTarget* resolve_target = new ResolveTarget;
  resolve_target->resource = resource;
  resolve_target->state = state;
  resolve_target->rtv_handle.ptr = rtv_handle.ptr;
  resolve_target->key.value = key.value;
#if 0
  resolve_target->heap_page_first = min_heap_page_first;
#endif
  UINT64 copy_buffer_size;
  device->GetCopyableFootprints(&resource_desc, 0, 1, 0,
                                &resolve_target->footprint, nullptr, nullptr,
                                &copy_buffer_size);
  // Safety (though if width and height are aligned to 32 it will be fine, but
  // just in case this changes).
  copy_buffer_size =
      xe::align(copy_buffer_size, UINT64(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
  resolve_target->copy_buffer_size = uint32_t(copy_buffer_size);
  resolve_targets_.insert(std::make_pair(key.value, resolve_target));
  COUNT_profile_set("gpu/render_target_cache/resolve_targets",
                    resolve_targets_.size());

  return resolve_target;
}

void RenderTargetCache::UnbindRenderTargets() {
  StoreRenderTargetsToEDRAM();
  ClearBindings();
}

void RenderTargetCache::UseEDRAMAsUAV() {
  TransitionEDRAMBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void RenderTargetCache::CreateEDRAMUint32UAV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = GetEDRAMBufferSize() / sizeof(uint32_t);
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.CounterOffsetInBytes = 0;
  desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  device->CreateUnorderedAccessView(edram_buffer_, nullptr, &desc, handle);
}

void RenderTargetCache::EndFrame() { UnbindRenderTargets(); }

ColorRenderTargetFormat RenderTargetCache::GetBaseColorFormat(
    ColorRenderTargetFormat format) {
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return ColorRenderTargetFormat::k_8_8_8_8;
    case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return ColorRenderTargetFormat::k_2_10_10_10;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return ColorRenderTargetFormat::k_2_10_10_10_FLOAT;
    default:
      return format;
  }
}

DXGI_FORMAT RenderTargetCache::GetColorDXGIFormat(
    ColorRenderTargetFormat format) {
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return DXGI_FORMAT_R10G10B10A2_UNORM;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ColorRenderTargetFormat::k_16_16:
      return DXGI_FORMAT_R16G16_SNORM;
    case ColorRenderTargetFormat::k_16_16_16_16:
      return DXGI_FORMAT_R16G16B16A16_SNORM;
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

uint32_t RenderTargetCache::GetEDRAMBufferSize() const {
  uint32_t size = 2048 * 5120;
  if (!command_processor_->IsROVUsedForEDRAM()) {
    // Two 10 MB pages, one containing color and integer depth data, another
    // with 32-bit float depth when 20e4 depth is used to allow for multipass
    // drawing without precision loss in case of EDRAM store/load.
    size *= 2;
  }
  if (resolution_scale_2x_) {
    size *= 4;
  }
  return size;
}

void RenderTargetCache::TransitionEDRAMBuffer(D3D12_RESOURCE_STATES new_state) {
  command_processor_->PushTransitionBarrier(edram_buffer_, edram_buffer_state_,
                                            new_state);
  edram_buffer_state_ = new_state;
}

void RenderTargetCache::ClearBindings() {
  current_surface_pitch_ = 0;
  current_msaa_samples_ = MsaaSamples::k1X;
  current_edram_max_rows_ = 0;
  std::memset(current_bindings_, 0, sizeof(current_bindings_));
}

#if 0
bool RenderTargetCache::MakeHeapResident(uint32_t heap_index) {
  if (heap_index >= 5) {
    assert_always();
    return false;
  }
  if (heaps_[heap_index] != nullptr) {
    return true;
  }
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = kHeap4MBPages << 22;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  // TODO(Triang3l): If real MSAA is added, alignment must be 4 MB.
  heap_desc.Alignment = 0;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  if (FAILED(
          device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heaps_[heap_index])))) {
    XELOGE("Failed to create a %u MB heap for render targets",
           kHeap4MBPages * 4);
    return false;
  }
  return true;
}
#endif

bool RenderTargetCache::EnsureRTVHeapAvailable(bool is_depth) {
  auto& heap = is_depth ? descriptor_heaps_depth_ : descriptor_heaps_color_;
  if (heap != nullptr &&
      heap->descriptors_used < kRenderTargetDescriptorHeapSize) {
    return true;
  }
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
  heap_desc.Type = is_depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV
                            : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = kRenderTargetDescriptorHeapSize;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;
  ID3D12DescriptorHeap* new_d3d_heap;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc,
                                          IID_PPV_ARGS(&new_d3d_heap)))) {
    XELOGE("Failed to create a heap for %u %s buffer descriptors",
           kRenderTargetDescriptorHeapSize, is_depth ? "depth" : "color");
    return false;
  }
  RenderTargetDescriptorHeap* new_heap = new RenderTargetDescriptorHeap;
  new_heap->heap = new_d3d_heap;
  new_heap->start_handle = new_d3d_heap->GetCPUDescriptorHandleForHeapStart();
  new_heap->descriptors_used = 0;
  new_heap->previous = heap;
  heap = new_heap;
  return true;
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
#if 0
    RenderTargetKey key, uint32_t heap_page_first
#else
    RenderTargetKey key, uint32_t instance
#endif
) {
#if 0
  assert_true(heap_page_first < kHeap4MBPages * 5);
#endif

  // Try to find an existing render target.
  auto found_range = render_targets_.equal_range(key.value);
  for (auto iter = found_range.first; iter != found_range.second; ++iter) {
    RenderTarget* found_render_target = iter->second;
#if 0
    if (found_render_target->heap_page_first == heap_page_first) {
      return found_render_target;
    }
#else
    if (found_render_target->instance == instance) {
      return found_render_target;
    }
#endif
  }

  D3D12_RESOURCE_DESC resource_desc;
  if (!GetResourceDesc(key, resource_desc)) {
    return nullptr;
  }

  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

#if 0
  // Get the number of heap pages needed for the render target.
  D3D12_RESOURCE_ALLOCATION_INFO allocation_info =
      device->GetResourceAllocationInfo(0, 1, &resource_desc);
  uint32_t heap_page_count =
      (uint32_t(allocation_info.SizeInBytes) + ((4 << 20) - 1)) >> 22;
  if (heap_page_count == 0 ||
      (heap_page_first % kHeap4MBPages) + heap_page_count > kHeap4MBPages) {
    assert_always();
    return nullptr;
  }
#endif

  // Ensure we can create a new descriptor in the render target heap.
  if (!EnsureRTVHeapAvailable(key.is_depth)) {
    return nullptr;
  }

#if 0
  // Create the memory heap if it doesn't exist yet.
  uint32_t heap_index = heap_page_first / kHeap4MBPages;
  if (!MakeHeapResident(heap_index)) {
    return nullptr;
  }
#endif

  // The first action likely to be done is EDRAM buffer load.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* resource;
#if 0
  if (FAILED(device->CreatePlacedResource(
          heaps_[heap_index], (heap_page_first % kHeap4MBPages) << 22,
          &resource_desc, state, nullptr, IID_PPV_ARGS(&resource)))) {
    XELOGE(
        "Failed to create a placed resource for %ux%u %s render target with "
        "format %u at heap 4 MB pages %u:%u",
        uint32_t(resource_desc.Width), resource_desc.Height,
        key.is_depth ? "depth" : "color", key.format, heap_page_first,
        heap_page_first + heap_page_count - 1);
    return nullptr;
  }
#else
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
          &resource_desc, state, nullptr, IID_PPV_ARGS(&resource)))) {
    XELOGE(
        "Failed to create a committed resource for %ux%u %s render target with "
        "format %u",
        uint32_t(resource_desc.Width), resource_desc.Height,
        key.is_depth ? "depth" : "color", key.format);
    return nullptr;
  }
#endif

  // Create the descriptor for the render target.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle;
  if (key.is_depth) {
    descriptor_handle = provider->OffsetDSVDescriptor(
        descriptor_heaps_depth_->start_handle,
        descriptor_heaps_depth_->descriptors_used);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Format = resource_desc.Format;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.Texture2D.MipSlice = 0;
    device->CreateDepthStencilView(resource, &dsv_desc, descriptor_handle);
    ++descriptor_heaps_depth_->descriptors_used;
  } else {
    descriptor_handle = provider->OffsetRTVDescriptor(
        descriptor_heaps_color_->start_handle,
        descriptor_heaps_color_->descriptors_used);
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    rtv_desc.Format = resource_desc.Format;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    device->CreateRenderTargetView(resource, &rtv_desc, descriptor_handle);
    ++descriptor_heaps_color_->descriptors_used;
  }

  // Get the layout for copying to the EDRAM buffer.
  RenderTarget* render_target = new RenderTarget;
  render_target->resource = resource;
  render_target->state = state;
  render_target->handle = descriptor_handle;
  render_target->key = key;
#if 0
  render_target->heap_page_first = heap_page_first;
  render_target->heap_page_count = heap_page_count;
#else
  render_target->instance = instance;
#endif
  UINT64 copy_buffer_size;
  device->GetCopyableFootprints(&resource_desc, 0, key.is_depth ? 2 : 1, 0,
                                render_target->footprints, nullptr, nullptr,
                                &copy_buffer_size);
  render_target->copy_buffer_size = uint32_t(copy_buffer_size);
  render_targets_.insert(std::make_pair(key.value, render_target));
  COUNT_profile_set("gpu/render_target_cache/render_targets",
                    render_targets_.size());
#if 0
  XELOGGPU(
      "Created %ux%u %s render target with format %u at heap 4 MB pages %u:%u",
      uint32_t(resource_desc.Width), resource_desc.Height,
      key.is_depth ? "depth" : "color", key.format, heap_page_first,
      heap_page_first + heap_page_count - 1);
#else
  XELOGGPU("Created %ux%u %s render target with format %u",
           uint32_t(resource_desc.Width), resource_desc.Height,
           key.is_depth ? "depth" : "color", key.format);
#endif
  return render_target;
}

bool RenderTargetCache::GetEDRAMLayout(
    uint32_t pitch_pixels, MsaaSamples msaa_samples, bool is_64bpp,
    uint32_t& base_in_out, D3D12_RECT& rect_in_out, uint32_t& pitch_tiles_out,
    uint32_t& row_width_ss_div_80_out, uint32_t& rows_out) {
  if (pitch_pixels == 0 || rect_in_out.right <= 0 || rect_in_out.bottom <= 0 ||
      rect_in_out.top >= rect_in_out.bottom) {
    return false;
  }
  pitch_pixels = std::min(pitch_pixels, 2560u);
  D3D12_RECT rect = rect_in_out;
  rect.left = std::max(rect.left, LONG(0));
  rect.top = std::max(rect.top, LONG(0));
  rect.right = std::min(rect.right, LONG(pitch_pixels));
  if (rect.left >= rect.right) {
    return false;
  }

  uint32_t samples_x_log2 = msaa_samples >= MsaaSamples::k4X ? 1 : 0;
  uint32_t samples_y_log2 = msaa_samples >= MsaaSamples::k2X ? 1 : 0;
  uint32_t sample_size_log2 = is_64bpp ? 1 : 0;

  uint32_t pitch_tiles = (((pitch_pixels << samples_x_log2) + 79) / 80)
                         << sample_size_log2;

  // Adjust the base and the rectangle to skip tiles to the left of the left
  // bound of the rectangle and to the top of the top bound.
  uint32_t base = base_in_out;
  uint32_t skip = rect.top << samples_y_log2 >> 4;
  base += skip * pitch_tiles;
  skip <<= 4 - samples_y_log2;
  rect.top -= skip;
  rect.bottom -= skip;
  skip = (rect.left << samples_x_log2) / 80;
  base += skip << sample_size_log2;
  skip *= 80 >> samples_x_log2;
  rect.left -= skip;
  rect.right -= skip;

  // Calculate the number of 16-sample rows this rectangle spans.
  uint32_t rows = ((rect.bottom << samples_y_log2) + 15) >> 4;
  uint32_t rows_max = (2048 - base) / pitch_tiles;
  if (rows_max == 0) {
    return false;
  }
  if (rows > rows_max) {
    // Clamp the rectangle if it's partially outside of EDRAM.
    rows = rows_max;
    rect.bottom = rows_max << (4 - samples_y_log2);
  }

  base_in_out = base;
  rect_in_out = rect;
  pitch_tiles_out = pitch_tiles;
  row_width_ss_div_80_out = ((rect.right << samples_x_log2) + 79) / 80;
  rows_out = rows;
  return true;
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
  auto command_list = command_processor_->GetDeferredCommandList();

  // Extract only the render targets that need to be stored, transition them to
  // copy sources and calculate copy buffer size.
  uint32_t store_bindings[5];
  uint32_t store_binding_count = 0;
  uint32_t copy_buffer_size = 0;
  for (uint32_t i = 0; i < 5; ++i) {
    const RenderTargetBinding& binding = current_bindings_[i];
    RenderTarget* render_target = binding.render_target;
    if (!binding.is_bound || render_target == nullptr ||
        binding.edram_dirty_rows < 0) {
      continue;
    }
    store_bindings[store_binding_count++] = i;
    copy_buffer_size =
        std::max(copy_buffer_size, render_target->copy_buffer_size);
  }
  if (store_binding_count == 0) {
    return;
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

  // Transition the render targets that need to be stored to copy sources and
  // the EDRAM buffer to a UAV.
  for (uint32_t i = 0; i < store_binding_count; ++i) {
    RenderTarget* render_target =
        current_bindings_[store_bindings[i]].render_target;
    command_processor_->PushTransitionBarrier(render_target->resource,
                                              render_target->state,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    render_target->state = D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  TransitionEDRAMBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  // Set up the bindings.
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  command_list->D3DSetComputeRootSignature(edram_load_store_root_signature_);
  ui::d3d12::util::CreateRawBufferSRV(device, descriptor_cpu_start, copy_buffer,
                                      copy_buffer_size);
  ui::d3d12::util::CreateRawBufferUAV(
      device, provider->OffsetViewDescriptor(descriptor_cpu_start, 1),
      edram_buffer_, GetEDRAMBufferSize());
  command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);

  // Sort the bindings in ascending order of EDRAM base so data in the render
  // targets placed farther in EDRAM isn't lost in case of overlap.
  std::sort(store_bindings, store_bindings + store_binding_count,
            [this](uint32_t a, uint32_t b) {
              uint32_t base_a = current_bindings_[a].edram_base;
              uint32_t base_b = current_bindings_[b].edram_base;
              if (base_a == base_b) {
                // If EDRAM bases are the same (not really a valid usage, but
                // happens in Banjo-Tooie - in case color writing was enabled
                // for invalid render targets in some draw call), treat the
                // render targets with the lowest index as more important (it's
                // the primary one after all, while the rest are additional).
                // Depth buffer has lower priority, otherwise the Xbox Live
                // Arcade logo disappears.
                return a > b;
              }
              return base_a < base_b;
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

    // Transition the copy buffer to copy destination.
    command_processor_->PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                              D3D12_RESOURCE_STATE_COPY_DEST);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
    command_processor_->SubmitBarriers();

    // Copy from the render target planes and set up the layout.
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = render_target->resource;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_source.SubresourceIndex = 0;
    location_dest.pResource = copy_buffer;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_dest.PlacedFootprint = render_target->footprints[0];
    // TODO(Triang3l): Box for color render targets.
    command_list->CopyTexture(location_dest, location_source);
    EDRAMLoadStoreRootConstants root_constants;
    uint32_t rt_pitch_tiles = surface_pitch_tiles;
    if (!render_target->key.is_depth &&
        IsColorFormat64bpp(
            ColorRenderTargetFormat(render_target->key.format))) {
      rt_pitch_tiles *= 2;
    }
    // TODO(Triang3l): log2(sample count, resolution scale).
    root_constants.base_samples_2x_depth_pitch =
        binding.edram_base | (rt_pitch_tiles << 16);
    root_constants.rt_color_depth_offset =
        uint32_t(location_dest.PlacedFootprint.Offset);
    root_constants.rt_color_depth_pitch =
        location_dest.PlacedFootprint.Footprint.RowPitch;
    if (render_target->key.is_depth) {
      root_constants.base_samples_2x_depth_pitch |= 1 << 15;
      location_source.SubresourceIndex = 1;
      location_dest.PlacedFootprint = render_target->footprints[1];
      command_list->CopyTexture(location_dest, location_source);
      root_constants.rt_stencil_offset =
          uint32_t(location_dest.PlacedFootprint.Offset);
      root_constants.rt_stencil_pitch =
          location_dest.PlacedFootprint.Footprint.RowPitch;
    }

    // Transition the copy buffer to SRV.
    command_processor_->PushTransitionBarrier(
        copy_buffer, copy_buffer_state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_processor_->SubmitBarriers();

    // Store the data.
    command_list->D3DSetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    EDRAMLoadStoreMode mode = GetLoadStoreMode(render_target->key.is_depth,
                                               render_target->key.format);
    command_processor_->SetComputePipeline(
        edram_store_pipelines_[size_t(mode)]);
    // 1 group per 80x16 samples.
    command_list->D3DDispatch(surface_pitch_tiles, binding.edram_dirty_rows, 1);

    // Commit the UAV write.
    command_processor_->PushUAVBarrier(edram_buffer_);
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

  auto command_list = command_processor_->GetDeferredCommandList();

  // Allocate descriptors for the buffers.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    return;
  }

  // Get the buffer for copying.
  uint32_t copy_buffer_size = 0;
  for (uint32_t i = 0; i < render_target_count; ++i) {
    copy_buffer_size =
        std::max(copy_buffer_size, render_targets[i]->copy_buffer_size);
  }
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_->RequestScratchGPUBuffer(
      copy_buffer_size, copy_buffer_state);
  if (copy_buffer == nullptr) {
    return;
  }

  // Transition the render targets to copy destinations and the EDRAM buffer to
  // a SRV.
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* render_target = render_targets[i];
    command_processor_->PushTransitionBarrier(render_target->resource,
                                              render_target->state,
                                              D3D12_RESOURCE_STATE_COPY_DEST);
    render_target->state = D3D12_RESOURCE_STATE_COPY_DEST;
  }
  TransitionEDRAMBuffer(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  // Set up the bindings.
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  command_list->D3DSetComputeRootSignature(edram_load_store_root_signature_);
  ui::d3d12::util::CreateRawBufferSRV(device, descriptor_cpu_start,
                                      edram_buffer_, GetEDRAMBufferSize());
  ui::d3d12::util::CreateRawBufferUAV(
      device, provider->OffsetViewDescriptor(descriptor_cpu_start, 1),
      copy_buffer, copy_buffer_size);
  command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);

  // Load each render target.
  for (uint32_t i = 0; i < render_target_count; ++i) {
    if (edram_bases[i] >= 2048) {
      // Something is wrong with the load.
      continue;
    }
    const RenderTarget* render_target = render_targets[i];

    // Get the number of EDRAM tiles per row.
    uint32_t edram_pitch_tiles = render_target->key.width_ss_div_80;
    if (!render_target->key.is_depth &&
        IsColorFormat64bpp(
            ColorRenderTargetFormat(render_target->key.format))) {
      edram_pitch_tiles *= 2;
    }
    // Clamp the height if somehow requested a render target that is too large.
    uint32_t edram_rows =
        std::min(render_target->key.height_ss_div_16,
                 (2048u - edram_bases[i]) / edram_pitch_tiles);
    if (edram_rows == 0) {
      continue;
    }

    // Transition the copy buffer back to UAV if it's not the first load.
    command_processor_->PushTransitionBarrier(
        copy_buffer, copy_buffer_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    copy_buffer_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Load the data.
    command_processor_->SubmitBarriers();
    EDRAMLoadStoreRootConstants root_constants;
    // TODO(Triang3l): log2(sample count, resolution scale).
    root_constants.base_samples_2x_depth_pitch =
        edram_bases[i] | (edram_pitch_tiles << 16);
    root_constants.rt_color_depth_offset =
        uint32_t(render_target->footprints[0].Offset);
    root_constants.rt_color_depth_pitch =
        render_target->footprints[0].Footprint.RowPitch;
    if (render_target->key.is_depth) {
      root_constants.base_samples_2x_depth_pitch |= 1 << 15;
      root_constants.rt_stencil_offset =
          uint32_t(render_target->footprints[1].Offset);
      root_constants.rt_stencil_pitch =
          render_target->footprints[1].Footprint.RowPitch;
    }
    command_list->D3DSetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    EDRAMLoadStoreMode mode = GetLoadStoreMode(render_target->key.is_depth,
                                               render_target->key.format);
    command_processor_->SetComputePipeline(edram_load_pipelines_[size_t(mode)]);
    // 1 group per 80x16 samples.
    command_list->D3DDispatch(render_target->key.width_ss_div_80, edram_rows,
                              1);

    // Commit the UAV write and transition the copy buffer to copy source now.
    command_processor_->PushUAVBarrier(copy_buffer);
    command_processor_->PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;

    // Copy to the render target planes.
    command_processor_->SubmitBarriers();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = copy_buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = render_target->footprints[0];
    location_dest.pResource = render_target->resource;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    command_list->CopyTexture(location_dest, location_source);
    if (render_target->key.is_depth) {
      location_source.PlacedFootprint = render_target->footprints[1];
      location_dest.SubresourceIndex = 1;
      command_list->CopyTexture(location_dest, location_source);
    }
  }

  command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
