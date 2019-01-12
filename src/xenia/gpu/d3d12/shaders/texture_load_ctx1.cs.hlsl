#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

// http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
// CXT1 is like DXT3/5 color, but 2-component and with 8:8 endpoints rather than
// 5:6:5.
//
// Dword 1:
// rrrrrrrr gggggggg
// RRRRRRRR GGGGGGGG
// Dword 2:
// AA BB CC DD
// EE FF GG HH
// II JJ KK LL
// MM NN OO PP

void XeCTX1FourBlocksRowToR8G8(uint4 end_low_rr00gg00, uint4 end_high_rr00gg00,
                               uint4 weights_high, out uint4 row_01,
                               out uint4 row_23) {
  uint4 weights_low = ~weights_high;
  uint4 row_3aaaa = (weights_low & 3u) * end_low_rr00gg00 +
                    (weights_high & 3u) * end_high_rr00gg00;
  uint4 row_3bbbb = ((weights_low >> 2u) & 3u) * end_low_rr00gg00 +
                    ((weights_high >> 2u) & 3u) * end_high_rr00gg00;
  uint4 row_3cccc = ((weights_low >> 4u) & 3u) * end_low_rr00gg00 +
                    ((weights_high >> 4u) & 3u) * end_high_rr00gg00;
  uint4 row_3dddd = ((weights_low >> 6u) & 3u) * end_low_rr00gg00 +
                    ((weights_high >> 6u) & 3u) * end_high_rr00gg00;
  uint4 row_half_3acac = uint4(row_3aaaa.xy, row_3cccc.xy).xzyw;
  uint4 row_half_3bdbd = uint4(row_3bbbb.xy, row_3dddd.xy).xzyw;
  // R0A G0A R0B G0B | R0C G0C R0D G0D | R1A G1A R1B G1B | R1C G1C R1D G1D
  row_01 = ((row_half_3acac & 0xFFFFu) / 3u) |
           (((row_half_3acac >> 16u) / 3u) << 8u) |
           (((row_half_3bdbd & 0xFFFFu) / 3u) << 16u) |
           (((row_half_3bdbd >> 16u) / 3u) << 24u);
  row_half_3acac = uint4(row_3aaaa.zw, row_3cccc.zw).xzyw;
  row_half_3bdbd = uint4(row_3bbbb.zw, row_3dddd.zw).xzyw;
  // R2A G2A R2B G2B | R2C G2C R2D G2D | R3A G3A R3B G3B | R3C G3C R3D G3D
  row_23 = ((row_half_3acac & 0xFFFFu) / 3u) |
           (((row_half_3acac >> 16u) / 3u) << 8u) |
           (((row_half_3bdbd & 0xFFFFu) / 3u) << 16u) |
           (((row_half_3bdbd >> 16u) / 3u) << 24u);
}

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 CTX1 (8bpb) blocks to 16x4 R8G8 texels.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 8u, 3u);
  uint4 blocks_01 = uint4(xe_texture_load_source.Load2(block_offsets_guest.x),
                          xe_texture_load_source.Load2(block_offsets_guest.y));
  uint4 blocks_23 = uint4(xe_texture_load_source.Load2(block_offsets_guest.z),
                          xe_texture_load_source.Load2(block_offsets_guest.w));
  blocks_01 = XeByteSwap(blocks_01, xe_texture_load_endianness);
  blocks_23 = XeByteSwap(blocks_23, xe_texture_load_endianness);

  // Unpack the endpoints as:
  // 0x00g000r0 0x00g100r1 0x00g200r2 0x00g300r3
  // 0x00G000R0 0x00G100R1 0x00G200R2 0x00G300R3
  // so they can be multiplied by their weights allowing overflow
  // (R is in the higher bits, according to how this format is used in Halo 3).
  uint4 end_packed = uint4(blocks_01.xz, blocks_23.xz);
  uint4 end_low_rr00gg00 =
      ((end_packed & 0xFF00u) >> 8u) | ((end_packed & 0xFFu) << 16u);
  uint4 end_high_rr00gg00 = (end_packed >> 24u) | (end_packed & 0xFF0000u);

  // Sort the color indices so they can be used as weights for the second
  // endpoint.
  uint4 weights_high = XeDXTHighColorWeights(uint4(blocks_01.yw, blocks_23.yw));

  // Uncompress and write the rows.
  uint3 texel_index_host = block_index << uint3(2u, 2u, 0u);
  uint texel_offset_host = XeTextureHostLinearOffset(
      texel_index_host, xe_texture_load_size_texels.y,
      xe_texture_load_host_pitch, 2u) + xe_texture_load_host_base;
  for (uint i = 0u; i < 4u; ++i) {
    uint4 row_01, row_23;
    XeCTX1FourBlocksRowToR8G8(end_low_rr00gg00, end_high_rr00gg00,
                              weights_high >> (i * 8u), row_01, row_23);
    xe_texture_load_dest.Store4(texel_offset_host, row_01);
    xe_texture_load_dest.Store4(texel_offset_host + 16u, row_23);
    if (++texel_index_host.y >= xe_texture_load_size_texels.y) {
      return;
    }
    texel_offset_host += xe_texture_load_host_pitch;
  }
}
