/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_texture_cache.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/d3d12/d3d12_shared_memory.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_128bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_128bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_16bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_16bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_32bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_32bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_64bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_64bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_8bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_8bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_bgrg8_rgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_bgrg8_rgbg8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_ctx1_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_float_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxn_rg8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt1_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt3_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt3a_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt3aas1111_bgra4_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt5_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt5a_r8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_gbgr8_grgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_gbgr8_rgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_snorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_snorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r4g4b4a4_b4g4r4a4_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r4g4b4a4_b4g4r4a4_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g6b5_b5g6r5_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g6b5_b5g6r5_scaled_cs.h"
}  // namespace shaders

/*
        chrispy: we're getting cache misses in GetHostFormatSwizzle, use a
   denser array todo: not all 65536 possible swizzles are used, this could
   probably be one cache line
*/
using SwizzleArray = std::array<unsigned short, 64>;

static constexpr SwizzleArray build_xenos_swizzle_for_format() {
  SwizzleArray result{0};

  for (int i = 0; i < 64; ++i) {
    result[i] =
        static_cast<uint16_t>(D3D12TextureCache::host_formats_[i].swizzle);
  }
  return result;
}
alignas(64) constexpr SwizzleArray xenos_swizzle_for_format =
    build_xenos_swizzle_for_format();

D3D12TextureCache::D3D12TextureCache(const RegisterFile& register_file,
                                     D3D12SharedMemory& shared_memory,
                                     uint32_t draw_resolution_scale_x,
                                     uint32_t draw_resolution_scale_y,
                                     D3D12CommandProcessor& command_processor,
                                     bool bindless_resources_used)
    : TextureCache(register_file, shared_memory, draw_resolution_scale_x,
                   draw_resolution_scale_y),
      command_processor_(command_processor),
      bindless_resources_used_(bindless_resources_used) {}

D3D12TextureCache::~D3D12TextureCache() {
  // While the texture descriptor cache still exists (referenced by
  // ~D3D12Texture), destroy all textures.
  DestroyAllTextures(true);

  // First release the buffers to detach them from the heaps.
  for (std::unique_ptr<ScaledResolveVirtualBuffer>& scaled_resolve_buffer_ptr :
       scaled_resolve_2gb_buffers_) {
    scaled_resolve_buffer_ptr.reset();
  }
  scaled_resolve_heaps_.clear();
  COUNT_profile_set("gpu/texture_cache/scaled_resolve_buffer_used_mb", 0);
}

bool D3D12TextureCache::Initialize() {
  const ui::d3d12::D3D12Provider& provider =
      command_processor_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();

  if (IsDrawResolutionScaled()) {
    // Buffers not used yet - no need aliasing barriers to change ownership of
    // gigabytes between even and odd buffers.
    std::memset(scaled_resolve_1gb_buffer_indices_, UINT8_MAX,
                sizeof(scaled_resolve_1gb_buffer_indices_));
    assert_true(scaled_resolve_heaps_.empty());
    uint64_t scaled_resolve_address_space_size =
        uint64_t(SharedMemory::kBufferSize) *
        (draw_resolution_scale_x() * draw_resolution_scale_y());
    scaled_resolve_heaps_.resize(size_t(scaled_resolve_address_space_size >>
                                        kScaledResolveHeapSizeLog2));
  }
  scaled_resolve_heap_count_ = 0;

  // Create the loading root signature.
  D3D12_ROOT_PARAMETER root_parameters[3];
  // Parameter 0 is constants (changed multiple times when untiling).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameters[0].Descriptor.ShaderRegister = 0;
  root_parameters[0].Descriptor.RegisterSpace = 0;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is the source (may be changed multiple times for the same
  // destination).
  D3D12_DESCRIPTOR_RANGE root_dest_range;
  root_dest_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  root_dest_range.NumDescriptors = 1;
  root_dest_range.BaseShaderRegister = 0;
  root_dest_range.RegisterSpace = 0;
  root_dest_range.OffsetInDescriptorsFromTableStart = 0;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[1].DescriptorTable.pDescriptorRanges = &root_dest_range;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 2 is the destination.
  D3D12_DESCRIPTOR_RANGE root_source_range;
  root_source_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  root_source_range.NumDescriptors = 1;
  root_source_range.BaseShaderRegister = 0;
  root_source_range.RegisterSpace = 0;
  root_source_range.OffsetInDescriptorsFromTableStart = 0;
  root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[2].DescriptorTable.pDescriptorRanges = &root_source_range;
  root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.NumParameters = UINT(xe::countof(root_parameters));
  root_signature_desc.pParameters = root_parameters;
  root_signature_desc.NumStaticSamplers = 0;
  root_signature_desc.pStaticSamplers = nullptr;
  root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(load_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider, root_signature_desc);
  if (!load_root_signature_) {
    XELOGE(
        "D3D12TextureCache: Failed to create the texture loading root "
        "signature");
    return false;
  }

  // Specify the load shader code.
  D3D12_SHADER_BYTECODE load_shader_code[kLoadShaderCount] = {};
  load_shader_code[kLoadShaderIndex8bpb] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_8bpb_cs, sizeof(shaders::texture_load_8bpb_cs)};
  load_shader_code[kLoadShaderIndex16bpb] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_16bpb_cs, sizeof(shaders::texture_load_16bpb_cs)};
  load_shader_code[kLoadShaderIndex32bpb] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_32bpb_cs, sizeof(shaders::texture_load_32bpb_cs)};
  load_shader_code[kLoadShaderIndex64bpb] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_64bpb_cs, sizeof(shaders::texture_load_64bpb_cs)};
  load_shader_code[kLoadShaderIndex128bpb] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_128bpb_cs, sizeof(shaders::texture_load_128bpb_cs)};
  load_shader_code[kLoadShaderIndexR5G5B5A1ToB5G5R5A1] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_r5g5b5a1_b5g5r5a1_cs,
                            sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_cs)};
  load_shader_code[kLoadShaderIndexR5G6B5ToB5G6R5] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_r5g6b5_b5g6r5_cs,
                            sizeof(shaders::texture_load_r5g6b5_b5g6r5_cs)};
  load_shader_code[kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle] =
      D3D12_SHADER_BYTECODE{
          shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs,
          sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs)};
  load_shader_code[kLoadShaderIndexRGBA4ToBGRA4] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_r4g4b4a4_b4g4r4a4_cs,
                            sizeof(shaders::texture_load_r4g4b4a4_b4g4r4a4_cs)};
  load_shader_code[kLoadShaderIndexGBGR8ToGRGB8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_gbgr8_grgb8_cs,
                            sizeof(shaders::texture_load_gbgr8_grgb8_cs)};
  load_shader_code[kLoadShaderIndexGBGR8ToRGB8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_gbgr8_rgb8_cs,
                            sizeof(shaders::texture_load_gbgr8_rgb8_cs)};
  load_shader_code[kLoadShaderIndexBGRG8ToRGBG8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_bgrg8_rgbg8_cs,
                            sizeof(shaders::texture_load_bgrg8_rgbg8_cs)};
  load_shader_code[kLoadShaderIndexBGRG8ToRGB8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_bgrg8_rgb8_cs,
                            sizeof(shaders::texture_load_bgrg8_rgb8_cs)};
  load_shader_code[kLoadShaderIndexR10G11B11ToRGBA16] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_r10g11b11_rgba16_cs,
                            sizeof(shaders::texture_load_r10g11b11_rgba16_cs)};
  load_shader_code[kLoadShaderIndexR10G11B11ToRGBA16SNorm] =
      D3D12_SHADER_BYTECODE{
          shaders::texture_load_r10g11b11_rgba16_snorm_cs,
          sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_cs)};
  load_shader_code[kLoadShaderIndexR11G11B10ToRGBA16] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_r11g11b10_rgba16_cs,
                            sizeof(shaders::texture_load_r11g11b10_rgba16_cs)};
  load_shader_code[kLoadShaderIndexR11G11B10ToRGBA16SNorm] =
      D3D12_SHADER_BYTECODE{
          shaders::texture_load_r11g11b10_rgba16_snorm_cs,
          sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_cs)};
  load_shader_code[kLoadShaderIndexDXT1ToRGBA8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_dxt1_rgba8_cs,
                            sizeof(shaders::texture_load_dxt1_rgba8_cs)};
  load_shader_code[kLoadShaderIndexDXT3ToRGBA8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_dxt3_rgba8_cs,
                            sizeof(shaders::texture_load_dxt3_rgba8_cs)};
  load_shader_code[kLoadShaderIndexDXT5ToRGBA8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_dxt5_rgba8_cs,
                            sizeof(shaders::texture_load_dxt5_rgba8_cs)};
  load_shader_code[kLoadShaderIndexDXNToRG8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_dxn_rg8_cs,
                            sizeof(shaders::texture_load_dxn_rg8_cs)};
  load_shader_code[kLoadShaderIndexDXT3A] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_dxt3a_cs, sizeof(shaders::texture_load_dxt3a_cs)};
  load_shader_code[kLoadShaderIndexDXT3AAs1111ToBGRA4] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_dxt3aas1111_bgra4_cs,
                            sizeof(shaders::texture_load_dxt3aas1111_bgra4_cs)};
  load_shader_code[kLoadShaderIndexDXT5AToR8] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_dxt5a_r8_cs,
                            sizeof(shaders::texture_load_dxt5a_r8_cs)};
  load_shader_code[kLoadShaderIndexCTX1] = D3D12_SHADER_BYTECODE{
      shaders::texture_load_ctx1_cs, sizeof(shaders::texture_load_ctx1_cs)};
  load_shader_code[kLoadShaderIndexDepthUnorm] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_depth_unorm_cs,
                            sizeof(shaders::texture_load_depth_unorm_cs)};
  load_shader_code[kLoadShaderIndexDepthFloat] =
      D3D12_SHADER_BYTECODE{shaders::texture_load_depth_float_cs,
                            sizeof(shaders::texture_load_depth_float_cs)};
  D3D12_SHADER_BYTECODE load_shader_code_scaled[kLoadShaderCount] = {};
  if (IsDrawResolutionScaled()) {
    load_shader_code_scaled[kLoadShaderIndex8bpb] =
        D3D12_SHADER_BYTECODE{shaders::texture_load_8bpb_scaled_cs,
                              sizeof(shaders::texture_load_8bpb_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndex16bpb] =
        D3D12_SHADER_BYTECODE{shaders::texture_load_16bpb_scaled_cs,
                              sizeof(shaders::texture_load_16bpb_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndex32bpb] =
        D3D12_SHADER_BYTECODE{shaders::texture_load_32bpb_scaled_cs,
                              sizeof(shaders::texture_load_32bpb_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndex64bpb] =
        D3D12_SHADER_BYTECODE{shaders::texture_load_64bpb_scaled_cs,
                              sizeof(shaders::texture_load_64bpb_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndex128bpb] =
        D3D12_SHADER_BYTECODE{shaders::texture_load_128bpb_scaled_cs,
                              sizeof(shaders::texture_load_128bpb_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR5G5B5A1ToB5G5R5A1] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r5g5b5a1_b5g5r5a1_scaled_cs,
            sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR5G6B5ToB5G6R5] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r5g6b5_b5g6r5_scaled_cs,
            sizeof(shaders::texture_load_r5g6b5_b5g6r5_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs,
            sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexRGBA4ToBGRA4] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r4g4b4a4_b4g4r4a4_scaled_cs,
            sizeof(shaders::texture_load_r4g4b4a4_b4g4r4a4_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR10G11B11ToRGBA16] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r10g11b11_rgba16_scaled_cs,
            sizeof(shaders::texture_load_r10g11b11_rgba16_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR10G11B11ToRGBA16SNorm] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r10g11b11_rgba16_snorm_scaled_cs,
            sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR11G11B10ToRGBA16] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r11g11b10_rgba16_scaled_cs,
            sizeof(shaders::texture_load_r11g11b10_rgba16_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexR11G11B10ToRGBA16SNorm] =
        D3D12_SHADER_BYTECODE{
            shaders::texture_load_r11g11b10_rgba16_snorm_scaled_cs,
            sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexDepthUnorm] = D3D12_SHADER_BYTECODE{
        shaders::texture_load_depth_unorm_scaled_cs,
        sizeof(shaders::texture_load_depth_unorm_scaled_cs)};
    load_shader_code_scaled[kLoadShaderIndexDepthFloat] = D3D12_SHADER_BYTECODE{
        shaders::texture_load_depth_float_scaled_cs,
        sizeof(shaders::texture_load_depth_float_scaled_cs)};
  }

  // Create the loading pipelines.
  for (size_t i = 0; i < kLoadShaderCount; ++i) {
    const D3D12_SHADER_BYTECODE& current_load_shader_code = load_shader_code[i];
    if (!current_load_shader_code.pShaderBytecode) {
      continue;
    }
    *(load_pipelines_[i].ReleaseAndGetAddressOf()) =
        ui::d3d12::util::CreateComputePipeline(
            device, current_load_shader_code.pShaderBytecode,
            current_load_shader_code.BytecodeLength,
            load_root_signature_.Get());
    if (!load_pipelines_[i]) {
      XELOGE(
          "D3D12TextureCache: Failed to create the texture loading pipeline "
          "for shader {}",
          i);
      return false;
    }
    if (IsDrawResolutionScaled()) {
      const D3D12_SHADER_BYTECODE& current_load_shader_code_scaled =
          load_shader_code_scaled[i];
      if (current_load_shader_code_scaled.pShaderBytecode) {
        *(load_pipelines_scaled_[i].ReleaseAndGetAddressOf()) =
            ui::d3d12::util::CreateComputePipeline(
                device, current_load_shader_code_scaled.pShaderBytecode,
                current_load_shader_code_scaled.BytecodeLength,
                load_root_signature_.Get());
        if (!load_pipelines_scaled_[i]) {
          XELOGE(
              "D3D12TextureCache: Failed to create the resolution-scaled "
              "texture loading pipeline for shader {}",
              i);
          return false;
        }
      }
    }
  }

  srv_descriptor_cache_allocated_ = 0;

  // Create a heap with null SRV descriptors, since it's faster to copy a
  // descriptor than to create an SRV, and null descriptors are used a lot (for
  // the signed version when only unsigned is used, for instance).
  D3D12_DESCRIPTOR_HEAP_DESC null_srv_descriptor_heap_desc;
  null_srv_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  null_srv_descriptor_heap_desc.NumDescriptors =
      uint32_t(NullSRVDescriptorIndex::kCount);
  null_srv_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  null_srv_descriptor_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &null_srv_descriptor_heap_desc,
          IID_PPV_ARGS(&null_srv_descriptor_heap_)))) {
    XELOGE(
        "D3D12TextureCache: Failed to create the descriptor heap for null "
        "SRVs");
    return false;
  }
  null_srv_descriptor_heap_start_ =
      null_srv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc;
  null_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  null_srv_desc.Shader4ComponentMapping =
      D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0);
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  null_srv_desc.Texture2DArray.MostDetailedMip = 0;
  null_srv_desc.Texture2DArray.MipLevels = 1;
  null_srv_desc.Texture2DArray.FirstArraySlice = 0;
  null_srv_desc.Texture2DArray.ArraySize = 1;
  null_srv_desc.Texture2DArray.PlaneSlice = 0;
  null_srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider.OffsetViewDescriptor(
          null_srv_descriptor_heap_start_,
          uint32_t(NullSRVDescriptorIndex::k2DArray)));
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  null_srv_desc.Texture3D.MostDetailedMip = 0;
  null_srv_desc.Texture3D.MipLevels = 1;
  null_srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider.OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                    uint32_t(NullSRVDescriptorIndex::k3D)));
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  null_srv_desc.TextureCube.MostDetailedMip = 0;
  null_srv_desc.TextureCube.MipLevels = 1;
  null_srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider.OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                    uint32_t(NullSRVDescriptorIndex::kCube)));

  return true;
}

