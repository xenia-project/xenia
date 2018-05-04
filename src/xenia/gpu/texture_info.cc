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
#include "xenia/base/memory.h"

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
  info.mip_address = fetch.mip_address << 12;
  info.mip_levels = fetch.packed_mips ? fetch.mip_max_level + 1 : 1;
  info.input_length = 0;  // Populated below.

  if (info.format_info()->format == TextureFormat::kUnknown) {
    XELOGE("Attempting to fetch from unsupported texture format %d",
           info.texture_format);
    return false;
  }

  // Must be called here when we know the format.
  switch (info.dimension) {
    case Dimension::k1D: {
      info.CalculateTextureSizes1D(fetch.size_1d.width + 1);
    } break;
    case Dimension::k2D: {
      info.CalculateTextureSizes2D(fetch.size_2d.width + 1,
                                   fetch.size_2d.height + 1);
    } break;
    case Dimension::k3D: {
      info.CalculateTextureSizes3D(fetch.size_3d.width + 1,
                                   fetch.size_3d.height + 1,
                                   fetch.size_3d.depth + 1);
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
  info.mip_address = 0;
  info.mip_levels = 1;
  info.input_length = 0;

  if (info.format_info()->format == TextureFormat::kUnknown) {
    assert_true("Unsupported texture format");
    return false;
  }

  info.CalculateTextureSizes2D(width, height);
  return true;
}

void TextureInfo::CalculateTextureSizes1D(uint32_t width) {
  size_1d.logical_width = width;

  auto format = format_info();

  // width in blocks.
  uint32_t block_width =
      xe::round_up(size_1d.logical_width, format->block_width) /
      format->block_width;

  if (is_tiled) {
    // If the texture is tiled, its dimensions must be a multiple of tile
    // dimensions (32x32 blocks).
    size_1d.block_width = xe::round_up(block_width, 32);
  } else {
    size_1d.block_width = block_width;
  }

  uint32_t bytes_per_block = format->block_width * format->bits_per_pixel / 8;
  uint32_t byte_pitch = size_1d.block_width * bytes_per_block;

  uint32_t texel_width;
  if (!is_tiled) {
    // Each row must be a multiple of 256 in linear textures.
    byte_pitch = xe::round_up(byte_pitch, 256);
    texel_width = (byte_pitch / bytes_per_block) * format->block_width;
  } else {
    texel_width = size_2d.block_width * format->block_width;
  }

  size_1d.input_width = texel_width;
  size_1d.input_pitch = byte_pitch;

  input_length = size_1d.input_pitch;
}

void TextureInfo::CalculateTextureSizes2D(uint32_t width, uint32_t height) {
  size_2d.logical_width = width;
  size_2d.logical_height = height;

  auto format = format_info();

  // w/h in blocks.
  uint32_t block_width =
      xe::round_up(size_2d.logical_width, format->block_width) /
      format->block_width;
  uint32_t block_height =
      xe::round_up(size_2d.logical_height, format->block_height) /
      format->block_height;

  if (is_tiled) {
    // If the texture is tiled, its dimensions must be a multiple of tile
    // dimensions (32x32 blocks).
    size_2d.block_width = xe::round_up(block_width, 32);
    size_2d.block_height = xe::round_up(block_height, 32);
  } else {
    size_2d.block_width = block_width;
    size_2d.block_height = block_height;
  }

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

void TextureInfo::CalculateTextureSizes3D(uint32_t width, uint32_t height,
                                          uint32_t depth) {
  size_3d.logical_width = width;
  size_3d.logical_height = height;

  auto format = format_info();

  // w/h in blocks must be a multiple of block size.
  uint32_t block_width =
      xe::round_up(size_3d.logical_width, format->block_width) /
      format->block_width;
  uint32_t block_height =
      xe::round_up(size_3d.logical_height, format->block_height) /
      format->block_height;

  if (is_tiled) {
    // If the texture is tiled, its dimensions must be a multiple of tile
    // dimensions (32x32 blocks).
    size_3d.block_width = xe::round_up(block_width, 32);
    size_3d.block_height = xe::round_up(block_height, 32);
  } else {
    size_3d.block_width = block_width;
    size_3d.block_height = block_height;
  }

  uint32_t bytes_per_block =
      format->block_width * format->block_height * format->bits_per_pixel / 8;
  uint32_t byte_pitch = size_3d.block_width * bytes_per_block;

  uint32_t texel_width;
  if (!is_tiled) {
    // Each row must be a multiple of 256 in linear textures.
    byte_pitch = xe::round_up(byte_pitch, 256);
    texel_width = (byte_pitch / bytes_per_block) * format->block_width;
  } else {
    texel_width = size_3d.block_width * format->block_width;
  }

  size_3d.input_width = texel_width;
  size_3d.input_height = size_3d.block_height * format->block_height;
  size_3d.input_pitch = byte_pitch;

  size_3d.input_face_length = size_3d.input_pitch * size_3d.block_height;
  input_length = size_3d.input_face_length * depth;
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

  if (is_tiled) {
    // If the texture is tiled, its dimensions must be a multiple of tile
    // dimensions (32x32 blocks).
    size_cube.block_width = xe::round_up(block_width, 32);
    size_cube.block_height = xe::round_up(block_height, 32);
  } else {
    size_cube.block_width = block_width;
    size_cube.block_height = block_height;
  }

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

static void TextureSwap(Endian endianness, void* dest, const void* src,
                        size_t length) {
  switch (endianness) {
    case Endian::k8in16:
      xe::copy_and_swap_16_unaligned(dest, src, length / 2);
      break;
    case Endian::k8in32:
      xe::copy_and_swap_32_unaligned(dest, src, length / 4);
      break;
    case Endian::k16in32:  // Swap high and low 16 bits within a 32 bit word
      xe::copy_and_swap_16_in_32_unaligned(dest, src, length);
      break;
    default:
    case Endian::kUnspecified:
      std::memcpy(dest, src, length);
      break;
  }
}

static void ConvertTexelCTX1(uint8_t* dest, size_t dest_pitch,
                             const uint8_t* src, Endian src_endianness) {
  // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
  union {
    uint8_t data[8];
    struct {
      uint8_t r0, g0, r1, g1;
      uint32_t xx;
    };
  } block;
  static_assert(sizeof(block) == 8, "CTX1 block mismatch");

  const uint32_t bytes_per_block = 8;
  TextureSwap(src_endianness, block.data, src, bytes_per_block);

  uint8_t cr[4] = {
      block.r0, block.r1,
      static_cast<uint8_t>(2.f / 3.f * block.r0 + 1.f / 3.f * block.r1),
      static_cast<uint8_t>(1.f / 3.f * block.r0 + 2.f / 3.f * block.r1)};
  uint8_t cg[4] = {
      block.g0, block.g1,
      static_cast<uint8_t>(2.f / 3.f * block.g0 + 1.f / 3.f * block.g1),
      static_cast<uint8_t>(1.f / 3.f * block.g0 + 2.f / 3.f * block.g1)};

  for (uint32_t oy = 0; oy < 4; ++oy) {
    for (uint32_t ox = 0; ox < 4; ++ox) {
      uint8_t xx = (block.xx >> (((ox + (oy * 4)) * 2))) & 3;
      dest[(oy * dest_pitch) + (ox * 2) + 0] = cr[xx];
      dest[(oy * dest_pitch) + (ox * 2) + 1] = cg[xx];
    }
  }
}

void TextureInfo::ConvertTiled(uint8_t* dest, const uint8_t* src, Endian endian,
                               const FormatInfo* format_info, uint32_t offset_x,
                               uint32_t offset_y, uint32_t block_pitch,
                               uint32_t width, uint32_t height,
                               uint32_t output_width) {
  // TODO(benvanik): optimize this inner loop (or work by tiles).
  uint32_t bytes_per_block = format_info->block_width *
                             format_info->block_height *
                             format_info->bits_per_pixel / 8;

  uint32_t output_pitch =
      output_width * format_info->block_width * format_info->bits_per_pixel / 8;

  uint32_t output_row_height = 1;
  if (format_info->format == TextureFormat::k_CTX1) {
    // TODO: Can we calculate this?
    output_row_height = 4;
  }

  // logical w/h in blocks.
  uint32_t block_width =
      xe::round_up(width, format_info->block_width) / format_info->block_width;
  uint32_t block_height = xe::round_up(height, format_info->block_height) /
                          format_info->block_height;

  // Bytes per pixel
  auto log2_bpp =
      (bytes_per_block / 4) + ((bytes_per_block / 2) >> (bytes_per_block / 4));

  // Offset to the current row, in bytes.
  uint32_t output_row_offset = 0;
  for (uint32_t y = 0; y < block_height; y++) {
    auto input_row_offset =
        TextureInfo::TiledOffset2DOuter(offset_y + y, block_pitch, log2_bpp);

    // Go block-by-block on this row.
    uint32_t output_offset = output_row_offset;
    for (uint32_t x = 0; x < block_width; x++) {
      auto input_offset = TextureInfo::TiledOffset2DInner(
          offset_x + x, offset_y + y, log2_bpp, input_row_offset);
      input_offset >>= log2_bpp;

      if (format_info->format == TextureFormat::k_CTX1) {
        // Convert to R8G8.
        ConvertTexelCTX1(&dest[output_offset], output_pitch, src, endian);
      } else {
        // Generic swap to destination.
        TextureSwap(endian, dest + output_offset,
                    src + input_offset * bytes_per_block, bytes_per_block);
      }

      output_offset += bytes_per_block;
    }

    output_row_offset += output_pitch * output_row_height;
  }
}

uint32_t TextureInfo::GetMaxMipLevels(uint32_t width, uint32_t height,
                                      uint32_t depth) {
  return 1 + xe::log2_floor(std::max({width, height, depth}));
}

uint32_t TextureInfo::GetMipLocation(const TextureInfo& src, uint32_t mip,
                                     uint32_t* offset_x, uint32_t* offset_y) {
  if (mip == 0) {
    // Short-circuit. Mip 0 is always stored in guest_address.
    GetPackedTileOffset(src, offset_x, offset_y);
    return src.guest_address;
  }

  // If the texture is <= 16 pixels w/h, the mips are packed with the base
  // texture. Otherwise, they're stored beginning from mip_address.
  uint32_t address_base = std::min(src.width, src.height) < 16
                              ? src.guest_address
                              : src.mip_address;
  uint32_t address_offset = 0;

  // Walk forward to find the address of the mip.
  for (uint32_t i = 1; i < mip; i++) {
    uint32_t logical_width = std::max(xe::next_pow2(src.width + 1) >> i, 1u);
    uint32_t logical_height = std::max(xe::next_pow2(src.height + 1) >> i, 1u);
    if (std::min(logical_width, logical_height) <= 16) {
      // We've reached the point where the mips are packed into a single tile.
      break;
    }

    address_offset += GetMipSize(src, i);
  }

  // Now, check if the mip is packed at an offset.
  uint32_t logical_width = std::max(xe::next_pow2(src.width + 1) >> mip, 1u);
  uint32_t logical_height = std::max(xe::next_pow2(src.height + 1) >> mip, 1u);

  *offset_x = 0;
  *offset_y = 0;
  if (std::min(logical_width, logical_height) <= 16) {
    // Find the block offset of the mip.
    if (xe::log2_ceil(logical_width) > xe::log2_ceil(logical_height)) {
      // Wider than tall. Laid out vertically.
      *offset_y = logical_height & ~0x3;
      *offset_x = logical_height & 0x3 ? logical_width << 1 : 0;
    } else {
      // Taller than wide. Laid out horizontally.
      *offset_x = logical_width & ~0x3;
      *offset_y = logical_width & 0x3 ? logical_height << 1 : 0;
    }
  }

  *offset_x /= src.format_info()->block_width;
  *offset_y /= src.format_info()->block_height;
  return address_base + address_offset;
}

uint32_t TextureInfo::GetMipSize(const TextureInfo& src, uint32_t mip) {
  uint32_t bytes_per_block = src.format_info()->block_width *
                             src.format_info()->block_height *
                             src.format_info()->bits_per_pixel / 8;

  uint32_t logical_width = xe::next_pow2(src.width + 1) >> mip;
  uint32_t logical_height = xe::next_pow2(src.height + 1) >> mip;

  // w/h in blocks
  uint32_t block_width =
      xe::round_up(logical_width, src.format_info()->block_width) /
      src.format_info()->block_width;
  uint32_t block_height =
      xe::round_up(logical_height, src.format_info()->block_height) /
      src.format_info()->block_height;

  uint32_t size = block_width * block_height * bytes_per_block;

  // Minimum of one tile, which is 32x32 blocks.
  uint32_t tile_size = 32 * 32 * bytes_per_block;
  return std::max(size, tile_size) * (src.depth + 1);
}

uint32_t TextureInfo::GetMipLinearSize(const TextureInfo& src, uint32_t mip) {
  uint32_t bytes_per_block = src.format_info()->block_width *
                             src.format_info()->block_height *
                             src.format_info()->bits_per_pixel / 8;
  uint32_t size = src.input_length >> (mip * 2);

  // The size is a multiple of the block size.
  return xe::round_up(size, bytes_per_block);
}

bool TextureInfo::GetPackedTileOffset(uint32_t width, uint32_t height,
                                      const FormatInfo* format_info,
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
  //
  // The 2x2 and 1x1 squares are packed in their specific positions because
  // each square is the size of at least one block (which is 4x4 pixels max)
  // 4x4: x = width & ~0x3
  // 2x2: y = (width & 0x3) << 2
  // 1x1: y = (width & 0x3) << 2
  //
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

  if (std::min(width, height) > 16) {
    // Too big, not packed.
    *out_offset_x = 0;
    *out_offset_y = 0;
    return false;
  }

  if (xe::log2_ceil(width) > xe::log2_ceil(height)) {
    // Wider than tall. Laid out vertically.
    *out_offset_x = 0;
    *out_offset_y = 16;
  } else {
    // Taller than wide. Laid out horizontally.
    *out_offset_x = 16;
    *out_offset_y = 0;
  }

  *out_offset_x /= format_info->block_width;
  *out_offset_y /= format_info->block_height;
  return true;
}

bool TextureInfo::GetPackedTileOffset(const TextureInfo& texture_info,
                                      uint32_t* out_offset_x,
                                      uint32_t* out_offset_y) {
  return GetPackedTileOffset(
      texture_info.size_2d.logical_width, texture_info.size_2d.logical_height,
      texture_info.format_info(), out_offset_x, out_offset_y);
}

// https://github.com/BinomialLLC/crunch/blob/ea9b8d8c00c8329791256adafa8cf11e4e7942a2/inc/crn_decomp.h#L4108
uint32_t TextureInfo::TiledOffset2DOuter(uint32_t y, uint32_t width,
                                         uint32_t log2_bpp) {
  uint32_t macro = ((y / 32) * (width / 32)) << (log2_bpp + 7);
  uint32_t micro = ((y & 6) << 2) << log2_bpp;
  return macro + ((micro & ~0xF) << 1) + (micro & 0xF) +
         ((y & 8) << (3 + log2_bpp)) + ((y & 1) << 4);
}

uint32_t TextureInfo::TiledOffset2DInner(uint32_t x, uint32_t y,
                                         uint32_t log2_bpp,
                                         uint32_t base_offset) {
  uint32_t macro = (x / 32) << (log2_bpp + 7);
  uint32_t micro = (x & 7) << log2_bpp;
  uint32_t offset =
      base_offset + (macro + ((micro & ~0xF) << 1) + (micro & 0xF));
  return ((offset & ~0x1FF) << 3) + ((offset & 0x1C0) << 2) + (offset & 0x3F) +
         ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

uint64_t TextureInfo::hash() const {
  return XXH64(this, sizeof(TextureInfo), 0);
}

}  //  namespace gpu
}  //  namespace xe
