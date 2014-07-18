/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/texture_resource.h>

#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


bool TextureResource::Info::Prepare(const xe_gpu_texture_fetch_t& fetch,
                                    Info& info) {
  // http://msdn.microsoft.com/en-us/library/windows/desktop/cc308051(v=vs.85).aspx
  // a2xx_sq_surfaceformat

  info.dimension = (TextureDimension)fetch.dimension;
  switch (info.dimension) {
  case TEXTURE_DIMENSION_1D:
    info.width = fetch.size_1d.width;
    break;
  case TEXTURE_DIMENSION_2D:
    info.width = fetch.size_2d.width;
    info.height = fetch.size_2d.height;
    break;
  case TEXTURE_DIMENSION_3D:
  case TEXTURE_DIMENSION_CUBE:
    info.width = fetch.size_3d.width;
    info.height = fetch.size_3d.height;
    info.depth = fetch.size_3d.depth;
    break;
  }
  info.block_size = 0;
  info.texel_pitch = 0;
  info.endianness = (XE_GPU_ENDIAN)fetch.endianness;
  info.is_tiled = fetch.tiled;
  info.is_compressed = false;
  info.input_length = 0;
  info.format = DXGI_FORMAT_UNKNOWN;
  switch (fetch.format) {
  case FMT_8:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_RRR1:
      info.format = DXGI_FORMAT_R8_UNORM;
      break;
    case XE_GPU_SWIZZLE_000R:
      info.format = DXGI_FORMAT_A8_UNORM;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_8");
      info.format = DXGI_FORMAT_A8_UNORM;
      break;
    }
    info.block_size = 1;
    info.texel_pitch = 1;
    break;
  case FMT_1_5_5_5:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_BGRA:
      info.format = DXGI_FORMAT_B5G5R5A1_UNORM;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_1_5_5_5");
      info.format = DXGI_FORMAT_B5G5R5A1_UNORM;
      break;
    }
    info.block_size = 1;
    info.texel_pitch = 2;
    break;
  case FMT_8_8_8_8:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_RGBA:
      info.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    case XE_GPU_SWIZZLE_BGRA:
      info.format = DXGI_FORMAT_B8G8R8A8_UNORM;
      break;
    case XE_GPU_SWIZZLE_RGB1:
      info.format = DXGI_FORMAT_R8G8B8A8_UNORM; // ?
      break;
    case XE_GPU_SWIZZLE_BGR1:
      info.format = DXGI_FORMAT_B8G8R8X8_UNORM;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_8_8_8_8");
      info.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    }
    info.block_size = 1;
    info.texel_pitch = 4;
    break;
  case FMT_4_4_4_4:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_BGRA:
      info.format = DXGI_FORMAT_B4G4R4A4_UNORM; // only supported on Windows 8+
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_4_4_4_4");
      info.format = DXGI_FORMAT_B4G4R4A4_UNORM; // only supported on Windows 8+
      break;
    }
    info.block_size = 1;
    info.texel_pitch = 2;
    break;
  case FMT_16_16_16_16_FLOAT:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_RGBA:
      info.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_16_16_16_16_FLOAT");
      info.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      break;
    }
    info.block_size = 1;
    info.texel_pitch = 8;
    break;
  case FMT_32_FLOAT:
    switch (fetch.swizzle) {
    case XE_GPU_SWIZZLE_R111:
      info.format = DXGI_FORMAT_R32_FLOAT;
      break;
    default:
      XELOGW("D3D11: unhandled swizzle for FMT_32_FLOAT");
      info.format = DXGI_FORMAT_R32_FLOAT;
      break;
    }
    info.block_size = 1;
    info.texel_pitch = 4;
    break;
  case FMT_DXT1:
    info.format = DXGI_FORMAT_BC1_UNORM;
    info.block_size = 4;
    info.texel_pitch = 8;
    info.is_compressed = true;
    break;
  case FMT_DXT2_3:
  case FMT_DXT4_5:
    info.format = (fetch.format == FMT_DXT4_5 ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC2_UNORM);
    info.block_size = 4;
    info.texel_pitch = 16;
    info.is_compressed = true;
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
    info.format = DXGI_FORMAT_UNKNOWN;
    break;
  }

  if (info.format == DXGI_FORMAT_UNKNOWN) {
    return false;
  }

  // Must be called here when we know the format.
  switch (info.dimension) {
  case TEXTURE_DIMENSION_1D:
    info.CalculateTextureSizes1D(fetch);
    break;
  case TEXTURE_DIMENSION_2D:
    info.CalculateTextureSizes2D(fetch);
    break;
  case TEXTURE_DIMENSION_3D:
    // TODO(benvanik): calculate size.
    return false;
  case TEXTURE_DIMENSION_CUBE:
    // TODO(benvanik): calculate size.
    return false;
  }
  return true;
}

void TextureResource::Info::CalculateTextureSizes1D(
    const xe_gpu_texture_fetch_t& fetch) {
  // ?
  size_1d.width = fetch.size_1d.width;
}

void TextureResource::Info::CalculateTextureSizes2D(
    const xe_gpu_texture_fetch_t& fetch) {
  size_2d.logical_width = 1 + fetch.size_2d.width;
  size_2d.logical_height = 1 + fetch.size_2d.height;

  size_2d.block_width = size_2d.logical_width / block_size;
  size_2d.block_height = size_2d.logical_height / block_size;

  if (!is_compressed) {
    // must be 32x32 but also must have a pitch that is a multiple of 256 bytes
    uint32_t bytes_per_block = block_size * block_size * texel_pitch;
    uint32_t width_multiple = 32;
    if (bytes_per_block) {
      uint32_t minimum_multiple = 256 / bytes_per_block;
      if (width_multiple < minimum_multiple) {
        width_multiple = minimum_multiple;
      }
    }
    size_2d.input_width = XEROUNDUP(size_2d.logical_width, width_multiple);
    size_2d.input_height = XEROUNDUP(size_2d.logical_height, 32);
    size_2d.output_width = size_2d.logical_width;
    size_2d.output_height = size_2d.logical_height;
  } else {
    // must be 128x128
    size_2d.input_width = XEROUNDUP(size_2d.logical_width, 128);
    size_2d.input_height = XEROUNDUP(size_2d.logical_height, 128);
    size_2d.output_width = XENEXTPOW2(size_2d.logical_width);
    size_2d.output_height = XENEXTPOW2(size_2d.logical_height);
  }

  size_2d.logical_pitch = (size_2d.logical_width / block_size) * texel_pitch;
  size_2d.input_pitch = (size_2d.input_width / block_size) * texel_pitch;

  if (!is_tiled) {
    input_length = size_2d.block_height * size_2d.logical_pitch;
  } else {
    input_length = size_2d.block_height * size_2d.logical_pitch; // ?
  }
}

TextureResource::TextureResource(const MemoryRange& memory_range,
                                 const Info& info)
    : PagedResource(memory_range),
      info_(info) {
}

TextureResource::~TextureResource() {
}

int TextureResource::Prepare() {
  if (!handle()) {
    if (CreateHandle()) {
      XELOGE("Unable to create texture handle");
      return 1;
    }
  }

  if (!dirtied_) {
    return 0;
  }
  dirtied_ = false;

  // pass dirty regions?
  return InvalidateRegion(memory_range_);
}

void TextureResource::TextureSwap(uint8_t* dest, const uint8_t* src,
                                  uint32_t pitch) const {
  // TODO(benvanik): optimize swapping paths.
  switch (info_.endianness) {
    case XE_GPU_ENDIAN_8IN16:
      for (uint32_t i = 0; i < pitch; i += 2, src += 2, dest += 2) {
        *(uint16_t*)dest = poly::byte_swap(*(uint16_t*)src);
      }
      break;
    case XE_GPU_ENDIAN_8IN32: // Swap bytes.
      for (uint32_t i = 0; i < pitch; i += 4, src += 4, dest += 4) {
        *(uint32_t*)dest = poly::byte_swap(*(uint32_t*)src);
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
uint32_t TextureResource::TiledOffset2DOuter(uint32_t y, uint32_t width,
                                             uint32_t log_bpp) const {
  uint32_t macro = ((y >> 5) * (width >> 5)) << (log_bpp + 7);
  uint32_t micro = ((y & 6) << 2) << log_bpp;
  return macro +
         ((micro & ~15) << 1) +
         (micro & 15) +
         ((y & 8) << (3 + log_bpp)) +
         ((y & 1) << 4);
}

uint32_t TextureResource::TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                                             uint32_t base_offset) const {
  uint32_t macro = (x >> 5) << (bpp + 7);
  uint32_t micro = (x & 7) << bpp;
  uint32_t offset = base_offset + (macro + ((micro & ~15) << 1) + (micro & 15));
  return ((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) +
         ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}