void D3D12TextureCache::ClearCache() {
  TextureCache::ClearCache();

  // Clear texture descriptor cache.
  srv_descriptor_cache_free_.clear();
  srv_descriptor_cache_allocated_ = 0;
  srv_descriptor_cache_.clear();
}

void D3D12TextureCache::BeginSubmission(uint64_t new_submission_index) {
  TextureCache::BeginSubmission(new_submission_index);

  // ExecuteCommandLists is a full UAV and aliasing barrier.
  if (IsDrawResolutionScaled()) {
    size_t scaled_resolve_buffer_count = GetScaledResolveBufferCount();
    for (size_t i = 0; i < scaled_resolve_buffer_count; ++i) {
      ScaledResolveVirtualBuffer* scaled_resolve_buffer =
          scaled_resolve_2gb_buffers_[i].get();
      if (scaled_resolve_buffer) {
        scaled_resolve_buffer->ClearUAVBarrierPending();
      }
    }
    std::memset(scaled_resolve_1gb_buffer_indices_, UINT8_MAX,
                sizeof(scaled_resolve_1gb_buffer_indices_));
  }
}

void D3D12TextureCache::BeginFrame() {
  TextureCache::BeginFrame();

  std::memset(unsupported_format_features_used_, 0,
              sizeof(unsupported_format_features_used_));
}

void D3D12TextureCache::EndFrame() {
  // Report used unsupported texture formats.
  bool unsupported_header_written = false;
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t unsupported_features = unsupported_format_features_used_[i];
    if (unsupported_features == 0) {
      continue;
    }
    if (!unsupported_header_written) {
      XELOGE("Unsupported texture formats used in the frame:");
      unsupported_header_written = true;
    }
    XELOGE("* {}{}{}{}", FormatInfo::GetName(xenos::TextureFormat(i)),
           unsupported_features & kUnsupportedResourceBit ? " resource" : "",
           unsupported_features & kUnsupportedUnormBit ? " unsigned" : "",
           unsupported_features & kUnsupportedSnormBit ? " signed" : "");
    unsupported_format_features_used_[i] = 0;
  }
}

