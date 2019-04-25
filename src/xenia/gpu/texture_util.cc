/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/texture_util.h"

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {
namespace texture_util {

void GetGuestMipBlocks(Dimension dimension, uint32_t width, uint32_t height,
                       uint32_t depth, TextureFormat format, uint32_t mip,
                       uint32_t& width_blocks_out, uint32_t& height_blocks_out,
                       uint32_t& depth_blocks_out) {
  // Get mipmap size.
  // TODO(Triang3l): Verify if mipmap storage actually needs to be power of two.
  if (mip != 0) {
    width = std::max(xe::next_pow2(width) >> mip, 1u);
    if (dimension != Dimension::k1D) {
      height = std::max(xe::next_pow2(height) >> mip, 1u);
      if (dimension == Dimension::k3D) {
        depth = std::max(xe::next_pow2(depth) >> mip, 1u);
      }
    }
  }

  // Get the size in blocks rather than in pixels.
  const FormatInfo* format_info = FormatInfo::Get(format);
  width = xe::align(width, format_info->block_width) / format_info->block_width;
  height =
      xe::align(height, format_info->block_height) / format_info->block_height;

  // 32x32x4-align.
  width_blocks_out = xe::align(width, 32u);
  if (dimension != Dimension::k1D) {
    height_blocks_out = xe::align(height, 32u);
    if (dimension == Dimension::k3D) {
      depth_blocks_out = xe::align(depth, 4u);
    } else {
      depth_blocks_out = 1;
    }
  } else {
    height_blocks_out = 1;
  }
}

uint32_t GetGuestMipSliceStorageSize(uint32_t width_blocks,
                                     uint32_t height_blocks,
                                     uint32_t depth_blocks, bool is_tiled,
                                     TextureFormat format,
                                     uint32_t* row_pitch_out, bool align_4kb) {
  const FormatInfo* format_info = FormatInfo::Get(format);
  uint32_t row_pitch = width_blocks * format_info->block_width *
                       format_info->block_height * format_info->bits_per_pixel /
                       8;
  if (!is_tiled) {
    row_pitch = xe::align(row_pitch, 256u);
  }
  if (row_pitch_out != nullptr) {
    *row_pitch_out = row_pitch;
  }
  uint32_t size = row_pitch * height_blocks * depth_blocks;
  if (align_4kb) {
    size = xe::align(size, 4096u);
  }
  return size;
}

bool GetPackedMipOffset(uint32_t width, uint32_t height, uint32_t depth,
                        TextureFormat format, uint32_t mip, uint32_t& x_blocks,
                        uint32_t& y_blocks, uint32_t& z_blocks) {
  // Tile size is 32x32, and once textures go <=16 they are packed into a
  // single tile together. The math here is insane. Most sourced from
  // graph paper, looking at dds dumps and executable reverse engineering.
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
  //
  // The 2x2 and 1x1 squares are packed in their specific positions because
  // each square is the size of at least one block (which is 4x4 pixels max)
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

  uint32_t log2_width = xe::log2_ceil(width);
  uint32_t log2_height = xe::log2_ceil(height);
  uint32_t log2_size = std::min(log2_width, log2_height);
  if (log2_size > 4 + mip) {
    // The shortest dimension is bigger than 16, not packed.
    x_blocks = 0;
    y_blocks = 0;
    z_blocks = 0;
    return false;
  }
  uint32_t packed_mip_base = (log2_size > 4) ? (log2_size - 4) : 0;
  uint32_t packed_mip = mip - packed_mip_base;

  // Find the block offset of the mip.
  if (packed_mip < 3) {
    if (log2_width > log2_height) {
      // Wider than tall. Laid out vertically.
      x_blocks = 0;
      y_blocks = 16 >> packed_mip;
    } else {
      // Taller than wide. Laid out horizontally.
      x_blocks = 16 >> packed_mip;
      y_blocks = 0;
    }
    z_blocks = 0;
  } else {
    uint32_t offset;
    if (log2_width > log2_height) {
      // Wider than tall. Laid out horizontally.
      offset = (1 << (log2_width - packed_mip_base)) >> (packed_mip - 2);
      x_blocks = offset;
      y_blocks = 0;
    } else {
      // Taller than wide. Laid out vertically.
      x_blocks = 0;
      offset = (1 << (log2_height - packed_mip_base)) >> (packed_mip - 2);
      y_blocks = offset;
    }
    if (offset < 4) {
      // Pack 1x1 Z mipmaps along Z - not reached for 2D.
      uint32_t log2_depth = xe::log2_ceil(depth);
      if (log2_depth > 1 + packed_mip) {
        z_blocks = (log2_depth - packed_mip) * 4;
      } else {
        z_blocks = 4;
      }
    } else {
      z_blocks = 0;
    }
  }

  const FormatInfo* format_info = FormatInfo::Get(format);
  x_blocks /= format_info->block_width;
  y_blocks /= format_info->block_height;
  return true;
}

void GetTextureTotalSize(Dimension dimension, uint32_t width, uint32_t height,
                         uint32_t depth, TextureFormat format, bool is_tiled,
                         bool packed_mips, uint32_t mip_max_level,
                         uint32_t* base_size, uint32_t* mip_size) {
  bool is_3d = dimension == Dimension::k3D;
  uint32_t width_blocks, height_blocks, depth_blocks;
  if (base_size != nullptr) {
    GetGuestMipBlocks(dimension, width, height, depth, format, 0, width_blocks,
                      height_blocks, depth_blocks);
    uint32_t size = GetGuestMipSliceStorageSize(
        width_blocks, height_blocks, depth_blocks, is_tiled, format, nullptr);
    if (!is_3d) {
      size *= depth;
    }
    *base_size = size;
  }
  if (mip_size != nullptr) {
    mip_max_level = std::min(
        mip_max_level,
        GetSmallestMipLevel(width, height, is_3d ? depth : 1, packed_mips));
    uint32_t size = 0;
    for (uint32_t i = 1; i <= mip_max_level; ++i) {
      GetGuestMipBlocks(dimension, width, height, depth, format, i,
                        width_blocks, height_blocks, depth_blocks);
      uint32_t level_size = GetGuestMipSliceStorageSize(
          width_blocks, height_blocks, depth_blocks, is_tiled, format, nullptr);
      if (!is_3d) {
        level_size *= depth;
      }
      size += level_size;
    }
    *mip_size = size;
  }
}

int32_t GetTiledOffset2D(int32_t x, int32_t y, uint32_t width,
                         uint32_t bpb_log2) {
  // https://github.com/gildor2/UModel/blob/de8fbd3bc922427ea056b7340202dcdcc19ccff5/Unreal/UnTexture.cpp#L489
  width = xe::align(width, 32u);
  // Top bits of coordinates.
  int32_t macro = ((x >> 5) + (y >> 5) * (width >> 5)) << (bpb_log2 + 7);
  // Lower bits of coordinates (result is 6-bit value).
  int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2;
  // Mix micro/macro + add few remaining x/y bits.
  int32_t offset =
      macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  // Mix bits again.
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

int32_t GetTiledOffset3D(int32_t x, int32_t y, int32_t z, uint32_t width,
                         uint32_t height, uint32_t bpb_log2) {
  // Reconstructed from disassembly of XGRAPHICS::TileVolume.
  width = xe::align(width, 32u);
  height = xe::align(height, 32u);
  int32_t macro_outer = ((y >> 4) + (z >> 2) * (height >> 4)) * (width >> 5);
  int32_t macro = ((((x >> 5) + macro_outer) << (bpb_log2 + 6)) & 0xFFFFFFF)
                  << 1;
  int32_t micro = (((x & 7) + ((y & 6) << 2)) << (bpb_log2 + 6)) >> 6;
  int32_t offset_outer = ((y >> 3) + (z >> 2)) & 1;
  int32_t offset1 =
      offset_outer + ((((x >> 3) + (offset_outer << 1)) & 3) << 1);
  int32_t offset2 = ((macro + (micro & ~15)) << 1) + (micro & 15) +
                    ((z & 3) << (bpb_log2 + 6)) + ((y & 1) << 4);
  int32_t address = (offset1 & 1) << 3;
  address += (offset2 >> 6) & 7;
  address <<= 3;
  address += offset1 & ~1;
  address <<= 2;
  address += offset2 & ~511;
  address <<= 3;
  address += offset2 & 63;
  return address;
}

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe
