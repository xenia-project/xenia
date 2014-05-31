/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_texture_cache.h>

#include <xenia/gpu/gpu-private.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


D3D11TextureCache::D3D11TextureCache(
    Memory* memory,
    ID3D11DeviceContext* context, ID3D11Device* device)
    : TextureCache(memory),
      context_(context), device_(device) {
  context_->AddRef();
  device_->AddRef();
}

D3D11TextureCache::~D3D11TextureCache() {
  XESAFERELEASE(device_);
  XESAFERELEASE(context_);
}

Texture* D3D11TextureCache::CreateTexture(
    uint32_t address, const xenos::xe_gpu_texture_fetch_t& fetch) {
  return new D3D11Texture(this, address);
}

ID3D11SamplerState* D3D11TextureCache::GetSamplerState(
    const xenos::xe_gpu_texture_fetch_t& fetch,
    const Shader::tex_buffer_desc_t& desc) {
  D3D11_SAMPLER_DESC sampler_desc;
  xe_zero_struct(&sampler_desc, sizeof(sampler_desc));
  uint32_t min_filter = desc.tex_fetch.min_filter == 3 ?
      fetch.min_filter : desc.tex_fetch.min_filter;
  uint32_t mag_filter = desc.tex_fetch.mag_filter == 3 ?
      fetch.mag_filter : desc.tex_fetch.mag_filter;
  uint32_t mip_filter = desc.tex_fetch.mip_filter == 3 ?
      fetch.mip_filter : desc.tex_fetch.mip_filter;
  // MIN, MAG, MIP
  static const D3D11_FILTER filter_matrix[2][2][3] = {
    {
      // min = POINT
      {
        // mag = POINT
        D3D11_FILTER_MIN_MAG_MIP_POINT,
        D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,
        D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR, // basemap?
      },
      {
        // mag = LINEAR
        D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
        D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,
        D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR, // basemap?
      },
    },
    {
      // min = LINEAR
      {
        // mag = POINT
        D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
        D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
        D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR, // basemap?
      },
      {
        // mag = LINEAR
        D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_FILTER_MIN_MAG_MIP_LINEAR, // basemap?
      },
    },
  };
  sampler_desc.Filter = filter_matrix[min_filter][mag_filter][mip_filter];
  static const D3D11_TEXTURE_ADDRESS_MODE mode_map[] = {
    D3D11_TEXTURE_ADDRESS_WRAP,
    D3D11_TEXTURE_ADDRESS_MIRROR,
    D3D11_TEXTURE_ADDRESS_CLAMP,        // ?
    D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,  // ?
    D3D11_TEXTURE_ADDRESS_CLAMP,        // ?
    D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,  // ?
    D3D11_TEXTURE_ADDRESS_BORDER,       // ?
    D3D11_TEXTURE_ADDRESS_MIRROR,       // ?
  };
  sampler_desc.AddressU = mode_map[fetch.clamp_x];
  sampler_desc.AddressV = mode_map[fetch.clamp_y];
  sampler_desc.AddressW = mode_map[fetch.clamp_z];
  sampler_desc.MipLODBias;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.BorderColor[0];
  sampler_desc.BorderColor[1];
  sampler_desc.BorderColor[2];
  sampler_desc.BorderColor[3];
  sampler_desc.MinLOD;
  sampler_desc.MaxLOD;
  ID3D11SamplerState* sampler_state = NULL;
  HRESULT hr = device_->CreateSamplerState(&sampler_desc, &sampler_state);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to create sampler state");
    return nullptr;
  }
  return sampler_state;
}
