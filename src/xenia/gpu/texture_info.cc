/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/texture_info.h"

#include <algorithm>
#include <cstring>

#include "third_party/xxhash/xxhash.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

const FormatInfo* FormatInfo::Get(uint32_t gpu_format) {
  static const FormatInfo format_infos[64] = {
      {TextureFormat::k_1_REVERSE, FormatType::kUncompressed, 1, 1, 1},
      {TextureFormat::k_1, FormatType::kUncompressed, 1, 1, 1},
      {TextureFormat::k_8, FormatType::kUncompressed, 1, 1, 8},
      {TextureFormat::k_1_5_5_5, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_5_6_5, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_6_5_5, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_8_8_8_8, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_2_10_10_10, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_8_A, FormatType::kUncompressed, 1, 1, 8},
      {TextureFormat::k_8_B, FormatType::kUncompressed, 1, 1, 8},
      {TextureFormat::k_8_8, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_Cr_Y1_Cb_Y0, FormatType::kCompressed, 2, 1, 16},
      {TextureFormat::k_Y1_Cr_Y0_Cb, FormatType::kCompressed, 2, 1, 16},
      {TextureFormat::kUnknown, FormatType::kUncompressed, 0, 0},
      {TextureFormat::k_8_8_8_8_A, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_4_4_4_4, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_10_11_11, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_11_11_10, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_DXT1, FormatType::kCompressed, 4, 4, 4},
      {TextureFormat::k_DXT2_3, FormatType::kCompressed, 4, 4, 8},
      {TextureFormat::k_DXT4_5, FormatType::kCompressed, 4, 4, 8},
      {TextureFormat::kUnknown, FormatType::kUncompressed, 0, 0},
      {TextureFormat::k_24_8, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_24_8_FLOAT, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_16, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_16_16, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_16_16_16_16, FormatType::kUncompressed, 1, 1, 64},
      {TextureFormat::k_16_EXPAND, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_16_16_EXPAND, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_16_16_16_16_EXPAND, FormatType::kUncompressed, 1, 1,
       64},
      {TextureFormat::k_16_FLOAT, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_16_16_FLOAT, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_16_16_16_16_FLOAT, FormatType::kUncompressed, 1, 1, 64},
      {TextureFormat::k_32, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_32_32, FormatType::kUncompressed, 1, 1, 64},
      {TextureFormat::k_32_32_32_32, FormatType::kUncompressed, 1, 1, 128},
      {TextureFormat::k_32_FLOAT, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_32_32_FLOAT, FormatType::kUncompressed, 1, 1, 64},
      {TextureFormat::k_32_32_32_32_FLOAT, FormatType::kUncompressed, 1, 1,
       128},
      {TextureFormat::k_32_AS_8, FormatType::kCompressed, 4, 1, 8},
      {TextureFormat::k_32_AS_8_8, FormatType::kCompressed, 2, 1, 16},
      {TextureFormat::k_16_MPEG, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_16_16_MPEG, FormatType::kUncompressed, 1, 1, 32},
      {TextureFormat::k_8_INTERLACED, FormatType::kUncompressed, 1, 1, 8},
      {TextureFormat::k_32_AS_8_INTERLACED, FormatType::kCompressed, 4, 1, 8},
      {TextureFormat::k_32_AS_8_8_INTERLACED, FormatType::kCompressed, 1, 1,
       16},
      {TextureFormat::k_16_INTERLACED, FormatType::kUncompressed, 1, 1, 16},
      {TextureFormat::k_16_MPEG_INTERLACED, FormatType::kUncompressed, 1, 1,
       16},
      {TextureFormat::k_16_16_MPEG_INTERLACED, FormatType::kUncompressed, 1, 1,
       32},
      {TextureFormat::k_DXN, FormatType::kCompressed, 4, 4, 8},
      {TextureFormat::k_8_8_8_8_AS_16_16_16_16, FormatType::kUncompressed, 1, 1,
       32},
      {TextureFormat::k_DXT1_AS_16_16_16_16, FormatType::kCompressed, 4, 4, 4},
      {TextureFormat::k_DXT2_3_AS_16_16_16_16, FormatType::kCompressed, 4, 4,
       8},
      {TextureFormat::k_DXT4_5_AS_16_16_16_16, FormatType::kCompressed, 4, 4,
       8},
      {TextureFormat::k_2_10_10_10_AS_16_16_16_16, FormatType::kUncompressed, 1,
       1, 32},
      {TextureFormat::k_10_11_11_AS_16_16_16_16, FormatType::kUncompressed, 1,
       1, 32},
      {TextureFormat::k_11_11_10_AS_16_16_16_16, FormatType::kUncompressed, 1,
       1, 32},
      {TextureFormat::k_32_32_32_FLOAT, FormatType::kUncompressed, 1, 1, 96},
      {TextureFormat::k_DXT3A, FormatType::kCompressed, 4, 4, 4},
      {TextureFormat::k_DXT5A, FormatType::kCompressed, 4, 4, 4},
      {TextureFormat::k_CTX1, FormatType::kCompressed, 4, 4, 4},
      {TextureFormat::k_DXT3A_AS_1_1_1_1, FormatType::kCompressed, 4, 4, 4},
      {TextureFormat::kUnknown, FormatType::kUncompressed, 0, 0},
      {TextureFormat::kUnknown, FormatType::kUncompressed, 0, 0},
  };
  return &format_infos[gpu_format];
}

