/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/texture_cache.h"

#include "xenia/gpu/d3d12/d3d12_command_processor.h"

namespace xe {
namespace gpu {
namespace d3d12 {

TextureCache::TextureCache(D3D12CommandProcessor* command_processor,
                           RegisterFile* register_file)
    : command_processor_(command_processor), register_file_(register_file) {}

TextureCache::~TextureCache() { Shutdown(); }

void TextureCache::Shutdown() { ClearCache(); }

void TextureCache::ClearCache() {}

void TextureCache::WriteTextureSRV(uint32_t fetch_constant,
                                   TextureDimension shader_dimension,
                                   D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  // TODO(Triang3l): Real texture descriptors instead of null.
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  switch (shader_dimension) {
    case TextureDimension::k3D:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc.Texture3D.MostDetailedMip = 0;
      desc.Texture3D.MipLevels = UINT32_MAX;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      break;
    case TextureDimension::kCube:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc.TextureCube.MostDetailedMip = 0;
      desc.TextureCube.MipLevels = UINT32_MAX;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      break;
    default:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = UINT32_MAX;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = 1;
      desc.Texture2DArray.PlaneSlice = 0;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      break;
  }
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateShaderResourceView(nullptr, &desc, handle);
}

void TextureCache::WriteSampler(uint32_t fetch_constant,
                                D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  // TODO(Triang3l): Real samplers instead of this dummy.
  D3D12_SAMPLER_DESC desc;
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.MipLODBias = 0.0f;
  desc.MaxAnisotropy = 1;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  desc.BorderColor[0] = 0.0f;
  desc.BorderColor[1] = 0.0f;
  desc.BorderColor[2] = 0.0f;
  desc.BorderColor[3] = 0.0f;
  desc.MinLOD = 0.0f;
  desc.MaxLOD = 0.0f;
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateSampler(&desc, handle);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
