/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_texture.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_texture_cache.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11Texture::D3D11Texture(D3D11TextureCache* cache, uint32_t address,
                           const uint8_t* host_address)
    : Texture(address, host_address),
      cache_(cache) {
}

D3D11Texture::~D3D11Texture() {
}

TextureView* D3D11Texture::FetchNew(
    const xenos::xe_gpu_texture_fetch_t& fetch) {
  D3D11TextureView* view = new D3D11TextureView();
  if (!FillViewInfo(view, fetch)) {
    return nullptr;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
  xe_zero_struct(&srv_desc, sizeof(srv_desc));
  // TODO(benvanik): this may need to be typed on the fetch instruction (float/int/etc?)
  srv_desc.Format = view->format;

  D3D_SRV_DIMENSION dimension = D3D11_SRV_DIMENSION_UNKNOWN;
  switch (view->dimensions) {
  case DIMENSION_1D:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
    srv_desc.Texture1D.MipLevels = 1;
    srv_desc.Texture1D.MostDetailedMip = 0;
    if (!CreateTexture1D(view, fetch)) {
      XELOGE("D3D11: failed to fetch Texture1D");
      return nullptr;
    }
    break;
  case DIMENSION_2D:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    if (!CreateTexture2D(view, fetch)) {
      XELOGE("D3D11: failed to fetch Texture2D");
      return nullptr;
    }
    break;
  case DIMENSION_3D:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srv_desc.Texture3D.MipLevels = 1;
    srv_desc.Texture3D.MostDetailedMip = 0;
    if (!CreateTexture3D(view, fetch)) {
      XELOGE("D3D11: failed to fetch Texture3D");
      return nullptr;
    }
    break;
  case DIMENSION_CUBE:
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MipLevels = 1;
    srv_desc.TextureCube.MostDetailedMip = 0;
    if (!CreateTextureCube(view, fetch)) {
      XELOGE("D3D11: failed to fetch TextureCube");
      return nullptr;
    }
    break;
  }

  HRESULT hr = cache_->device()->CreateShaderResourceView(
      view->resource, &srv_desc, &view->srv);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to create texture resource view");
    return nullptr;
  }

  return view;
}

bool D3D11Texture::FetchDirty(
    TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) {
  auto d3d_view = static_cast<D3D11TextureView*>(view);
  switch (view->dimensions) {
  case DIMENSION_1D:
    return FetchTexture1D(d3d_view, fetch);
  case DIMENSION_2D:
    return FetchTexture2D(d3d_view, fetch);
  case DIMENSION_3D:
    return FetchTexture3D(d3d_view, fetch);
  case DIMENSION_CUBE:
    return FetchTextureCube(d3d_view, fetch);
  }
  return false;
}

bool D3D11Texture::CreateTexture1D(
    D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) {
  uint32_t width = 1 + fetch.size_1d.width;

  D3D11_TEXTURE1D_DESC texture_desc;
  xe_zero_struct(&texture_desc, sizeof(texture_desc));
  texture_desc.Width          = width;
  texture_desc.MipLevels      = 1;
  texture_desc.ArraySize      = 1;
  texture_desc.Format         = view->format;
  texture_desc.Usage          = D3D11_USAGE_DYNAMIC;
  texture_desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  texture_desc.MiscFlags      = 0; // D3D11_RESOURCE_MISC_GENERATE_MIPS?
  HRESULT hr = cache_->device()->CreateTexture1D(
      &texture_desc, NULL, (ID3D11Texture1D**)&view->resource);
  if (FAILED(hr)) {
    return false;
  }

  return FetchTexture1D(view, fetch);
}

bool D3D11Texture::FetchTexture1D(
    D3D11TextureView* view, const xe_gpu_texture_fetch_t& fetch) {
  SCOPE_profile_cpu_f("gpu");

  // TODO(benvanik): upload!
  XELOGE("D3D11: FetchTexture1D not yet implemented");
  return false;
}

bool D3D11Texture::CreateTexture2D(
    D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) {
  XEASSERTTRUE(fetch.dimension == 1);

  D3D11_TEXTURE2D_DESC texture_desc;
  xe_zero_struct(&texture_desc, sizeof(texture_desc));
  texture_desc.Width              = view->sizes_2d.output_width;
  texture_desc.Height             = view->sizes_2d.output_height;
  texture_desc.MipLevels          = 1;
  texture_desc.ArraySize          = 1;
  texture_desc.Format             = view->format;
  texture_desc.SampleDesc.Count   = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage              = D3D11_USAGE_DYNAMIC;
  texture_desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
  texture_desc.MiscFlags          = 0; // D3D11_RESOURCE_MISC_GENERATE_MIPS?
  HRESULT hr = cache_->device()->CreateTexture2D(
      &texture_desc, NULL, (ID3D11Texture2D**)&view->resource);
  if (FAILED(hr)) {
    return false;
  }

  return FetchTexture2D(view, fetch);
}

bool D3D11Texture::FetchTexture2D(
    D3D11TextureView* view, const xe_gpu_texture_fetch_t& fetch) {
  SCOPE_profile_cpu_f("gpu");

  XEASSERTTRUE(fetch.dimension == 1);

  auto sizes = GetTextureSizes2D(view);

  // TODO(benvanik): all mip levels.
  D3D11_MAPPED_SUBRESOURCE res;
  HRESULT hr = cache_->context()->Map(view->resource, 0,
                                      D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to map texture");
    return false;
  }

  const uint8_t* src = cache_->memory()->Translate(address_);
  uint8_t* dest = (uint8_t*)res.pData;

  //memset(dest, 0, output_pitch * (output_height / view->block_size)); // TODO(gibbed): remove me later

  uint32_t output_pitch = res.RowPitch; // (output_width / info.block_size) * info.texel_pitch;
  if (!fetch.tiled) {
    dest = (uint8_t*)res.pData;
    for (uint32_t y = 0; y < sizes.block_height; y++) {
      for (uint32_t x = 0; x < sizes.logical_pitch; x += view->texel_pitch) {
        TextureSwap(dest + x, src + x, view->texel_pitch, (XE_GPU_ENDIAN)fetch.endianness);
      }
      src += sizes.input_pitch;
      dest += output_pitch;
    }
  } else {
    auto bpp = (view->texel_pitch >> 2) + ((view->texel_pitch >> 1) >> (view->texel_pitch >> 2));
    for (uint32_t y = 0, output_base_offset = 0;
         y < sizes.block_height;
         y++, output_base_offset += output_pitch) {
      auto input_base_offset = TiledOffset2DOuter(y, (sizes.input_width / view->block_size), bpp);
      for (uint32_t x = 0, output_offset = output_base_offset;
           x < sizes.block_width;
           x++, output_offset += view->texel_pitch) {
        auto input_offset = TiledOffset2DInner(x, y, bpp, input_base_offset) >> bpp;
        TextureSwap(dest + output_offset,
                    src + input_offset * view->texel_pitch,
                    view->texel_pitch, (XE_GPU_ENDIAN)fetch.endianness);
      }
    }
  }
  cache_->context()->Unmap(view->resource, 0);
  return true;
}

bool D3D11Texture::CreateTexture3D(
    D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) {
  XELOGE("D3D11: CreateTexture3D not yet implemented");
  XEASSERTALWAYS();
  return false;
}

bool D3D11Texture::FetchTexture3D(
    D3D11TextureView* view, const xe_gpu_texture_fetch_t& fetch) {
  SCOPE_profile_cpu_f("gpu");

  XELOGE("D3D11: FetchTexture3D not yet implemented");
  XEASSERTALWAYS();
  return false;
  //D3D11_TEXTURE3D_DESC texture_desc;
  //xe_zero_struct(&texture_desc, sizeof(texture_desc));
  //texture_desc.Width;
  //texture_desc.Height;
  //texture_desc.Depth;
  //texture_desc.MipLevels;
  //texture_desc.Format;
  //texture_desc.Usage;
  //texture_desc.BindFlags;
  //texture_desc.CPUAccessFlags;
  //texture_desc.MiscFlags;
  //hr = device_->CreateTexture3D(
  //    &texture_desc, &initial_data, (ID3D11Texture3D**)&view->resource);
}

bool D3D11Texture::CreateTextureCube(
    D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) {
  XELOGE("D3D11: CreateTextureCube not yet implemented");
  XEASSERTALWAYS();
  return false;
}

bool D3D11Texture::FetchTextureCube(
    D3D11TextureView* view, const xe_gpu_texture_fetch_t& fetch) {
  SCOPE_profile_cpu_f("gpu");

  XELOGE("D3D11: FetchTextureCube not yet implemented");
  XEASSERTALWAYS();
  return false;
}
