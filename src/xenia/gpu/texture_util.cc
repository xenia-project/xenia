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
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {
namespace texture_util {

void GetSubresourcesFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, uint32_t* width_out,
    uint32_t* height_out, uint32_t* depth_or_faces_out, uint32_t* base_page_out,
    uint32_t* mip_page_out, uint32_t* mip_min_level_out,
    uint32_t* mip_max_level_out, xenos::TextureFilter sampler_mip_filter) {
  uint32_t width = 0, height = 0, depth_or_faces = 0;
  switch (fetch.dimension) {
    case xenos::DataDimension::k1D:
      assert_false(fetch.stacked);
      assert_false(fetch.tiled);
      assert_false(fetch.packed_mips);
      width = fetch.size_1d.width;
      break;
    case xenos::DataDimension::k2DOrStacked:
      width = fetch.size_2d.width;
      height = fetch.size_2d.height;
      depth_or_faces = fetch.stacked ? fetch.size_2d.stack_depth : 0;
      break;
    case xenos::DataDimension::k3D:
      assert_false(fetch.stacked);
      width = fetch.size_3d.width;
      height = fetch.size_3d.height;
      depth_or_faces = fetch.size_3d.depth;
      break;
    case xenos::DataDimension::kCube:
      assert_false(fetch.stacked);
      assert_true(fetch.size_2d.stack_depth == 5);
      width = fetch.size_2d.width;
      height = fetch.size_2d.height;
      depth_or_faces = 5;
      break;
  }
  ++width;
  ++height;
  ++depth_or_faces;
  if (width_out) {
    *width_out = width;
  }
  if (height_out) {
    *height_out = height;
  }
  if (depth_or_faces_out) {
    *depth_or_faces_out = depth_or_faces;
  }

  uint32_t longest_axis = std::max(width, height);
  if (fetch.dimension == xenos::DataDimension::k3D) {
    longest_axis = std::max(longest_axis, depth_or_faces);
  }
  uint32_t size_mip_max_level = xe::log2_floor(longest_axis);
  xenos::TextureFilter mip_filter =
      sampler_mip_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.mip_filter
          : sampler_mip_filter;

  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;

  uint32_t mip_min_level, mip_max_level;
  if (mip_filter == xenos::TextureFilter::kBaseMap || mip_page == 0) {
    mip_min_level = 0;
    mip_max_level = 0;
  } else {
    mip_min_level = std::min(fetch.mip_min_level, size_mip_max_level);
    mip_max_level = std::max(std::min(fetch.mip_max_level, size_mip_max_level),
                             mip_min_level);
  }
  if (mip_max_level != 0) {
    if (base_page == 0) {
      mip_min_level = std::max(mip_min_level, uint32_t(1));
    }
    if (mip_min_level != 0) {
      base_page = 0;
    }
  } else {
    mip_page = 0;
  }

  if (base_page_out) {
    *base_page_out = base_page;
  }
  if (mip_page_out) {
    *mip_page_out = mip_page;
  }
  if (mip_min_level_out) {
    *mip_min_level_out = mip_min_level;
  }
  if (mip_max_level_out) {
    *mip_max_level_out = mip_max_level;
  }
}

bool GetPackedMipOffset(uint32_t width, uint32_t height, uint32_t depth,
                        xenos::TextureFormat format, uint32_t mip,
                        uint32_t& x_blocks, uint32_t& y_blocks,
                        uint32_t& z_blocks) {
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

TextureGuestLevelLayout GetGuestLevelLayout(
    xenos::DataDimension dimension, uint32_t base_pitch_texels_div_32,
    uint32_t width_texels, uint32_t height_texels, uint32_t depth_or_array_size,
    bool is_tiled, xenos::TextureFormat format, bool is_mip, uint32_t level,
    bool is_packed_level) {
  // If with packed mips the mips 1... happen to be packed in what's stored as
  // mip 0, this mip tail appears to be stored like mips (with power of two size
  // rounding) rather than like the base level (with the pitch from the fetch
  // constant), so we distinguish between them for mip == 0.
  // Base is by definition the level 0.
  assert_false(!is_mip && level);
  // Level 0 for mips is the special case for a packed mip tail of very small
  // textures, where the tail is stored like it's at the level 0.
  assert_false(is_mip && !level && !is_packed_level);

  TextureGuestLevelLayout layout;

  // For safety, for instance, with empty resolve regions (extents calculation
  // may overflow otherwise due to the assumption of at least one row, for
  // example, but an empty texture is empty anyway).
  if (!width_texels ||
      (dimension != xenos::DataDimension::k1D && !height_texels) ||
      ((dimension == xenos::DataDimension::k2DOrStacked ||
        dimension == xenos::DataDimension::k3D) &&
       !depth_or_array_size)) {
    std::memset(&layout, 0, sizeof(layout));
    return layout;
  }

  switch (dimension) {
    case xenos::DataDimension::k2DOrStacked:
      layout.array_size = depth_or_array_size;
      break;
    case xenos::DataDimension::kCube:
      layout.array_size = 6;
      break;
    default:
      layout.array_size = 1;
  }

  const FormatInfo* format_info = FormatInfo::Get(format);
  uint32_t bytes_per_block = format_info->bytes_per_block();

  // Calculate the strides.
  // Mips have row / depth slice strides calculated from a mip of a texture
  // whose base size is a power of two.
  // The base mip has tightly packed depth slices, and takes the row pitch from
  // the fetch constant.
  // For stride calculation purposes, mip dimensions are always aligned to
  // 32x32x4 blocks (or x1 for the missing dimensions), including for linear
  // textures.
  // Linear texture rows are 256-byte-aligned.
  uint32_t row_pitch_texels_unaligned;
  uint32_t z_slice_stride_texel_rows_unaligned;
  if (is_mip) {
    row_pitch_texels_unaligned =
        std::max(xe::next_pow2(width_texels) >> level, uint32_t(1));
    z_slice_stride_texel_rows_unaligned =
        std::max(xe::next_pow2(height_texels) >> level, uint32_t(1));
  } else {
    row_pitch_texels_unaligned = base_pitch_texels_div_32 << 5;
    z_slice_stride_texel_rows_unaligned = height_texels;
  }
  uint32_t row_pitch_blocks_tile_aligned = xe::align(
      xe::align(row_pitch_texels_unaligned, format_info->block_width) /
          format_info->block_width,
      xenos::kTextureTileWidthHeight);
  layout.row_pitch_bytes = row_pitch_blocks_tile_aligned * bytes_per_block;
  // Assuming the provided pitch is already 256-byte-aligned for linear, but
  // considering the guest-provided pitch more important (no information about
  // how the GPU actually handles unaligned rows).
  if (!is_tiled && is_mip) {
    layout.row_pitch_bytes = xe::align(layout.row_pitch_bytes,
                                       xenos::kTextureLinearRowAlignmentBytes);
  }
  layout.z_slice_stride_block_rows =
      dimension != xenos::DataDimension::k1D
          ? xe::align(xe::align(z_slice_stride_texel_rows_unaligned,
                                format_info->block_height) /
                          format_info->block_height,
                      xenos::kTextureTileWidthHeight)
          : 1;
  layout.array_slice_stride_bytes =
      layout.row_pitch_bytes * layout.z_slice_stride_block_rows;
  uint32_t z_stride_bytes = layout.array_slice_stride_bytes;
  if (dimension == xenos::DataDimension::k3D) {
    layout.array_slice_stride_bytes *=
        xe::align(depth_or_array_size, xenos::kTextureTiledDepthGranularity);
  }
  uint32_t array_slice_stride_bytes_non_4kb_aligned =
      layout.array_slice_stride_bytes;
  layout.array_slice_stride_bytes =
      xe::align(array_slice_stride_bytes_non_4kb_aligned,
                xenos::kTextureSubresourceAlignmentBytes);

  // Estimate the memory amount actually referenced by the texture, which may be
  // smaller (especially in the 2x2 linear k_8_8_8_8 case in Test Drive
  // Unlimited, for which 4 KB are allocated, while the stride is 8 KB) or
  // bigger than the stride. For tiled textures, this is the dimensions aligned
  // to 32x32x4 blocks (or x1 for the missing dimensions).
  // For linear, doing almost the same for the mip tail (which can be used for
  // both the mips and, if the texture is very small, the base) because it
  // stores multiple mips outside the first mip in it in the tile padding
  // (though there's no need to align the size to the next power of two for this
  // purpose for mips - packed mips are only used when min(width, height) <= 16,
  // and packing is first done along the shorter axis - even if the longer axis
  // is larger than 32, nothing will be packed beyond the extent of the longer
  // axis). "Almost" because for linear textures, we're rounding the size to
  // 32x32x4 texels, not blocks - first packed mips start from 16-texel, not
  // 16-block, shortest dimension, and are placed in 32x- or x32-texel tiles,
  // while 32 blocks for compressed textures are bigger in memory than 32
  // texels.
  layout.x_extent_blocks = xe::align(width_texels, format_info->block_width) /
                           format_info->block_width;
  layout.y_extent_blocks =
      dimension != xenos::DataDimension::k1D
          ? xe::align(height_texels, format_info->block_height) /
                format_info->block_height
          : 1;
  layout.z_extent =
      dimension == xenos::DataDimension::k3D ? depth_or_array_size : 1;
  if (is_tiled) {
    layout.x_extent_blocks =
        xe::align(layout.x_extent_blocks, xenos::kTextureTileWidthHeight);
    assert_true(dimension != xenos::DataDimension::k1D);
    layout.y_extent_blocks =
        xe::align(layout.y_extent_blocks, xenos::kTextureTileWidthHeight);
    if (dimension == xenos::DataDimension::k3D) {
      layout.z_extent =
          xe::align(layout.z_extent, xenos::kTextureTiledDepthGranularity);
      // 3D texture addressing is pretty complex, so it's hard to determine the
      // memory extent of a subregion - just use pitch_tiles * height_tiles *
      // depth_tiles * bytes_per_tile at least for now, until we find a case
      // where it causes issues. width > pitch is a very weird edge case anyway,
      // and is extremely unlikely.
      assert_true(layout.x_extent_blocks <= row_pitch_blocks_tile_aligned);
      layout.array_slice_data_extent_bytes =
          array_slice_stride_bytes_non_4kb_aligned;
    } else {
      // 2D 32x32-block tiles are laid out linearly in the texture.
      // Calculate the extent as ((all rows except for the last * pitch in
      // tiles + last row length in tiles) * bytes per tile).
      layout.array_slice_data_extent_bytes =
          (layout.y_extent_blocks - xenos::kTextureTileWidthHeight) *
              layout.row_pitch_bytes +
          bytes_per_block * layout.x_extent_blocks *
              xenos::kTextureTileWidthHeight;
    }
  } else {
    if (is_packed_level) {
      layout.x_extent_blocks =
          xe::align(layout.x_extent_blocks,
                    xenos::kTextureTileWidthHeight / format_info->block_width);
      if (dimension != xenos::DataDimension::k1D) {
        layout.y_extent_blocks =
            xe::align(layout.y_extent_blocks, xenos::kTextureTileWidthHeight /
                                                  format_info->block_height);
        if (dimension == xenos::DataDimension::k3D) {
          layout.z_extent =
              xe::align(layout.z_extent, xenos::kTextureTiledDepthGranularity);
        }
      }
    }
    layout.array_slice_data_extent_bytes =
        z_stride_bytes * (layout.z_extent - 1) +
        layout.row_pitch_bytes * (layout.y_extent_blocks - 1) +
        bytes_per_block * layout.x_extent_blocks;
  }
  layout.level_data_extent_bytes =
      layout.array_slice_stride_bytes * (layout.array_size - 1) +
      layout.array_slice_data_extent_bytes;

  return layout;
}

TextureGuestLayout GetGuestTextureLayout(
    xenos::DataDimension dimension, uint32_t base_pitch_texels_div_32,
    uint32_t width_texels, uint32_t height_texels, uint32_t depth_or_array_size,
    bool is_tiled, xenos::TextureFormat format, bool has_packed_levels,
    bool has_base, uint32_t max_level) {
  TextureGuestLayout layout;

  if (dimension == xenos::DataDimension::k1D) {
    height_texels = 1;
  }

  // For safety, clamp the maximum level.
  uint32_t longest_axis = std::max(width_texels, height_texels);
  if (dimension == xenos::DataDimension::k3D) {
    longest_axis = std::max(longest_axis, depth_or_array_size);
  }
  uint32_t max_level_for_dimensions = xe::log2_floor(longest_axis);
  assert_true(max_level <= max_level_for_dimensions);
  max_level = std::min(max_level, max_level_for_dimensions);
  layout.max_level = max_level;

  layout.packed_level = has_packed_levels
                            ? GetPackedMipLevel(width_texels, height_texels)
                            : UINT32_MAX;

  if (has_base) {
    layout.base =
        GetGuestLevelLayout(dimension, base_pitch_texels_div_32, width_texels,
                            height_texels, depth_or_array_size, is_tiled,
                            format, false, 0, layout.packed_level == 0);
  } else {
    std::memset(&layout.base, 0, sizeof(layout.base));
  }

  std::memset(layout.mips, 0, sizeof(layout.mips));
  std::memset(layout.mip_offsets_bytes, 0, sizeof(layout.mip_offsets_bytes));
  layout.mips_total_extent_bytes = 0;
  if (max_level) {
    uint32_t mip_offset_bytes = 0;
    uint32_t max_stored_mip = std::min(max_level, layout.packed_level);
    for (uint32_t mip = std::min(uint32_t(1), layout.packed_level);
         mip <= max_stored_mip; ++mip) {
      layout.mip_offsets_bytes[mip] = mip_offset_bytes;
      TextureGuestLevelLayout& mip_layout = layout.mips[mip];
      mip_layout =
          GetGuestLevelLayout(dimension, base_pitch_texels_div_32, width_texels,
                              height_texels, depth_or_array_size, is_tiled,
                              format, true, mip, mip == layout.packed_level);
      layout.mips_total_extent_bytes =
          std::max(layout.mips_total_extent_bytes,
                   mip_offset_bytes + mip_layout.level_data_extent_bytes);
      mip_offset_bytes += mip_layout.next_level_distance_bytes();
    }
  }

  return layout;
}

int32_t GetTiledOffset2D(int32_t x, int32_t y, uint32_t pitch,
                         uint32_t bytes_per_block_log2) {
  // https://github.com/gildor2/UModel/blob/de8fbd3bc922427ea056b7340202dcdcc19ccff5/Unreal/UnTexture.cpp#L489
  pitch = xe::align(pitch, xenos::kTextureTileWidthHeight);
  // Top bits of coordinates.
  int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5))
                  << (bytes_per_block_log2 + 7);
  // Lower bits of coordinates (result is 6-bit value).
  int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  // Mix micro/macro + add few remaining x/y bits.
  int32_t offset =
      macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  // Mix bits again.
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

int32_t GetTiledOffset3D(int32_t x, int32_t y, int32_t z, uint32_t pitch,
                         uint32_t height, uint32_t bytes_per_block_log2) {
  // Reconstructed from disassembly of XGRAPHICS::TileVolume.
  pitch = xe::align(pitch, xenos::kTextureTileWidthHeight);
  height = xe::align(height, xenos::kTextureTileWidthHeight);
  int32_t macro_outer =
      ((y >> 4) + (z >> 2) * int32_t(height >> 4)) * int32_t(pitch >> 5);
  int32_t macro =
      ((((x >> 5) + macro_outer) << (bytes_per_block_log2 + 6)) & 0xFFFFFFF)
      << 1;
  int32_t micro =
      (((x & 7) + ((y & 6) << 2)) << (bytes_per_block_log2 + 6)) >> 6;
  int32_t offset_outer = ((y >> 3) + (z >> 2)) & 1;
  int32_t offset1 =
      offset_outer + ((((x >> 3) + (offset_outer << 1)) & 3) << 1);
  int32_t offset2 = ((macro + (micro & ~15)) << 1) + (micro & 15) +
                    ((z & 3) << (bytes_per_block_log2 + 6)) + ((y & 1) << 4);
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

uint8_t SwizzleSigns(const xenos::xe_gpu_texture_fetch_t& fetch) {
  uint8_t signs = 0;
  bool any_not_signed = false, any_signed = false;
  // 0b00 or 0b01 for each component, whether it's constant 0/1.
  uint8_t constant_mask = 0b00000000;
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t swizzle = (fetch.swizzle >> (i * 3)) & 0b111;
    if (swizzle & 0b100) {
      constant_mask |= uint8_t(1) << (i * 2);
    } else {
      xenos::TextureSign sign =
          xenos::TextureSign((fetch.dword_0 >> (2 + swizzle * 2)) & 0b11);
      signs |= uint8_t(sign) << (i * 2);
      if (sign == xenos::TextureSign::kSigned) {
        any_signed = true;
      } else {
        any_not_signed = true;
      }
    }
  }
  xenos::TextureSign constants_sign = xenos::TextureSign::kUnsigned;
  if (constant_mask == 0b01010101) {
    // If only constant components, choose according to the original format
    // (what would more likely be loaded if there were non-constant components).
    // If all components would be signed, use signed.
    if (((fetch.dword_0 >> 2) & 0b11111111) ==
        uint32_t(xenos::TextureSign::kSigned) * 0b01010101) {
      constants_sign = xenos::TextureSign::kSigned;
    }
  } else {
    // If only signed and constant components, reading just from the signed host
    // view is enough.
    if (any_signed && !any_not_signed) {
      constants_sign = xenos::TextureSign::kSigned;
    }
  }
  signs |= uint8_t(constants_sign) * constant_mask;
  return signs;
}

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe
