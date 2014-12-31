/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/texture_info.h>

#include <poly/math.h>

namespace xe {
namespace gpu {

using namespace xe::gpu::ucode;
using namespace xe::gpu::xenos;

bool TextureInfo::Prepare(const xe_gpu_texture_fetch_t& fetch,
                          TextureInfo* out_info) {
  // http://msdn.microsoft.com/en-us/library/windows/desktop/cc308051(v=vs.85).aspx
  // a2xx_sq_surfaceformat

  auto& info = *out_info;
  info.swizzle = fetch.swizzle;

  info.dimension = static_cast<Dimension>(fetch.dimension);
  switch (info.dimension) {
    case Dimension::k1D:
      info.width = fetch.size_1d.width;
      break;
    case Dimension::k2D:
      info.width = fetch.size_2d.width;
      info.height = fetch.size_2d.height;
      break;
    case Dimension::k3D:
    case Dimension::kCube:
      info.width = fetch.size_3d.width;
      info.height = fetch.size_3d.height;
      info.depth = fetch.size_3d.depth;
      break;
  }
  info.endianness = static_cast<Endian>(fetch.endianness);

  info.block_size = 0;
  info.texel_pitch = 0;
  info.is_tiled = fetch.tiled;
  info.is_compressed = false;
  info.input_length = 0;
  info.format = static_cast<TextureFormat>(fetch.format);
  switch (fetch.format) {
    case FMT_8:
      info.block_size = 1;
      info.texel_pitch = 1;
      break;
    case FMT_1_5_5_5:
      info.block_size = 1;
      info.texel_pitch = 2;
      break;
    case FMT_8_8_8_8:
    case FMT_8_8_8_8_AS_16_16_16_16:
      info.block_size = 1;
      info.texel_pitch = 4;
      break;
    case FMT_4_4_4_4:
      info.block_size = 1;
      info.texel_pitch = 2;
      break;
    case FMT_16_16_16_16_FLOAT:
      info.block_size = 1;
      info.texel_pitch = 8;
      break;
    case FMT_32_FLOAT:
      info.block_size = 1;
      info.texel_pitch = 4;
      break;
    case FMT_DXT1:
      info.block_size = 4;
      info.texel_pitch = 8;
      info.is_compressed = true;
      break;
    case FMT_DXT2_3:
    case FMT_DXT4_5:
      info.block_size = 4;
      info.texel_pitch = 16;
      info.is_compressed = true;
      break;
    case FMT_DXN:
      // BC5
      info.block_size = 4;
      info.texel_pitch = 16;
      info.is_compressed = true;
      break;
    case FMT_DXT1_AS_16_16_16_16:
      // TODO(benvanik): conversion?
      info.block_size = 4;
      info.texel_pitch = 8;
      info.is_compressed = true;
      break;
    case FMT_DXT2_3_AS_16_16_16_16:
    case FMT_DXT4_5_AS_16_16_16_16:
      // TODO(benvanik): conversion?
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
    case FMT_2_10_10_10_AS_16_16_16_16:
    case FMT_10_11_11_AS_16_16_16_16:
    case FMT_11_11_10_AS_16_16_16_16:
    case FMT_32_32_32_FLOAT:
    case FMT_DXT3A:
    case FMT_DXT5A:
    case FMT_CTX1:
    case FMT_DXT3A_AS_1_1_1_1:
      PLOGE("Unhandled texture format");
      return false;
    default:
      assert_unhandled_case(fetch.format);
      return false;
  }

  // Must be called here when we know the format.
  switch (info.dimension) {
    case Dimension::k1D:
      info.CalculateTextureSizes1D(fetch);
      break;
    case Dimension::k2D:
      info.CalculateTextureSizes2D(fetch);
      break;
    case Dimension::k3D:
      // TODO(benvanik): calculate size.
      return false;
    case Dimension::kCube:
      // TODO(benvanik): calculate size.
      return false;
  }

  return true;
}

void TextureInfo::CalculateTextureSizes1D(const xe_gpu_texture_fetch_t& fetch) {
  // ?
  size_1d.width = fetch.size_1d.width;
}

void TextureInfo::CalculateTextureSizes2D(const xe_gpu_texture_fetch_t& fetch) {
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
    size_2d.input_width = poly::round_up(size_2d.logical_width, width_multiple);
    size_2d.input_height = poly::round_up(size_2d.logical_height, 32);
    size_2d.output_width = size_2d.logical_width;
    size_2d.output_height = size_2d.logical_height;
  } else {
    // must be 128x128
    size_2d.input_width = poly::round_up(size_2d.logical_width, 128);
    size_2d.input_height = poly::round_up(size_2d.logical_height, 128);
    size_2d.output_width = poly::next_pow2(size_2d.logical_width);
    size_2d.output_height = poly::next_pow2(size_2d.logical_height);
  }

  size_2d.logical_pitch = (size_2d.logical_width / block_size) * texel_pitch;
  size_2d.input_pitch = (size_2d.input_width / block_size) * texel_pitch;

  if (!is_tiled) {
    input_length = size_2d.block_height * size_2d.logical_pitch;
  } else {
    input_length = size_2d.block_height * size_2d.logical_pitch;  // ?
  }
}

// https://code.google.com/p/crunch/source/browse/trunk/inc/crn_decomp.h#4104
uint32_t TextureInfo::TiledOffset2DOuter(uint32_t y, uint32_t width,
                                         uint32_t log_bpp) {
  uint32_t macro = ((y >> 5) * (width >> 5)) << (log_bpp + 7);
  uint32_t micro = ((y & 6) << 2) << log_bpp;
  return macro + ((micro & ~15) << 1) + (micro & 15) +
         ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
}

uint32_t TextureInfo::TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                                         uint32_t base_offset) {
  uint32_t macro = (x >> 5) << (bpp + 7);
  uint32_t micro = (x & 7) << bpp;
  uint32_t offset = base_offset + (macro + ((micro & ~15) << 1) + (micro & 15));
  return ((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) +
         ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

}  //  namespace gpu
}  //  namespace xe
