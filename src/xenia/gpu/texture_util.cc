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

uint32_t GetGuestMipStorageSize(uint32_t width_blocks, uint32_t height_blocks,
                                uint32_t depth_blocks, bool is_tiled,
                                TextureFormat format, uint32_t* row_pitch_out) {
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
  return xe::align(row_pitch * height_blocks * depth_blocks, 4096u);
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

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe
