/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/texture_cache.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_128bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_16bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_32bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_64bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_8bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_ctx1_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxt3a_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_64bpp_cs.h"

const TextureCache::HostFormat TextureCache::host_formats_[64] = {
    // k_1_REVERSE
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_1
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_8
    {DXGI_FORMAT_R8_UNORM, LoadMode::k8bpb, TileMode::kUnknown},
    // k_1_5_5_5
    {DXGI_FORMAT_B5G5R5A1_UNORM, LoadMode::k16bpb, TileMode::kUnknown},
    // k_5_6_5
    {DXGI_FORMAT_B5G6R5_UNORM, LoadMode::k16bpb, TileMode::kUnknown},
    // k_6_5_5
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_8_8_8_8
    {DXGI_FORMAT_R8G8B8A8_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_2_10_10_10
    {DXGI_FORMAT_R10G10B10A2_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_8_A
    {DXGI_FORMAT_R8_UNORM, LoadMode::k8bpb, TileMode::kUnknown},
    // k_8_B
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_8_8
    {DXGI_FORMAT_R8G8_UNORM, LoadMode::k16bpb, TileMode::kUnknown},
    // k_Cr_Y1_Cb_Y0_REP
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_Y1_Cr_Y0_Cb_REP
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_16_16_EDRAM
    {DXGI_FORMAT_R16G16_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_8_8_8_8_A
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_4_4_4_4
    {DXGI_FORMAT_B4G4R4A4_UNORM, LoadMode::k16bpb, TileMode::kUnknown},
    // k_10_11_11
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_11_11_10
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_DXT1
    {DXGI_FORMAT_BC1_UNORM, LoadMode::k64bpb, TileMode::kUnknown},
    // k_DXT2_3
    {DXGI_FORMAT_BC2_UNORM, LoadMode::k128bpb, TileMode::kUnknown},
    // k_DXT4_5
    {DXGI_FORMAT_BC3_UNORM, LoadMode::k128bpb, TileMode::kUnknown},
    // k_16_16_16_16_EDRAM
    {DXGI_FORMAT_R16G16B16A16_UNORM, LoadMode::k64bpb, TileMode::k64bpp},
    // R32_FLOAT for depth because shaders would require an additional SRV to
    // sample stencil, which we don't provide.
    // k_24_8
    {DXGI_FORMAT_R32_FLOAT, LoadMode::kDepthUnorm, TileMode::kUnknown},
    // k_24_8_FLOAT
    {DXGI_FORMAT_R32_FLOAT, LoadMode::kDepthFloat, TileMode::kUnknown},
    // k_16
    {DXGI_FORMAT_R16_UNORM, LoadMode::k16bpb, TileMode::kUnknown},
    // k_16_16
    {DXGI_FORMAT_R16G16_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_16_16_16_16
    {DXGI_FORMAT_R16G16B16A16_UNORM, LoadMode::k64bpb, TileMode::k64bpp},
    // k_16_EXPAND
    {DXGI_FORMAT_R16_FLOAT, LoadMode::k16bpb, TileMode::kUnknown},
    // k_16_16_EXPAND
    {DXGI_FORMAT_R16G16_FLOAT, LoadMode::k32bpb, TileMode::k32bpp},
    // k_16_16_16_16_EXPAND
    {DXGI_FORMAT_R16G16B16A16_FLOAT, LoadMode::k64bpb, TileMode::k64bpp},
    // k_16_FLOAT
    {DXGI_FORMAT_R16_FLOAT, LoadMode::k16bpb, TileMode::kUnknown},
    // k_16_16_FLOAT
    {DXGI_FORMAT_R16G16_FLOAT, LoadMode::k32bpb, TileMode::k32bpp},
    // k_16_16_16_16_FLOAT
    {DXGI_FORMAT_R16G16B16A16_FLOAT, LoadMode::k64bpb, TileMode::k64bpp},
    // k_32
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_32
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_32_32_32
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_FLOAT
    {DXGI_FORMAT_R32_FLOAT, LoadMode::k32bpb, TileMode::k32bpp},
    // k_32_32_FLOAT
    {DXGI_FORMAT_R32G32_FLOAT, LoadMode::k64bpb, TileMode::k64bpp},
    // k_32_32_32_32_FLOAT
    {DXGI_FORMAT_R32G32B32A32_FLOAT, LoadMode::k128bpb, TileMode::kUnknown},
    // k_32_AS_8
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_AS_8_8
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_16_MPEG
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_16_16_MPEG
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_AS_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_AS_8_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_16_INTERLACED
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_16_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_DXN
    {DXGI_FORMAT_BC5_UNORM, LoadMode::k128bpb, TileMode::kUnknown},
    // k_8_8_8_8_AS_16_16_16_16
    {DXGI_FORMAT_R8G8B8A8_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_DXT1_AS_16_16_16_16
    {DXGI_FORMAT_BC1_UNORM, LoadMode::k64bpb, TileMode::kUnknown},
    // k_DXT2_3_AS_16_16_16_16
    {DXGI_FORMAT_BC2_UNORM, LoadMode::k128bpb, TileMode::kUnknown},
    // k_DXT4_5_AS_16_16_16_16
    {DXGI_FORMAT_BC3_UNORM, LoadMode::k128bpb, TileMode::kUnknown},
    // k_2_10_10_10_AS_16_16_16_16
    {DXGI_FORMAT_R10G10B10A2_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_10_11_11_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_11_11_10_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_32_32_32_FLOAT
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_DXT3A
    {DXGI_FORMAT_BC2_UNORM, LoadMode::kDXT3A, TileMode::kUnknown},
    // k_DXT5A
    {DXGI_FORMAT_BC4_UNORM, LoadMode::k64bpb, TileMode::kUnknown},
    // k_CTX1
    {DXGI_FORMAT_R8G8_UNORM, LoadMode::kCTX1, TileMode::kUnknown},
    // k_DXT3A_AS_1_1_1_1
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
    // k_8_8_8_8_GAMMA
    {DXGI_FORMAT_R8G8B8A8_UNORM, LoadMode::k32bpb, TileMode::k32bpp},
    // k_2_10_10_10_FLOAT_EDRAM
    {DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, TileMode::kUnknown},
};

const char* const TextureCache::dimension_names_[4] = {"1D", "2D", "3D",
                                                       "cube"};

const TextureCache::LoadModeInfo TextureCache::load_mode_info_[] = {
    {texture_load_8bpb_cs, sizeof(texture_load_8bpb_cs)},
    {texture_load_16bpb_cs, sizeof(texture_load_16bpb_cs)},
    {texture_load_32bpb_cs, sizeof(texture_load_32bpb_cs)},
    {texture_load_64bpb_cs, sizeof(texture_load_64bpb_cs)},
    {texture_load_128bpb_cs, sizeof(texture_load_128bpb_cs)},
    {texture_load_dxt3a_cs, sizeof(texture_load_dxt3a_cs)},
    {texture_load_ctx1_cs, sizeof(texture_load_ctx1_cs)},
    {texture_load_depth_unorm_cs, sizeof(texture_load_depth_unorm_cs)},
    {texture_load_depth_float_cs, sizeof(texture_load_depth_float_cs)},
};

const TextureCache::TileModeInfo TextureCache::tile_mode_info_[] = {
    {texture_tile_32bpp_cs, sizeof(texture_tile_32bpp_cs)},
    {texture_tile_64bpp_cs, sizeof(texture_tile_64bpp_cs)},
};

TextureCache::TextureCache(D3D12CommandProcessor* command_processor,
                           RegisterFile* register_file,
                           SharedMemory* shared_memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      shared_memory_(shared_memory) {}

TextureCache::~TextureCache() { Shutdown(); }

bool TextureCache::Initialize() {
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();

  // Create the loading root signature.
  D3D12_ROOT_PARAMETER root_parameters[2];
  // Parameter 0 is constants (changed very often when untiling).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameters[0].Descriptor.ShaderRegister = 0;
  root_parameters[0].Descriptor.RegisterSpace = 0;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is source and target.
  D3D12_DESCRIPTOR_RANGE root_copy_ranges[2];
  root_copy_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  root_copy_ranges[0].NumDescriptors = 1;
  root_copy_ranges[0].BaseShaderRegister = 0;
  root_copy_ranges[0].RegisterSpace = 0;
  root_copy_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  root_copy_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  root_copy_ranges[1].NumDescriptors = 1;
  root_copy_ranges[1].BaseShaderRegister = 0;
  root_copy_ranges[1].RegisterSpace = 0;
  root_copy_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[1].DescriptorTable.NumDescriptorRanges = 2;
  root_parameters[1].DescriptorTable.pDescriptorRanges = root_copy_ranges;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.NumParameters = UINT(xe::countof(root_parameters));
  root_signature_desc.pParameters = root_parameters;
  root_signature_desc.NumStaticSamplers = 0;
  root_signature_desc.pStaticSamplers = nullptr;
  root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  load_root_signature_ =
      ui::d3d12::util::CreateRootSignature(device, root_signature_desc);
  if (load_root_signature_ == nullptr) {
    XELOGE("Failed to create the texture loading root signature");
    Shutdown();
    return false;
  }
  // Create the tiling root signature (almost the same, but with root constants
  // in parameter 0).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameters[0].Constants.ShaderRegister = 0;
  root_parameters[0].Constants.RegisterSpace = 0;
  root_parameters[0].Constants.Num32BitValues =
      sizeof(TileConstants) / sizeof(uint32_t);
  tile_root_signature_ =
      ui::d3d12::util::CreateRootSignature(device, root_signature_desc);
  if (tile_root_signature_ == nullptr) {
    XELOGE("Failed to create the texture tiling root signature");
    Shutdown();
    return false;
  }

  // Create the loading and tiling pipelines.
  for (uint32_t i = 0; i < uint32_t(LoadMode::kCount); ++i) {
    const LoadModeInfo& mode_info = load_mode_info_[i];
    load_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.shader, mode_info.shader_size, load_root_signature_);
    if (load_pipelines_[i] == nullptr) {
      XELOGE("Failed to create the texture loading pipeline for mode %u", i);
      Shutdown();
      return false;
    }
  }
  for (uint32_t i = 0; i < uint32_t(TileMode::kCount); ++i) {
    const TileModeInfo& mode_info = tile_mode_info_[i];
    tile_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.shader, mode_info.shader_size, tile_root_signature_);
    if (tile_pipelines_[i] == nullptr) {
      XELOGE("Failed to create the texture tiling pipeline for mode %u", i);
      Shutdown();
      return false;
    }
  }

  return true;
}

void TextureCache::Shutdown() {
  ClearCache();

  for (uint32_t i = 0; i < uint32_t(TileMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(tile_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(tile_root_signature_);
  for (uint32_t i = 0; i < uint32_t(LoadMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(load_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(load_root_signature_);
}

void TextureCache::ClearCache() {
  // Destroy all the textures.
  for (auto texture_pair : textures_) {
    Texture* texture = texture_pair.second;
    if (texture->resource != nullptr) {
      texture->resource->Release();
    }
    delete texture;
  }
  textures_.clear();
}

void TextureCache::TextureFetchConstantWritten(uint32_t index) {
  texture_keys_in_sync_ &= ~(1u << index);
}

void TextureCache::BeginFrame() {
  // In case there was a failure creating something in the previous frame, make
  // sure bindings are reset so a new attempt will surely be made if the texture
  // is requested again.
  ClearBindings();
}

void TextureCache::RequestTextures(uint32_t used_vertex_texture_mask,
                                   uint32_t used_pixel_texture_mask) {
  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return;
  }
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // If a texture was invalidated, ignore whether bindings have been changed
  // when determining whether textures need to be reloaded.
  bool force_load =
      texture_invalidated_.exchange(false, std::memory_order_relaxed);
  if (force_load) {
    texture_keys_in_sync_ = 0;
  }

  // Update the texture keys and the textures.
  uint32_t used_texture_mask =
      used_vertex_texture_mask | used_pixel_texture_mask;
  uint32_t index = 0;
  while (xe::bit_scan_forward(used_texture_mask, &index)) {
    uint32_t index_bit = 1u << index;
    used_texture_mask &= ~index_bit;
    if (texture_keys_in_sync_ & index_bit) {
      continue;
    }
    TextureBinding& binding = texture_bindings_[index];
    uint32_t r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + index * 6;
    auto group =
        reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
    TextureKey old_key = binding.key;
    TextureKeyFromFetchConstant(group->texture_fetch, binding.key,
                                binding.swizzle);
    texture_keys_in_sync_ |= index_bit;
    if (binding.key.IsInvalid()) {
      binding.texture = nullptr;
      continue;
    }
    bool load = force_load;
    if (binding.key != old_key) {
      binding.texture = FindOrCreateTexture(binding.key);
      load = true;
    }
    if (load && binding.texture != nullptr) {
      LoadTextureData(binding.texture);
    }
  }

  // Transition the textures to the needed usage.
  used_texture_mask = used_vertex_texture_mask | used_pixel_texture_mask;
  while (xe::bit_scan_forward(used_texture_mask, &index)) {
    uint32_t index_bit = 1u << index;
    used_texture_mask &= ~index_bit;
    Texture* texture = texture_bindings_[index].texture;
    if (texture == nullptr) {
      continue;
    }
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATES(0);
    if (used_vertex_texture_mask & index_bit) {
      state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (used_pixel_texture_mask & index_bit) {
      state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    command_processor_->PushTransitionBarrier(texture->resource, texture->state,
                                              state);
    texture->state = state;
  }
}

void TextureCache::WriteTextureSRV(uint32_t fetch_constant,
                                   TextureDimension shader_dimension,
                                   D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  const TextureBinding& binding = texture_bindings_[fetch_constant];
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = host_formats_[uint32_t(binding.key.format)].dxgi_format;
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    // A null descriptor must still have a valid format.
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  // XE_GPU_SWIZZLE and D3D12_SHADER_COMPONENT_MAPPING are the same except for
  // one bit.
  desc.Shader4ComponentMapping =
      binding.swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  ID3D12Resource* resource =
      binding.texture != nullptr ? binding.texture->resource : nullptr;
  switch (shader_dimension) {
    case TextureDimension::k3D:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc.Texture3D.MostDetailedMip = 0;
      desc.Texture3D.MipLevels = binding.key.mip_max_level + 1;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      if (binding.key.dimension != Dimension::k3D) {
        // Create a null descriptor so it's safe to sample this texture even
        // though it has different dimensions.
        resource = nullptr;
      }
      break;
    case TextureDimension::kCube:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc.TextureCube.MostDetailedMip = 0;
      desc.TextureCube.MipLevels = binding.key.mip_max_level + 1;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      if (binding.key.dimension != Dimension::kCube) {
        resource = nullptr;
      }
      break;
    default:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = binding.key.mip_max_level + 1;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = binding.key.depth;
      desc.Texture2DArray.PlaneSlice = 0;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      if (binding.key.dimension == Dimension::k3D ||
          binding.key.dimension == Dimension::kCube) {
        resource = nullptr;
      }
      break;
  }
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateShaderResourceView(resource, &desc, handle);
}

void TextureCache::WriteSampler(uint32_t fetch_constant,
                                TextureFilter mag_filter,
                                TextureFilter min_filter,
                                TextureFilter mip_filter,
                                AnisoFilter aniso_filter,
                                D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto& regs = *register_file_;
  uint32_t r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + fetch_constant * 6;
  auto group =
      reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;
  if (mag_filter == TextureFilter::kUseFetchConst) {
    mag_filter = TextureFilter(fetch.mag_filter);
  }
  if (min_filter == TextureFilter::kUseFetchConst) {
    min_filter = TextureFilter(fetch.min_filter);
  }
  if (mip_filter == TextureFilter::kUseFetchConst) {
    mip_filter = TextureFilter(fetch.mip_filter);
  }
  if (aniso_filter == AnisoFilter::kUseFetchConst) {
    aniso_filter = AnisoFilter(fetch.aniso_filter);
  }
  D3D12_SAMPLER_DESC desc;
  if (aniso_filter != AnisoFilter::kDisabled) {
    desc.Filter = D3D12_FILTER_ANISOTROPIC;
    desc.MaxAnisotropy = std::min(1u << (uint32_t(aniso_filter) - 1), 16u);
  } else {
    D3D12_FILTER_TYPE d3d_filter_min = min_filter == TextureFilter::kLinear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE d3d_filter_mag = mag_filter == TextureFilter::kLinear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE d3d_filter_mip = mip_filter == TextureFilter::kLinear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    // TODO(Triang3l): Investigate mip_filter TextureFilter::kBaseMap.
    desc.Filter = D3D12_ENCODE_BASIC_FILTER(
        d3d_filter_min, d3d_filter_mag, d3d_filter_mip,
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.MaxAnisotropy = 1;
  }
  // FIXME(Triang3l): Halfway and mirror clamp to border aren't mapped properly.
  static const D3D12_TEXTURE_ADDRESS_MODE kAddressModeMap[] = {
      /* kRepeat               */ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      /* kMirroredRepeat       */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
      /* kClampToEdge          */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      /* kMirrorClampToEdge    */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      /* kClampToHalfway       */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      /* kMirrorClampToHalfway */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      /* kClampToBorder        */ D3D12_TEXTURE_ADDRESS_MODE_BORDER,
      /* kMirrorClampToBorder  */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
  };
  desc.AddressU = kAddressModeMap[fetch.clamp_x];
  desc.AddressV = kAddressModeMap[fetch.clamp_y];
  desc.AddressW = kAddressModeMap[fetch.clamp_z];
  desc.MipLODBias = fetch.lod_bias * (1.0f / 32.0f);
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  // TODO(Triang3l): Border colors k_ACBYCR_BLACK and k_ACBCRY_BLACK.
  if (BorderColor(fetch.border_color) == BorderColor::k_AGBR_White) {
    desc.BorderColor[0] = 1.0f;
    desc.BorderColor[1] = 1.0f;
    desc.BorderColor[2] = 1.0f;
    desc.BorderColor[3] = 1.0f;
  } else {
    desc.BorderColor[0] = 0.0f;
    desc.BorderColor[1] = 0.0f;
    desc.BorderColor[2] = 0.0f;
    desc.BorderColor[3] = 0.0f;
  }
  desc.MinLOD = float(fetch.mip_min_level);
  desc.MaxLOD = float(fetch.mip_max_level);
  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;
  if (base_page == 0 || base_page == mip_page) {
    // Games should clamp mip level in this case anyway, but just for safety.
    desc.MinLOD = std::max(desc.MinLOD, 1.0f);
  }
  if (mip_page == 0) {
    desc.MaxLOD = 0.0f;
  }
  desc.MaxLOD = std::max(desc.MaxLOD, desc.MinLOD);
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateSampler(&desc, handle);
}

DXGI_FORMAT TextureCache::GetResolveDXGIFormat(TextureFormat format) {
  const HostFormat& host_format = host_formats_[uint32_t(format)];
  return host_format.tile_mode != TileMode::kUnknown ? host_format.dxgi_format
                                                     : DXGI_FORMAT_UNKNOWN;
}

bool TextureCache::TileResolvedTexture(
    TextureFormat format, uint32_t texture_base, uint32_t texture_pitch,
    uint32_t texture_height, uint32_t resolve_width, uint32_t resolve_height,
    Endian128 endian, ID3D12Resource* buffer, uint32_t buffer_size,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
  TileMode tile_mode = host_formats_[uint32_t(format)].tile_mode;
  if (tile_mode == TileMode::kUnknown) {
    assert_always();
    return false;
  }

  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return false;
  }
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  texture_base &= 0x1FFFFFFF;
  // TODO(Triang3l): Allow smaller alignment for 8- and 16-bit textures (but
  // probably not really needed).
  assert_false(texture_base & 0x3);

  // Calculate the texture size for memory operations and ensure we can write to
  // the specified shared memory location.
  uint32_t texture_size = texture_util::GetGuestMipStorageSize(
      xe::align(texture_pitch, 32u), xe::align(texture_height, 32u), 1, true,
      format, nullptr);
  if (!shared_memory_->MakeTilesResident(texture_base, texture_size)) {
    return false;
  }

  // Tile the texture.
  // TODO(Triang3l): Typed UAVs for 8- and 16-bit textures.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    return false;
  }
  shared_memory_->UseForWriting();
  command_processor_->SubmitBarriers();
  command_list->SetComputeRootSignature(tile_root_signature_);
  TileConstants tile_constants;
  tile_constants.guest_base = texture_base;
  tile_constants.endian_guest_pitch = uint32_t(endian) | (texture_pitch << 3);
  tile_constants.size = resolve_width | (resolve_height << 16);
  tile_constants.host_base = uint32_t(footprint.Offset);
  tile_constants.host_pitch = uint32_t(footprint.Footprint.RowPitch);
  command_list->SetComputeRoot32BitConstants(
      0, sizeof(tile_constants) / sizeof(uint32_t), &tile_constants, 0);
  ui::d3d12::util::CreateRawBufferSRV(device, descriptor_cpu_start, buffer,
                                      buffer_size);
  shared_memory_->CreateRawUAV(
      provider->OffsetViewDescriptor(descriptor_cpu_start, 1));
  command_list->SetComputeRootDescriptorTable(1, descriptor_gpu_start);
  command_processor_->SetComputePipeline(tile_pipelines_[uint32_t(tile_mode)]);
  command_list->Dispatch((resolve_width + 31) >> 5, (resolve_height + 31) >> 5,
                         1);

  // Commit the write.
  command_processor_->PushUAVBarrier(shared_memory_->GetBuffer());

  // Invalidate textures.
  shared_memory_->RangeWrittenByGPU(texture_base, texture_size);

  return true;
}

bool TextureCache::RequestSwapTexture(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto group = reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(
      &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]);
  auto& fetch = group->texture_fetch;
  TextureKey key;
  uint32_t swizzle;
  TextureKeyFromFetchConstant(group->texture_fetch, key, swizzle);
  if (key.base_page == 0 || key.dimension != Dimension::k2D) {
    return false;
  }
  Texture* texture = FindOrCreateTexture(key);
  if (texture == nullptr || !LoadTextureData(texture)) {
    return false;
  }
  command_processor_->PushTransitionBarrier(
      texture->resource, texture->state,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  texture->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
  srv_desc.Format = host_formats_[uint32_t(key.format)].dxgi_format;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping =
      swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.MipLevels = 1;
  srv_desc.Texture2D.PlaneSlice = 0;
  srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateShaderResourceView(texture->resource, &srv_desc, handle);
  return true;
}

void TextureCache::TextureKeyFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, TextureKey& key_out,
    uint32_t& swizzle_out) {
  // Reset the key and the swizzle.
  key_out.MakeInvalid();
  swizzle_out = xenos::XE_GPU_SWIZZLE_0 | (xenos::XE_GPU_SWIZZLE_0 << 3) |
                (xenos::XE_GPU_SWIZZLE_0 << 6) | (xenos::XE_GPU_SWIZZLE_0 << 9);

  if (fetch.type != 2) {
    XELOGGPU("Texture fetch type is not 2 - ignoring!");
    return;
  }

  // Validate the dimensions, get the size and clamp the maximum mip level.
  Dimension dimension = Dimension(fetch.dimension);
  uint32_t width, height, depth;
  switch (dimension) {
    case Dimension::k1D:
      if (fetch.tiled || fetch.stacked || fetch.packed_mips) {
        assert_always();
        XELOGGPU(
            "1D texture has unsupported properties - ignoring! "
            "Report the game to Xenia developers");
        return;
      }
      width = fetch.size_1d.width + 1;
      if (width > 8192) {
        assert_always();
        XELOGGPU(
            "1D texture is too wide (%u) - ignoring! "
            "Report the game to Xenia developers",
            width);
      }
      height = 1;
      depth = 1;
      break;
    case Dimension::k2D:
      width = fetch.size_stack.width + 1;
      height = fetch.size_stack.height + 1;
      depth = fetch.stacked ? fetch.size_stack.depth + 1 : 1;
      break;
    case Dimension::k3D:
      width = fetch.size_3d.width + 1;
      height = fetch.size_3d.height + 1;
      depth = fetch.size_3d.depth + 1;
      break;
    case Dimension::kCube:
      width = fetch.size_2d.width + 1;
      height = fetch.size_2d.height + 1;
      depth = 6;
      break;
  }
  uint32_t mip_max_level = texture_util::GetSmallestMipLevel(
      width, height, dimension == Dimension::k3D ? depth : 1, false);
  mip_max_level = std::min(mip_max_level, fetch.mip_max_level);

  // Normalize and check the addresses.
  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = mip_max_level != 0 ? fetch.mip_address & 0x1FFFF : 0;
  // Special case for streaming. Games such as Banjo-Kazooie: Nuts & Bolts
  // specify the same address for both the base level and the mips and set
  // mip_min_index to 1 until the texture is actually loaded - this is the way
  // recommended by a GPU hang error message found in game executables. In this
  // case we assume that the base level is not loaded yet.
  // TODO(Triang3l): Ignore the base level completely if min_mip_level is not 0
  // once we start reusing textures with zero base address to reduce memory
  // usage.
  if (base_page == mip_page) {
    base_page = 0;
  }
  if (base_page == 0 && mip_page == 0) {
    // No texture data at all.
    return;
  }

  TextureFormat format = GetBaseFormat(TextureFormat(fetch.format));

  key_out.base_page = base_page;
  key_out.mip_page = mip_page;
  key_out.dimension = dimension;
  key_out.width = width;
  key_out.height = height;
  key_out.depth = depth;
  key_out.mip_max_level = mip_max_level;
  key_out.tiled = fetch.tiled;
  key_out.packed_mips = fetch.packed_mips;
  key_out.format = format;
  key_out.endianness = Endian(fetch.endianness);

  uint32_t swizzle = fetch.swizzle;
  const uint32_t swizzle_constant_mask = 4 | (4 << 3) | (4 << 6) | (4 << 9);
  uint32_t swizzle_constant = swizzle & swizzle_constant_mask;
  uint32_t swizzle_not_constant = swizzle_constant ^ swizzle_constant_mask;
  // Get rid of 6 and 7 values (to prevent device losses if the game has
  // something broken) the quick and dirty way - by changing them to 4 and 5.
  swizzle &= ~(swizzle_constant >> 1);
  // Remap the swizzle according to the texture format.
  if (format == TextureFormat::k_1_5_5_5 || format == TextureFormat::k_5_6_5 ||
      format == TextureFormat::k_4_4_4_4) {
    // Swap red and blue.
    swizzle ^= ((~swizzle & (1 | (1 << 3) | (1 << 6) | (1 << 9))) << 1) &
               (swizzle_not_constant >> 1);
  } else if (format == TextureFormat::k_DXT3A) {
    // DXT3A is emulated as DXT3 with zero color, but the alpha should be
    // replicated into all channels.
    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    // If not 0.0 or 1.0 (if the high bit isn't set), make 3 (alpha).
    swizzle |= (swizzle_not_constant >> 1) | (swizzle_not_constant >> 2);
  } else if (format == TextureFormat::k_DXT5A) {
    // DXT5A is emulated as BC4, but DXT5 alpha (BC4 red) should be replicated.
    swizzle &= ~((swizzle_not_constant >> 1) | (swizzle_not_constant >> 2));
  }
  swizzle_out = swizzle;
}

void TextureCache::LogTextureKeyAction(TextureKey key, const char* action) {
  XELOGGPU(
      "%s %s %ux%ux%u %s %s texture with %u %spacked mip level%s, "
      "base at 0x%.8X, mips at 0x%.8X",
      action, key.tiled ? "tiled" : "linear", key.width, key.height, key.depth,
      dimension_names_[uint32_t(key.dimension)],
      FormatInfo::Get(key.format)->name, key.mip_max_level + 1,
      key.packed_mips ? "" : "un", key.mip_max_level != 0 ? "s" : "",
      key.base_page << 12, key.mip_page << 12);
}

void TextureCache::LogTextureAction(const Texture* texture,
                                    const char* action) {
  XELOGGPU(
      "%s %s %ux%ux%u %s %s texture with %u %spacked mip level%s, "
      "base at 0x%.8X (size %u), mips at 0x%.8X (size %u)",
      action, texture->key.tiled ? "tiled" : "linear", texture->key.width,
      texture->key.height, texture->key.depth,
      dimension_names_[uint32_t(texture->key.dimension)],
      FormatInfo::Get(texture->key.format)->name,
      texture->key.mip_max_level + 1, texture->key.packed_mips ? "" : "un",
      texture->key.mip_max_level != 0 ? "s" : "", texture->key.base_page << 12,
      texture->base_size, texture->key.mip_page << 12, texture->mip_size);
}

TextureCache::Texture* TextureCache::FindOrCreateTexture(TextureKey key) {
  uint64_t map_key = key.GetMapKey();

  // Try to find an existing texture.
  auto found_range = textures_.equal_range(map_key);
  for (auto iter = found_range.first; iter != found_range.second; ++iter) {
    Texture* found_texture = iter->second;
    if (found_texture->key.bucket_key == key.bucket_key) {
      return found_texture;
    }
  }

  // Create the resource. If failed to create one, don't create a texture object
  // at all so it won't be in indeterminate state.
  D3D12_RESOURCE_DESC desc;
  desc.Format = host_formats_[uint32_t(key.format)].dxgi_format;
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    return nullptr;
  }
  if (key.dimension == Dimension::k3D) {
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  } else {
    // 1D textures are treated as 2D for simplicity.
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  }
  desc.Alignment = 0;
  desc.Width = key.width;
  desc.Height = key.height;
  desc.DepthOrArraySize = key.depth;
  desc.MipLevels = key.mip_max_level + 1;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  // Untiling through a buffer instead of using unordered access because copying
  // is not done that often.
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  // Assuming untiling will be the next operation.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* resource;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE, &desc,
          state, nullptr, IID_PPV_ARGS(&resource)))) {
    LogTextureKeyAction(key, "Failed to create");
    return nullptr;
  }

  // Create the texture object and add it to the map.
  Texture* texture = new Texture;
  texture->key = key;
  texture->resource = resource;
  texture->state = state;
  texture->mip_offsets[0] = 0;
  uint32_t width_blocks, height_blocks, depth_blocks;
  if (key.base_page != 0) {
    texture_util::GetGuestMipBlocks(key.dimension, key.width, key.height,
                                    key.depth, key.format, 0, width_blocks,
                                    height_blocks, depth_blocks);
    texture->base_slice_size = texture_util::GetGuestMipStorageSize(
        width_blocks, height_blocks, depth_blocks, key.tiled, key.format,
        &texture->mip_pitches[0]);
    texture->base_in_sync = false;
  } else {
    texture->base_slice_size = 0;
    texture->mip_pitches[0] = 0;
    // Never try to upload the base level if there is none.
    texture->base_in_sync = true;
  }
  texture->mip_slice_size = 0;
  if (key.mip_page != 0) {
    uint32_t mip_max_storage_level = key.mip_max_level;
    if (key.packed_mips) {
      mip_max_storage_level =
          std::min(mip_max_storage_level,
                   texture_util::GetPackedMipLevel(key.width, key.height));
    }
    for (uint32_t i = 1; i <= mip_max_storage_level; ++i) {
      texture_util::GetGuestMipBlocks(key.dimension, key.width, key.height,
                                      key.depth, key.format, i, width_blocks,
                                      height_blocks, depth_blocks);
      texture->mip_offsets[i] = texture->mip_slice_size;
      texture->mip_slice_size += texture_util::GetGuestMipStorageSize(
          width_blocks, height_blocks, depth_blocks, key.tiled, key.format,
          &texture->mip_pitches[i]);
    }
    // The rest are either packed levels or don't exist at all.
    for (uint32_t i = mip_max_storage_level + 1;
         i < xe::countof(texture->mip_offsets); ++i) {
      texture->mip_offsets[i] = texture->mip_offsets[mip_max_storage_level];
      texture->mip_pitches[i] = texture->mip_pitches[mip_max_storage_level];
    }
    texture->mips_in_sync = false;
  } else {
    std::memset(&texture->mip_offsets[1], 0,
                (xe::countof(texture->mip_offsets) - 1) * sizeof(uint32_t));
    std::memset(&texture->mip_pitches[1], 0,
                (xe::countof(texture->mip_pitches) - 1) * sizeof(uint32_t));
    // Never try to upload the mipmaps if there are none.
    texture->mips_in_sync = true;
  }
  texture->base_size = texture->base_slice_size;
  texture->mip_size = texture->mip_slice_size;
  if (key.dimension != Dimension::k3D) {
    texture->base_size *= key.depth;
    texture->mip_size *= key.depth;
  }
  texture->base_watch_handle = nullptr;
  texture->mip_watch_handle = nullptr;
  textures_.insert(std::make_pair(map_key, texture));
  LogTextureAction(texture, "Created");

  return texture;
}

bool TextureCache::LoadTextureData(Texture* texture) {
  // See what we need to upload.
  shared_memory_->LockWatchMutex();
  bool base_in_sync = texture->base_in_sync;
  bool mips_in_sync = texture->mips_in_sync;
  shared_memory_->UnlockWatchMutex();
  if (base_in_sync && mips_in_sync) {
    return true;
  }

  auto command_list = command_processor_->GetCurrentCommandList();
  if (command_list == nullptr) {
    return false;
  }
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Get the pipeline.
  const HostFormat& host_format = host_formats_[uint32_t(texture->key.format)];
  if (host_format.load_mode == LoadMode::kUnknown) {
    return false;
  }
  ID3D12PipelineState* pipeline =
      load_pipelines_[uint32_t(host_format.load_mode)];
  if (pipeline == nullptr) {
    return false;
  }

  // Request uploading of the texture data to the shared memory.
  if (!base_in_sync) {
    if (!shared_memory_->RequestRange(texture->key.base_page << 12,
                                      texture->base_size)) {
      return false;
    }
  }
  if (!mips_in_sync) {
    if (!shared_memory_->RequestRange(texture->key.mip_page << 12,
                                      texture->mip_size)) {
      return false;
    }
  }

  // Get the guest layout.
  bool is_3d = texture->key.dimension == Dimension::k3D;
  uint32_t width = texture->key.width;
  uint32_t height = texture->key.height;
  uint32_t depth = is_3d ? texture->key.depth : 1;
  uint32_t slice_count = is_3d ? 1 : texture->key.depth;
  TextureFormat guest_format = texture->key.format;
  const FormatInfo* guest_format_info = FormatInfo::Get(guest_format);
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;

  // Get the host layout and the buffer.
  D3D12_RESOURCE_DESC resource_desc = texture->resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT host_layouts[D3D12_REQ_MIP_LEVELS];
  UINT64 host_slice_size;
  device->GetCopyableFootprints(&resource_desc, 0, resource_desc.MipLevels, 0,
                                host_layouts, nullptr, nullptr,
                                &host_slice_size);
  // The shaders deliberately overflow for simplicity, and GetCopyableFootprints
  // doesn't align the size of the last row (or the size if there's only one
  // row, not really sure) to row pitch, so add some excess bytes for safety.
  // 1x1 8-bit and 16-bit textures even give a device loss because the raw UAV
  // has a size of 0.
  host_slice_size =
      xe::align(host_slice_size, UINT64(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_->RequestScratchGPUBuffer(
      uint32_t(host_slice_size), copy_buffer_state);
  if (copy_buffer == nullptr) {
    return false;
  }

  // Begin loading.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
    return false;
  }
  shared_memory_->UseForReading();
  shared_memory_->CreateSRV(descriptor_cpu_start);
  ui::d3d12::util::CreateRawBufferUAV(
      device, provider->OffsetViewDescriptor(descriptor_cpu_start, 1),
      copy_buffer, uint32_t(host_slice_size));
  command_processor_->SetComputePipeline(pipeline);
  command_list->SetComputeRootSignature(load_root_signature_);
  command_list->SetComputeRootDescriptorTable(1, descriptor_gpu_start);

  // Submit commands.
  command_processor_->PushTransitionBarrier(texture->resource, texture->state,
                                            D3D12_RESOURCE_STATE_COPY_DEST);
  texture->state = D3D12_RESOURCE_STATE_COPY_DEST;
  uint32_t mip_first = base_in_sync ? 1 : 0;
  uint32_t mip_last = mips_in_sync ? 0 : resource_desc.MipLevels - 1;
  auto cbuffer_pool = command_processor_->GetConstantBufferPool();
  LoadConstants load_constants;
  load_constants.is_3d = is_3d ? 1 : 0;
  load_constants.endianness = uint32_t(texture->key.endianness);
  if (!texture->key.packed_mips) {
    load_constants.guest_mip_offset[0] = 0;
    load_constants.guest_mip_offset[1] = 0;
    load_constants.guest_mip_offset[2] = 0;
  }
  for (uint32_t i = 0; i < slice_count; ++i) {
    command_processor_->PushTransitionBarrier(
        copy_buffer, copy_buffer_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    copy_buffer_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    for (uint32_t j = mip_first; j <= mip_last; ++j) {
      if (j == 0) {
        load_constants.guest_base =
            (texture->key.base_page << 12) + i * texture->base_slice_size;
      } else {
        load_constants.guest_base =
            (texture->key.mip_page << 12) + i * texture->mip_slice_size;
      }
      load_constants.guest_base += texture->mip_offsets[j];
      load_constants.guest_pitch = texture->key.tiled
                                       ? LoadConstants::kGuestPitchTiled
                                       : texture->mip_pitches[j];
      load_constants.host_base = uint32_t(host_layouts[j].Offset);
      load_constants.host_pitch = host_layouts[j].Footprint.RowPitch;
      load_constants.size_texels[0] = std::max(width >> j, 1u);
      load_constants.size_texels[1] = std::max(height >> j, 1u);
      load_constants.size_texels[2] = std::max(depth >> j, 1u);
      load_constants.size_blocks[0] =
          (load_constants.size_texels[0] + (block_width - 1)) / block_width;
      load_constants.size_blocks[1] =
          (load_constants.size_texels[1] + (block_height - 1)) / block_height;
      load_constants.size_blocks[2] = load_constants.size_texels[2];
      if (texture->key.packed_mips) {
        texture_util::GetPackedMipOffset(width, height, depth, guest_format, j,
                                         load_constants.guest_mip_offset[0],
                                         load_constants.guest_mip_offset[1],
                                         load_constants.guest_mip_offset[2]);
      }
      D3D12_GPU_VIRTUAL_ADDRESS cbuffer_gpu_address;
      uint8_t* cbuffer_mapping = cbuffer_pool->RequestFull(
          xe::align(uint32_t(sizeof(load_constants)), 256u), nullptr, nullptr,
          &cbuffer_gpu_address);
      if (cbuffer_mapping == nullptr) {
        command_processor_->ReleaseScratchGPUBuffer(copy_buffer,
                                                    copy_buffer_state);
        return false;
      }
      std::memcpy(cbuffer_mapping, &load_constants, sizeof(load_constants));
      command_list->SetComputeRootConstantBufferView(0, cbuffer_gpu_address);
      command_processor_->SubmitBarriers();
      // Each thread group processes 32x32x1 blocks.
      command_list->Dispatch((load_constants.size_blocks[0] + 31) >> 5,
                             (load_constants.size_blocks[1] + 31) >> 5,
                             load_constants.size_blocks[2]);
    }
    command_processor_->PushUAVBarrier(copy_buffer);
    command_processor_->PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_processor_->SubmitBarriers();
    UINT slice_first_subresource = i * resource_desc.MipLevels;
    for (uint32_t j = mip_first; j <= mip_last; ++j) {
      D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
      location_source.pResource = copy_buffer;
      location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      location_source.PlacedFootprint = host_layouts[j];
      location_dest.pResource = texture->resource;
      location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      location_dest.SubresourceIndex = slice_first_subresource + j;
      command_list->CopyTextureRegion(&location_dest, 0, 0, 0, &location_source,
                                      nullptr);
    }
  }

  command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);

  // Mark the ranges as uploaded and watch them.
  shared_memory_->LockWatchMutex();
  texture->base_in_sync = true;
  texture->mips_in_sync = true;
  if (!base_in_sync) {
    texture->base_watch_handle = shared_memory_->WatchMemoryRange(
        texture->key.base_page << 12, texture->base_size, WatchCallbackThunk,
        this, texture, 0);
  }
  if (!mips_in_sync) {
    texture->mip_watch_handle = shared_memory_->WatchMemoryRange(
        texture->key.mip_page << 12, texture->mip_size, WatchCallbackThunk,
        this, texture, 1);
  }
  shared_memory_->UnlockWatchMutex();

  LogTextureAction(texture, "Loaded");
  return true;
}

void TextureCache::WatchCallbackThunk(void* context, void* data,
                                      uint64_t argument) {
  TextureCache* texture_cache = reinterpret_cast<TextureCache*>(context);
  texture_cache->WatchCallback(reinterpret_cast<Texture*>(data), argument != 0);
}

void TextureCache::WatchCallback(Texture* texture, bool is_mip) {
  // Mutex already locked here.
  if (is_mip) {
    texture->mips_in_sync = false;
    texture->mip_watch_handle = nullptr;
  } else {
    texture->base_in_sync = false;
    texture->base_watch_handle = nullptr;
  }
  XELOGGPU("Texture %s at 0x%.8X invalidated", is_mip ? "mips" : "base",
           (is_mip ? texture->key.mip_page : texture->key.base_page) << 12);
  texture_invalidated_.store(true, std::memory_order_relaxed);
}

void TextureCache::ClearBindings() {
  std::memset(texture_bindings_, 0, sizeof(texture_bindings_));
  texture_keys_in_sync_ = 0;
  // Already reset everything.
  texture_invalidated_.store(false, std::memory_order_relaxed);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
