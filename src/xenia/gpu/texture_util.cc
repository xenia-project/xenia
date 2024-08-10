/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/texture_util.h"
namespace xe {
namespace gpu {
namespace texture_util {

void GetSubresourcesFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, uint32_t* width_minus_1_out,
    uint32_t* height_minus_1_out, uint32_t* depth_or_array_size_minus_1_out,
    uint32_t* base_page_out, uint32_t* mip_page_out,
    uint32_t* mip_min_level_out, uint32_t* mip_max_level_out) {
  uint32_t width_minus_1 = 0;
  uint32_t height_minus_1 = 0;
  uint32_t depth_or_array_size_minus_1 = 0;
  switch (fetch.dimension) {
    case xenos::DataDimension::k1D:
      assert_false(fetch.stacked);
      assert_false(fetch.tiled);
      assert_false(fetch.packed_mips);
      width_minus_1 = fetch.size_1d.width;
      break;
    case xenos::DataDimension::k2DOrStacked:
      width_minus_1 = fetch.size_2d.width;
      height_minus_1 = fetch.size_2d.height;
      depth_or_array_size_minus_1 =
          fetch.stacked ? fetch.size_2d.stack_depth : 0;
      break;
    case xenos::DataDimension::k3D:
      assert_false(fetch.stacked);
      width_minus_1 = fetch.size_3d.width;
      height_minus_1 = fetch.size_3d.height;
      depth_or_array_size_minus_1 = fetch.size_3d.depth;
      break;
    case xenos::DataDimension::kCube:
      assert_false(fetch.stacked);
      assert_true(fetch.size_2d.stack_depth == 5);
      width_minus_1 = fetch.size_2d.width;
      height_minus_1 = fetch.size_2d.height;
      depth_or_array_size_minus_1 = 5;
      break;
  }
  if (width_minus_1_out) {
    *width_minus_1_out = width_minus_1;
  }
  if (height_minus_1_out) {
    *height_minus_1_out = height_minus_1;
  }
  if (depth_or_array_size_minus_1_out) {
    *depth_or_array_size_minus_1_out = depth_or_array_size_minus_1;
  }

  uint32_t longest_axis_minus_1 = std::max(width_minus_1, height_minus_1);
  if (fetch.dimension == xenos::DataDimension::k3D) {
    longest_axis_minus_1 =
        std::max(longest_axis_minus_1, depth_or_array_size_minus_1);
  }
  uint32_t size_mip_max_level =
      xe::log2_floor(longest_axis_minus_1 + uint32_t(1));

  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;

  uint32_t mip_min_level, mip_max_level;
  // Not taking mip_filter == kBaseMap into account for mip_max_level because
  // the mip filter may be overridden by shader fetch instructions.
  if (mip_page == 0) {
    mip_min_level = 0;
    mip_max_level = 0;
  } else {
    mip_min_level = std::min(uint32_t(fetch.mip_min_level), size_mip_max_level);
    mip_max_level =
        std::max(std::min(uint32_t(fetch.mip_max_level), size_mip_max_level),
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

  x_blocks >>= FormatInfo::GetWidthShift(format);
  y_blocks >>= FormatInfo::GetHeightShift(format);
  return true;
}

TextureGuestLayout GetGuestTextureLayout(
    xenos::DataDimension dimension, uint32_t base_pitch_texels_div_32,
    uint32_t width_texels, uint32_t height_texels, uint32_t depth_or_array_size,
    bool is_tiled, xenos::TextureFormat format, bool has_packed_levels,
    bool has_base, uint32_t max_level) {
  TextureGuestLayout layout;

  if (dimension == xenos::DataDimension::k1D) {
    assert_false(is_tiled);
    // GetPackedMipOffset may result in packing along Y for `width > height`
    // textures.
    assert_false(has_packed_levels);
    height_texels = 1;
  }
  uint32_t depth =
      dimension == xenos::DataDimension::k3D ? depth_or_array_size : 1;
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

  // For safety, for instance, with empty resolve regions (extents calculation
  // may overflow otherwise due to the assumption of at least one row, for
  // example, but an empty texture is empty anyway).
  if (!width_texels || !height_texels || !depth || !layout.array_size) {
    std::memset(&layout, 0, sizeof(layout));
    return layout;
  }

  // For safety, clamp the maximum level.
  uint32_t max_level_for_dimensions =
      xe::log2_floor(std::max(std::max(width_texels, height_texels), depth));
  assert_true(max_level <= max_level_for_dimensions);
  max_level = std::min(max_level, max_level_for_dimensions);
  layout.max_level = max_level;

  layout.packed_level = has_packed_levels
                            ? GetPackedMipLevel(width_texels, height_texels)
                            : UINT32_MAX;

  // Clear unused level layouts to zero strides/sizes.
  if (!has_base) {
    std::memset(&layout.base, 0, sizeof(layout.base));
  }
  if (layout.packed_level != 0) {
    std::memset(&layout.mips[0], 0, sizeof(layout.mips[0]));
  }
  uint32_t max_stored_level =
      std::min(max_level, uint32_t(layout.packed_level));
  {
    uint32_t mips_end = max_stored_level + 1;
    assert_true(mips_end <= xe::countof(layout.mips));
    uint32_t mips_unused_count = uint32_t(xe::countof(layout.mips)) - mips_end;
    if (mips_unused_count) {
      std::memset(&layout.mips[mips_end], 0,
                  sizeof(layout.mips[0]) * mips_unused_count);
      std::memset(&layout.mip_offsets_bytes[mips_end], 0,
                  sizeof(layout.mip_offsets_bytes[0]) * mips_unused_count);
    }
  }
  layout.mips_total_extent_bytes = 0;

  const FormatInfo* format_info = FormatInfo::Get(format);
  uint32_t bytes_per_block = format_info->bytes_per_block();

  // The loop counter can mean two things depending on whether the packed mip
  // tail is stored as mip 0, because in this case, it would be ambiguous since
  // both the base and the mips would be on "level 0", but stored separately and
  // possibly with a different layout.
  uint32_t loop_level_last;
  if (layout.packed_level == 0) {
    // Packed mip tail is the level 0 - may need to load mip tails for the base,
    // the mips, or both.
    // Loop iteration 0 - base packed mip tail.
    // Loop iteration 1 - mips packed mip tail.
    loop_level_last = uint32_t(max_level != 0);
  } else {
    // Packed mip tail is not the level 0.
    // Loop iteration is the actual level being loaded.
    loop_level_last = max_stored_level;
  }
  uint32_t mip_offset_bytes = 0;
  for (uint32_t loop_level = has_base ? 0 : 1; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (layout.packed_level == 0) ? 0 : loop_level;
    TextureGuestLayout::Level& level_layout =
        is_base ? layout.base : layout.mips[level];

    // Calculate the strides.
    // Mips have row / depth slice strides calculated from a mip of a texture
    // whose base size is a power of two.
    // The base mip has tightly packed depth slices, and takes the row pitch
    // from the fetch constant.
    // For stride calculation purposes, mip dimensions are always aligned to
    // 32x32x4 blocks (or x1 for the missing dimensions), including for linear
    // textures.
    // Linear texture rows are 256-byte-aligned.
    uint32_t row_pitch_texels_unaligned;
    uint32_t z_slice_stride_texel_rows_unaligned;
    if (is_base) {
      row_pitch_texels_unaligned = base_pitch_texels_div_32 << 5;
      z_slice_stride_texel_rows_unaligned = height_texels;
    } else {
      row_pitch_texels_unaligned =
          std::max(xe::next_pow2(width_texels) >> level, uint32_t(1));
      z_slice_stride_texel_rows_unaligned =
          std::max(xe::next_pow2(height_texels) >> level, uint32_t(1));
    }
    uint32_t row_pitch_blocks_tile_aligned = xe::align(
        xe::align(row_pitch_texels_unaligned, format_info->block_width) /
            format_info->block_width,
        xenos::kTextureTileWidthHeight);
    level_layout.row_pitch_bytes =
        row_pitch_blocks_tile_aligned * bytes_per_block;
    // Assuming the provided pitch is already 256-byte-aligned for linear, but
    // considering the guest-provided pitch more important (no information about
    // how the GPU actually handles unaligned rows).
    if (!is_tiled && !is_base) {
      level_layout.row_pitch_bytes = xe::align(
          level_layout.row_pitch_bytes, xenos::kTextureLinearRowAlignmentBytes);
    }
    level_layout.z_slice_stride_block_rows =
        dimension != xenos::DataDimension::k1D
            ? xe::align(xe::align(z_slice_stride_texel_rows_unaligned,
                                  format_info->block_height) /
                            format_info->block_height,
                        xenos::kTextureTileWidthHeight)
            : 1;
    level_layout.array_slice_stride_bytes =
        level_layout.row_pitch_bytes * level_layout.z_slice_stride_block_rows;
    uint32_t z_stride_bytes = level_layout.array_slice_stride_bytes;
    if (dimension == xenos::DataDimension::k3D) {
      level_layout.array_slice_stride_bytes *=
          xe::align(depth_or_array_size, xenos::kTextureTileDepth);
    }
    level_layout.array_slice_stride_bytes =
        xe::align(level_layout.array_slice_stride_bytes,
                  xenos::kTextureSubresourceAlignmentBytes);

    // Estimate the memory amount actually referenced by the texture, which may
    // be smaller (especially in the 1280x720 linear k_8_8_8_8 case in 4E4D083E,
    // for which memory exactly for 1280x720 is allocated, and aligning the
    // height to 32 would cause access of an unallocated page) or bigger than
    // the stride.
    if (level == layout.packed_level) {
      // Calculate the portion of the mip tail actually used by the needed mips.
      // The actually used region may be significantly smaller than the full
      // 32x32-texel-aligned (and, for mips, calculated from the base dimensions
      // rounded to powers of two - 58410A7A has an 80x260 tiled texture with
      // packed mips at level 3 containing a mip ending at Y = 36, while
      // 260 >> 3 == 32, but 512 >> 3 == 64) tail. A 2x2 texture (for example,
      // in 494707D4, there's a 2x2 k_8_8_8_8 linear texture with packed mips),
      // for instance, would have its 2x2 base at (16, 0) and its 1x1 mip at
      // (8, 0) - and we need 2 or 1 rows in these cases, not 32 - the 32 rows
      // in a linear texture (with 256-byte pitch alignment) would span two 4 KB
      // pages rather than one.
      level_layout.x_extent_blocks = 0;
      level_layout.y_extent_blocks = 0;
      level_layout.z_extent = 0;
      uint32_t packed_sublevel_last = is_base ? 0 : max_level;
      for (uint32_t packed_sublevel = layout.packed_level;
           packed_sublevel <= packed_sublevel_last; ++packed_sublevel) {
        uint32_t packed_sublevel_x_blocks;
        uint32_t packed_sublevel_y_blocks;
        uint32_t packed_sublevel_z;
        GetPackedMipOffset(width_texels, height_texels, depth, format,
                           packed_sublevel, packed_sublevel_x_blocks,
                           packed_sublevel_y_blocks, packed_sublevel_z);
        level_layout.x_extent_blocks = std::max(
            level_layout.x_extent_blocks,
            packed_sublevel_x_blocks +
                xe::align(
                    std::max(width_texels >> packed_sublevel, uint32_t(1)),
                    format_info->block_width) /
                    format_info->block_width);
        level_layout.y_extent_blocks = std::max(
            level_layout.y_extent_blocks,
            packed_sublevel_y_blocks +
                xe::align(
                    std::max(height_texels >> packed_sublevel, uint32_t(1)),
                    format_info->block_height) /
                    format_info->block_height);
        level_layout.z_extent =
            std::max(level_layout.z_extent,
                     packed_sublevel_z +
                         std::max(depth >> packed_sublevel, uint32_t(1)));
      }
    } else {
      level_layout.x_extent_blocks =
          xe::align(std::max(width_texels >> level, uint32_t(1)),
                    format_info->block_width) /
          format_info->block_width;
      level_layout.y_extent_blocks =
          xe::align(std::max(height_texels >> level, uint32_t(1)),
                    format_info->block_height) /
          format_info->block_height;
      level_layout.z_extent = std::max(depth >> level, uint32_t(1));
    }
    if (is_tiled) {
      uint32_t bytes_per_block_log2 = xe::log2_floor(bytes_per_block);
      if (dimension == xenos::DataDimension::k3D) {
        level_layout.array_slice_data_extent_bytes =
            GetTiledAddressUpperBound3D(
                level_layout.x_extent_blocks, level_layout.y_extent_blocks,
                level_layout.z_extent, row_pitch_blocks_tile_aligned,
                level_layout.y_extent_blocks, bytes_per_block_log2);
      } else {
        level_layout.array_slice_data_extent_bytes =
            GetTiledAddressUpperBound2D(
                level_layout.x_extent_blocks, level_layout.y_extent_blocks,
                row_pitch_blocks_tile_aligned, bytes_per_block_log2);
      }
    } else {
      level_layout.array_slice_data_extent_bytes =
          z_stride_bytes * (level_layout.z_extent - 1) +
          level_layout.row_pitch_bytes * (level_layout.y_extent_blocks - 1) +
          bytes_per_block * level_layout.x_extent_blocks;
    }
    level_layout.level_data_extent_bytes =
        level_layout.array_slice_stride_bytes * (layout.array_size - 1) +
        level_layout.array_slice_data_extent_bytes;

    if (!is_base) {
      layout.mip_offsets_bytes[level] = mip_offset_bytes;
      layout.mips_total_extent_bytes =
          std::max(layout.mips_total_extent_bytes,
                   mip_offset_bytes + level_layout.level_data_extent_bytes);
      mip_offset_bytes +=
          level_layout.array_slice_stride_bytes * layout.array_size;
    }
  }

  return layout;
}
XE_NOINLINE
XE_NOALIAS
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
XE_NOINLINE
XE_NOALIAS
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
XE_NOINLINE
XE_NOALIAS
uint32_t GetTiledAddressUpperBound2D(uint32_t right, uint32_t bottom,
                                     uint32_t pitch,
                                     uint32_t bytes_per_block_log2) {
  if (!right || !bottom) {
    return 0;
  }
  // Get the origin of the 32x32 tile containing the last texel.
  uint32_t upper_bound = uint32_t(GetTiledOffset2D(
      int32_t((right - 1) & ~(xenos::kTextureTileWidthHeight - 1)),
      int32_t((bottom - 1) & ~(xenos::kTextureTileWidthHeight - 1)), pitch,
      bytes_per_block_log2));
  switch (bytes_per_block_log2) {
    case 0:
      // Independent addressing within 128x128 portions, but the extent is 0xA00
      // bytes from the 32x32 tile origin.
      upper_bound += 0xA00;
      break;
    case 1:
      // Independent addressing within 64x64 portions, but the extent is 0xC00
      // bytes from the 32x32 tile origin.
      upper_bound += 0xC00;
      break;
    default:
      upper_bound += UINT32_C(0x400) << bytes_per_block_log2;
      break;
  }
  return upper_bound;
}
XE_NOINLINE
XE_NOALIAS
uint32_t GetTiledAddressUpperBound3D(uint32_t right, uint32_t bottom,
                                     uint32_t back, uint32_t pitch,
                                     uint32_t height,
                                     uint32_t bytes_per_block_log2) {
  if (!right || !bottom || !back) {
    return 0;
  }
  // Get the origin of the 32x32x4 tile containing the last texel.
  uint32_t upper_bound = uint32_t(GetTiledOffset3D(
      int32_t((right - 1) & ~(xenos::kTextureTileWidthHeight - 1)),
      int32_t((bottom - 1) & ~(xenos::kTextureTileWidthHeight - 1)),
      int32_t((back - 1) & ~(xenos::kTextureTileDepth - 1)), pitch, height,
      bytes_per_block_log2));
  uint32_t pitch_aligned = xe::align(pitch, xenos::kTextureTileWidthHeight);
  switch (bytes_per_block_log2) {
    case 0:
      // 64x32x8 portions have independent addressing.
      // Extent relative to the 32x32x4 tile origin:
      // - Pitch = 32, 96, 160...: (Pitch / 64) * 0x1000 + 0x1000
      // - Pitch = 64, 128, 192...: (Pitch / 64) * 0x1000 + 0xC00
      upper_bound += ((pitch_aligned >> 6) << 12) + 0xC00 +
                     ((pitch_aligned & (1 << 5)) << (10 - 5));
      break;
    default:
      // 32x32x8 portions have independent addressing.
      // Extent: ((Pitch / 32) * 0x1000 + 0x1000) * (BPB / 2)
      // Or: ((Pitch / 32) * 0x1000 / 2 + 0x1000 / 2) * BPB
      upper_bound += ((pitch_aligned << (12 - 5 - 1)) + (0x1000 >> 1))
                     << bytes_per_block_log2;
      break;
  }
  return upper_bound;
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
    // Textures with only constant components must still be bound to shaders for
    // various queries (such as filtering weights) not involving the color data
    // itself.
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

void GetClampModesForDimension(const xenos::xe_gpu_texture_fetch_t& fetch,
                               xenos::ClampMode& clamp_x_out,
                               xenos::ClampMode& clamp_y_out,
                               xenos::ClampMode& clamp_z_out) {
  clamp_x_out = xenos::ClampMode::kClampToEdge;
  clamp_y_out = xenos::ClampMode::kClampToEdge;
  clamp_z_out = xenos::ClampMode::kClampToEdge;
  switch (fetch.dimension) {
    case xenos::DataDimension::k3D:
      clamp_z_out = fetch.clamp_z;
      [[fallthrough]];
    case xenos::DataDimension::k2DOrStacked:
      clamp_y_out = fetch.clamp_y;
      [[fallthrough]];
    case xenos::DataDimension::k1D:
      clamp_x_out = fetch.clamp_x;
      break;
    default:
      // Not applicable to cube textures.
      break;
  }
}

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe
