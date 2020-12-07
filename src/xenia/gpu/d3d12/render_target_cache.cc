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
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/ui/d3d12/d3d12_util.h"

DEFINE_bool(d3d12_16bit_rtv_full_range, true,
            "Use full -32...32 range for RG16 and RGBA16 render targets "
            "(at the expense of blending correctness) without ROV.",
            "D3D12");

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_color_7e3_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_depth_float24and32_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_load_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_color_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_color_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_color_7e3_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_depth_float24and32_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/edram_store_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_clear_32bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_clear_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_clear_64bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_clear_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_clear_depth_24_32_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_fast_32bpp_1x2xmsaa_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_fast_32bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_fast_32bpp_4xmsaa_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_fast_64bpp_1x2xmsaa_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_fast_64bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_fast_64bpp_4xmsaa_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_128bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_128bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_16bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_16bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_32bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_64bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_8bpp_2xres_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/resolve_full_8bpp_cs.h"

const RenderTargetCache::EdramLoadStoreModeInfo
    RenderTargetCache::edram_load_store_mode_info_[size_t(
        RenderTargetCache::EdramLoadStoreMode::kCount)] = {
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
        {edram_load_depth_float24and32_cs,
         sizeof(edram_load_depth_float24and32_cs),
         L"EDRAM Load 24-bit & 32-bit Float Depth",
         edram_store_depth_float24and32_cs,
         sizeof(edram_store_depth_float24and32_cs),
         L"EDRAM Store 24-bit & 32-bit Float Depth"},
};

const std::pair<const uint8_t*, size_t>
    RenderTargetCache::resolve_copy_shaders_[size_t(
        draw_util::ResolveCopyShaderIndex::kCount)] = {
        {resolve_fast_32bpp_1x2xmsaa_cs,
         sizeof(resolve_fast_32bpp_1x2xmsaa_cs)},
        {resolve_fast_32bpp_4xmsaa_cs, sizeof(resolve_fast_32bpp_4xmsaa_cs)},
        {resolve_fast_32bpp_2xres_cs, sizeof(resolve_fast_32bpp_2xres_cs)},
        {resolve_fast_64bpp_1x2xmsaa_cs,
         sizeof(resolve_fast_64bpp_1x2xmsaa_cs)},
        {resolve_fast_64bpp_4xmsaa_cs, sizeof(resolve_fast_64bpp_4xmsaa_cs)},
        {resolve_fast_64bpp_2xres_cs, sizeof(resolve_fast_64bpp_2xres_cs)},
        {resolve_full_8bpp_cs, sizeof(resolve_full_8bpp_cs)},
        {resolve_full_8bpp_2xres_cs, sizeof(resolve_full_8bpp_2xres_cs)},
        {resolve_full_16bpp_cs, sizeof(resolve_full_16bpp_cs)},
        {resolve_full_16bpp_2xres_cs, sizeof(resolve_full_16bpp_2xres_cs)},
        {resolve_full_32bpp_cs, sizeof(resolve_full_32bpp_cs)},
        {resolve_full_32bpp_2xres_cs, sizeof(resolve_full_32bpp_2xres_cs)},
        {resolve_full_64bpp_cs, sizeof(resolve_full_64bpp_cs)},
        {resolve_full_64bpp_2xres_cs, sizeof(resolve_full_64bpp_2xres_cs)},
        {resolve_full_128bpp_cs, sizeof(resolve_full_128bpp_cs)},
        {resolve_full_128bpp_2xres_cs, sizeof(resolve_full_128bpp_2xres_cs)},
};

RenderTargetCache::RenderTargetCache(D3D12CommandProcessor& command_processor,
                                     const RegisterFile& register_file,
                                     TraceWriter& trace_writer,
                                     bool bindless_resources_used,
                                     bool edram_rov_used)
    : command_processor_(command_processor),
      register_file_(register_file),
      trace_writer_(trace_writer),
      bindless_resources_used_(bindless_resources_used),
      edram_rov_used_(edram_rov_used) {}

RenderTargetCache::~RenderTargetCache() { Shutdown(); }

bool RenderTargetCache::Initialize(const TextureCache& texture_cache) {
  depth_float24_conversion_ = flags::GetDepthFloat24Conversion();

  // EDRAM buffer size depends on this.
  resolution_scale_2x_ = texture_cache.IsResolutionScale2X();
  assert_false(resolution_scale_2x_ && !edram_rov_used_);

  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();

  uint32_t edram_buffer_size = GetEdramBufferSize();

  // Create the buffer for reinterpreting EDRAM contents.
  D3D12_RESOURCE_DESC edram_buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      edram_buffer_desc, edram_buffer_size,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  // The first operation will likely be drawing with ROV or a load without ROV.
  edram_buffer_state_ = edram_rov_used_
                            ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                            : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  // Request zeroed (though no guarantee) when not using ROV so the host 32-bit
  // depth buffer will be initialized to deterministic values (because it's
  // involved in comparison with converted 24-bit values - whether the 32-bit
  // value is up to date is determined by whether it's equal to the 24-bit
  // value in the main EDRAM buffer when converted to 24-bit).
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault,
          edram_rov_used_ ? provider.GetHeapFlagCreateNotZeroed()
                          : D3D12_HEAP_FLAG_NONE,
          &edram_buffer_desc, edram_buffer_state_, nullptr,
          IID_PPV_ARGS(&edram_buffer_)))) {
    XELOGE("Failed to create the EDRAM buffer");
    Shutdown();
    return false;
  }
  edram_buffer_modified_ = false;

  // Create non-shader-visible descriptors of the EDRAM buffer for copying.
  D3D12_DESCRIPTOR_HEAP_DESC edram_buffer_descriptor_heap_desc;
  edram_buffer_descriptor_heap_desc.Type =
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  edram_buffer_descriptor_heap_desc.NumDescriptors =
      uint32_t(EdramBufferDescriptorIndex::kCount);
  edram_buffer_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  edram_buffer_descriptor_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &edram_buffer_descriptor_heap_desc,
          IID_PPV_ARGS(&edram_buffer_descriptor_heap_)))) {
    XELOGE("Failed to create the descriptor heap for EDRAM buffer views");
    Shutdown();
    return false;
  }
  edram_buffer_descriptor_heap_start_ =
      edram_buffer_descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
  ui::d3d12::util::CreateBufferRawSRV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kRawSRV)),
      edram_buffer_, edram_buffer_size);
  ui::d3d12::util::CreateBufferTypedSRV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kR32UintSRV)),
      edram_buffer_, DXGI_FORMAT_R32_UINT, edram_buffer_size >> 2);
  ui::d3d12::util::CreateBufferTypedSRV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kR32G32UintSRV)),
      edram_buffer_, DXGI_FORMAT_R32G32_UINT, edram_buffer_size >> 3);
  ui::d3d12::util::CreateBufferTypedSRV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kR32G32B32A32UintSRV)),
      edram_buffer_, DXGI_FORMAT_R32G32B32A32_UINT, edram_buffer_size >> 4);
  ui::d3d12::util::CreateBufferRawUAV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kRawUAV)),
      edram_buffer_, edram_buffer_size);
  ui::d3d12::util::CreateBufferTypedUAV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kR32UintUAV)),
      edram_buffer_, DXGI_FORMAT_R32_UINT, edram_buffer_size >> 2);
  ui::d3d12::util::CreateBufferTypedUAV(
      device,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kR32G32B32A32UintUAV)),
      edram_buffer_, DXGI_FORMAT_R32G32B32A32_UINT, edram_buffer_size >> 4);

  if (!edram_rov_used_) {
    // Create the root signature for EDRAM buffer load/store.
    D3D12_ROOT_PARAMETER load_store_root_parameters[3];
    // Parameter 0 is constants (changed for each render target binding).
    load_store_root_parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    load_store_root_parameters[0].Constants.ShaderRegister = 0;
    load_store_root_parameters[0].Constants.RegisterSpace = 0;
    load_store_root_parameters[0].Constants.Num32BitValues =
        sizeof(EdramLoadStoreRootConstants) / sizeof(uint32_t);
    load_store_root_parameters[0].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
    // Parameter 1 is the destination.
    D3D12_DESCRIPTOR_RANGE load_store_root_dest_range;
    load_store_root_dest_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    load_store_root_dest_range.NumDescriptors = 1;
    load_store_root_dest_range.BaseShaderRegister = 0;
    load_store_root_dest_range.RegisterSpace = 0;
    load_store_root_dest_range.OffsetInDescriptorsFromTableStart = 0;
    load_store_root_parameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    load_store_root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    load_store_root_parameters[1].DescriptorTable.pDescriptorRanges =
        &load_store_root_dest_range;
    load_store_root_parameters[1].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
    // Parameter 2 is the source.
    D3D12_DESCRIPTOR_RANGE load_store_root_source_range;
    load_store_root_source_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    load_store_root_source_range.NumDescriptors = 1;
    load_store_root_source_range.BaseShaderRegister = 0;
    load_store_root_source_range.RegisterSpace = 0;
    load_store_root_source_range.OffsetInDescriptorsFromTableStart = 0;
    load_store_root_parameters[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    load_store_root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    load_store_root_parameters[2].DescriptorTable.pDescriptorRanges =
        &load_store_root_source_range;
    load_store_root_parameters[2].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
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

    // Create the EDRAM load/store pipelines.
    for (uint32_t i = 0; i < uint32_t(EdramLoadStoreMode::kCount); ++i) {
      const EdramLoadStoreModeInfo& mode_info = edram_load_store_mode_info_[i];
      edram_load_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
          device, mode_info.load_shader, mode_info.load_shader_size,
          edram_load_store_root_signature_);
      edram_store_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
          device, mode_info.store_shader, mode_info.store_shader_size,
          edram_load_store_root_signature_);
      if (edram_load_pipelines_[i] == nullptr ||
          edram_store_pipelines_[i] == nullptr) {
        XELOGE("Failed to create the EDRAM load/store pipelines for mode {}",
               i);
        Shutdown();
        return false;
      }
      edram_load_pipelines_[i]->SetName(mode_info.load_pipeline_name);
      edram_store_pipelines_[i]->SetName(mode_info.store_pipeline_name);
    }
  }

  // Create the resolve root signatures and pipelines.
  D3D12_ROOT_PARAMETER resolve_root_parameters[3];

  // Copying root signature.
  // Parameter 0 is constants.
  resolve_root_parameters[0].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  resolve_root_parameters[0].Constants.ShaderRegister = 0;
  resolve_root_parameters[0].Constants.RegisterSpace = 0;
  // Binding all of the shared memory at 1x resolution, portions with scaled
  // resolution.
  resolve_root_parameters[0].Constants.Num32BitValues =
      (resolution_scale_2x_
           ? sizeof(draw_util::ResolveCopyShaderConstants::DestRelative)
           : sizeof(draw_util::ResolveCopyShaderConstants)) /
      sizeof(uint32_t);
  resolve_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is the destination (shared memory for copying, EDRAM for
  // clearing).
  D3D12_DESCRIPTOR_RANGE resolve_dest_range;
  resolve_dest_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  resolve_dest_range.NumDescriptors = 1;
  resolve_dest_range.BaseShaderRegister = 0;
  resolve_dest_range.RegisterSpace = 0;
  resolve_dest_range.OffsetInDescriptorsFromTableStart = 0;
  resolve_root_parameters[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  resolve_root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  resolve_root_parameters[1].DescriptorTable.pDescriptorRanges =
      &resolve_dest_range;
  resolve_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 2 is the source, if exists (EDRAM for copying).
  D3D12_DESCRIPTOR_RANGE resolve_source_range;
  resolve_source_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  resolve_source_range.NumDescriptors = 1;
  resolve_source_range.BaseShaderRegister = 0;
  resolve_source_range.RegisterSpace = 0;
  resolve_source_range.OffsetInDescriptorsFromTableStart = 0;
  resolve_root_parameters[2].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  resolve_root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  resolve_root_parameters[2].DescriptorTable.pDescriptorRanges =
      &resolve_source_range;
  resolve_root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC resolve_root_signature_desc;
  resolve_root_signature_desc.NumParameters = 3;
  resolve_root_signature_desc.pParameters = resolve_root_parameters;
  resolve_root_signature_desc.NumStaticSamplers = 0;
  resolve_root_signature_desc.pStaticSamplers = nullptr;
  resolve_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  resolve_copy_root_signature_ = ui::d3d12::util::CreateRootSignature(
      provider, resolve_root_signature_desc);
  if (resolve_copy_root_signature_ == nullptr) {
    XELOGE("Failed to create the resolve copy root signature");
    Shutdown();
    return false;
  }

  // Clearing root signature.
  resolve_root_parameters[0].Constants.Num32BitValues =
      sizeof(draw_util::ResolveClearShaderConstants) / sizeof(uint32_t);
  resolve_root_signature_desc.NumParameters = 2;
  resolve_clear_root_signature_ = ui::d3d12::util::CreateRootSignature(
      provider, resolve_root_signature_desc);
  if (resolve_clear_root_signature_ == nullptr) {
    XELOGE("Failed to create the resolve clear root signature");
    Shutdown();
    return false;
  }

  // Copying pipelines.
  uint32_t resolution_scale = resolution_scale_2x_ ? 2 : 1;
  for (size_t i = 0; i < size_t(draw_util::ResolveCopyShaderIndex::kCount);
       ++i) {
    // Somewhat verification whether resolve_copy_shaders_ is up to date.
    assert_not_null(resolve_copy_shaders_[i].first);
    const draw_util::ResolveCopyShaderInfo& resolve_copy_shader_info =
        draw_util::resolve_copy_shader_info[i];
    if (resolve_copy_shader_info.resolution_scale != resolution_scale) {
      continue;
    }
    const auto& resolve_copy_shader = resolve_copy_shaders_[i];
    ID3D12PipelineState* resolve_copy_pipeline =
        ui::d3d12::util::CreateComputePipeline(
            device, resolve_copy_shader.first, resolve_copy_shader.second,
            resolve_copy_root_signature_);
    if (resolve_copy_pipeline == nullptr) {
      XELOGE("Failed to create {} resolve copy pipeline",
             resolve_copy_shader_info.debug_name);
    }
    resolve_copy_pipeline->SetName(reinterpret_cast<LPCWSTR>(
        xe::to_utf16(resolve_copy_shader_info.debug_name).c_str()));
    resolve_copy_pipelines_[i] = resolve_copy_pipeline;
  }

  // Clearing pipelines.
  resolve_clear_32bpp_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device,
      resolution_scale_2x_ ? resolve_clear_32bpp_2xres_cs
                           : resolve_clear_32bpp_cs,
      resolution_scale_2x_ ? sizeof(resolve_clear_32bpp_2xres_cs)
                           : sizeof(resolve_clear_32bpp_cs),
      resolve_clear_root_signature_);
  if (resolve_clear_32bpp_pipeline_ == nullptr) {
    XELOGE("Failed to create the 32bpp resolve clear pipeline");
    Shutdown();
    return false;
  }
  resolve_clear_32bpp_pipeline_->SetName(L"Resolve Clear 32bpp");
  resolve_clear_64bpp_pipeline_ = ui::d3d12::util::CreateComputePipeline(
      device,
      resolution_scale_2x_ ? resolve_clear_64bpp_2xres_cs
                           : resolve_clear_64bpp_cs,
      resolution_scale_2x_ ? sizeof(resolve_clear_64bpp_2xres_cs)
                           : sizeof(resolve_clear_64bpp_cs),
      resolve_clear_root_signature_);
  if (resolve_clear_64bpp_pipeline_ == nullptr) {
    XELOGE("Failed to create the 64bpp resolve clear pipeline");
    Shutdown();
    return false;
  }
  resolve_clear_64bpp_pipeline_->SetName(L"Resolve Clear 64bpp");
  if (!edram_rov_used_ &&
      depth_float24_conversion_ == flags::DepthFloat24Conversion::kOnCopy) {
    assert_false(resolution_scale_2x_);
    resolve_clear_depth_24_32_pipeline_ =
        ui::d3d12::util::CreateComputePipeline(
            device, resolve_clear_depth_24_32_cs,
            sizeof(resolve_clear_depth_24_32_cs),
            resolve_clear_root_signature_);
    if (resolve_clear_depth_24_32_pipeline_ == nullptr) {
      XELOGE(
          "Failed to create the 24-bit and 32-bit depth resolve clear pipeline "
          "state");
      Shutdown();
      return false;
    }
    resolve_clear_depth_24_32_pipeline_->SetName(
        L"Resolve Clear 24-bit & 32-bit Depth");
  }

  ClearBindings();

  return true;
}

