/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_UTIL_H_
#define XENIA_GPU_TEXTURE_UTIL_H_

#include <algorithm>

#include "xenia/base/math.h"
#include "xenia/gpu/texture_info.h"

namespace xe {
namespace gpu {
namespace texture_util {

// This namespace replaces texture_extent and most of texture_info for
// simplicity.

// Calculates width, height and depth of the image backing the guest mipmap (or
// the base level if mip is 0).
void GetGuestMipBlocks(Dimension dimension, uint32_t width, uint32_t height,
                       uint32_t depth, TextureFormat format, uint32_t mip,
                       uint32_t& width_blocks_out, uint32_t& height_blocks_out,
                       uint32_t& depth_blocks_out);

// Calculates the number of bytes required to store a single array slice within
// a single mip level - width, height and depth must be obtained via
// GetGuestMipBlocks. align_4kb can be set to false when calculating relatively
// to some offset in the texture rather than the top-left corner of it.
uint32_t GetGuestMipSliceStorageSize(uint32_t width_blocks,
                                     uint32_t height_blocks,
                                     uint32_t depth_blocks, bool is_tiled,
                                     TextureFormat format,
                                     uint32_t* row_pitch_out,
                                     bool align_4kb = true);

// Gets the number of the mipmap level where the packed mips are stored.
inline uint32_t GetPackedMipLevel(uint32_t width, uint32_t height) {
  uint32_t log2_size = xe::log2_ceil(std::min(width, height));
  return log2_size > 4 ? log2_size - 4 : 0;
}

// Gets the offset of the mipmap within the tail in blocks, or zeros (and
// returns false) if the mip level is not packed. Width, height and depth are in
// texels. For non-3D textures, set depth to 1.
bool GetPackedMipOffset(uint32_t width, uint32_t height, uint32_t depth,
                        TextureFormat format, uint32_t mip, uint32_t& x_blocks,
                        uint32_t& y_blocks, uint32_t& z_blocks);

// Calculates the index of the smallest mip (1x1x1 for unpacked or min(w,h)<=16
// packed mips) for a texture. For non-3D textures, set the depth to 1.
inline uint32_t GetSmallestMipLevel(uint32_t width, uint32_t height,
                                    uint32_t depth, bool packed_mips) {
  uint32_t size = std::max(width, height);
  size = std::max(size, depth);
  uint32_t smallest_mip = xe::log2_ceil(size);
  if (packed_mips) {
    smallest_mip = std::min(smallest_mip, GetPackedMipLevel(width, height));
  }
  return smallest_mip;
}

// Returns the total size of memory the texture uses starting from its base and
// mip addresses, in bytes (both are optional).
void GetTextureTotalSize(Dimension dimension, uint32_t width, uint32_t height,
                         uint32_t depth, TextureFormat format, bool is_tiled,
                         bool packed_mips, uint32_t mip_max_level,
                         uint32_t* base_size, uint32_t* mip_size);

int32_t GetTiledOffset2D(int32_t x, int32_t y, uint32_t width,
                         uint32_t bpb_log2);
int32_t GetTiledOffset3D(int32_t x, int32_t y, int32_t z, uint32_t width,
                         uint32_t height, uint32_t bpb_log2);

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_UTIL_H_
