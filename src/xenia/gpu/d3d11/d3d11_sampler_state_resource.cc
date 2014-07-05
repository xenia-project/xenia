/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_sampler_state_resource.h>

#include <xenia/gpu/d3d11/d3d11_resource_cache.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11SamplerStateResource::D3D11SamplerStateResource(
    D3D11ResourceCache* resource_cache, const Info& info)
    : SamplerStateResource(info),
      resource_cache_(resource_cache),
      handle_(nullptr) {
}

D3D11SamplerStateResource::~D3D11SamplerStateResource() {
  XESAFERELEASE(handle_);
}

int D3D11SamplerStateResource::Prepare() {
  if (handle_) {
    return 0;
  }

  D3D11_SAMPLER_DESC sampler_desc;
  xe_zero_struct(&sampler_desc, sizeof(sampler_desc));
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
  sampler_desc.Filter =
      filter_matrix[info_.min_filter][info_.mag_filter][info_.mip_filter];
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
  sampler_desc.AddressU = mode_map[info_.clamp_u];
  sampler_desc.AddressV = mode_map[info_.clamp_v];
  sampler_desc.AddressW = mode_map[info_.clamp_w];
  sampler_desc.MipLODBias;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.BorderColor[0];
  sampler_desc.BorderColor[1];
  sampler_desc.BorderColor[2];
  sampler_desc.BorderColor[3];
  sampler_desc.MinLOD;
  sampler_desc.MaxLOD;

  HRESULT hr = resource_cache_->device()->CreateSamplerState(
      &sampler_desc, &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to create sampler state");
    return 1;
  }

  return 0;
}
