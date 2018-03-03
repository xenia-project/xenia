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
#include <cmath>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"

#include "third_party/xxhash/xxhash.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

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
      info.dimension = Dimension::k2D;
      info.width = fetch.size_1d.width;
      info.height = 1;
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
  info.texture_format = static_cast<TextureFormat>(fetch.format);
  info.endianness = static_cast<Endian>(fetch.endianness);
  info.is_tiled = fetch.tiled;
  info.has_packed_mips = fetch.packed_mips;
  info.input_length = 0;  // Populated below.

  if (info.format_info()->format == TextureFormat::kUnknown) {
    XELOGE("Attempting to fetch from unsupported texture format %d",
           info.texture_format);
    return false;
  }

  // Must be called here when we know the format.
  switch (info.dimension) {
    case Dimension::k1D: {
      assert_always();
    } break;
    case Dimension::k2D: {
      info.CalculateTextureSizes2D(fetch.size_2d.width + 1,
                                   fetch.size_2d.height + 1);
    } break;
    case Dimension::k3D: {
      // TODO(benvanik): calculate size.
      return false;
    }
    case Dimension::kCube: {
      info.CalculateTextureSizesCube(fetch.size_stack.width + 1,
                                     fetch.size_stack.height + 1,
                                     fetch.size_stack.depth + 1);
    } break;
  }

  return true;
}

bool TextureInfo::PrepareResolve(uint32_t physical_address,
                                 TextureFormat texture_format, Endian endian,
                                 uint32_t width, uint32_t height,
                                 TextureInfo* out_info) {
  std::memset(out_info, 0, sizeof(TextureInfo));
  auto& info = *out_info;
  info.guest_address = physical_address;
  info.dimension = Dimension::k2D;
  assert_true(width > 0);
  assert_true(height > 0);
  info.width = width - 1;
  info.height = height - 1;
  info.texture_format = texture_format;
  info.endianness = endian;
  info.is_tiled = true;
  info.has_packed_mips = false;
  info.input_length = 0;

  if (info.format_info()->format == TextureFormat::kUnknown) {
    assert_true("Unsupported texture format");
    return false;
  }

  info.CalculateTextureSizes2D(width, height);
  return true;
}

void TextureInfo::CalculateTextureSizes2D(uint32_t width, uint32_t height) {
  size_2d.logical_width = width;
  size_2d.logical_height = height;

  // Here be dragons. The values here are used in texture_cache.cc to copy
  // images and create GL textures. Changes here will impact that code.
  // TODO(benvanik): generic texture copying utility.

  auto format = format_info();

  // w/h in blocks.
  uint32_t block_width =
      xe::round_up(size_2d.logical_width, format->block_width) /
      format->block_width;
  uint32_t block_height =
      xe::round_up(size_2d.logical_height, format->block_height) /
      format->block_height;

  // Tiles are 32x32 blocks. The pitch of all textures must a multiple of tile
  // dimensions.
  uint32_t tile_width = xe::round_up(block_width, 32) / 32;
  size_2d.block_width = tile_width * 32;
  size_2d.block_height = block_height;

  uint32_t bytes_per_block =
      format->block_width * format->block_height * format->bits_per_pixel / 8;
  uint32_t byte_pitch = size_2d.block_width * bytes_per_block;

  uint32_t texel_width;
  if (!is_tiled) {
    // Each row must be a multiple of 256 in linear textures.
    byte_pitch = xe::round_up(byte_pitch, 256);
    texel_width = (byte_pitch / bytes_per_block) * format->block_width;
  } else {
    texel_width = size_2d.block_width * format->block_width;
  }

  size_2d.input_width = texel_width;
  size_2d.input_height = size_2d.block_height * format->block_height;
  size_2d.input_pitch = byte_pitch;

  input_length = size_2d.input_pitch * size_2d.block_height;
}

void TextureInfo::CalculateTextureSizesCube(uint32_t width, uint32_t height,
                                            uint32_t depth) {
  assert_true(depth == 6);
  size_cube.logical_width = width;
  size_cube.logical_height = height;

  auto format = format_info();

  // w/h in blocks must be a multiple of block size.
  uint32_t block_width =
      xe::round_up(size_cube.logical_width, format->block_width) /
      format->block_width;
  uint32_t block_height =
      xe::round_up(size_cube.logical_height, format->block_height) /
      format->block_height;

  // Tiles are 32x32 blocks. All textures must be multiples of tile dimensions.
  uint32_t tile_width = xe::round_up(block_width, 32) / 32;
  uint32_t tile_height = xe::round_up(block_height, 32) / 32;
  size_cube.block_width = tile_width * 32;
  size_cube.block_height = tile_height * 32;

  uint32_t bytes_per_block =
      format->block_width * format->block_height * format->bits_per_pixel / 8;
  uint32_t byte_pitch = size_cube.block_width * bytes_per_block;

  uint32_t texel_width;
  if (!is_tiled) {
    // Each row must be a multiple of 256 in linear textures.
    byte_pitch = xe::round_up(byte_pitch, 256);
    texel_width = (byte_pitch / bytes_per_block) * format->block_width;
  } else {
    texel_width = size_cube.block_width * format->block_width;
  }

  size_cube.input_width = texel_width;
  size_cube.input_height = size_cube.block_height * format->block_height;
  size_cube.input_pitch = byte_pitch;

  size_cube.input_face_length = size_cube.input_pitch * size_cube.block_height;
  input_length = size_cube.input_face_length * 6;
}

bool TextureInfo::GetPackedTileOffset(const TextureInfo& texture_info,
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
    return false;
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
  *out_offset_x /= texture_info.format_info()->block_width;
  *out_offset_y /= texture_info.format_info()->block_height;
  return true;
}

// https://github.com/BinomialLLC/crunch/blob/ea9b8d8c00c8329791256adafa8cf11e4e7942a2/inc/crn_decomp.h#L4108
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
