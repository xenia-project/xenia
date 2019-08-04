#include "texture_load.hlsli"

uint4 XeDXT3AAs1111TwoBlocksRowToBGRA4(uint2 halfblocks) {
  // Only 16 bits of half-blocks are used. X contains pixels 0123, Y - 4567 (in
  // the image, halfblocks.y is halfblocks.x + 8).
  // In the row, X contains pixels 01, Y - 23, Z - 45, W - 67.
  // Assuming alpha in LSB and red in MSB, because it's consistent with how
  // DXT1/DXT3/DXT5 color components and CTX1 X/Y are ordered in:
  // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
  // (LSB on the right, MSB on the left.)
  // TODO(Triang3l): Investigate this better, Halo: Reach is the only known game
  // that uses it (for lighting in certain places - one of easy to notice usages
  // is the T-shaped (or somewhat H-shaped) metal beams in the beginning of
  // Winter Contingency), however the contents don't say anything about the
  // channel order.
  uint4 row = (((halfblocks.xxyy >> uint2(3u, 11u).xyxy) & 1u) << 8u) |
                (((halfblocks.xxyy >> uint2(7u, 15u).xyxy) & 1u) << 24u) |
                (((halfblocks.xxyy >> uint2(2u, 10u).xyxy) & 1u) << 4u) |
                (((halfblocks.xxyy >> uint2(6u, 14u).xyxy) & 1u) << 20u) |
                ((halfblocks.xxyy >> uint2(1u, 9u).xyxy) & 1u) |
                (((halfblocks.xxyy >> uint2(5u, 13u).xyxy) & 1u) << 16u) |
                (((halfblocks.xxyy >> uint2(0u, 8u).xyxy) & 1u) << 12u) |
                (((halfblocks.xxyy >> uint2(4u, 12u).xyxy) & 1u) << 28u);
  row |= row << 1u;
  row |= row << 2u;
  return row;
}

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 DXT3A-as-1111 blocks to 8x4 B4G4R4A4 texels.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 1u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 8u, 3u);
  uint4 blocks = uint4(xe_texture_load_source.Load2(block_offsets_guest.x),
                       xe_texture_load_source.Load2(block_offsets_guest.y));
  blocks = XeByteSwap(blocks, xe_texture_load_endianness);
  uint3 texel_index_host = block_index << uint3(2u, 2u, 0u);
  uint texel_offset_host = XeTextureHostLinearOffset(
      texel_index_host, xe_texture_load_size_texels.y,
      xe_texture_load_host_pitch, 2u) + xe_texture_load_host_base;
  for (uint i = 0u; i < 4u; ++i) {
    xe_texture_load_dest.Store4(
        texel_offset_host,
        XeDXT3AAs1111TwoBlocksRowToBGRA4(
            (i >= 2u ? blocks.yw : blocks.xz) >> ((i & 1u) << 4u)));
    if (++texel_index_host.y >= xe_texture_load_size_texels.y) {
      return;
    }
    texel_offset_host += xe_texture_load_host_pitch;
  }
}
