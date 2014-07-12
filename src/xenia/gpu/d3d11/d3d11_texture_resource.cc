/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_texture_resource.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_resource_cache.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11TextureResource::D3D11TextureResource(
    D3D11ResourceCache* resource_cache,
    const MemoryRange& memory_range,
    const Info& info)
    : TextureResource(memory_range, info),
      resource_cache_(resource_cache),
      texture_(nullptr),
      handle_(nullptr) {
}

D3D11TextureResource::~D3D11TextureResource() {
  XESAFERELEASE(texture_);
  XESAFERELEASE(handle_);
}

int D3D11TextureResource::CreateHandle() {
  SCOPE_profile_cpu_f("gpu");

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
  xe_zero_struct(&srv_desc, sizeof(srv_desc));
  // TODO(benvanik): this may need to be typed on the fetch instruction (float/int/etc?)
  srv_desc.Format = info_.format;

  D3D_SRV_DIMENSION dimension = D3D11_SRV_DIMENSION_UNKNOWN;
  switch (info_.dimension) {
  case TEXTURE_DIMENSION_1D:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
    srv_desc.Texture1D.MipLevels = 1;
    srv_desc.Texture1D.MostDetailedMip = 0;
    if (CreateHandle1D()) {
      XELOGE("D3D11: failed to create Texture1D");
      return 1;
    }
    break;
  case TEXTURE_DIMENSION_2D:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    if (CreateHandle2D()) {
      XELOGE("D3D11: failed to create Texture2D");
      return 1;
    }
    break;
  case TEXTURE_DIMENSION_3D:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srv_desc.Texture3D.MipLevels = 1;
    srv_desc.Texture3D.MostDetailedMip = 0;
    if (CreateHandle3D()) {
      XELOGE("D3D11: failed to create Texture3D");
      return 1;
    }
    break;
  case TEXTURE_DIMENSION_CUBE:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MipLevels = 1;
    srv_desc.TextureCube.MostDetailedMip = 0;
    if (CreateHandleCube()) {
      XELOGE("D3D11: failed to create TextureCube");
      return 1;
    }
    break;
  }

  HRESULT hr = resource_cache_->device()->CreateShaderResourceView(
      texture_, &srv_desc, &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to create texture resource view");
    return 1;
  }
  return 0;
}

int D3D11TextureResource::CreateHandle1D() {
  uint32_t width = 1 + info_.size_1d.width;

  D3D11_TEXTURE1D_DESC texture_desc;
  xe_zero_struct(&texture_desc, sizeof(texture_desc));
  texture_desc.Width          = width;
  texture_desc.MipLevels      = 1;
  texture_desc.ArraySize      = 1;
  texture_desc.Format         = info_.format;
  texture_desc.Usage          = D3D11_USAGE_DYNAMIC;
  texture_desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  texture_desc.MiscFlags      = 0; // D3D11_RESOURCE_MISC_GENERATE_MIPS?
  HRESULT hr = resource_cache_->device()->CreateTexture1D(
      &texture_desc, NULL, (ID3D11Texture1D**)&texture_);
  if (FAILED(hr)) {
    return 1;
  }
  return 0;
}

int D3D11TextureResource::CreateHandle2D() {
  D3D11_TEXTURE2D_DESC texture_desc;
  xe_zero_struct(&texture_desc, sizeof(texture_desc));
  texture_desc.Width              = info_.size_2d.output_width;
  texture_desc.Height             = info_.size_2d.output_height;
  texture_desc.MipLevels          = 1;
  texture_desc.ArraySize          = 1;
  texture_desc.Format             = info_.format;
  texture_desc.SampleDesc.Count   = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage              = D3D11_USAGE_DYNAMIC;
  texture_desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
  texture_desc.MiscFlags          = 0; // D3D11_RESOURCE_MISC_GENERATE_MIPS?
  HRESULT hr = resource_cache_->device()->CreateTexture2D(
      &texture_desc, NULL, (ID3D11Texture2D**)&texture_);
  if (FAILED(hr)) {
    return 1;
  }
  return 0;
}

int D3D11TextureResource::CreateHandle3D() {
  XELOGE("D3D11: CreateTexture3D not yet implemented");
  assert_always();
  return 1;
}

int D3D11TextureResource::CreateHandleCube() {
  XELOGE("D3D11: CreateTextureCube not yet implemented");
  assert_always();
  return 1;
}

int D3D11TextureResource::InvalidateRegion(const MemoryRange& memory_range) {
  SCOPE_profile_cpu_f("gpu");

  switch (info_.dimension) {
  case TEXTURE_DIMENSION_1D:
    return InvalidateRegion1D(memory_range);
  case TEXTURE_DIMENSION_2D:
    return InvalidateRegion2D(memory_range);
  case TEXTURE_DIMENSION_3D:
    return InvalidateRegion3D(memory_range);
  case TEXTURE_DIMENSION_CUBE:
    return InvalidateRegionCube(memory_range);
  }
  return 1;
}

int D3D11TextureResource::InvalidateRegion1D(const MemoryRange& memory_range) {
  return 1;
}

int D3D11TextureResource::InvalidateRegion2D(const MemoryRange& memory_range) {
  // TODO(benvanik): all mip levels.
  D3D11_MAPPED_SUBRESOURCE res;
  HRESULT hr = resource_cache_->context()->Map(
      texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to map texture");
    return 1;
  }

  const uint8_t* src = memory_range_.host_base;
  uint8_t* dest = (uint8_t*)res.pData;

  uint32_t output_pitch = res.RowPitch; // (output_width / info.block_size) * info.texel_pitch;
  if (!info_.is_tiled) {
    dest = (uint8_t*)res.pData;
    for (uint32_t y = 0; y < info_.size_2d.block_height; y++) {
      for (uint32_t x = 0; x < info_.size_2d.logical_pitch; x += info_.texel_pitch) {
        TextureSwap(dest + x, src + x, info_.texel_pitch);
      }
      src += info_.size_2d.input_pitch;
      dest += output_pitch;
    }
  } else {
    auto bpp = (info_.texel_pitch >> 2) + ((info_.texel_pitch >> 1) >> (info_.texel_pitch >> 2));
    for (uint32_t y = 0, output_base_offset = 0;
         y < info_.size_2d.block_height;
         y++, output_base_offset += output_pitch) {
      auto input_base_offset = TiledOffset2DOuter(y, (info_.size_2d.input_width / info_.block_size), bpp);
      for (uint32_t x = 0, output_offset = output_base_offset;
           x < info_.size_2d.block_width;
           x++, output_offset += info_.texel_pitch) {
        auto input_offset = TiledOffset2DInner(x, y, bpp, input_base_offset) >> bpp;
        TextureSwap(dest + output_offset,
                    src + input_offset * info_.texel_pitch,
                    info_.texel_pitch);
      }
    }
  }
  resource_cache_->context()->Unmap(texture_, 0);
  return 0;
}

int D3D11TextureResource::InvalidateRegion3D(const MemoryRange& memory_range) {
  return 1;
}

int D3D11TextureResource::InvalidateRegionCube(
    const MemoryRange& memory_range) {
  return 1;
}