bool TextureInfo::Prepare(const xe_gpu_texture_fetch_t& fetch,
                          TextureInfo* out_info) {
  std::memset(out_info, 0, sizeof(TextureInfo));

  // http://msdn.microsoft.com/en-us/library/windows/desktop/cc308051(v=vs.85).aspx
  // a2xx_sq_surfaceformat
  auto& info = *out_info;
  info.guest_address = fetch.address << 12;

  info.dimension = static_cast<Dimension>(fetch.dimension);
  info.width = info.height = info.depth = 0;
  switch (info.dimension) {
    case Dimension::k1D:
      info.width = fetch.size_1d.width;
      break;
    case Dimension::k2D:
      info.width = fetch.size_2d.width;
      info.height = fetch.size_2d.height;
      break;
    case Dimension::k3D:
      info.width = fetch.size_3d.width;
      info.height = fetch.size_3d.height;
      info.depth = fetch.size_3d.depth;
      break;
    case Dimension::kCube:
      info.width = fetch.size_stack.width;
      info.height = fetch.size_stack.height;
      info.depth = fetch.size_stack.depth;
      break;
  }
  info.format_info = FormatInfo::Get(fetch.format);
  info.endianness = static_cast<Endian>(fetch.endianness);
  info.is_tiled = fetch.tiled;
  info.input_length = 0;  // Populated below.
  info.output_length = 0;

  if (info.format_info->format == TextureFormat::kUnknown) {
    assert_true("Unsupported texture format");
    return false;
  }

  // Must be called here when we know the format.
  info.input_length = 0;  // Populated below.
  info.output_length = 0;
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
      info.CalculateTextureSizesCube(fetch);
      break;
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

  // Here be dragons. The values here are used in texture_cache.cc to copy
  // images and create GL textures. Changes here will impact that code.
  // TODO(benvanik): generic texture copying utility.

  // w/h in blocks must be a multiple of block size.
  uint32_t block_width =
      xe::round_up(size_2d.logical_width, format_info->block_width) /
      format_info->block_width;
  uint32_t block_height =
      xe::round_up(size_2d.logical_height, format_info->block_height) /
      format_info->block_height;

  // Tiles are 32x32 blocks. All textures must be multiples of tile dimensions.
  uint32_t tile_width = uint32_t(std::ceil(block_width / 32.0f));
  uint32_t tile_height = uint32_t(std::ceil(block_height / 32.0f));
  size_2d.block_width = tile_width * 32;
  size_2d.block_height = tile_height * 32;

  uint32_t bytes_per_block = format_info->block_width *
                             format_info->block_height *
                             format_info->bits_per_pixel / 8;
  uint32_t byte_pitch = tile_width * 32 * bytes_per_block;
  if (!is_tiled) {
    // Each row must be a multiple of 256 in linear textures.
    byte_pitch = xe::round_up(byte_pitch, 256);
  }

  size_2d.input_width = tile_width * 32 * format_info->block_width;
  size_2d.input_height = tile_height * 32 * format_info->block_height;

  size_2d.output_width = block_width * format_info->block_width;
  size_2d.output_height = block_height * format_info->block_height;

  size_2d.input_pitch = byte_pitch;
  size_2d.output_pitch = block_width * bytes_per_block;

  input_length = size_2d.input_pitch * size_2d.block_height;
  output_length = size_2d.output_pitch * block_height;
}

