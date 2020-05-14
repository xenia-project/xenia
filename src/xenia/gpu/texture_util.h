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

// Extracts the size from the fetch constant, and also cleans up addresses and
// mip range based on real presence of the base level and mips. Returns 6 faces
// for cube textures.
void GetSubresourcesFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, uint32_t* width_out,
    uint32_t* height_out, uint32_t* depth_or_faces_out, uint32_t* base_page_out,
    uint32_t* mip_page_out, uint32_t* mip_min_level_out,
    uint32_t* mip_max_level_out,
    TextureFilter sampler_mip_filter = TextureFilter::kUseFetchConst);

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
                         uint32_t* base_size_out, uint32_t* mip_size_out);

// Notes about tiled addresses that can be useful for simplifying and optimizing
// tiling/untiling:
// - Offset2D(X * 32 + x, Y * 32 + y) ==
//       Offset2D(X * 32, Y * 32) + Offset2D(x, y)
//   (true for negative offsets too).
// - Offset3D(X * 32 + x, Y * 32 + y, Z * 8 + z) ==
//       Offset3D(X * 32, Y * 32, Z * 8) + Offset3D(x, y, z)
//   (true for negative offsets too).
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
// - Resolve granularity for both offset and size is 8x8 pixels
//   (GPU_RESOLVE_ALIGNMENT - for example, Halo 3 resolves a 24x16 region for a
//   18x10 texture, 8x8 region for a 1x1 texture).
//   https://github.com/jmfauvel/CSGO-SDK/blob/master/game/client/view.cpp#L944
//   https://github.com/stanriders/hl2-asw-port/blob/master/src/game/client/vgui_int.cpp#L901
//   So, multiple pixels can still be loaded/stored when resolving, taking
//   contiguous storage patterns into account.

int32_t GetTiledOffset2D(int32_t x, int32_t y, uint32_t width,
                         uint32_t bpb_log2);
int32_t GetTiledOffset3D(int32_t x, int32_t y, int32_t z, uint32_t width,
                         uint32_t height, uint32_t bpb_log2);

// Returns four packed TextureSign values swizzled according to the swizzle in
// the fetch constant, so the shader can apply TextureSigns after reading a
// pre-swizzled texture. 0/1 elements are considered unsigned (and not biased),
// however, if all non-constant components are signed, 0/1 are considered signed
// too (because in backends, unsigned and signed textures may use separate views
// with different formats, so just one view is used for both signed and constant
// components).
uint32_t SwizzleSigns(const xenos::xe_gpu_texture_fetch_t& fetch,
                      bool* any_unsigned_out = nullptr,
                      bool* any_signed_out = nullptr);

}  // namespace texture_util
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_UTIL_H_