void D3D12TextureCache::RequestTextures(uint32_t used_texture_mask) {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  TextureCache::RequestTextures(used_texture_mask);

  // Transition the textures to the needed usage - always in
  // NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE states because barriers
  // between read-only stages, if needed, are discouraged (also if these were
  // tracked separately, checks would be needed to make sure, if the same
  // texture is bound through different fetch constants to both VS and PS, it
  // would be in both states).
  uint32_t textures_remaining = used_texture_mask;
  uint32_t index;
  while (xe::bit_scan_forward(textures_remaining, &index)) {
    textures_remaining = xe::clear_lowest_bit(textures_remaining);
    const TextureBinding* binding = GetValidTextureBinding(index);
    if (!binding) {
      continue;
    }
    D3D12Texture* binding_texture =
        static_cast<D3D12Texture*>(binding->texture);
    if (binding_texture != nullptr) {
      // Will be referenced by the command list, so mark as used.
      binding_texture->MarkAsUsed();
      command_processor_.PushTransitionBarrier(
          binding_texture->resource(),
          binding_texture->SetResourceState(
              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    D3D12Texture* binding_texture_signed =
        static_cast<D3D12Texture*>(binding->texture_signed);
    if (binding_texture_signed != nullptr) {
      binding_texture_signed->MarkAsUsed();
      command_processor_.PushTransitionBarrier(
          binding_texture_signed->resource(),
          binding_texture_signed->SetResourceState(
              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
  }
}
// chrispy: optimize this further
bool D3D12TextureCache::AreActiveTextureSRVKeysUpToDate(
    const TextureSRVKey* keys,
    const D3D12Shader::TextureBinding* host_shader_bindings,
    size_t host_shader_binding_count) const {
  for (size_t i = 0; i < host_shader_binding_count; ++i) {
    if (i + 8 < host_shader_binding_count) {
      PrefetchTextureBinding<swcache::PrefetchTag::Nontemporal>(
          host_shader_bindings[i + 8].fetch_constant);
    }
    const TextureSRVKey& key = keys[i];
    const TextureBinding* binding =
        GetValidTextureBinding(host_shader_bindings[i].fetch_constant);
    if (!binding) {
      if (key.key.is_valid) {
        return false;
      }
      continue;
    }
    if ((key.key != binding->key) |
        (key.host_swizzle != binding->host_swizzle) |
        (key.swizzled_signs != binding->swizzled_signs)) {
      return false;
    }
  }
  return true;
}

void D3D12TextureCache::WriteActiveTextureSRVKeys(
    TextureSRVKey* keys,
    const D3D12Shader::TextureBinding* host_shader_bindings,
    size_t host_shader_binding_count) const {
  for (size_t i = 0; i < host_shader_binding_count; ++i) {
    TextureSRVKey& key = keys[i];
    const TextureBinding* binding =
        GetValidTextureBinding(host_shader_bindings[i].fetch_constant);
    if (!binding) {
      key.key.MakeInvalid();
      key.host_swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_0000;
      key.swizzled_signs = kSwizzledSignsUnsigned;
      continue;
    }
    key.key = binding->key;
    key.host_swizzle = binding->host_swizzle;
    key.swizzled_signs = binding->swizzled_signs;
  }
}

void D3D12TextureCache::WriteActiveTextureBindfulSRV(
    const D3D12Shader::TextureBinding& host_shader_binding,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  assert_false(bindless_resources_used_);
  uint32_t descriptor_index = UINT32_MAX;
  Texture* texture = nullptr;
  uint32_t fetch_constant_index = host_shader_binding.fetch_constant;
  const TextureBinding* binding = GetValidTextureBinding(fetch_constant_index);
  if (binding && AreDimensionsCompatible(host_shader_binding.dimension,
                                         binding->key.dimension)) {
    const D3D12TextureBinding& d3d12_binding =
        d3d12_texture_bindings_[fetch_constant_index];
    if (host_shader_binding.is_signed) {
      // Not supporting signed compressed textures - hopefully DXN and DXT5A are
      // not used as signed.
      if (texture_util::IsAnySignSigned(binding->swizzled_signs)) {
        descriptor_index = d3d12_binding.descriptor_index_signed;
        texture = IsSignedVersionSeparateForFormat(binding->key)
                      ? binding->texture_signed
                      : binding->texture;
      }
    } else {
      if (texture_util::IsAnySignNotSigned(binding->swizzled_signs)) {
        descriptor_index = d3d12_binding.descriptor_index;
        texture = binding->texture;
      }
    }
  }
  const ui::d3d12::D3D12Provider& provider =
      command_processor_.GetD3D12Provider();
  D3D12_CPU_DESCRIPTOR_HANDLE source_handle;
  if (descriptor_index != UINT32_MAX) {
    assert_not_null(texture);
    texture->MarkAsUsed();
    source_handle = GetTextureDescriptorCPUHandle(descriptor_index);
  } else {
    NullSRVDescriptorIndex null_descriptor_index;
    switch (host_shader_binding.dimension) {
      case xenos::FetchOpDimension::k3DOrStacked:
        null_descriptor_index = NullSRVDescriptorIndex::k3D;
        break;
      case xenos::FetchOpDimension::kCube:
        null_descriptor_index = NullSRVDescriptorIndex::kCube;
        break;
      default:
        assert_true(
            host_shader_binding.dimension == xenos::FetchOpDimension::k1D ||
            host_shader_binding.dimension == xenos::FetchOpDimension::k2D);
        null_descriptor_index = NullSRVDescriptorIndex::k2DArray;
    }
    source_handle = provider.OffsetViewDescriptor(
        null_srv_descriptor_heap_start_, uint32_t(null_descriptor_index));
  }
  auto device = provider.GetDevice();
  {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
    SCOPE_profile_cpu_i(
        "gpu",
        "xe::gpu::d3d12::D3D12TextureCache::WriteActiveTextureBindfulSRV->"
        "CopyDescriptorsSimple");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
    device->CopyDescriptorsSimple(1, handle, source_handle,
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
}

uint32_t D3D12TextureCache::GetActiveTextureBindlessSRVIndex(
    const D3D12Shader::TextureBinding& host_shader_binding) {
  assert_true(bindless_resources_used_);
  uint32_t descriptor_index = UINT32_MAX;
  uint32_t fetch_constant_index = host_shader_binding.fetch_constant;
  const TextureBinding* binding = GetValidTextureBinding(fetch_constant_index);
  if (binding && AreDimensionsCompatible(host_shader_binding.dimension,
                                         binding->key.dimension)) {
    const D3D12TextureBinding& d3d12_binding =
        d3d12_texture_bindings_[fetch_constant_index];
    descriptor_index = host_shader_binding.is_signed
                           ? d3d12_binding.descriptor_index_signed
                           : d3d12_binding.descriptor_index;
  }
  if (descriptor_index == UINT32_MAX) {
    switch (host_shader_binding.dimension) {
      case xenos::FetchOpDimension::k3DOrStacked:
        descriptor_index =
            uint32_t(D3D12CommandProcessor::SystemBindlessView::kNullTexture3D);
        break;
      case xenos::FetchOpDimension::kCube:
        descriptor_index = uint32_t(
            D3D12CommandProcessor::SystemBindlessView::kNullTextureCube);
        break;
      default:
        assert_true(
            host_shader_binding.dimension == xenos::FetchOpDimension::k1D ||
            host_shader_binding.dimension == xenos::FetchOpDimension::k2D);
        descriptor_index = uint32_t(
            D3D12CommandProcessor::SystemBindlessView::kNullTexture2DArray);
    }
  }
  return descriptor_index;
}
void D3D12TextureCache::PrefetchSamplerParameters(
	const D3D12Shader::SamplerBinding& binding) const {
  swcache::PrefetchL1(&register_file()[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                                       binding.fetch_constant * 6]);
}
    D3D12TextureCache::SamplerParameters D3D12TextureCache::GetSamplerParameters(
    const D3D12Shader::SamplerBinding& binding) const {
  const auto& regs = register_file();
  const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6);

  SamplerParameters parameters;

  xenos::ClampMode fetch_clamp_x, fetch_clamp_y, fetch_clamp_z;
  texture_util::GetClampModesForDimension(fetch, fetch_clamp_x, fetch_clamp_y,
                                          fetch_clamp_z);
  parameters.clamp_x = NormalizeClampMode(fetch_clamp_x);
  parameters.clamp_y = NormalizeClampMode(fetch_clamp_y);
  parameters.clamp_z = NormalizeClampMode(fetch_clamp_z);
  if (xenos::ClampModeUsesBorder(parameters.clamp_x) ||
      xenos::ClampModeUsesBorder(parameters.clamp_y) ||
      xenos::ClampModeUsesBorder(parameters.clamp_z)) {
    parameters.border_color = fetch.border_color;
  } else {
    parameters.border_color = xenos::BorderColor::k_ABGR_Black;
  }

  uint32_t mip_min_level;
  texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr,
                                                 &mip_min_level, nullptr);
  parameters.mip_min_level = mip_min_level;
  //high cache miss count here, prefetch fetch earlier
  // TODO(Triang3l): Disable filtering for texture formats not supporting it.
  xenos::AnisoFilter aniso_filter =
      binding.aniso_filter == xenos::AnisoFilter::kUseFetchConst
          ? fetch.aniso_filter
          : binding.aniso_filter;
  aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  parameters.aniso_filter = aniso_filter;
  xenos::TextureFilter mip_filter =
      binding.mip_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.mip_filter
          : binding.mip_filter;
  if (aniso_filter != xenos::AnisoFilter::kDisabled) {
    parameters.mag_linear = 1;
    parameters.min_linear = 1;
    parameters.mip_linear = 1;
  } else {
    xenos::TextureFilter mag_filter =
        binding.mag_filter == xenos::TextureFilter::kUseFetchConst
            ? fetch.mag_filter
            : binding.mag_filter;
    parameters.mag_linear = mag_filter == xenos::TextureFilter::kLinear;
    xenos::TextureFilter min_filter =
        binding.min_filter == xenos::TextureFilter::kUseFetchConst
            ? fetch.min_filter
            : binding.min_filter;
    parameters.min_linear = min_filter == xenos::TextureFilter::kLinear;
    parameters.mip_linear = mip_filter == xenos::TextureFilter::kLinear;
  }
  parameters.mip_base_map = mip_filter == xenos::TextureFilter::kBaseMap;

  return parameters;
}

void D3D12TextureCache::WriteSampler(SamplerParameters parameters,
                                     D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
  D3D12_SAMPLER_DESC desc;
  if (parameters.aniso_filter != xenos::AnisoFilter::kDisabled) {
    desc.Filter = D3D12_FILTER_ANISOTROPIC;
    desc.MaxAnisotropy = 1u << (uint32_t(parameters.aniso_filter) - 1);
  } else {
    D3D12_FILTER_TYPE d3d_filter_min = parameters.min_linear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE d3d_filter_mag = parameters.mag_linear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE d3d_filter_mip = parameters.mip_linear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    desc.Filter = D3D12_ENCODE_BASIC_FILTER(
        d3d_filter_min, d3d_filter_mag, d3d_filter_mip,
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.MaxAnisotropy = 1;
  }
  static const D3D12_TEXTURE_ADDRESS_MODE kAddressModeMap[] = {
      /* kRepeat               */ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      /* kMirroredRepeat       */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
      /* kClampToEdge          */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      /* kMirrorClampToEdge    */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      // No GL_CLAMP (clamp to half edge, half border) equivalent in Direct3D
      // 12, but there's no Direct3D 9 equivalent anyway, and too weird to be
      // suitable for intentional real usage.
      /* kClampToHalfway       */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      // No mirror and clamp to border equivalents in Direct3D 12, but they
      // aren't there in Direct3D 9 either.
      /* kMirrorClampToHalfway */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      /* kClampToBorder        */ D3D12_TEXTURE_ADDRESS_MODE_BORDER,
      /* kMirrorClampToBorder  */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
  };
  desc.AddressU = kAddressModeMap[uint32_t(parameters.clamp_x)];
  desc.AddressV = kAddressModeMap[uint32_t(parameters.clamp_y)];
  desc.AddressW = kAddressModeMap[uint32_t(parameters.clamp_z)];
  // LOD biasing is performed in shaders.
  desc.MipLODBias = 0.0f;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  switch (parameters.border_color) {
    case xenos::BorderColor::k_ABGR_White:
      desc.BorderColor[0] = 1.0f;
      desc.BorderColor[1] = 1.0f;
      desc.BorderColor[2] = 1.0f;
      desc.BorderColor[3] = 1.0f;
      break;
    case xenos::BorderColor::k_ACBYCR_Black:
      desc.BorderColor[0] = 0.5f;
      desc.BorderColor[1] = 0.0f;
      desc.BorderColor[2] = 0.5f;
      desc.BorderColor[3] = 0.0f;
      break;
    case xenos::BorderColor::k_ACBCRY_Black:
      desc.BorderColor[0] = 0.0f;
      desc.BorderColor[1] = 0.5f;
      desc.BorderColor[2] = 0.5f;
      desc.BorderColor[3] = 0.0f;
      break;
    default:
      assert_true(parameters.border_color == xenos::BorderColor::k_ABGR_Black);
      desc.BorderColor[0] = 0.0f;
      desc.BorderColor[1] = 0.0f;
      desc.BorderColor[2] = 0.0f;
      desc.BorderColor[3] = 0.0f;
      break;
  }
  desc.MinLOD = float(parameters.mip_min_level);
  if (parameters.mip_base_map) {
    // "It is undefined whether LOD clamping based on MinLOD and MaxLOD Sampler
    // states should happen before or after deciding if magnification is
    // occuring" - Direct3D 11.3 Functional Specification.
    // Using the GL_NEAREST / GL_LINEAR minification filter emulation logic
    // described in the Vulkan VkSamplerCreateInfo specification, preserving
    // magnification vs. minification - point mip sampling (usable only without
    // anisotropic filtering on Direct3D 12) and MaxLOD 0.25. With anisotropic
    // filtering, magnification vs. minification doesn't matter as the filter is
    // always linear for both on Direct3D 12 - but linear filtering specifically
    // is what must not be done for kBaseMap, so setting MaxLOD to MinLOD.
    desc.MaxLOD = desc.MinLOD;
    if (parameters.aniso_filter == xenos::AnisoFilter::kDisabled) {
      assert_false(parameters.mip_linear);
      desc.MaxLOD += 0.25f;
    }
  } else {
    // Maximum mip level is in the texture resource itself.
    desc.MaxLOD = FLT_MAX;
  }
  ID3D12Device* device = command_processor_.GetD3D12Provider().GetDevice();
  device->CreateSampler(&desc, handle);
}

bool D3D12TextureCache::ClampDrawResolutionScaleToMaxSupported(
    uint32_t& scale_x, uint32_t& scale_y,
    const ui::d3d12::D3D12Provider& provider) {
  bool was_clamped;
  if (provider.GetTiledResourcesTier() < D3D12_TILED_RESOURCES_TIER_1) {
    was_clamped = scale_x > 1 || scale_y > 1;
    scale_x = 1;
    scale_y = 1;
    return !was_clamped;
  }
  // Limit to the virtual address space available for a resource.
  was_clamped = false;
  uint32_t virtual_address_bits_per_resource =
      provider.GetVirtualAddressBitsPerResource();
  while (scale_x > 1 || scale_y > 1) {
    uint64_t highest_scaled_address =
        uint64_t(SharedMemory::kBufferSize) * (scale_x * scale_y) - 1;
    if (uint32_t(64) - xe::lzcnt(highest_scaled_address) <=
        virtual_address_bits_per_resource) {
      break;
    }
    // When reducing from a square size, prefer decreasing the horizontal
    // resolution as vertical resolution difference is visible more clearly in
    // perspective.
    was_clamped = true;
    if (scale_x >= scale_y) {
      --scale_x;
    } else {
      --scale_y;
    }
  }
  return !was_clamped;
}

bool D3D12TextureCache::EnsureScaledResolveMemoryCommitted(
    uint32_t start_unscaled, uint32_t length_unscaled,
    uint32_t length_scaled_alignment_log2) {
  assert_true(IsDrawResolutionScaled());

  if (length_unscaled == 0) {
    return true;
  }
  if (start_unscaled > SharedMemory::kBufferSize ||
      (SharedMemory::kBufferSize - start_unscaled) < length_unscaled) {
    // Exceeds the physical address space.
    return false;
  }

  uint32_t draw_resolution_scale_area =
      draw_resolution_scale_x() * draw_resolution_scale_y();
  uint64_t first_scaled = uint64_t(start_unscaled) * draw_resolution_scale_area;
  uint64_t length_scaled_alignment_bits =
      (UINT64_C(1) << length_scaled_alignment_log2) - 1;
  uint64_t last_scaled = (uint64_t(start_unscaled + (length_unscaled - 1)) *
                              draw_resolution_scale_area +
                          length_scaled_alignment_bits) &
                         ~length_scaled_alignment_bits;

  const ui::d3d12::D3D12Provider& provider =
      command_processor_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();

  // Ensure GPU virtual memory for buffers that may be used to access the range
  // is allocated - buffers are created. Always creating both buffers for all
  // addresses before creating the heaps so when creating a new buffer, it can
  // be safely assumed that no existing heaps should be mapped to it.
  std::array<size_t, 2> possible_buffers_first =
      GetPossibleScaledResolveBufferIndices(first_scaled);
  std::array<size_t, 2> possible_buffers_last =
      GetPossibleScaledResolveBufferIndices(last_scaled);
  size_t possible_buffer_first =
      std::min(possible_buffers_first[0], possible_buffers_first[1]);
  size_t possible_buffer_last =
      std::max(possible_buffers_last[0], possible_buffers_last[1]);
  for (size_t i = possible_buffer_first; i <= possible_buffer_last; ++i) {
    if (scaled_resolve_2gb_buffers_[i]) {
      continue;
    }
    D3D12_RESOURCE_DESC scaled_resolve_buffer_desc;
    // Buffer indices are gigabytes.
    ui::d3d12::util::FillBufferResourceDesc(
        scaled_resolve_buffer_desc,
        std::min(uint64_t(1) << 31, uint64_t(SharedMemory::kBufferSize) *
                                            draw_resolution_scale_area -
                                        (uint64_t(i) << 30)),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    // The first access will be a resolve.
    constexpr D3D12_RESOURCE_STATES kScaledResolveVirtualBufferInitialState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12Resource* scaled_resolve_buffer_resource;
    if (FAILED(device->CreateReservedResource(
            &scaled_resolve_buffer_desc,
            kScaledResolveVirtualBufferInitialState, nullptr,
            IID_PPV_ARGS(&scaled_resolve_buffer_resource)))) {
      XELOGE(
          "D3D12TextureCache: Failed to create a 2 GB tiled buffer for draw "
          "resolution scaling");
      return false;
    }
    scaled_resolve_2gb_buffers_[i] =
        std::unique_ptr<ScaledResolveVirtualBuffer>(
            new ScaledResolveVirtualBuffer(
                scaled_resolve_buffer_resource,
                kScaledResolveVirtualBufferInitialState));
    scaled_resolve_buffer_resource->Release();
  }

  uint32_t heap_first = uint32_t(first_scaled >> kScaledResolveHeapSizeLog2);
  uint32_t heap_last = uint32_t(last_scaled >> kScaledResolveHeapSizeLog2);
  for (uint32_t i = heap_first; i <= heap_last; ++i) {
    if (scaled_resolve_heaps_[i]) {
      continue;
    }
    auto direct_queue = provider.GetDirectQueue();
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = kScaledResolveHeapSize;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS |
                      provider.GetHeapFlagCreateNotZeroed();
    Microsoft::WRL::ComPtr<ID3D12Heap> scaled_resolve_heap;
    if (FAILED(device->CreateHeap(&heap_desc,
                                  IID_PPV_ARGS(&scaled_resolve_heap)))) {
      XELOGE("D3D12TextureCache: Failed to create a scaled resolve tile heap");
      return false;
    }
    scaled_resolve_heaps_[i] = scaled_resolve_heap;
    ++scaled_resolve_heap_count_;
    COUNT_profile_set(
        "gpu/texture_cache/scaled_resolve_buffer_used_mb",
        scaled_resolve_heap_count_ << (kScaledResolveHeapSizeLog2 - 20));
    D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
    region_start_coordinates.Y = 0;
    region_start_coordinates.Z = 0;
    region_start_coordinates.Subresource = 0;
    D3D12_TILE_REGION_SIZE region_size;
    region_size.NumTiles =
        kScaledResolveHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    region_size.UseBox = FALSE;
    D3D12_TILE_RANGE_FLAGS range_flags = D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_range_start_offset = 0;
    UINT range_tile_count =
        kScaledResolveHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    std::array<size_t, 2> buffer_indices =
        GetPossibleScaledResolveBufferIndices(uint64_t(i)
                                              << kScaledResolveHeapSizeLog2);
    for (size_t j = 0; j < 2; ++j) {
      size_t buffer_index = buffer_indices[j];
      if (j && buffer_index == buffer_indices[0]) {
        break;
      }
      region_start_coordinates.X =
          UINT(((uint64_t(i) << kScaledResolveHeapSizeLog2) -
                (uint64_t(buffer_index) << 30)) /
               D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
      direct_queue->UpdateTileMappings(
          scaled_resolve_2gb_buffers_[buffer_index]->resource(), 1,
          &region_start_coordinates, &region_size, scaled_resolve_heap.Get(), 1,
          &range_flags, &heap_range_start_offset, &range_tile_count,
          D3D12_TILE_MAPPING_FLAG_NONE);
    }
    command_processor_.NotifyQueueOperationsDoneDirectly();
  }
  return true;
}

bool D3D12TextureCache::MakeScaledResolveRangeCurrent(
    uint32_t start_unscaled, uint32_t length_unscaled,
    uint32_t length_scaled_alignment_log2) {
  assert_true(IsDrawResolutionScaled());

  if (!length_unscaled || start_unscaled >= SharedMemory::kBufferSize ||
      (SharedMemory::kBufferSize - start_unscaled) < length_unscaled) {
    // If length is 0, the needed buffer can't be chosen because no buffer is
    // needed.
    return false;
  }

  uint32_t draw_resolution_scale_area =
      draw_resolution_scale_x() * draw_resolution_scale_y();
  uint64_t start_scaled = uint64_t(start_unscaled) * draw_resolution_scale_area;
  uint64_t length_scaled_alignment_bits =
      (UINT64_C(1) << length_scaled_alignment_log2) - 1;
  uint64_t length_scaled =
      (uint64_t(length_unscaled) * draw_resolution_scale_area +
       length_scaled_alignment_bits) &
      ~length_scaled_alignment_bits;
  uint64_t last_scaled = start_scaled + (length_scaled - 1);

  // Get one or two buffers that can hold the whole range.
  std::array<size_t, 2> possible_buffer_indices_first =
      GetPossibleScaledResolveBufferIndices(start_scaled);
  std::array<size_t, 2> possible_buffer_indices_last =
      GetPossibleScaledResolveBufferIndices(last_scaled);
  size_t possible_buffer_indices_common[2];
  size_t possible_buffer_indices_common_count = 0;
  for (size_t i = 0; i <= size_t(possible_buffer_indices_first[0] !=
                                 possible_buffer_indices_first[1]);
       ++i) {
    size_t possible_buffer_index_first = possible_buffer_indices_first[i];
    for (size_t j = 0; j <= size_t(possible_buffer_indices_last[0] !=
                                   possible_buffer_indices_last[1]);
         ++j) {
      if (possible_buffer_indices_last[j] == possible_buffer_index_first) {
        bool possible_buffer_index_already_added = false;
        for (size_t k = 0; k < possible_buffer_indices_common_count; ++k) {
          if (possible_buffer_indices_common[k] ==
              possible_buffer_index_first) {
            possible_buffer_index_already_added = true;
            break;
          }
        }
        if (!possible_buffer_index_already_added) {
          assert_true(possible_buffer_indices_common_count < 2);
          possible_buffer_indices_common
              [possible_buffer_indices_common_count++] =
                  possible_buffer_index_first;
        }
      }
    }
  }
  if (!possible_buffer_indices_common_count) {
    // Too wide range requested - no buffer that contains both the start and the
    // end.
    return false;
  }

  size_t gigabyte_first = size_t(start_scaled >> 30);
  size_t gigabyte_last = size_t(last_scaled >> 30);

  // Choose the buffer that the range will be accessed through.
  size_t new_buffer_index;
  if (possible_buffer_indices_common_count >= 2) {
    // Prefer the buffer that is already used to make less aliasing barriers.
    assert_true(gigabyte_first + 1 >= gigabyte_last);
    size_t possible_buffer_indices_already_used[2] = {};
    for (size_t i = gigabyte_first; i <= gigabyte_last; ++i) {
      size_t gigabyte_current_buffer_index =
          scaled_resolve_1gb_buffer_indices_[i];
      for (size_t j = 0; j < possible_buffer_indices_common_count; ++j) {
        if (possible_buffer_indices_common[j] ==
            gigabyte_current_buffer_index) {
          ++possible_buffer_indices_already_used[j];
        }
      }
    }
    new_buffer_index = possible_buffer_indices_common[size_t(
        possible_buffer_indices_already_used[1] >
        possible_buffer_indices_already_used[0])];
  } else {
    // The range can be accessed only by one buffer.
    new_buffer_index = possible_buffer_indices_common[0];
  }

  // Switch the current buffer for the range.
  const ScaledResolveVirtualBuffer* new_buffer =
      scaled_resolve_2gb_buffers_[new_buffer_index].get();
  assert_not_null(new_buffer);
  ID3D12Resource* new_buffer_resource = new_buffer->resource();
  for (size_t i = gigabyte_first; i <= gigabyte_last; ++i) {
    size_t gigabyte_current_buffer_index =
        scaled_resolve_1gb_buffer_indices_[i];
    if (gigabyte_current_buffer_index == new_buffer_index) {
      continue;
    }
    if (gigabyte_current_buffer_index != SIZE_MAX) {
      ScaledResolveVirtualBuffer* gigabyte_current_buffer =
          scaled_resolve_2gb_buffers_[gigabyte_current_buffer_index].get();
      assert_not_null(gigabyte_current_buffer);
      command_processor_.PushAliasingBarrier(
          gigabyte_current_buffer->resource(), new_buffer_resource);
      // An aliasing barrier synchronizes and flushes everything.
      gigabyte_current_buffer->ClearUAVBarrierPending();
    }
    scaled_resolve_1gb_buffer_indices_[i] = new_buffer_index;
  }

  scaled_resolve_current_range_start_scaled_ = start_scaled;
  scaled_resolve_current_range_length_scaled_ = length_scaled;
  return true;
}

void D3D12TextureCache::TransitionCurrentScaledResolveRange(
    D3D12_RESOURCE_STATES new_state) {
  assert_true(IsDrawResolutionScaled());
  ScaledResolveVirtualBuffer& buffer = GetCurrentScaledResolveBuffer();
  command_processor_.PushTransitionBarrier(
      buffer.resource(), buffer.SetResourceState(new_state), new_state);
}

void D3D12TextureCache::CreateCurrentScaledResolveRangeUintPow2SRV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2) {
  assert_true(IsDrawResolutionScaled());
  size_t buffer_index = GetCurrentScaledResolveBufferIndex();
  const ScaledResolveVirtualBuffer* buffer =
      scaled_resolve_2gb_buffers_[buffer_index].get();
  assert_not_null(buffer);
  ui::d3d12::util::CreateBufferTypedSRV(
      command_processor_.GetD3D12Provider().GetDevice(), handle,
      buffer->resource(),
      ui::d3d12::util::GetUintPow2DXGIFormat(element_size_bytes_pow2),
      uint32_t(scaled_resolve_current_range_length_scaled_ >>
               element_size_bytes_pow2),
      (scaled_resolve_current_range_start_scaled_ -
       (uint64_t(buffer_index) << 30)) >>
          element_size_bytes_pow2);
}

void D3D12TextureCache::CreateCurrentScaledResolveRangeUintPow2UAV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2) {
  assert_true(IsDrawResolutionScaled());
  size_t buffer_index = GetCurrentScaledResolveBufferIndex();
  const ScaledResolveVirtualBuffer* buffer =
      scaled_resolve_2gb_buffers_[buffer_index].get();
  assert_not_null(buffer);
  ui::d3d12::util::CreateBufferTypedUAV(
      command_processor_.GetD3D12Provider().GetDevice(), handle,
      buffer->resource(),
      ui::d3d12::util::GetUintPow2DXGIFormat(element_size_bytes_pow2),
      uint32_t(scaled_resolve_current_range_length_scaled_ >>
               element_size_bytes_pow2),
      (scaled_resolve_current_range_start_scaled_ -
       (uint64_t(buffer_index) << 30)) >>
          element_size_bytes_pow2);
}

ID3D12Resource* D3D12TextureCache::RequestSwapTexture(
    D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc_out,
    xenos::TextureFormat& format_out) {
  const auto& regs = register_file();
  const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0);
  TextureKey key;
  BindingInfoFromFetchConstant(fetch, key, nullptr);
  if (!key.is_valid || key.base_page == 0 ||
      key.dimension != xenos::DataDimension::k2DOrStacked) {
    return nullptr;
  }
  D3D12Texture* texture = static_cast<D3D12Texture*>(FindOrCreateTexture(key));
  if (texture == nullptr || !LoadTextureData(*texture)) {
    return nullptr;
  }
  texture->MarkAsUsed();
  // The swap texture is likely to be used only for the presentation compute
  // shader, and not during emulation, where it'd be NON_PIXEL_SHADER_RESOURCE |
  // PIXEL_SHADER_RESOURCE.
  ID3D12Resource* texture_resource = texture->resource();
  command_processor_.PushTransitionBarrier(
      texture_resource,
      texture->SetResourceState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  srv_desc_out.Format = GetDXGIUnormFormat(key);
  srv_desc_out.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc_out.Shader4ComponentMapping =
      GuestToHostSwizzle(fetch.swizzle, GetHostFormatSwizzle(key)) |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  srv_desc_out.Texture2D.MostDetailedMip = 0;
  srv_desc_out.Texture2D.MipLevels = 1;
  srv_desc_out.Texture2D.PlaneSlice = 0;
  srv_desc_out.Texture2D.ResourceMinLODClamp = 0.0f;
  format_out = key.format;
  return texture_resource;
}

D3D12TextureCache::D3D12Texture::D3D12Texture(
    D3D12TextureCache& texture_cache, const TextureKey& key,
    ID3D12Resource* resource, D3D12_RESOURCE_STATES resource_state)
    : Texture(texture_cache, key),
      resource_(resource),
      resource_state_(resource_state) {
  ID3D12Device* device =
      texture_cache.command_processor_.GetD3D12Provider().GetDevice();
  D3D12_RESOURCE_DESC resource_desc = resource_->GetDesc();
  SetHostMemoryUsage(
      device->GetResourceAllocationInfo(0, 1, &resource_desc).SizeInBytes);
}

D3D12TextureCache::D3D12Texture::~D3D12Texture() {
  auto& d3d12_texture_cache = static_cast<D3D12TextureCache&>(texture_cache());
  for (const auto& descriptor_pair : srv_descriptors_) {
    d3d12_texture_cache.ReleaseTextureDescriptor(descriptor_pair.second);
  }
}

bool D3D12TextureCache::IsDecompressionNeeded(xenos::TextureFormat format,
                                              uint32_t width,
                                              uint32_t height) const {
  DXGI_FORMAT dxgi_format_uncompressed =
      host_formats_[uint32_t(format)].dxgi_format_uncompressed;
  if (dxgi_format_uncompressed == DXGI_FORMAT_UNKNOWN) {
    return false;
  }
  const FormatInfo* format_info = FormatInfo::Get(format);
  if (!(width & (format_info->block_width - 1)) &&
      !(height & (format_info->block_height - 1))) {
    return false;
  }
  // UnalignedBlockTexturesSupported is for block-compressed textures with the
  // block size of 4x4, but not for 2x1 (4:2:2) subsampled formats.
  if (format_info->block_width == 4 && format_info->block_height == 4 &&
      command_processor_.GetD3D12Provider()
          .AreUnalignedBlockTexturesSupported()) {
    return false;
  }
  return true;
}

TextureCache::LoadShaderIndex D3D12TextureCache::GetLoadShaderIndex(
    TextureKey key) const {
  const HostFormat& host_format = host_formats_[uint32_t(key.format)];
  if (key.signed_separate) {
    return host_format.load_shader_signed;
  }
  if (IsDecompressionNeeded(key.format, key.GetWidth(), key.GetHeight())) {
    return host_format.load_shader_decompress;
  }
  return host_format.load_shader;
}

bool D3D12TextureCache::IsSignedVersionSeparateForFormat(TextureKey key) const {
  const HostFormat& host_format = host_formats_[uint32_t(key.format)];
  return host_format.load_shader_signed != kLoadShaderIndexUnknown &&
         host_format.load_shader_signed != host_format.load_shader;
}

bool D3D12TextureCache::IsScaledResolveSupportedForFormat(
    TextureKey key) const {
  LoadShaderIndex load_shader = GetLoadShaderIndex(key);
  return load_shader != kLoadShaderIndexUnknown &&
         load_pipelines_scaled_[load_shader] != nullptr;
}

uint32_t D3D12TextureCache::GetHostFormatSwizzle(TextureKey key) const {
  // return host_formats_[uint32_t(key.format)].swizzle;
  return xenos_swizzle_for_format[uint32_t(key.format)];
}

uint32_t D3D12TextureCache::GetMaxHostTextureWidthHeight(
    xenos::DataDimension dimension) const {
  switch (dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      // 1D and 2D are emulated as 2D arrays.
      return D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    case xenos::DataDimension::k3D:
      return D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    case xenos::DataDimension::kCube:
      return D3D12_REQ_TEXTURECUBE_DIMENSION;
    default:
      assert_unhandled_case(dimension);
      return 0;
  }
}

uint32_t D3D12TextureCache::GetMaxHostTextureDepthOrArraySize(
    xenos::DataDimension dimension) const {
  switch (dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      // 1D and 2D are emulated as 2D arrays.
      return D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    case xenos::DataDimension::k3D:
      return D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    case xenos::DataDimension::kCube:
      return D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION / 6 * 6;
    default:
      assert_unhandled_case(dimension);
      return 0;
  }
}

std::unique_ptr<TextureCache::Texture> D3D12TextureCache::CreateTexture(
    TextureKey key) {
  D3D12_RESOURCE_DESC desc;
  desc.Format = GetDXGIResourceFormat(key);
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    unsupported_format_features_used_[uint32_t(key.format)] |=
        kUnsupportedResourceBit;
    return nullptr;
  }
  if (key.dimension == xenos::DataDimension::k3D) {
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  } else {
    // 1D textures are treated as 2D for simplicity.
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  }
  desc.Alignment = 0;
  desc.Width = key.GetWidth();
  desc.Height = key.GetHeight();
  if (key.scaled_resolve) {
    desc.Width *= draw_resolution_scale_x();
    desc.Height *= draw_resolution_scale_y();
  }
  desc.DepthOrArraySize = key.GetDepthOrArraySize();
  desc.MipLevels = key.mip_max_level + 1;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  // Untiling through a buffer instead of using unordered access because copying
  // is not done that often.
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  const ui::d3d12::D3D12Provider& provider =
      command_processor_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  // Assuming untiling will be the next operation.
  D3D12_RESOURCE_STATES resource_state = D3D12_RESOURCE_STATE_COPY_DEST;
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault,
          provider.GetHeapFlagCreateNotZeroed(), &desc, resource_state, nullptr,
          IID_PPV_ARGS(&resource)))) {
    return nullptr;
  }
  return std::unique_ptr<Texture>(
      new D3D12Texture(*this, key, resource.Get(), resource_state));
}

bool D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture,
                                                              bool load_base,
                                                              bool load_mips) {
  D3D12Texture& d3d12_texture = static_cast<D3D12Texture&>(texture);
  TextureKey texture_key = d3d12_texture.key();

  DeferredCommandList& command_list =
      command_processor_.GetDeferredCommandList();
  ID3D12Device* device = command_processor_.GetD3D12Provider().GetDevice();

  // Get the pipeline.
  LoadShaderIndex load_shader = GetLoadShaderIndex(texture_key);
  if (load_shader == kLoadShaderIndexUnknown) {
    return false;
  }
  bool texture_resolution_scaled = texture_key.scaled_resolve;
  ID3D12PipelineState* pipeline =
      texture_resolution_scaled ? load_pipelines_scaled_[load_shader].Get()
                                : load_pipelines_[load_shader].Get();
  if (pipeline == nullptr) {
    return false;
  }
  const LoadShaderInfo& load_shader_info = GetLoadShaderInfo(load_shader);

  // Get the guest layout.
  const texture_util::TextureGuestLayout& guest_layout =
      d3d12_texture.guest_layout();
  xenos::DataDimension dimension = texture_key.dimension;
  bool is_3d = dimension == xenos::DataDimension::k3D;
  uint32_t width = texture_key.GetWidth();
  uint32_t height = texture_key.GetHeight();
  uint32_t depth_or_array_size = texture_key.GetDepthOrArraySize();
  uint32_t depth = is_3d ? depth_or_array_size : 1;
  uint32_t array_size = is_3d ? 1 : depth_or_array_size;
  xenos::TextureFormat guest_format = texture_key.format;
  const FormatInfo* guest_format_info = FormatInfo::Get(guest_format);
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;
  uint32_t bytes_per_block = guest_format_info->bytes_per_block();
  uint32_t level_first = load_base ? 0 : 1;
  uint32_t level_last = load_mips ? texture_key.mip_max_level : 0;
  assert_true(level_first <= level_last);
  uint32_t level_packed = guest_layout.packed_level;
  uint32_t level_stored_first = std::min(level_first, level_packed);
  uint32_t level_stored_last = std::min(level_last, level_packed);
  uint32_t texture_resolution_scale_x =
      texture_resolution_scaled ? draw_resolution_scale_x() : 1;
  uint32_t texture_resolution_scale_y =
      texture_resolution_scaled ? draw_resolution_scale_y() : 1;

  // The loop counter can mean two things depending on whether the packed mip
  // tail is stored as mip 0, because in this case, it would be ambiguous since
  // both the base and the mips would be on "level 0", but stored in separate
  // places.
  uint32_t loop_level_first, loop_level_last;
  if (level_packed == 0) {
    // Packed mip tail is the level 0 - may need to load mip tails for the base,
    // the mips, or both.
    // Loop iteration 0 - base packed mip tail.
    // Loop iteration 1 - mips packed mip tail.
    loop_level_first = uint32_t(level_first != 0);
    loop_level_last = uint32_t(level_last != 0);
  } else {
    // Packed mip tail is not the level 0.
    // Loop iteration is the actual level being loaded.
    loop_level_first = level_stored_first;
    loop_level_last = level_stored_last;
  }

  // Get the host layout and the buffer.
  bool host_block_compressed =
      host_formats_[uint32_t(guest_format)].is_block_compressed &&
      !IsDecompressionNeeded(guest_format, width, height);
  uint32_t host_block_width = host_block_compressed ? block_width : 1;
  uint32_t host_block_height = host_block_compressed ? block_height : 1;
  uint32_t host_x_blocks_per_thread =
      UINT32_C(1) << load_shader_info.guest_x_blocks_per_thread_log2;
  if (!host_block_compressed) {
    // Decompressing guest blocks.
    host_x_blocks_per_thread *= block_width;
  }
  UINT64 copy_buffer_size = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT host_slice_layout_base;
  UINT64 host_slice_size_base;
  // Indexing is the same as for guest stored mips:
  // 1...min(level_last, level_packed) if level_packed is not 0, or only 0 if
  // level_packed == 0.
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT
  host_slice_layouts_mips[xenos::kTextureMaxMips];
  UINT64 host_slice_sizes_mips[xenos::kTextureMaxMips];
  // Using custom calculations instead of GetCopyableFootprints because
  // shaders may unconditionally copy multiple blocks along X per thread for
  // simplicity, to make sure all rows (also including the last one -
  // GetCopyableFootprints aligns row offsets, but not the total size) are
  // properly padded to the number of blocks copied in an invocation without
  // implicit assumptions about D3D12_TEXTURE_DATA_PITCH_ALIGNMENT.
  DXGI_FORMAT host_copy_format =
      GetDXGIResourceFormat(guest_format, width, height);
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (level_packed == 0) ? 0 : loop_level;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT& level_host_slice_layout =
        is_base ? host_slice_layout_base : host_slice_layouts_mips[level];
    level_host_slice_layout.Offset = copy_buffer_size;
    level_host_slice_layout.Footprint.Format = host_copy_format;
    if (level == level_packed) {
      // Loading the packed tail for the base or the mips - load the whole tail
      // to copy regions out of it.
      const texture_util::TextureGuestLayout::Level& guest_layout_packed =
          is_base ? guest_layout.base : guest_layout.mips[level];
      level_host_slice_layout.Footprint.Width =
          guest_layout_packed.x_extent_blocks * block_width;
      level_host_slice_layout.Footprint.Height =
          guest_layout_packed.y_extent_blocks * block_height;
      level_host_slice_layout.Footprint.Depth = guest_layout_packed.z_extent;
    } else {
      level_host_slice_layout.Footprint.Width =
          std::max(width >> level, uint32_t(1));
      level_host_slice_layout.Footprint.Height =
          std::max(height >> level, uint32_t(1));
      level_host_slice_layout.Footprint.Depth =
          std::max(depth >> level, uint32_t(1));
    }
    level_host_slice_layout.Footprint.Width = xe::round_up(
        level_host_slice_layout.Footprint.Width * texture_resolution_scale_x,
        UINT(host_block_width));
    level_host_slice_layout.Footprint.Height = xe::round_up(
        level_host_slice_layout.Footprint.Height * texture_resolution_scale_y,
        UINT(host_block_height));
    level_host_slice_layout.Footprint.RowPitch = xe::align(
        xe::round_up(level_host_slice_layout.Footprint.Width / host_block_width,
                     host_x_blocks_per_thread) *
            load_shader_info.bytes_per_host_block,
        uint32_t(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
    UINT64 level_host_slice_size = xe::align(
        UINT64(level_host_slice_layout.Footprint.RowPitch) *
            (level_host_slice_layout.Footprint.Height / host_block_height) *
            level_host_slice_layout.Footprint.Depth,
        UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT));
    (is_base ? host_slice_size_base : host_slice_sizes_mips[level]) =
        level_host_slice_size;
    copy_buffer_size += level_host_slice_size * array_size;
  }
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_.RequestScratchGPUBuffer(
      uint32_t(copy_buffer_size), copy_buffer_state);
  if (copy_buffer == nullptr) {
    return false;
  }

  // Begin loading.
  // May use different buffers for scaled base and mips, and also addressability
  // of more than 128 * 2^20 (2^D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP)
  // texels is not mandatory - need two separate UAV descriptors for base and
  // mips.
  // Destination.
  uint32_t descriptor_count = 1;
  if (texture_resolution_scaled) {
    // Source - base and mips, one or both.
    descriptor_count += (level_first == 0 && level_last != 0) ? 2 : 1;
  } else {
    // Source - shared memory.
    if (!bindless_resources_used_) {
      ++descriptor_count;
    }
  }
  ui::d3d12::util::DescriptorCpuGpuHandlePair descriptors_allocated[3];
  if (!command_processor_.RequestOneUseSingleViewDescriptors(
          descriptor_count, descriptors_allocated)) {
    command_processor_.ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
    return false;
  }
  uint32_t descriptor_write_index = 0;
  command_processor_.SetExternalPipeline(pipeline);
  command_list.D3DSetComputeRootSignature(load_root_signature_.Get());
  // Set up the destination descriptor.
  assert_true(descriptor_write_index < descriptor_count);
  ui::d3d12::util::DescriptorCpuGpuHandlePair descriptor_dest =
      descriptors_allocated[descriptor_write_index++];
  ui::d3d12::util::CreateBufferTypedUAV(
      device, descriptor_dest.first, copy_buffer,
      ui::d3d12::util::GetUintPow2DXGIFormat(load_shader_info.dest_bpe_log2),
      uint32_t(copy_buffer_size) >> load_shader_info.dest_bpe_log2);
  command_list.D3DSetComputeRootDescriptorTable(2, descriptor_dest.second);
  // Set up the unscaled source descriptor (scaled needs two descriptors that
  // depend on the buffer being current, so they will be set later - for mips,
  // after loading the base is done).
  if (!texture_resolution_scaled) {
    D3D12SharedMemory& d3d12_shared_memory =
        static_cast<D3D12SharedMemory&>(shared_memory());
    d3d12_shared_memory.UseForReading();
    ui::d3d12::util::DescriptorCpuGpuHandlePair descriptor_unscaled_source;
    if (bindless_resources_used_) {
      descriptor_unscaled_source =
          command_processor_.GetSharedMemoryUintPow2BindlessSRVHandlePair(
              load_shader_info.source_bpe_log2);
    } else {
      assert_true(descriptor_write_index < descriptor_count);
      descriptor_unscaled_source =
          descriptors_allocated[descriptor_write_index++];
      d3d12_shared_memory.WriteUintPow2SRVDescriptor(
          descriptor_unscaled_source.first, load_shader_info.source_bpe_log2);
    }
    command_list.D3DSetComputeRootDescriptorTable(
        1, descriptor_unscaled_source.second);
  }

  // Submit the copy buffer population commands.

  auto& cbuffer_pool = command_processor_.GetConstantBufferPool();
  LoadConstants load_constants;
  // 3 bits for each.
  assert_true(texture_resolution_scale_x <= 7);
  assert_true(texture_resolution_scale_y <= 7);
  load_constants.is_tiled_3d_endian_scale =
      uint32_t(texture_key.tiled) | (uint32_t(is_3d) << 1) |
      (uint32_t(texture_key.endianness) << 2) |
      (texture_resolution_scale_x << 4) | (texture_resolution_scale_y << 7);

  // The loop is slices within levels because the base and the levels may need
  // different portions of the scaled resolve virtual address space to be
  // available through buffers, and to create a descriptor, the buffer start
  // address is required - which may be different for base and mips.
  bool scaled_mips_source_set_up = false;
  uint32_t guest_x_blocks_per_group_log2 =
      load_shader_info.GetGuestXBlocksPerGroupLog2();
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (level_packed == 0) ? 0 : loop_level;

    uint32_t guest_address =
        (is_base ? texture_key.base_page : texture_key.mip_page) << 12;

    // Set up the base or mips source, also making it accessible if loading from
    // scaled resolve memory.
    if (texture_resolution_scaled && (is_base || !scaled_mips_source_set_up)) {
      uint32_t guest_size_unscaled = is_base ? d3d12_texture.GetGuestBaseSize()
                                             : d3d12_texture.GetGuestMipsSize();
      if (!MakeScaledResolveRangeCurrent(guest_address, guest_size_unscaled,
                                         load_shader_info.source_bpe_log2)) {
        command_processor_.ReleaseScratchGPUBuffer(copy_buffer,
                                                   copy_buffer_state);
        return false;
      }
      TransitionCurrentScaledResolveRange(
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      assert_true(descriptor_write_index < descriptor_count);
      ui::d3d12::util::DescriptorCpuGpuHandlePair descriptor_scaled_source =
          descriptors_allocated[descriptor_write_index++];
      CreateCurrentScaledResolveRangeUintPow2SRV(
          descriptor_scaled_source.first, load_shader_info.source_bpe_log2);
      command_list.D3DSetComputeRootDescriptorTable(
          1, descriptor_scaled_source.second);
      if (!is_base) {
        scaled_mips_source_set_up = true;
      }
    }

    if (texture_resolution_scaled) {
      // Offset already applied in the buffer because more than 512 MB can't be
      // directly addresses as R32 on some hardware (above
      // 2^D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP).
      load_constants.guest_offset = 0;
    } else {
      load_constants.guest_offset = guest_address;
    }
    if (!is_base) {
      load_constants.guest_offset +=
          guest_layout.mip_offsets_bytes[level] *
          (texture_resolution_scale_x * texture_resolution_scale_y);
    }
    const texture_util::TextureGuestLayout::Level& level_guest_layout =
        is_base ? guest_layout.base : guest_layout.mips[level];
    uint32_t level_guest_pitch = level_guest_layout.row_pitch_bytes;
    if (texture_key.tiled) {
      // Shaders expect pitch in blocks for tiled textures.
      level_guest_pitch /= bytes_per_block;
      assert_zero(level_guest_pitch & (xenos::kTextureTileWidthHeight - 1));
    }
    load_constants.guest_pitch_aligned = level_guest_pitch;
    load_constants.guest_z_stride_block_rows_aligned =
        level_guest_layout.z_slice_stride_block_rows;
    assert_true(dimension != xenos::DataDimension::k3D ||
                !(load_constants.guest_z_stride_block_rows_aligned &
                  (xenos::kTextureTileWidthHeight - 1)));

    uint32_t level_width, level_height, level_depth;
    if (level == level_packed) {
      // This is the packed mip tail, containing not only the specified level,
      // but also other levels at different offsets - load the entire needed
      // extents.
      level_width = level_guest_layout.x_extent_blocks * block_width;
      level_height = level_guest_layout.y_extent_blocks * block_height;
      level_depth = level_guest_layout.z_extent;
    } else {
      level_width = std::max(width >> level, uint32_t(1));
      level_height = std::max(height >> level, uint32_t(1));
      level_depth = std::max(depth >> level, uint32_t(1));
    }
    load_constants.size_blocks[0] = (level_width + (block_width - 1)) /
                                    block_width * texture_resolution_scale_x;
    load_constants.size_blocks[1] = (level_height + (block_height - 1)) /
                                    block_height * texture_resolution_scale_y;
    load_constants.size_blocks[2] = level_depth;
    load_constants.height_texels = level_height;

    uint32_t group_count_x =
        (load_constants.size_blocks[0] +
         ((UINT32_C(1) << guest_x_blocks_per_group_log2) - 1)) >>
        guest_x_blocks_per_group_log2;
    uint32_t group_count_y =
        (load_constants.size_blocks[1] +
         ((UINT32_C(1) << kLoadGuestYBlocksPerGroupLog2) - 1)) >>
        kLoadGuestYBlocksPerGroupLog2;

    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& level_host_slice_layout =
        is_base ? host_slice_layout_base : host_slice_layouts_mips[level];
    uint32_t host_slice_size =
        uint32_t(is_base ? host_slice_size_base : host_slice_sizes_mips[level]);
    load_constants.host_offset = uint32_t(level_host_slice_layout.Offset);
    load_constants.host_pitch = level_host_slice_layout.Footprint.RowPitch;

    uint32_t level_array_slice_stride_bytes_scaled =
        level_guest_layout.array_slice_stride_bytes *
        (texture_resolution_scale_x * texture_resolution_scale_y);
    for (uint32_t slice = 0; slice < array_size; ++slice) {
      D3D12_GPU_VIRTUAL_ADDRESS cbuffer_gpu_address;
      uint8_t* cbuffer_mapping = cbuffer_pool.Request(
          command_processor_.GetCurrentFrame(), sizeof(load_constants),
          D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
          &cbuffer_gpu_address);
      if (cbuffer_mapping == nullptr) {
        command_processor_.ReleaseScratchGPUBuffer(copy_buffer,
                                                   copy_buffer_state);
        return false;
      }
      std::memcpy(cbuffer_mapping, &load_constants, sizeof(load_constants));
      command_list.D3DSetComputeRootConstantBufferView(0, cbuffer_gpu_address);
      assert_true(copy_buffer_state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      command_processor_.SubmitBarriers();
      command_list.D3DDispatch(group_count_x, group_count_y,
                               load_constants.size_blocks[2]);
      load_constants.guest_offset += level_array_slice_stride_bytes_scaled;
      load_constants.host_offset += host_slice_size;
    }
  }

  // Update LRU caching because the texture will be used by the command list.
  d3d12_texture.MarkAsUsed();

  // Submit copying from the copy buffer to the host texture.
  ID3D12Resource* texture_resource = d3d12_texture.resource();
  command_processor_.PushTransitionBarrier(
      texture_resource,
      d3d12_texture.SetResourceState(D3D12_RESOURCE_STATE_COPY_DEST),
      D3D12_RESOURCE_STATE_COPY_DEST);
  command_processor_.PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                           D3D12_RESOURCE_STATE_COPY_SOURCE);
  copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
  command_processor_.SubmitBarriers();
  uint32_t texture_level_count = texture_key.mip_max_level + 1;
  D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
  location_source.pResource = copy_buffer;
  location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  location_dest.pResource = texture_resource;
  location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  for (uint32_t level = level_first; level <= level_last; ++level) {
    uint32_t guest_level = std::min(level, level_packed);
    location_source.PlacedFootprint =
        level ? host_slice_layouts_mips[guest_level] : host_slice_layout_base;
    location_dest.SubresourceIndex = level;
    UINT64 host_slice_size =
        level ? host_slice_sizes_mips[guest_level] : host_slice_size_base;
    D3D12_BOX source_box;
    const D3D12_BOX* source_box_ptr;
    if (level >= level_packed) {
      uint32_t level_offset_blocks_x, level_offset_blocks_y, level_offset_z;
      texture_util::GetPackedMipOffset(width, height, depth, guest_format,
                                       level, level_offset_blocks_x,
                                       level_offset_blocks_y, level_offset_z);
      source_box.left =
          level_offset_blocks_x * block_width * texture_resolution_scale_x;
      source_box.top =
          level_offset_blocks_y * block_height * texture_resolution_scale_y;
      source_box.front = level_offset_z;
      source_box.right =
          source_box.left +
          xe::align(std::max((width * texture_resolution_scale_x) >> level,
                             uint32_t(1)),
                    host_block_width);
      source_box.bottom =
          source_box.top +
          xe::align(std::max((height * texture_resolution_scale_y) >> level,
                             uint32_t(1)),
                    host_block_height);
      source_box.back =
          source_box.front + std::max(depth >> level, uint32_t(1));
      source_box_ptr = &source_box;
    } else {
      source_box_ptr = nullptr;
    }
    for (uint32_t slice = 0; slice < array_size; ++slice) {
      command_list.D3DCopyTextureRegion(&location_dest, 0, 0, 0,
                                        &location_source, source_box_ptr);
      location_dest.SubresourceIndex += texture_level_count;
      location_source.PlacedFootprint.Offset += host_slice_size;
    }
  }

  command_processor_.ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);

  return true;
}

void D3D12TextureCache::UpdateTextureBindingsImpl(
    uint32_t fetch_constant_mask) {
  uint32_t bindings_remaining = fetch_constant_mask;
  uint32_t binding_index;
  while (xe::bit_scan_forward(bindings_remaining, &binding_index)) {
    bindings_remaining &= ~(UINT32_C(1) << binding_index);
    D3D12TextureBinding& d3d12_binding = d3d12_texture_bindings_[binding_index];
    d3d12_binding.Reset();
    const TextureBinding* binding = GetValidTextureBinding(binding_index);
    if (!binding) {
      continue;
    }
    if (IsSignedVersionSeparateForFormat(binding->key)) {
      if (binding->texture &&
          texture_util::IsAnySignNotSigned(binding->swizzled_signs)) {
        d3d12_binding.descriptor_index = FindOrCreateTextureDescriptor(
            *static_cast<D3D12Texture*>(binding->texture), false,
            binding->host_swizzle);
      }
      if (binding->texture_signed &&
          texture_util::IsAnySignSigned(binding->swizzled_signs)) {
        d3d12_binding.descriptor_index_signed = FindOrCreateTextureDescriptor(
            *static_cast<D3D12Texture*>(binding->texture_signed), true,
            binding->host_swizzle);
      }
    } else {
      D3D12Texture* texture = static_cast<D3D12Texture*>(binding->texture);
      if (texture) {
        if (texture_util::IsAnySignNotSigned(binding->swizzled_signs)) {
          d3d12_binding.descriptor_index = FindOrCreateTextureDescriptor(
              *texture, false, binding->host_swizzle);
        }
        if (texture_util::IsAnySignSigned(binding->swizzled_signs)) {
          d3d12_binding.descriptor_index_signed = FindOrCreateTextureDescriptor(
              *texture, true, binding->host_swizzle);
        }
      }
    }
  }
}

uint32_t D3D12TextureCache::FindOrCreateTextureDescriptor(
    D3D12Texture& texture, bool is_signed, uint32_t host_swizzle) {
  D3D12Texture::SRVDescriptorKey descriptor_key;
  descriptor_key.is_signed = uint32_t(is_signed);
  descriptor_key.host_swizzle = host_swizzle;

  // Try to find an existing descriptor.
  uint32_t existing_descriptor_index =
      texture.GetSRVDescriptorIndex(descriptor_key);
  if (existing_descriptor_index != UINT32_MAX) {
    return existing_descriptor_index;
  }

  TextureKey texture_key = texture.key();

  // Create a new bindless or cached descriptor if supported.
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;

  if (IsSignedVersionSeparateForFormat(texture_key) &&
      texture_key.signed_separate != uint32_t(is_signed)) {
    // Not the version with the needed signedness.
    return UINT32_MAX;
  }
  xenos::TextureFormat format = texture_key.format;
  if (is_signed) {
    // Not supporting signed compressed textures - hopefully DXN and DXT5A are
    // not used as signed.
    desc.Format = host_formats_[uint32_t(format)].dxgi_format_signed;
  } else {
    desc.Format = GetDXGIUnormFormat(texture_key);
  }
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    unsupported_format_features_used_[uint32_t(format)] |=
        is_signed ? kUnsupportedSnormBit : kUnsupportedUnormBit;
    return UINT32_MAX;
  }

  uint32_t mip_levels = texture_key.mip_max_level + 1;
  switch (texture_key.dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = mip_levels;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = texture_key.GetDepthOrArraySize();
      desc.Texture2DArray.PlaneSlice = 0;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      break;
    case xenos::DataDimension::k3D:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc.Texture3D.MostDetailedMip = 0;
      desc.Texture3D.MipLevels = mip_levels;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      break;
    case xenos::DataDimension::kCube:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc.TextureCube.MostDetailedMip = 0;
      desc.TextureCube.MipLevels = mip_levels;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      break;
    default:
      assert_unhandled_case(texture_key.dimension);
      return UINT32_MAX;
  }

  desc.Shader4ComponentMapping =
      host_swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;

  ID3D12Device* device = command_processor_.GetD3D12Provider().GetDevice();
  uint32_t descriptor_index;
  if (bindless_resources_used_) {
    descriptor_index =
        command_processor_.RequestPersistentViewBindlessDescriptor();
    if (descriptor_index == UINT32_MAX) {
      XELOGE(
          "Failed to create a texture descriptor - no free bindless view "
          "descriptors");
      return UINT32_MAX;
    }
  } else {
    if (!srv_descriptor_cache_free_.empty()) {
      descriptor_index = srv_descriptor_cache_free_.back();
      srv_descriptor_cache_free_.pop_back();
    } else {
      // Allocated + 1 (including the descriptor that is being added), rounded
      // up to kSRVDescriptorCachePageSize, (allocated + 1 + size - 1).
      uint32_t cache_pages_needed =
          (srv_descriptor_cache_allocated_ + kSRVDescriptorCachePageSize) /
          kSRVDescriptorCachePageSize;
      if (srv_descriptor_cache_.size() < cache_pages_needed) {
        D3D12_DESCRIPTOR_HEAP_DESC cache_heap_desc;
        cache_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cache_heap_desc.NumDescriptors = kSRVDescriptorCachePageSize;
        cache_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        cache_heap_desc.NodeMask = 0;
        while (srv_descriptor_cache_.size() < cache_pages_needed) {
          Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cache_heap;
          if (FAILED(device->CreateDescriptorHeap(&cache_heap_desc,
                                                  IID_PPV_ARGS(&cache_heap)))) {
            XELOGE(
                "D3D12TextureCache: Failed to create a texture descriptor - "
                "couldn't create a descriptor cache heap");
            return UINT32_MAX;
          }
          srv_descriptor_cache_.emplace_back(cache_heap.Get());
        }
      }
      descriptor_index = srv_descriptor_cache_allocated_++;
    }
  }
  device->CreateShaderResourceView(
      texture.resource(), &desc,
      GetTextureDescriptorCPUHandle(descriptor_index));
  texture.AddSRVDescriptorIndex(descriptor_key, descriptor_index);
  return descriptor_index;
}

void D3D12TextureCache::ReleaseTextureDescriptor(uint32_t descriptor_index) {
  if (bindless_resources_used_) {
    command_processor_.ReleaseViewBindlessDescriptorImmediately(
        descriptor_index);
  } else {
    srv_descriptor_cache_free_.push_back(descriptor_index);
  }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12TextureCache::GetTextureDescriptorCPUHandle(
    uint32_t descriptor_index) const {
  const ui::d3d12::D3D12Provider& provider =
      command_processor_.GetD3D12Provider();
  if (bindless_resources_used_) {
    return provider.OffsetViewDescriptor(
        command_processor_.GetViewBindlessHeapCPUStart(), descriptor_index);
  }
  D3D12_CPU_DESCRIPTOR_HANDLE heap_start =
      srv_descriptor_cache_[descriptor_index / kSRVDescriptorCachePageSize]
          .heap_start();
  uint32_t heap_offset = descriptor_index % kSRVDescriptorCachePageSize;
  return provider.OffsetViewDescriptor(heap_start, heap_offset);
}

xenos::ClampMode D3D12TextureCache::NormalizeClampMode(
    xenos::ClampMode clamp_mode) const {
  if (clamp_mode == xenos::ClampMode::kClampToHalfway) {
    // No GL_CLAMP (clamp to half edge, half border) equivalent in Direct3D 12,
    // but there's no Direct3D 9 equivalent anyway, and too weird to be suitable
    // for intentional real usage.
    return xenos::ClampMode::kClampToEdge;
  }
  if (clamp_mode == xenos::ClampMode::kMirrorClampToHalfway ||
      clamp_mode == xenos::ClampMode::kMirrorClampToBorder) {
    // No Direct3D 12 equivalents.
    return xenos::ClampMode::kMirrorClampToEdge;
  }
  return clamp_mode;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