void TextureInfo::CalculateTextureSizesCube(
    const xe_gpu_texture_fetch_t& fetch) {
  assert_true(fetch.size_stack.depth + 1 == 6);
  size_cube.logical_width = 1 + fetch.size_stack.width;
  size_cube.logical_height = 1 + fetch.size_stack.height;

  // w/h in blocks must be a multiple of block size.
  uint32_t block_width =
      xe::round_up(size_cube.logical_width, format_info->block_width) /
      format_info->block_width;
  uint32_t block_height =
      xe::round_up(size_cube.logical_height, format_info->block_height) /
      format_info->block_height;

  // Tiles are 32x32 blocks. All textures must be multiples of tile dimensions.
  uint32_t tile_width = uint32_t(std::ceil(block_width / 32.0f));
  uint32_t tile_height = uint32_t(std::ceil(block_height / 32.0f));
  size_cube.block_width = tile_width * 32;
  size_cube.block_height = tile_height * 32;

  uint32_t bytes_per_block = format_info->block_width *
                             format_info->block_height *
                             format_info->bits_per_pixel / 8;
  uint32_t byte_pitch = tile_width * 32 * bytes_per_block;
  if (!is_tiled) {
    // Each row must be a multiple of 256 in linear textures.
    byte_pitch = xe::round_up(byte_pitch, 256);
  }

  size_cube.input_width = tile_width * 32 * format_info->block_width;
  size_cube.input_height = tile_height * 32 * format_info->block_height;

  size_cube.output_width = block_width * format_info->block_width;
  size_cube.output_height = block_height * format_info->block_height;

  size_cube.input_pitch = byte_pitch;
  size_cube.output_pitch = block_width * bytes_per_block;

  size_cube.input_face_length = size_cube.input_pitch * size_cube.block_height;
  input_length = size_cube.input_face_length * 6;
  size_cube.output_face_length = size_cube.output_pitch * block_height;
  output_length = size_cube.output_face_length * 6;
}

void TextureInfo::GetPackedTileOffset(const TextureInfo& texture_info,
                                      uint32_t* out_offset_x,
                                      uint32_t* out_offset_y) {
  // Tile size is 32x32, and once textures go <=16 they are packed into a
  // single tile together. The math here is insane. Most sourced
  // from graph paper and looking at dds dumps.
  //   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  // 0         +.4x4.+ +.....8x8.....+ +............16x16............+
  // 1         +.4x4.+ +.....8x8.....+ +............16x16............+
  // 2         +.4x4.+ +.....8x8.....+ +............16x16............+
  // 3         +.4x4.+ +.....8x8.....+ +............16x16............+
  // 4 x               +.....8x8.....+ +............16x16............+
  // 5                 +.....8x8.....+ +............16x16............+
  // 6                 +.....8x8.....+ +............16x16............+
  // 7                 +.....8x8.....+ +............16x16............+
  // 8 2x2                             +............16x16............+
  // 9 2x2                             +............16x16............+
  // 0                                 +............16x16............+
  // ...                                            .....
  // This only works for square textures, or textures that are some non-pot
  // <= square. As soon as the aspect ratio goes weird, the textures start to
  // stretch across tiles.
  // if (tile_aligned(w) > tile_aligned(h)) {
  //   // wider than tall, so packed horizontally
  // } else if (tile_aligned(w) < tile_aligned(h)) {
  //   // taller than wide, so packed vertically
  // } else {
  //   square
  // }
  // It's important to use logical sizes here, as the input sizes will be
  // for the entire packed tile set, not the actual texture.
  // The minimum dimension is what matters most: if either width or height
  // is <= 16 this mode kicks in.

  if (std::min(texture_info.size_2d.logical_width,
               texture_info.size_2d.logical_height) > 16) {
    // Too big, not packed.
    *out_offset_x = 0;
    *out_offset_y = 0;
    return;
  }

  if (xe::log2_ceil(texture_info.size_2d.logical_width) >
      xe::log2_ceil(texture_info.size_2d.logical_height)) {
    // Wider than tall. Laid out vertically.
    *out_offset_x = 0;
    *out_offset_y = 16;
  } else {
    // Taller than wide. Laid out horizontally.
    *out_offset_x = 16;
    *out_offset_y = 0;
  }
  *out_offset_x /= texture_info.format_info->block_width;
  *out_offset_y /= texture_info.format_info->block_height;
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

uint64_t TextureInfo::hash() const {
  return XXH64(this, sizeof(TextureInfo), 0);
}

}  //  namespace gpu
}  //  namespace xe
