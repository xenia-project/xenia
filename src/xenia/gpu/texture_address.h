/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_ADDRESS_H_
#define XENIA_GPU_TEXTURE_ADDRESS_H_

#include <cstdint>

#include "xenia/base/assert.h"

namespace xe {
namespace gpu {
namespace texture_address {

// Cases for regression testing of specific details of texture addressing:
// - 3D tiled textures:
//   - 4E4D07E9: Font, can be seen in various places such as the subtitles for
//     the intro, as well as the text in the continue menu. 512x512x16 DXT5,
//     though in the English version, only the first 3 slices seem to contain
//     glyphs.
// - Cubemaps with mips:
//   - 415607D1: Skybox.
// - Linear textures with packed mips:
//   - 545407E0: Many DXT-compressed textures. Includes examples of the base
//     level itself being in the packed mip tail (the 8x8 DXT1 texture of the
//     two orange lights on the game machines on the first level).
// - Mismatching pitch and width:
//   - 584109FF: 1280x menu background with a pitch of 1408.
// - Tight memory allocation:
//   - 4E4D083E: Memory allocated for a linear 1280x720 8_8_8_8 texture tightly,
//     must not access the data in the padding after aligning to 32x32
//     (resulting in 1280x736) via the guest CPU memory mappings.

// Level pitch (or width for non-base mip levels), height, and depth (for 3D
// textures - stacked 2D textures use the exact array layer count for the stride
// at every level, and cubemaps are normally stored with a stride of 6 faces)
// are rounded up by the hardware for the purposes of calculating strides of
// subresources and of macro tiles within a subresource to:
// - 32x32 (2D) or 32x32x4 (3D) blocks. This is applicable to both tiled and
//   linear textures. Note that for 3D, this is larger than a macro tile
//   (32x16x4).
// - Power of two, for non-base mip levels.
// - 256 bytes for rows of linear textures.
//
// Note that for estimating the upper bound of memory used by a texture, merely
// multiplying the rounded pitch, height and depth is not enough. Sometimes,
// especially for linear textures, aligning the dimensions to this amount
// results in more memory pages assumed to be needed, and the game may not
// always actually allocate the memory for that padding. An example is 4E4D083E,
// which has a 1280x720 8_8_8_8 linear texture, and it only allocates memory for
// exactly 1280x720 elements (900 pages). However, if the dimensions are aligned
// (1280x736), the texture is assumed to take up 920 pages, and when accessing
// its memory respecting guest allocations and protection, access violations
// occur when those unallocated 20 pages are read.
//
// Another scenario where the product of the aligned dimensions may not be an
// accurate estimate (but in this case too small rather than too large) is when
// the width exceeds the pitch.
constexpr unsigned int kStoragePitchHeightAlignmentBlocksLog2 = 5;
constexpr uint32_t kStoragePitchHeightAlignmentBlocks =
    uint32_t(1) << kStoragePitchHeightAlignmentBlocksLog2;
constexpr unsigned int kStorageDepthAlignmentBlocksLog2 = 2;
constexpr uint32_t kStorageDepthAlignmentBlocks =
    uint32_t(1) << kStorageDepthAlignmentBlocksLog2;

// 2D tiling from:
// https://github.com/BinomialLLC/crunch/blob/36479bc697be19168daafbf15f47f3c60ccec004/inc/crn_decomp.h#L4107
// 3D tiling differences discovered via game reverse engineering.
//
// For easier calculation of various properties of addresses, offsetting within
// smaller regions, etc., the bits of a tiled address are decomposed into
// logical parts in this code. The meanings of the bits may not be entirely
// accurate, but they're based on the structure of a tiled address in AMD's
// AddrLib:
// https://github.com/GPUOpen-Drivers/pal/tree/7ce51b199c72021b8d027c50b55da515fd9b2e0b/src/core/imported/addrlib/src/r800
// (see `EgBasedLib::ComputeSurfaceAddrFromCoordMacroTiled` primarily).
//
// Additional information about the memory layout of AMD/ATI GPUs, specifically
// channels and banks, can be found in the AMD Accelerated Parallel Processing
// SDK OpenCL Optimization Guide:
// https://docs.amd.com/v/u/en-US/AMD_OpenCL_Programming_Optimization_Guide2
//
// In Xenia, the terminology differs from the AddrLib to avoid confusion with
// other concepts under the same names in Xenia. With Xenia naming, a hardware
// tiled address consists of:
//
//  [:12] = `outer_inner_bytes[:8]` (page)
//   [11] = `bank`
// [10:8] = `outer_inner_bytes[7:5]` (bank interleave upper bits)
//  [7:6] = `pipe`
//    [5] = `outer_inner_bytes[4]` (pipe interleave upper bit)
//    [4] = `y & 1` (Y least significant bit)
//  [3:0] = `outer_inner_bytes[3:0]` (pipe interleave lower bits)
//
// The "outer_inner" part is scaled by the number of bytes per block:
// `outer_inner_bytes = outer_inner_blocks << log2(bytes_per_block)`
// where `outer_inner_blocks` is a sum (or bitwise OR) of two terms:
// - `outer_blocks`: Origin of the 32x32 (2D) or 32x16x4 (3D) macro tile within
//   the subresource.
//   Macro tiles are treated as laid out macro-tile-linearly in the calculation
//   of this part, with Z being the most significant term, Y in between, and X
//   being the least significant.
//   2D:
//   (x[:5] + ceil(pitch / 32) * y[:5]) << 6
//   3D:
//   (x[:5] + ceil(pitch / 32) * (y[:4] + 2 * ceil(height / 32) * z[:2])) << 7
// - `inner_blocks`: Offset of 1x2x1 blocks within a 8x16 (2D) or 8x8x4 micro
//   tile.
//   Though the LSB of Y always goes to the bit 4 of the memory address, within
//   a micro tile, {X, Y[:1], Z} can be treated as linear in the calculation of
//   this part, with Z being the most significant, Y in between, and X being the
//   least significant.
//   2D:
//   - inner_blocks[5:3] = y[3:1]
//   - inner_blocks[2:0] = x[2:0]
//   3D:
//   - inner_blocks[6:5] = z[1:0]
//   - inner_blocks[4:3] = y[2:1]
//   - inner_blocks[2:0] = x[2:0]
//
// However, the "outer/inner" part doesn't include the offset of the micro tile
// within the macro tile: X[4:3], and Y[4] (2D) or Y[3] (3D). These are encoded
// in the pipe and bank selection bits (with certain bits flipped depending on
// Y[3] and Z[2] to avoid access conflicts). According to the AMD APP SDK
// programming guides, memory is split into banks, and banks have multiple
// channels (also known as pipes in the AddrLib), and tiling is designed for
// avoiding bank and channel conflicts during texture sampling.
// 2D:
// - bank = y[4]
// - pipe[1] = x[4] ^ y[3]
// - pipe[0] = x[3]
// 3D:
// - bank = y[3] ^ z[2]
// - pipe[1] = x[4] ^ y[3] ^ z[2]
// - pipe[0] = x[3]
//
// "Page" refers to a 4 KB RAM row. While it's not exactly a correct term, as
// physical memory is non-paged, it was chosen to distinguish from rows in
// general (such as rows of elements), and its size matches the small page size
// on the CPU. Texture subresource base addresses are aligned to this amount,
// and pipe and bank selection bits are below it. These bits are particularly
// important for estimating the extent of the texture for the purposes of
// synchronizing GPU resource memory on the host due to it matching the CPU page
// size.
//
// The tiled address functions can be used in both positive and negative
// directions. One example of negative offsetting done by Direct3D 9 itself on
// the Xbox 360 is resolving EDRAM render target data to texture subregions,
// primarily in tiled rendering. The GPU uses the same source EDRAM render
// target and destination texture pixel coordinates while resolving, and to
// resolve to a different location, Direct3D 9 adjusts the destination texture
// base pointer, and it can do that in both directions. The granularity of such
// offsetting is limited, however, by the requirement that relative memory
// addresses for relative pixel coordinates must stay the same after the
// offsetting - more specifically, it's limited by the page size and the macro
// tile size, whichever is greater.

constexpr unsigned int kMacroTileWidthLog2 = 5;
constexpr uint32_t kMacroTileWidth = uint32_t(1) << kMacroTileWidthLog2;
constexpr unsigned int kMacroTileHeight2DLog2 = 5;
constexpr uint32_t kMacroTileHeight2D = uint32_t(1) << kMacroTileHeight2DLog2;
constexpr unsigned int kMacroTileHeight3DLog2 = 4;
constexpr uint32_t kMacroTileHeight3D = uint32_t(1) << kMacroTileHeight3DLog2;
constexpr unsigned int kMacroTileDepthLog2 = 2;
constexpr uint32_t kMacroTileDepth = uint32_t(1) << kMacroTileDepthLog2;

template <typename Address>
Address TiledCombine(const Address outer_inner_bytes, const uint32_t bank,
                     const uint32_t pipe, const uint32_t y_lsb) {
  assert_true(bank <= 0b1);
  assert_true(pipe <= 0b11);
  assert_true(y_lsb <= 0b1);
  return int32_t((y_lsb << 4) | (pipe << 6) | (bank << 11)) |
         (outer_inner_bytes & 0b1111) |
         (((outer_inner_bytes >> 4) & 0b1) << 5) |
         (((outer_inner_bytes >> 5) & 0b111) << 8) |
         (outer_inner_bytes >> 8 << 12);
}

// The absolute of the return value is below 2^31 (for 16384x8192 with 16 bytes
// per block, though it's not even clear if the hardware allows pitches greater
// than 8192, the maximum texture size, but the pitch field in a texture fetch
// constant is 14 bits wide).
inline int32_t Tiled2D(const int32_t x, const int32_t y,
                       const uint32_t pitch_aligned,
                       const unsigned int bytes_per_block_log2) {
  // Expecting that all the needed rounding (not only to 32x32, but also to a
  // power of two for mips) is done before the call so this function works with
  // the actual storage dimensions.
  assert_zero(pitch_aligned & (kStoragePitchHeightAlignmentBlocks - 1));
  const int32_t outer_blocks =
      ((y >> kMacroTileHeight2DLog2) *
           int32_t(pitch_aligned >> kMacroTileWidthLog2) +
       (x >> kMacroTileWidthLog2))
      << 6;
  const int32_t inner_blocks = (((y >> 1) & 0b111) << 3) | (x & 0b111);
  const int32_t outer_inner_bytes = (outer_blocks | inner_blocks)
                                    << bytes_per_block_log2;
  const uint32_t bank = (y >> 4) & 0b1;
  const uint32_t pipe = ((x >> 3) & 0b11) ^ (((y >> 3) & 0b1) << 1);
  return TiledCombine(outer_inner_bytes, bank, pipe, y & 1);
}

// The absolute of the return value is below 2^39 (for 16384x2048x1024 with 16
// bytes per block). This also means that 32 (more precisely, 27) bits is always
// enough for the page index within a subresource.
inline int64_t Tiled3D(const int32_t x, const int32_t y, const int32_t z,
                       const uint32_t pitch_aligned,
                       const uint32_t height_aligned,
                       const unsigned int bytes_per_block_log2) {
  // Expecting that all the needed rounding (not only to 32x32x4, but also to a
  // power of two for mips) is done before the call so this function works with
  // the actual storage dimensions.
  assert_zero(pitch_aligned & (kStoragePitchHeightAlignmentBlocks - 1));
  assert_zero(height_aligned & (kStoragePitchHeightAlignmentBlocks - 1));
  // The absolute of `outer_blocks` is below (1024 / 4) * (2048 / 16) *
  // (16384 / 32) * 128 = 2^31.
  const int32_t outer_blocks =
      ((((z >> kMacroTileDepthLog2) *
             (height_aligned >> kMacroTileHeight3DLog2) +
         (y >> kMacroTileHeight3DLog2)) *
        int32_t(pitch_aligned >> kMacroTileWidthLog2)) +
       (x >> kMacroTileWidthLog2))
      << 7;
  const int32_t inner_blocks =
      ((z & 0b11) << 5) | (((y >> 1) & 0b11) << 3) | (x & 0b111);
  const int64_t outer_inner_bytes = int64_t(outer_blocks | inner_blocks)
                                    << bytes_per_block_log2;
  const uint32_t bank = ((y >> 3) ^ (z >> 2)) & 0b1;
  const uint32_t pipe = ((x >> 3) & 0b11) ^ (bank << 1);
  return TiledCombine(outer_inner_bytes, bank, pipe, y & 1);
}

}  // namespace texture_address
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_ADDRESS_H_
