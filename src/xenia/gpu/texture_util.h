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
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace texture_util {

// This namespace replaces texture_extent and most of texture_info for
// simplicity.

// Extracts the size from the fetch constant, and also cleans up addresses and
// mip range based on real presence of the base level and mips. Returns 6 faces
// for cube textures.
void GetSubresourcesFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, uint32_t* width_out,
    uint32_t* height_out, uint32_t* depth_or_faces_out, uint32_t* base_page_out,
    uint32_t* mip_page_out, uint32_t* mip_min_level_out,
    uint32_t* mip_max_level_out,
    xenos::TextureFilter sampler_mip_filter =
        xenos::TextureFilter::kUseFetchConst);

// Calculates width, height and depth of the image backing the guest mipmap (or
// the base level if mip is 0).
void GetGuestMipBlocks(xenos::DataDimension dimension, uint32_t width,
                       uint32_t height, uint32_t depth,
                       xenos::TextureFormat format, uint32_t mip,
                       uint32_t& width_blocks_out, uint32_t& height_blocks_out,
                       uint32_t& depth_blocks_out);

// Calculates the number of bytes required to store a single array slice within
// a single mip level - width, height and depth must be obtained via
// GetGuestMipBlocks. align_4kb can be set to false when calculating relatively
// to some offset in the texture rather than the top-left corner of it.
uint32_t GetGuestMipSliceStorageSize(uint32_t width_blocks,
                                     uint32_t height_blocks,
                                     uint32_t depth_blocks, bool is_tiled,
                                     xenos::TextureFormat format,
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
                        xenos::TextureFormat format, uint32_t mip,
                        uint32_t& x_blocks, uint32_t& y_blocks,
                        uint32_t& z_blocks);

// Returns the total size of memory the texture uses starting from its base and
// mip addresses, in bytes (both are optional).
void GetTextureTotalSize(xenos::DataDimension dimension, uint32_t width,
                         uint32_t height, uint32_t depth,
                         xenos::TextureFormat format, bool is_tiled,
                         bool packed_mips, uint32_t mip_max_level,
                         uint32_t* base_size_out, uint32_t* mip_size_out);

// Notes about tiled addresses that can be useful for simplifying and optimizing
// tiling/untiling:
// - Offset2D(X * 32 + x, Y * 32 + y) ==
//       Offset2D(X * 32, Y * 32) + Offset2D(x, y)
//   (true for negative offsets too).
// - Offset3D(X * 32 + x, Y * 32 + y, Z * 8 + z) ==
//       Offset3D(X * 32, Y * 32, Z * 8) + Offset3D(x, y, z)
//   (true for negative offsets too).
// - 2D 32x32 tiles are laid out linearly.
// - 3D tiled texture slices 0:3 and 4:7 are stored separately in memory, in
//   non-overlapping ranges, but addressing in 4:7 is different than in 0:3.
// - Addressing of blocks that are contiguous along X (for tiling/untiling of
//   larger portions at once):
//   - 1bpb - each 8 blocks are laid out sequentially, odd 8 blocks =
//     even 8 blocks + 64 bytes (two R32G32_UINT tiled accesses for one
//     R32G32B32A32_UINT linear access).
//   - 2bpb, 4bpb, 8bpb, 16bpb - each 16 bytes contain blocks laid out
//     sequentially (can tile/untile in R32G32B32A32_UINT portions).
//   - 2bpb - odd 8 blocks = even 8 blocks + 64 bytes.
//   - 4bpb - odd 4 blocks = even 4 blocks + 32 bytes.
//   - 8bpb - odd 2 blocks = even 2 blocks + 32 bytes.
//   - 16bpb - odd block = even block + 32 bytes.
// - Resolve granularity for both offset and size is 8x8 pixels - see
//   xenos::kResolveAlignmentPixels. So, multiple pixels can still be loaded and
//   stored when resolving, taking the contiguous storage patterns described
//   above into account.

int32_t GetTiledOffset2D(int32_t x, int32_t y, uint32_t width,
                         uint32_t bpb_log2);
int32_t GetTiledOffset3D(int32_t x, int32_t y, int32_t z, uint32_t width,
                         uint32_t height, uint32_t bpb_log2);

// Returns four packed TextureSign values swizzled according to the swizzle in
// the fetch constant, so the shader can apply TextureSigns after reading a
// pre-swizzled texture. 0/1 elements are considered unsigned (and not biased),
// however, if all non-constant components are signed, 0/1 are considered signed
// too (because in backends, unsigned and signed textures may use separate host
// textures with different formats, so just one is used for both signed and
// constant components).
uint8_t SwizzleSigns(const xenos::xe_gpu_texture_fetch_t& fetch);
constexpr bool IsAnySignNotSigned(uint8_t packed_signs) {
  return packed_signs != uint32_t(xenos::TextureSign::kSigned) * 0b01010101;
}
constexpr bool IsAnySignSigned(uint8_t packed_signs) {
  // Make signed 00 - check if all are 01, 10 or 11.
  uint32_t xor_signed =
      packed_signs ^ (uint32_t(xenos::TextureSign::kSigned) * 0b01010101);
  return ((xor_signed | (xor_signed >> 1)) & 0b01010101) != 0b01010101;
}

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_UTIL_H_
