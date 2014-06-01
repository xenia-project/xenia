/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/texture.h>

#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>

// TODO(benvanik): replace DXGI constants with xenia constants.
#include <d3d11.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


Texture::Texture(uint32_t address, const uint8_t* host_address)
    : address_(address), host_address_(host_address) {
}

Texture::~Texture() {
  for (auto it = views_.begin(); it != views_.end(); ++it) {
    auto view = *it;
    delete view;
  }
  views_.clear();
}

TextureView* Texture::Fetch(
    const xenos::xe_gpu_texture_fetch_t& fetch) {
  // TODO(benvanik): compute length for hash check.
  size_t length = 0;
  switch (fetch.dimension) {
  case DIMENSION_1D:
    break;
  case DIMENSION_2D:
    break;
  case DIMENSION_3D:
    break;
  case DIMENSION_CUBE:
    break;
  }
  uint64_t hash = xe_hash64(host_address_, length);

  for (auto it = views_.begin(); it != views_.end(); ++it) {
    auto view = *it;
    if (memcmp(&view->fetch, &fetch, sizeof(fetch))) {
      continue;
    }
    bool dirty = hash != view->hash;
    if (dirty) {
      return FetchDirty(view, fetch) ? view : nullptr;
    } else {
      return view;
    }
  }

  auto new_view = FetchNew(fetch);
  if (!new_view) {
    return nullptr;
  }
  new_view->hash = hash;
  views_.push_back(new_view);
  return new_view;
}

bool Texture::FillViewInfo(TextureView* view,
                           const xenos::xe_gpu_texture_fetch_t& fetch) {
  // http://msdn.microsoft.com/en-us/library/windows/desktop/cc308051(v=vs.85).aspx
  // a2xx_sq_surfaceformat

  view->texture = this;
  view->fetch = fetch;

  view->dimensions = fetch.dimension;
  switch (fetch.dimension) {
  case DIMENSION_1D:
    view->width = fetch.size_1d.width;
    break;
  case DIMENSION_2D:
    view->width = fetch.size_2d.width;
    view->height = fetch.size_2d.height;
    break;
  case DIMENSION_3D:
    view->width = fetch.size_3d.width;
    view->height = fetch.size_3d.height;
    view->depth = fetch.size_3d.depth;
    break;
  case DIMENSION_CUBE:
    view->width = fetch.size_stack.width;
    view->height = fetch.size_stack.height;
    view->depth = fetch.size_stack.depth;
    break;
  }
  view->format = DXGI_FORMAT_UNKNOWN;
  view->block_size = 0;
  view->texel_pitch = 0;
  view->is_compressed = false;
  switch (fetch.format) {
  case FMT_8:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_RRR1:
      view->format = DXGI_FORMAT_R8_UNORM;
      break;
    case XE_GPU_SWIZZLE_000R:
      view->format = DXGI_FORMAT_A8_UNORM;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_8");
      view->format = DXGI_FORMAT_A8_UNORM;
      break;
    }
    view->block_size = 1;
    view->texel_pitch = 1;
    break;
  case FMT_1_5_5_5:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_BGRA:
      view->format = DXGI_FORMAT_B5G5R5A1_UNORM;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_1_5_5_5");
      view->format = DXGI_FORMAT_B5G5R5A1_UNORM;
      break;
    }
    view->block_size = 1;
    view->texel_pitch = 2;
    break;
  case FMT_8_8_8_8:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_RGBA:
      view->format = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    case XE_GPU_SWIZZLE_BGRA:
      view->format = DXGI_FORMAT_B8G8R8A8_UNORM;
      break;
    case XE_GPU_SWIZZLE_RGB1:
      view->format = DXGI_FORMAT_R8G8B8A8_UNORM; // ?
      break;
    case XE_GPU_SWIZZLE_BGR1:
      view->format = DXGI_FORMAT_B8G8R8X8_UNORM;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_8_8_8_8");
      view->format = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    }
    view->block_size = 1;
    view->texel_pitch = 4;
    break;
  case FMT_4_4_4_4:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_BGRA:
      view->format = DXGI_FORMAT_B4G4R4A4_UNORM; // only supported on Windows 8+
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_4_4_4_4");
      view->format = DXGI_FORMAT_B4G4R4A4_UNORM; // only supported on Windows 8+
      break;
    }
    view->block_size = 1;
    view->texel_pitch = 2;
    break;
  case FMT_16_16_16_16_FLOAT:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_RGBA:
      view->format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_16_16_16_16_FLOAT");
      view->format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      break;
    }
    view->block_size = 1;
    view->texel_pitch = 8;
    break;
  case FMT_32_FLOAT:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_R111:
      view->format = DXGI_FORMAT_R32_FLOAT;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_32_FLOAT");
      view->format = DXGI_FORMAT_R32_FLOAT;
      break;
    }
    view->block_size = 1;
    view->texel_pitch = 4;
    break;
  case FMT_DXT1:
    view->format = DXGI_FORMAT_BC1_UNORM;
    view->block_size = 4;
    view->texel_pitch = 8;
    view->is_compressed = true;
    break;
  case FMT_DXT2_3:
  case FMT_DXT4_5:
    view->format = (fetch.format == FMT_DXT4_5 ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC2_UNORM);
    view->block_size = 4;
    view->texel_pitch = 16;
    view->is_compressed = true;
    break;
  case FMT_1_REVERSE:
  case FMT_1:
  case FMT_5_6_5:
  case FMT_6_5_5:
  case FMT_2_10_10_10:
  case FMT_8_A:
  case FMT_8_B:
  case FMT_8_8:
  case FMT_Cr_Y1_Cb_Y0:
  case FMT_Y1_Cr_Y0_Cb:
  case FMT_5_5_5_1:
  case FMT_8_8_8_8_A:
  case FMT_10_11_11:
  case FMT_11_11_10:
  case FMT_24_8:
  case FMT_24_8_FLOAT:
  case FMT_16:
  case FMT_16_16:
  case FMT_16_16_16_16:
  case FMT_16_EXPAND:
  case FMT_16_16_EXPAND:
  case FMT_16_16_16_16_EXPAND:
  case FMT_16_FLOAT:
  case FMT_16_16_FLOAT:
  case FMT_32:
  case FMT_32_32:
  case FMT_32_32_32_32:
  case FMT_32_32_FLOAT:
  case FMT_32_32_32_32_FLOAT:
  case FMT_32_AS_8:
  case FMT_32_AS_8_8:
  case FMT_16_MPEG:
  case FMT_16_16_MPEG:
  case FMT_8_INTERLACED:
  case FMT_32_AS_8_INTERLACED:
  case FMT_32_AS_8_8_INTERLACED:
  case FMT_16_INTERLACED:
  case FMT_16_MPEG_INTERLACED:
  case FMT_16_16_MPEG_INTERLACED:
  case FMT_DXN:
  case FMT_8_8_8_8_AS_16_16_16_16:
  case FMT_DXT1_AS_16_16_16_16:
  case FMT_DXT2_3_AS_16_16_16_16:
  case FMT_DXT4_5_AS_16_16_16_16:
  case FMT_2_10_10_10_AS_16_16_16_16:
  case FMT_10_11_11_AS_16_16_16_16:
  case FMT_11_11_10_AS_16_16_16_16:
  case FMT_32_32_32_FLOAT:
  case FMT_DXT3A:
  case FMT_DXT5A:
  case FMT_CTX1:
  case FMT_DXT3A_AS_1_1_1_1:
    view->format = DXGI_FORMAT_UNKNOWN;
    break;
  }

  if (view->format == DXGI_FORMAT_UNKNOWN) {
    return false;
  }

  switch (fetch.dimension) {
  case DIMENSION_1D:
    break;
  case DIMENSION_2D:
    view->sizes_2d = GetTextureSizes2D(view);
    break;
  case DIMENSION_3D:
    break;
  case DIMENSION_CUBE:
    break;
  }
  return true;
}

const TextureSizes2D Texture::GetTextureSizes2D(TextureView* view) {
  TextureSizes2D sizes;

  sizes.logical_width = 1 + view->fetch.size_2d.width;
  sizes.logical_height = 1 + view->fetch.size_2d.height;

  sizes.block_width = sizes.logical_width / view->block_size;
  sizes.block_height = sizes.logical_height / view->block_size;

  if (!view->is_compressed) {
    // must be 32x32, but also must have a pitch that is a multiple of 256 bytes
    uint32_t bytes_per_block = view->block_size * view->block_size *
                               view->texel_pitch;
    uint32_t width_multiple = 32;
    if (bytes_per_block) {
      uint32_t minimum_multiple = 256 / bytes_per_block;
      if (width_multiple < minimum_multiple) {
        width_multiple = minimum_multiple;
      }
    }
    sizes.input_width = XEROUNDUP(sizes.logical_width, width_multiple);
    sizes.input_height = XEROUNDUP(sizes.logical_height, 32);
    sizes.output_width = sizes.logical_width;
    sizes.output_height = sizes.logical_height;
  } else {
    // must be 128x128
    sizes.input_width = XEROUNDUP(sizes.logical_width, 128);
    sizes.input_height = XEROUNDUP(sizes.logical_height, 128);
    sizes.output_width = XENEXTPOW2(sizes.logical_width);
    sizes.output_height = XENEXTPOW2(sizes.logical_height);
  }

  sizes.logical_pitch =
      (sizes.logical_width / view->block_size) * view->texel_pitch;
  sizes.input_pitch =
      (sizes.input_width / view->block_size) * view->texel_pitch;

  return sizes;
}

void Texture::TextureSwap(uint8_t* dest, const uint8_t* src, uint32_t pitch,
                          XE_GPU_ENDIAN endianness) {
  switch (endianness) {
    case XE_GPU_ENDIAN_8IN16:
      for (uint32_t i = 0; i < pitch; i += 2, src += 2, dest += 2) {
        *(uint16_t*)dest = XESWAP16(*(uint16_t*)src);
      }
      break;
    case XE_GPU_ENDIAN_8IN32: // Swap bytes.
      for (uint32_t i = 0; i < pitch; i += 4, src += 4, dest += 4) {
        *(uint32_t*)dest = XESWAP32(*(uint32_t*)src);
      }
      break;
    case XE_GPU_ENDIAN_16IN32: // Swap half words.
      for (uint32_t i = 0; i < pitch; i += 4, src += 4, dest += 4) {
        uint32_t value = *(uint32_t*)src;
        *(uint32_t*)dest = ((value >> 16) & 0xFFFF) | (value << 16);
      }
      break;
    default:
    case XE_GPU_ENDIAN_NONE:
      memcpy(dest, src, pitch);
      break;
  }
}

// https://code.google.com/p/crunch/source/browse/trunk/inc/crn_decomp.h#4104
uint32_t Texture::TiledOffset2DOuter(uint32_t y, uint32_t width,
                                     uint32_t log_bpp) {
  uint32_t macro = ((y >> 5) * (width >> 5)) << (log_bpp + 7);
  uint32_t micro = ((y & 6) << 2) << log_bpp;
  return macro +
         ((micro & ~15) << 1) +
         (micro & 15) +
         ((y & 8) << (3 + log_bpp)) +
         ((y & 1) << 4);
}

uint32_t Texture::TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                                     uint32_t base_offset) {
  uint32_t macro = (x >> 5) << (bpp + 7);
  uint32_t micro = (x & 7) << bpp;
  uint32_t offset = base_offset + (macro + ((micro & ~15) << 1) + (micro & 15));
  return ((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) +
         ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}