void RenderTargetCache::Shutdown() {
  ClearCache();

  edram_snapshot_restore_pool_.reset();
  ui::d3d12::util::ReleaseAndNull(edram_snapshot_download_buffer_);
  ui::d3d12::util::ReleaseAndNull(resolve_clear_depth_24_32_pipeline_);
  ui::d3d12::util::ReleaseAndNull(resolve_clear_64bpp_pipeline_);
  ui::d3d12::util::ReleaseAndNull(resolve_clear_32bpp_pipeline_);
  ui::d3d12::util::ReleaseAndNull(resolve_clear_root_signature_);
  for (size_t i = 0; i < xe::countof(resolve_copy_pipelines_); ++i) {
    ui::d3d12::util::ReleaseAndNull(resolve_copy_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(resolve_copy_root_signature_);
  for (uint32_t i = 0; i < uint32_t(EdramLoadStoreMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(edram_store_pipelines_[i]);
    ui::d3d12::util::ReleaseAndNull(edram_load_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(edram_load_store_root_signature_);
  ui::d3d12::util::ReleaseAndNull(edram_buffer_descriptor_heap_);
  ui::d3d12::util::ReleaseAndNull(edram_buffer_);
}

void RenderTargetCache::ClearCache() {
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

  edram_snapshot_restore_pool_.reset();
}

void RenderTargetCache::CompletedSubmissionUpdated() {
  if (edram_snapshot_restore_pool_) {
    edram_snapshot_restore_pool_->Reclaim(
        command_processor_.GetCompletedSubmission());
  }
}

void RenderTargetCache::BeginSubmission() {
  // With the ROV, a submission does not always end in a resolve (for example,
  // when memexport readback happens) or something else that would surely submit
  // the UAV barrier, so we need to preserve the `current_` variables.
  //
  // With RTVs, simply going to a different command list doesn't have to cause
  // storing the render targets to the EDRAM buffer, however, the new command
  // list doesn't have the needed RTVs/DSV bound yet.
  //
  // Just make sure they are bound to the new command list.
  apply_to_command_list_ = true;
}

void RenderTargetCache::EndFrame() {
  // May be clearing the cache after this.
  FlushAndUnbindRenderTargets();
}

bool RenderTargetCache::UpdateRenderTargets(const D3D12Shader* pixel_shader) {
  // There are two kinds of render target binding updates in this implementation
  // in case something has been changed - full and partial.
  //
  // For the RTV/DSV path, a full update involves flushing all the currently
  // bound render targets that have been modified to the EDRAM buffer,
  // allocating all the newly bound render targets in the heaps, loading them
  // from the EDRAM buffer and binding them.
  //
  // For the ROV path, a full update places a UAV barrier because across draws,
  // pixels with different SV_Positions or different sample counts (thus without
  // interlocking between each other) may access the same data now. Not having
  // the barriers causes visual glitches in many games, such as Halo 3 where the
  // right side of the menu and shadow maps get corrupted (at least on Nvidia).
  //
  // ("Bound" here means ever used since the last full update - and in this case
  // it's bound to the Direct3D 12 command list in the RTV/DSV path.)
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
  //   is currently available in the textures (RTV/DSV only).
  // - EDRAM base of a currently used RT changed.
  // - Format of a currently used RT changed (RTV/DSV) or pixel size of a
  //   currently used RT changed (ROV).
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

  const auto& regs = register_file_;

#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  uint32_t surface_pitch = std::min(rb_surface_info.surface_pitch, 2560u);
  if (surface_pitch == 0) {
    // TODO(Triang3l): Do something if a memexport-only draw has 0 surface
    // pitch (never seen in any game so far, not sure if even legal).
    return false;
  }
  uint32_t msaa_samples_x =
      rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 2 : 1;
  uint32_t msaa_samples_y =
      rb_surface_info.msaa_samples >= xenos::MsaaSamples::k2X ? 2 : 1;

  // Extract color/depth info in a unified way.
  bool enabled[5];
  uint32_t edram_bases[5];
  uint32_t formats[5];
  bool formats_are_64bpp[5];
  uint32_t color_mask = command_processor_.GetCurrentColorMask(pixel_shader);
  for (uint32_t i = 0; i < 4; ++i) {
    enabled[i] = (color_mask & (0xF << (i * 4))) != 0;
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    edram_bases[i] = std::min(color_info.color_base, 2048u);
    formats[i] = uint32_t(GetBaseColorFormat(color_info.color_format));
    formats_are_64bpp[i] = xenos::IsColorRenderTargetFormat64bpp(
        xenos::ColorRenderTargetFormat(formats[i]));
  }
  auto rb_depthcontrol = regs.Get<reg::RB_DEPTHCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  // 0x1 = stencil test, 0x2 = depth test.
  enabled[4] = rb_depthcontrol.stencil_enable || rb_depthcontrol.z_enable;
  edram_bases[4] = std::min(rb_depth_info.depth_base, 2048u);
  formats[4] = uint32_t(rb_depth_info.depth_format);
  formats_are_64bpp[4] = false;
  // Don't mark depth regions as dirty if not writing the depth.
  // TODO(Triang3l): Make a common function for checking if stencil writing is
  // really done?
  bool depth_readonly =
      !rb_depthcontrol.stencil_enable && !rb_depthcontrol.z_write_enable;

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
      current_msaa_samples_ != rb_surface_info.msaa_samples) {
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
  if (edram_max_rows == UINT32_MAX) {
    // No render targets needed - likely a memexport-only draw, just keep using
    // the current state (or 0 if nothing bound yet, but nothing will be bound
    // anyway so it won't matter).
    edram_max_rows = current_edram_max_rows_;
  } else {
    if (edram_max_rows == 0) {
      // Some render target is totally in the end of EDRAM - can't create
      // textures with 0 height.
      return false;
    }
  }
  // Don't create render targets larger than x2560.
  edram_max_rows = std::min(edram_max_rows, 160u * msaa_samples_y);
  // Check the following full update conditions:
  // - Render target is disabled and another render target got more space than
  //   is currently available in the textures (RTV/DSV only).
  if (!edram_rov_used_ && edram_max_rows > current_edram_max_rows_) {
    full_update = true;
  }

  // Get EDRAM usage of the current draw so dirty regions can be calculated.
  // See D3D12CommandProcessor::UpdateFixedFunctionState for more info.
  int32_t window_offset_y =
      regs.Get<reg::PA_SC_WINDOW_OFFSET>().window_y_offset;
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  float viewport_scale_y = pa_cl_vte_cntl.vport_y_scale_ena
                               ? regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32
                               : 1280.0f;
  float viewport_offset_y = pa_cl_vte_cntl.vport_y_offset_ena
                                ? regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32
                                : std::abs(viewport_scale_y);
  if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
    viewport_offset_y += float(window_offset_y);
  }
  uint32_t viewport_bottom = uint32_t(std::max(
      0.0f, std::ceil(viewport_offset_y + std::abs(viewport_scale_y))));
  uint32_t scissor_bottom = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>().br_y;
  if (!regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>().window_offset_disable) {
    scissor_bottom = std::max(int32_t(scissor_bottom) + window_offset_y, 0);
  }
  uint32_t dirty_bottom =
      std::min(std::min(viewport_bottom, scissor_bottom), 2560u);
  uint32_t edram_dirty_rows =
      std::min((dirty_bottom * msaa_samples_y + 15) >> 4, edram_max_rows);

  // Check the following full update conditions:
  // - EDRAM base of a currently used RT changed.
  // - Format of a currently used RT changed (RTV/DSV) or pixel size of a
  //   currently used RT changed (ROV).
  // Also build a list of render targets to attach in a partial update.
  uint32_t render_targets_to_attach = 0;
  if (!full_update) {
    for (uint32_t i = 0; i < 5; ++i) {
      if (!enabled[i]) {
        continue;
      }
      const RenderTargetBinding& binding = current_bindings_[i];
      if (binding.is_bound) {
        if (binding.edram_base != edram_bases[i]) {
          full_update = true;
          break;
        }
        if (edram_rov_used_) {
          if (i != 4) {
            full_update |= xenos::IsColorRenderTargetFormat64bpp(
                               binding.color_format) != formats_are_64bpp[i];
          }
        } else {
          full_update |= binding.format != formats[i];
        }
        if (full_update) {
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

  bool sample_positions_set = false;

  // Need to change the bindings.
  if (full_update || render_targets_to_attach) {
#if 0
    uint32_t heap_usage[5] = {};
#endif
    if (full_update) {
      if (edram_rov_used_) {
        // Place a UAV barrier because across draws, pixels with different
        // SV_Positions or different sample counts (thus without interlocking
        // between each other) may access the same data now.
        CommitEdramBufferUAVWrites(false);
      } else {
        // Export the currently bound render targets before we ruin the
        // bindings.
        StoreRenderTargetsToEdram();
      }

      ClearBindings();
      current_surface_pitch_ = surface_pitch;
      current_msaa_samples_ = rb_surface_info.msaa_samples;
      if (!edram_rov_used_) {
        current_edram_max_rows_ = edram_max_rows;
      }

      // If updating fully, need to reattach all the render targets and allocate
      // from scratch.
      for (uint32_t i = 0; i < 5; ++i) {
        if (enabled[i]) {
          render_targets_to_attach |= 1 << i;
        }
      }
    } else {
#if 0
      if (!edram_rov_used_) {
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
      }
#endif
    }
    XELOGGPU("RT Cache: {} update - pitch {}, samples {}, RTs to attach {}",
             full_update ? "Full" : "Partial", surface_pitch,
             rb_surface_info.msaa_samples, render_targets_to_attach);

#if 0
    auto device =
        command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();
#endif

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

      if (!edram_rov_used_) {
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
        command_processor_.PushAliasingBarrier(nullptr,
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
    }

    if (!edram_rov_used_) {
      // Sample positions when loading depth must match sample positions when
      // drawing.
      command_processor_.SetSamplePositions(current_msaa_samples_);
      sample_positions_set = true;

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
        LoadRenderTargetsFromEdram(load_render_target_count,
                                   load_render_targets, load_edram_bases);
      }

      // Transition the render targets to the appropriate state if needed,
      // compress the list of the render target because null RTV descriptors are
      // broken in Direct3D 12 and update the list of the render targets to bind
      // to the command list.
      uint32_t rtv_count = 0;
      for (uint32_t i = 0; i < 4; ++i) {
        const RenderTargetBinding& binding = current_bindings_[i];
        if (!binding.is_bound) {
          continue;
        }
        RenderTarget* render_target = binding.render_target;
        if (render_target == nullptr) {
          continue;
        }
        XELOGGPU("RT Color {}: base {}, format {}", i, edram_bases[i],
                 formats[i]);
        command_processor_.PushTransitionBarrier(
            render_target->resource, render_target->state,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        render_target->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        current_pipeline_render_targets_[rtv_count].guest_render_target = i;
        current_pipeline_render_targets_[rtv_count].format =
            GetColorDXGIFormat(xenos::ColorRenderTargetFormat(formats[i]));
        ++rtv_count;
      }
      for (uint32_t i = rtv_count; i < 4; ++i) {
        current_pipeline_render_targets_[i].guest_render_target = i;
        current_pipeline_render_targets_[i].format = DXGI_FORMAT_UNKNOWN;
      }
      const RenderTargetBinding& depth_binding = current_bindings_[4];
      RenderTarget* depth_render_target = depth_binding.render_target;
      current_pipeline_render_targets_[4].guest_render_target = 4;
      if (depth_binding.is_bound && depth_render_target != nullptr) {
        XELOGGPU("RT Depth: base {}, format {}", edram_bases[4], formats[4]);
        command_processor_.PushTransitionBarrier(
            depth_render_target->resource, depth_render_target->state,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depth_render_target->state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        current_pipeline_render_targets_[4].format =
            GetDepthDXGIFormat(xenos::DepthRenderTargetFormat(formats[4]));
      } else {
        current_pipeline_render_targets_[4].format = DXGI_FORMAT_UNKNOWN;
      }
      command_processor_.SubmitBarriers();
      apply_to_command_list_ = true;
    }
  }

  // Bind the render targets to the command list, either in case of an update or
  // if asked to externally.
  if (!edram_rov_used_ && apply_to_command_list_) {
    apply_to_command_list_ = false;
    if (!sample_positions_set) {
      command_processor_.SetSamplePositions(current_msaa_samples_);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[4];
    uint32_t rtv_count;
    for (rtv_count = 0; rtv_count < 4; ++rtv_count) {
      const PipelineRenderTarget& pipeline_render_target =
          current_pipeline_render_targets_[rtv_count];
      if (pipeline_render_target.format == DXGI_FORMAT_UNKNOWN) {
        break;
      }
      const RenderTargetBinding& binding =
          current_bindings_[pipeline_render_target.guest_render_target];
      rtv_handles[rtv_count] = binding.render_target->handle;
    }
    const RenderTargetBinding& depth_binding = current_bindings_[4];
    const D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle =
        current_pipeline_render_targets_[4].format != DXGI_FORMAT_UNKNOWN
            ? &depth_binding.render_target->handle
            : nullptr;
    command_processor_.GetDeferredCommandList().D3DOMSetRenderTargets(
        rtv_count, rtv_handles, FALSE, dsv_handle);
  }

  // Update the dirty regions.
  for (uint32_t i = 0; i < 5; ++i) {
    if (!enabled[i] || (i == 4 && depth_readonly)) {
      continue;
    }
    RenderTargetBinding& binding = current_bindings_[i];
    if (!edram_rov_used_ && binding.render_target == nullptr) {
      // Nothing to store to the EDRAM buffer if there was an error.
      continue;
    }
    binding.edram_dirty_rows =
        std::max(binding.edram_dirty_rows, edram_dirty_rows);
  }

  if (edram_rov_used_) {
    // The buffer will be used for ROV drawing now.
    TransitionEdramBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    edram_buffer_modified_ = true;
  }

  return true;
}

bool RenderTargetCache::Resolve(const Memory& memory,
                                D3D12SharedMemory& shared_memory,
                                TextureCache& texture_cache,
                                uint32_t& written_address_out,
                                uint32_t& written_length_out) {
  written_address_out = 0;
  written_length_out = 0;

  uint32_t resolution_scale = resolution_scale_2x_ ? 2 : 1;
  draw_util::ResolveInfo resolve_info;
  if (!draw_util::GetResolveInfo(
          register_file_, memory, trace_writer_, resolution_scale,
          !edram_rov_used_ && !cvars::d3d12_16bit_rtv_full_range,
          resolve_info)) {
    return false;
  }

  // Nothing to copy/clear.
  if (!resolve_info.address.width_div_8 || !resolve_info.address.height_div_8) {
    return true;
  }

  if (!edram_rov_used_) {
    // Save the currently bound render targets to the EDRAM buffer that will be
    // used as the resolve source and clear bindings to allow render target
    // resources to be reused as source textures for format conversion,
    // resolving samples, to let format conversion bind other render targets,
    // and so after a clear new data will be loaded.
    StoreRenderTargetsToEdram();
    ClearBindings();
  }

  auto& command_list = command_processor_.GetDeferredCommandList();

  // Copying.
  bool copied = false;
  if (resolve_info.copy_dest_length) {
    draw_util::ResolveCopyShaderConstants copy_shader_constants;
    uint32_t copy_group_count_x, copy_group_count_y;
    draw_util::ResolveCopyShaderIndex copy_shader =
        resolve_info.GetCopyShader(resolution_scale, copy_shader_constants,
                                   copy_group_count_x, copy_group_count_y);
    assert_true(copy_group_count_x && copy_group_count_y);
    if (copy_shader != draw_util::ResolveCopyShaderIndex::kUnknown) {
      const draw_util::ResolveCopyShaderInfo& copy_shader_info =
          draw_util::resolve_copy_shader_info[size_t(copy_shader)];

      // Make sure we have the memory to write to.
      bool copy_dest_resident;
      if (resolution_scale_2x_) {
        copy_dest_resident = texture_cache.EnsureScaledResolveBufferResident(
            resolve_info.copy_dest_base, resolve_info.copy_dest_length);
      } else {
        copy_dest_resident = shared_memory.RequestRange(
            resolve_info.copy_dest_base, resolve_info.copy_dest_length);
      }
      if (copy_dest_resident) {
        // Write the descriptors and transition the resources.
        // Full shared memory without resolution scaling, range of the scaled
        // resolve buffer with scaling because only 128 R32 elements can be
        // addressed on Nvidia.
        ui::d3d12::util::DescriptorCPUGPUHandlePair descriptor_dest;
        ui::d3d12::util::DescriptorCPUGPUHandlePair descriptor_source;
        ui::d3d12::util::DescriptorCPUGPUHandlePair descriptors[2];
        if (command_processor_.RequestOneUseSingleViewDescriptors(
                bindless_resources_used_ ? (resolution_scale_2x_ ? 1 : 0) : 2,
                descriptors)) {
          if (bindless_resources_used_) {
            if (resolution_scale_2x_) {
              descriptor_dest = descriptors[0];
            } else {
              descriptor_dest =
                  command_processor_
                      .GetSharedMemoryUintPow2BindlessUAVHandlePair(
                          copy_shader_info.dest_bpe_log2);
            }
            if (copy_shader_info.source_is_raw) {
              descriptor_source =
                  command_processor_.GetSystemBindlessViewHandlePair(
                      D3D12CommandProcessor::SystemBindlessView::kEdramRawSRV);
            } else {
              descriptor_source =
                  command_processor_.GetEdramUintPow2BindlessSRVHandlePair(
                      copy_shader_info.source_bpe_log2);
            }
          } else {
            descriptor_dest = descriptors[0];
            if (!resolution_scale_2x_) {
              shared_memory.WriteUintPow2UAVDescriptor(
                  descriptor_dest.first, copy_shader_info.dest_bpe_log2);
            }
            descriptor_source = descriptors[1];
            if (copy_shader_info.source_is_raw) {
              WriteEdramRawSRVDescriptor(descriptor_source.first);
            } else {
              WriteEdramUintPow2SRVDescriptor(descriptor_source.first,
                                              copy_shader_info.source_bpe_log2);
            }
          }
          if (resolution_scale_2x_) {
            texture_cache.CreateScaledResolveBufferUintPow2UAV(
                descriptor_dest.first, resolve_info.copy_dest_base,
                resolve_info.copy_dest_length, copy_shader_info.dest_bpe_log2);
            texture_cache.UseScaledResolveBufferForWriting();
          } else {
            shared_memory.UseForWriting();
          }
          TransitionEdramBuffer(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

          // Submit the resolve.
          command_list.D3DSetComputeRootSignature(resolve_copy_root_signature_);
          command_list.D3DSetComputeRootDescriptorTable(
              2, descriptor_source.second);
          command_list.D3DSetComputeRootDescriptorTable(1,
                                                        descriptor_dest.second);
          if (resolution_scale_2x_) {
            command_list.D3DSetComputeRoot32BitConstants(
                0,
                sizeof(copy_shader_constants.dest_relative) / sizeof(uint32_t),
                &copy_shader_constants.dest_relative, 0);
          } else {
            command_list.D3DSetComputeRoot32BitConstants(
                0, sizeof(copy_shader_constants) / sizeof(uint32_t),
                &copy_shader_constants, 0);
          }
          command_processor_.SetComputePipeline(
              resolve_copy_pipelines_[size_t(copy_shader)]);
          command_processor_.SubmitBarriers();
          command_list.D3DDispatch(copy_group_count_x, copy_group_count_y, 1);

          // Order the resolve with other work using the destination as a UAV.
          if (resolution_scale_2x_) {
            texture_cache.MarkScaledResolveBufferUAVWritesCommitNeeded();
          } else {
            shared_memory.MarkUAVWritesCommitNeeded();
          }

          // Invalidate textures and mark the range as scaled if needed.
          texture_cache.MarkRangeAsResolved(resolve_info.copy_dest_base,
                                            resolve_info.copy_dest_length);
          written_address_out = resolve_info.copy_dest_base;
          written_length_out = resolve_info.copy_dest_length;
          copied = true;
        }
      } else {
        XELOGE("Failed to obtain the resolve destination memory region");
      }
    }
  } else {
    copied = true;
  }

  // Clearing.
  bool cleared = false;
  bool clear_depth = resolve_info.IsClearingDepth();
  bool clear_color = resolve_info.IsClearingColor();
  if (clear_depth || clear_color) {
    ui::d3d12::util::DescriptorCPUGPUHandlePair descriptor_edram;
    bool descriptor_edram_obtained;
    if (bindless_resources_used_) {
      descriptor_edram = command_processor_.GetSystemBindlessViewHandlePair(
          D3D12CommandProcessor::SystemBindlessView::kEdramR32G32B32A32UintUAV);
      descriptor_edram_obtained = true;
    } else {
      descriptor_edram_obtained =
          command_processor_.RequestOneUseSingleViewDescriptors(
              1, &descriptor_edram);
      if (descriptor_edram_obtained) {
        WriteEdramUintPow2UAVDescriptor(descriptor_edram.first, 4);
      }
    }
    if (descriptor_edram_obtained) {
      TransitionEdramBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      // Should be safe to only commit once (if was UAV previously - if nothing
      // to copy for some reason, for instance), overlap of the depth and the
      // color ranges is highly unlikely.
      CommitEdramBufferUAVWrites(false);
      command_list.D3DSetComputeRootSignature(resolve_clear_root_signature_);
      command_list.D3DSetComputeRootDescriptorTable(1, descriptor_edram.second);
      std::pair<uint32_t, uint32_t> clear_group_count =
          resolve_info.GetClearShaderGroupCount();
      assert_true(clear_group_count.first && clear_group_count.second);
      if (clear_depth) {
        // Also clear the host 32-bit floating-point depth used for loaing and
        // storing 24-bit floating-point depth at full precision.
        bool clear_float32_depth = !edram_rov_used_ &&
                                   depth_float24_conversion_ ==
                                       flags::DepthFloat24Conversion::kOnCopy &&
                                   xenos::DepthRenderTargetFormat(
                                       resolve_info.depth_edram_info.format) ==
                                       xenos::DepthRenderTargetFormat::kD24FS8;
        draw_util::ResolveClearShaderConstants depth_clear_constants;
        resolve_info.GetDepthClearShaderConstants(clear_float32_depth,
                                                  depth_clear_constants);
        command_list.D3DSetComputeRoot32BitConstants(
            0, sizeof(depth_clear_constants) / sizeof(uint32_t),
            &depth_clear_constants, 0);
        command_processor_.SetComputePipeline(
            clear_float32_depth ? resolve_clear_depth_24_32_pipeline_
                                : resolve_clear_32bpp_pipeline_);
        command_processor_.SubmitBarriers();
        command_list.D3DDispatch(clear_group_count.first,
                                 clear_group_count.second, 1);
      }
      if (clear_color) {
        draw_util::ResolveClearShaderConstants color_clear_constants;
        resolve_info.GetColorClearShaderConstants(color_clear_constants);
        if (clear_depth) {
          // Non-RT-specific constants have already been set.
          command_list.D3DSetComputeRoot32BitConstants(
              0, sizeof(color_clear_constants.rt_specific) / sizeof(uint32_t),
              &color_clear_constants.rt_specific,
              offsetof(draw_util::ResolveClearShaderConstants, rt_specific) /
                  sizeof(uint32_t));
        } else {
          command_list.D3DSetComputeRoot32BitConstants(
              0, sizeof(color_clear_constants) / sizeof(uint32_t),
              &color_clear_constants, 0);
        }
        command_processor_.SetComputePipeline(
            resolve_info.color_edram_info.format_is_64bpp
                ? resolve_clear_64bpp_pipeline_
                : resolve_clear_32bpp_pipeline_);
        command_processor_.SubmitBarriers();
        command_list.D3DDispatch(clear_group_count.first,
                                 clear_group_count.second, 1);
      }
      assert_true(edram_buffer_state_ == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      edram_buffer_modified_ = true;
      cleared = true;
    }
  } else {
    cleared = true;
  }

  return copied && cleared;
}

void RenderTargetCache::FlushAndUnbindRenderTargets() {
  if (edram_rov_used_) {
    return;
  }
  StoreRenderTargetsToEdram();
  ClearBindings();
}

void RenderTargetCache::WriteEdramRawSRVDescriptor(
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();
  device->CopyDescriptorsSimple(
      1, handle,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kRawSRV)),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RenderTargetCache::WriteEdramRawUAVDescriptor(
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();
  device->CopyDescriptorsSimple(
      1, handle,
      provider.OffsetViewDescriptor(
          edram_buffer_descriptor_heap_start_,
          uint32_t(EdramBufferDescriptorIndex::kRawUAV)),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RenderTargetCache::WriteEdramUintPow2SRVDescriptor(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2) {
  EdramBufferDescriptorIndex descriptor_index;
  switch (element_size_bytes_pow2) {
    case 2:
      descriptor_index = EdramBufferDescriptorIndex::kR32UintSRV;
      break;
    case 3:
      descriptor_index = EdramBufferDescriptorIndex::kR32G32UintSRV;
      break;
    case 4:
      descriptor_index = EdramBufferDescriptorIndex::kR32G32B32A32UintSRV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      return;
  }
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();
  device->CopyDescriptorsSimple(
      1, handle,
      provider.OffsetViewDescriptor(edram_buffer_descriptor_heap_start_,
                                    uint32_t(descriptor_index)),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RenderTargetCache::WriteEdramUintPow2UAVDescriptor(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2) {
  EdramBufferDescriptorIndex descriptor_index;
  switch (element_size_bytes_pow2) {
    case 2:
      descriptor_index = EdramBufferDescriptorIndex::kR32UintUAV;
      break;
    // 3 is not needed currently.
    case 4:
      descriptor_index = EdramBufferDescriptorIndex::kR32G32B32A32UintUAV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      return;
  }
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();
  device->CopyDescriptorsSimple(
      1, handle,
      provider.OffsetViewDescriptor(edram_buffer_descriptor_heap_start_,
                                    uint32_t(descriptor_index)),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

xenos::ColorRenderTargetFormat RenderTargetCache::GetBaseColorFormat(
    xenos::ColorRenderTargetFormat format) {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return xenos::ColorRenderTargetFormat::k_8_8_8_8;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return xenos::ColorRenderTargetFormat::k_2_10_10_10;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT;
    default:
      return format;
  }
}

DXGI_FORMAT RenderTargetCache::GetColorDXGIFormat(
    xenos::ColorRenderTargetFormat format) {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return DXGI_FORMAT_R10G10B10A2_UNORM;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case xenos::ColorRenderTargetFormat::k_16_16:
      return DXGI_FORMAT_R16G16_SNORM;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      return DXGI_FORMAT_R16G16B16A16_SNORM;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return DXGI_FORMAT_R16G16_FLOAT;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return DXGI_FORMAT_R32_FLOAT;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return DXGI_FORMAT_R32G32_FLOAT;
    default:
      break;
  }
  return DXGI_FORMAT_UNKNOWN;
}

bool RenderTargetCache::InitializeTraceSubmitDownloads() {
  if (resolution_scale_2x_) {
    // No 1:1 mapping.
    return false;
  }
  if (!edram_snapshot_download_buffer_) {
    D3D12_RESOURCE_DESC edram_snapshot_download_buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(edram_snapshot_download_buffer_desc,
                                            xenos::kEdramSizeBytes,
                                            D3D12_RESOURCE_FLAG_NONE);
    auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
    auto device = provider.GetDevice();
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback,
            provider.GetHeapFlagCreateNotZeroed(),
            &edram_snapshot_download_buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&edram_snapshot_download_buffer_)))) {
      XELOGE("Failed to create a EDRAM snapshot download buffer");
      return false;
    }
  }
  auto& command_list = command_processor_.GetDeferredCommandList();
  TransitionEdramBuffer(D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_processor_.SubmitBarriers();
  command_list.D3DCopyBufferRegion(edram_snapshot_download_buffer_, 0,
                                   edram_buffer_, 0, xenos::kEdramSizeBytes);
  return true;
}

void RenderTargetCache::InitializeTraceCompleteDownloads() {
  if (!edram_snapshot_download_buffer_) {
    return;
  }
  void* download_mapping;
  if (SUCCEEDED(edram_snapshot_download_buffer_->Map(0, nullptr,
                                                     &download_mapping))) {
    trace_writer_.WriteEdramSnapshot(download_mapping);
    D3D12_RANGE download_write_range = {};
    edram_snapshot_download_buffer_->Unmap(0, &download_write_range);
  } else {
    XELOGE("Failed to map the EDRAM snapshot download buffer");
  }
  edram_snapshot_download_buffer_->Release();
  edram_snapshot_download_buffer_ = nullptr;
}

void RenderTargetCache::RestoreEdramSnapshot(const void* snapshot) {
  if (resolution_scale_2x_) {
    // No 1:1 mapping.
    return;
  }
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  if (!edram_snapshot_restore_pool_) {
    edram_snapshot_restore_pool_ =
        std::make_unique<ui::d3d12::D3D12UploadBufferPool>(
            provider, xenos::kEdramSizeBytes);
  }
  ID3D12Resource* upload_buffer;
  size_t upload_buffer_offset;
  void* upload_buffer_mapping = edram_snapshot_restore_pool_->Request(
      command_processor_.GetCurrentSubmission(), xenos::kEdramSizeBytes, 1,
      &upload_buffer, &upload_buffer_offset, nullptr);
  if (!upload_buffer_mapping) {
    XELOGE("Failed to get a buffer for restoring a EDRAM snapshot");
    return;
  }
  std::memcpy(upload_buffer_mapping, snapshot, xenos::kEdramSizeBytes);
  auto& command_list = command_processor_.GetDeferredCommandList();
  TransitionEdramBuffer(D3D12_RESOURCE_STATE_COPY_DEST);
  command_processor_.SubmitBarriers();
  command_list.D3DCopyBufferRegion(edram_buffer_, 0, upload_buffer,
                                   UINT64(upload_buffer_offset),
                                   xenos::kEdramSizeBytes);
  if (!edram_rov_used_) {
    // Clear and ignore the old 32-bit float depth - the non-ROV path is
    // inaccurate anyway, and this is backend-specific, not a part of a guest
    // trace.
    bool edram_shader_visible_r32_uav_obtained;
    ui::d3d12::util::DescriptorCPUGPUHandlePair edram_shader_visible_r32_uav;
    if (bindless_resources_used_) {
      edram_shader_visible_r32_uav_obtained = true;
      edram_shader_visible_r32_uav =
          command_processor_.GetSystemBindlessViewHandlePair(
              D3D12CommandProcessor::SystemBindlessView::kEdramR32UintUAV);
    } else {
      edram_shader_visible_r32_uav_obtained =
          command_processor_.RequestOneUseSingleViewDescriptors(
              1, &edram_shader_visible_r32_uav);
      if (edram_shader_visible_r32_uav_obtained) {
        WriteEdramUintPow2UAVDescriptor(edram_shader_visible_r32_uav.first, 2);
      }
    }
    if (edram_shader_visible_r32_uav_obtained) {
      UINT clear_value[4] = {0, 0, 0, 0};
      D3D12_RECT clear_rect;
      clear_rect.left = xenos::kEdramSizeBytes >> 2;
      clear_rect.top = 0;
      clear_rect.right = (xenos::kEdramSizeBytes >> 2) << 1;
      clear_rect.bottom = 1;
      TransitionEdramBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      command_processor_.SubmitBarriers();
      // ClearUnorderedAccessView takes a shader-visible GPU descriptor and a
      // non-shader-visible CPU descriptor.
      command_list.D3DClearUnorderedAccessViewUint(
          edram_shader_visible_r32_uav.second,
          provider.OffsetViewDescriptor(
              edram_buffer_descriptor_heap_start_,
              uint32_t(EdramBufferDescriptorIndex::kR32UintUAV)),
          edram_buffer_, clear_value, 1, &clear_rect);
    }
  }
}

uint32_t RenderTargetCache::GetEdramBufferSize() const {
  uint32_t size = xenos::kEdramSizeBytes;
  if (!edram_rov_used_ &&
      depth_float24_conversion_ == flags::DepthFloat24Conversion::kOnCopy) {
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

void RenderTargetCache::TransitionEdramBuffer(D3D12_RESOURCE_STATES new_state) {
  command_processor_.PushTransitionBarrier(edram_buffer_, edram_buffer_state_,
                                           new_state);
  edram_buffer_state_ = new_state;
  if (new_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    edram_buffer_modified_ = false;
  }
}

void RenderTargetCache::CommitEdramBufferUAVWrites(bool force) {
  if ((edram_buffer_modified_ || force) &&
      edram_buffer_state_ == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    command_processor_.PushUAVBarrier(edram_buffer_);
  }
  edram_buffer_modified_ = false;
}

void RenderTargetCache::ClearBindings() {
  current_surface_pitch_ = 0;
  current_msaa_samples_ = xenos::MsaaSamples::k1X;
  current_edram_max_rows_ = 0;
  std::memset(current_bindings_, 0, sizeof(current_bindings_));
  apply_to_command_list_ = true;
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
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = kHeap4MBPages << 22;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  // TODO(Triang3l): If real MSAA is added, alignment must be 4 MB.
  heap_desc.Alignment = 0;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES |
                    provider.GetHeapFlagCreateNotZeroed();
  if (FAILED(
          device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heaps_[heap_index])))) {
    XELOGE("Failed to create a {} MB heap for render targets",
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
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
  heap_desc.Type = is_depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV
                            : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = kRenderTargetDescriptorHeapSize;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;
  ID3D12DescriptorHeap* new_d3d_heap;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc,
                                          IID_PPV_ARGS(&new_d3d_heap)))) {
    XELOGE("Failed to create a heap for {} {} buffer descriptors",
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
      key.is_depth
          ? GetDepthDXGIFormat(xenos::DepthRenderTargetFormat(key.format))
          : GetColorDXGIFormat(xenos::ColorRenderTargetFormat(key.format));
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

  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();

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
        "Failed to create a placed resource for {}x{} {} render target with "
        "format {} at heap 4 MB pages {}:{}",
        uint32_t(resource_desc.Width), resource_desc.Height,
        key.is_depth ? "depth" : "color", key.format, heap_page_first,
        heap_page_first + heap_page_count - 1);
    return nullptr;
  }
#else
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault,
          provider.GetHeapFlagCreateNotZeroed(), &resource_desc, state, nullptr,
          IID_PPV_ARGS(&resource)))) {
    XELOGE(
        "Failed to create a committed resource for {}x{} {} render target with "
        "format {}",
        uint32_t(resource_desc.Width), resource_desc.Height,
        key.is_depth ? "depth" : "color", key.format);
    return nullptr;
  }
#endif

  // Create the descriptor for the render target.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle;
  if (key.is_depth) {
    descriptor_handle =
        provider.OffsetDSVDescriptor(descriptor_heaps_depth_->start_handle,
                                     descriptor_heaps_depth_->descriptors_used);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Format = resource_desc.Format;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.Texture2D.MipSlice = 0;
    device->CreateDepthStencilView(resource, &dsv_desc, descriptor_handle);
    ++descriptor_heaps_depth_->descriptors_used;
  } else {
    descriptor_handle =
        provider.OffsetRTVDescriptor(descriptor_heaps_color_->start_handle,
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
  render_targets_.emplace(key.value, render_target);
  COUNT_profile_set("gpu/render_target_cache/render_targets",
                    render_targets_.size());
#if 0
  XELOGGPU(
      "Created {}x{} {} render target with format {} at heap 4 MB pages {}:{}",
      uint32_t(resource_desc.Width), resource_desc.Height,
      key.is_depth ? "depth" : "color", key.format, heap_page_first,
      heap_page_first + heap_page_count - 1);
#else
  XELOGGPU("Created {}x{} {} render target with format {}",
           uint32_t(resource_desc.Width), resource_desc.Height,
           key.is_depth ? "depth" : "color", key.format);
#endif
  return render_target;
}

RenderTargetCache::EdramLoadStoreMode RenderTargetCache::GetLoadStoreMode(
    bool is_depth, uint32_t format) const {
  if (is_depth) {
    if (xenos::DepthRenderTargetFormat(format) ==
        xenos::DepthRenderTargetFormat::kD24FS8) {
      return depth_float24_conversion_ == flags::DepthFloat24Conversion::kOnCopy
                 ? EdramLoadStoreMode::kDepthFloat24And32
                 : EdramLoadStoreMode::kDepthFloat;
    }
    return EdramLoadStoreMode::kDepthUnorm;
  }
  xenos::ColorRenderTargetFormat color_format =
      xenos::ColorRenderTargetFormat(format);
  if (color_format == xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
      color_format ==
          xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16) {
    return EdramLoadStoreMode::kColor7e3;
  }
  return xenos::IsColorRenderTargetFormat64bpp(color_format)
             ? EdramLoadStoreMode::kColor64bpp
             : EdramLoadStoreMode::kColor32bpp;
}

void RenderTargetCache::StoreRenderTargetsToEdram() {
  if (edram_rov_used_) {
    return;
  }

  auto& command_list = command_processor_.GetDeferredCommandList();

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
  ui::d3d12::util::DescriptorCPUGPUHandlePair descriptor_edram;
  ui::d3d12::util::DescriptorCPUGPUHandlePair descriptor_source;
  if (bindless_resources_used_) {
    if (!command_processor_.RequestOneUseSingleViewDescriptors(
            1, &descriptor_source)) {
      return;
    }
    descriptor_edram = command_processor_.GetSystemBindlessViewHandlePair(
        D3D12CommandProcessor::SystemBindlessView::kEdramRawUAV);
  } else {
    ui::d3d12::util::DescriptorCPUGPUHandlePair descriptors[2];
    if (!command_processor_.RequestOneUseSingleViewDescriptors(2,
                                                               descriptors)) {
      return;
    }
    descriptor_edram = descriptors[0];
    WriteEdramRawUAVDescriptor(descriptor_edram.first);
    descriptor_source = descriptors[1];
  }

  // Get the buffer for copying.
  D3D12_RESOURCE_STATES copy_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* copy_buffer = command_processor_.RequestScratchGPUBuffer(
      copy_buffer_size, copy_buffer_state);
  if (copy_buffer == nullptr) {
    return;
  }

  // Transition the render targets that need to be stored to copy sources and
  // the EDRAM buffer to a UAV.
  for (uint32_t i = 0; i < store_binding_count; ++i) {
    RenderTarget* render_target =
        current_bindings_[store_bindings[i]].render_target;
    command_processor_.PushTransitionBarrier(render_target->resource,
                                             render_target->state,
                                             D3D12_RESOURCE_STATE_COPY_SOURCE);
    render_target->state = D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  TransitionEdramBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  // Set up the bindings.
  auto device =
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();
  command_list.D3DSetComputeRootSignature(edram_load_store_root_signature_);
  ui::d3d12::util::CreateBufferRawSRV(device, descriptor_source.first,
                                      copy_buffer, copy_buffer_size);
  command_list.D3DSetComputeRootDescriptorTable(2, descriptor_source.second);
  command_list.D3DSetComputeRootDescriptorTable(1, descriptor_edram.second);

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
      (current_msaa_samples_ >= xenos::MsaaSamples::k4X ? 2 : 1);
  uint32_t surface_pitch_tiles = (surface_pitch_ss + 79) / 80;
  assert_true(surface_pitch_tiles != 0);

  // Store each render target.
  for (uint32_t i = 0; i < store_binding_count; ++i) {
    const RenderTargetBinding& binding = current_bindings_[store_bindings[i]];
    const RenderTarget* render_target = binding.render_target;
    bool is_64bpp = false;

    // Transition the copy buffer to copy destination.
    command_processor_.PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                             D3D12_RESOURCE_STATE_COPY_DEST);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
    command_processor_.SubmitBarriers();

    // Copy from the render target planes and set up the layout.
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = render_target->resource;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_source.SubresourceIndex = 0;
    location_dest.pResource = copy_buffer;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_dest.PlacedFootprint = render_target->footprints[0];
    // TODO(Triang3l): Box for color render targets.
    command_list.CopyTexture(location_dest, location_source);
    EdramLoadStoreRootConstants root_constants;
    uint32_t rt_pitch_tiles = surface_pitch_tiles;
    if (!render_target->key.is_depth &&
        xenos::IsColorRenderTargetFormat64bpp(
            xenos::ColorRenderTargetFormat(render_target->key.format))) {
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
      command_list.CopyTexture(location_dest, location_source);
      root_constants.rt_stencil_offset =
          uint32_t(location_dest.PlacedFootprint.Offset);
      root_constants.rt_stencil_pitch =
          location_dest.PlacedFootprint.Footprint.RowPitch;
    }

    // Transition the copy buffer to SRV.
    command_processor_.PushTransitionBarrier(
        copy_buffer, copy_buffer_state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_processor_.SubmitBarriers();

    // Store the data.
    command_list.D3DSetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    EdramLoadStoreMode mode = GetLoadStoreMode(render_target->key.is_depth,
                                               render_target->key.format);
    command_processor_.SetComputePipeline(edram_store_pipelines_[size_t(mode)]);
    // 1 group per 80x16 samples.
    command_list.D3DDispatch(surface_pitch_tiles, binding.edram_dirty_rows, 1);

    // Commit the UAV write.
    CommitEdramBufferUAVWrites(true);
  }

  command_processor_.ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
}

void RenderTargetCache::LoadRenderTargetsFromEdram(
    uint32_t render_target_count, RenderTarget* const* render_targets,
    const uint32_t* edram_bases) {
  assert_true(render_target_count <= 5);
  if (render_target_count == 0 || render_target_count > 5) {
    return;
  }

  auto& command_list = command_processor_.GetDeferredCommandList();

  // Allocate descriptors for the buffers.
  ui::d3d12::util::DescriptorCPUGPUHandlePair descriptor_dest, descriptor_edram;
  if (bindless_resources_used_) {
    if (!command_processor_.RequestOneUseSingleViewDescriptors(
            1, &descriptor_dest)) {
      return;
    }
    descriptor_edram = command_processor_.GetSystemBindlessViewHandlePair(
        D3D12CommandProcessor::SystemBindlessView::kEdramRawSRV);
  } else {
    ui::d3d12::util::DescriptorCPUGPUHandlePair descriptors[2];
    if (!command_processor_.RequestOneUseSingleViewDescriptors(2,
                                                               descriptors)) {
      return;
    }
    descriptor_dest = descriptors[0];
    descriptor_edram = descriptors[1];
    WriteEdramRawSRVDescriptor(descriptor_edram.first);
  }

  // Get the buffer for copying.
  uint32_t copy_buffer_size = 0;
  for (uint32_t i = 0; i < render_target_count; ++i) {
    copy_buffer_size =
        std::max(copy_buffer_size, render_targets[i]->copy_buffer_size);
  }
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_.RequestScratchGPUBuffer(
      copy_buffer_size, copy_buffer_state);
  if (copy_buffer == nullptr) {
    return;
  }

  // Transition the render targets to copy destinations and the EDRAM buffer to
  // a SRV.
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* render_target = render_targets[i];
    command_processor_.PushTransitionBarrier(render_target->resource,
                                             render_target->state,
                                             D3D12_RESOURCE_STATE_COPY_DEST);
    render_target->state = D3D12_RESOURCE_STATE_COPY_DEST;
  }
  TransitionEdramBuffer(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  // Set up the bindings.
  auto device =
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();
  command_list.D3DSetComputeRootSignature(edram_load_store_root_signature_);
  command_list.D3DSetComputeRootDescriptorTable(2, descriptor_edram.second);
  ui::d3d12::util::CreateBufferRawUAV(device, descriptor_dest.first,
                                      copy_buffer, copy_buffer_size);
  command_list.D3DSetComputeRootDescriptorTable(1, descriptor_dest.second);

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
        xenos::IsColorRenderTargetFormat64bpp(
            xenos::ColorRenderTargetFormat(render_target->key.format))) {
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
    command_processor_.PushTransitionBarrier(
        copy_buffer, copy_buffer_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    copy_buffer_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Load the data.
    command_processor_.SubmitBarriers();
    EdramLoadStoreRootConstants root_constants;
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
    command_list.D3DSetComputeRoot32BitConstants(
        0, sizeof(root_constants) / sizeof(uint32_t), &root_constants, 0);
    EdramLoadStoreMode mode = GetLoadStoreMode(render_target->key.is_depth,
                                               render_target->key.format);
    command_processor_.SetComputePipeline(edram_load_pipelines_[size_t(mode)]);
    // 1 group per 80x16 samples.
    command_list.D3DDispatch(render_target->key.width_ss_div_80, edram_rows, 1);

    // Commit the UAV write and transition the copy buffer to copy source now.
    command_processor_.PushUAVBarrier(copy_buffer);
    command_processor_.PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                             D3D12_RESOURCE_STATE_COPY_SOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;

    // Copy to the render target planes.
    command_processor_.SubmitBarriers();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = copy_buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = render_target->footprints[0];
    location_dest.pResource = render_target->resource;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    command_list.CopyTexture(location_dest, location_source);
    if (render_target->key.is_depth) {
      location_source.PlacedFootprint = render_target->footprints[1];
      location_dest.SubresourceIndex = 1;
      command_list.CopyTexture(location_dest, location_source);
    }
  }

  command_processor_.ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
