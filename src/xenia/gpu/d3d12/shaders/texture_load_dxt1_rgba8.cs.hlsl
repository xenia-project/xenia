#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

void XeDXT1FourTransBlocksRowToRGBA8(
    uint4 rgb_10b_low, uint4 rgb_10b_high, uint4 weights, out uint4 row_0,
    out uint4 row_1, out uint4 row_2, out uint4 row_3) {
  const uint4 weights_shifts_low = uint4(0u, 2u, 4u, 6u);
  const uint4 weights_shifts_high = uint4(1u, 3u, 5u, 7u);
  // Whether the texel is (RGB0+RGB1)/2 - divide the weighted sum by 2 (shift
  // right by 1) if it is.
  uint4 weights_sums_log2 = weights & ((weights & 0xAAAAAAAAu) >> 1u);
  // Whether the texel is opaque.
  uint4 weights_alpha =
      (weights & 0x55555555u) | ((weights & 0xAAAAAAAAu) >> 1u);
  uint4 block_rgb_10b =
      ((weights.xxxx >> weights_shifts_low) & 1u) * rgb_10b_low.x +
      ((weights.xxxx >> weights_shifts_high) & 1u) * rgb_10b_high.x;
  uint4 block_rgb_shift = (weights_sums_log2.xxxx >> weights_shifts_low) & 1u;
  row_0 = ((block_rgb_10b & 1023u) >> block_rgb_shift) +
          ((((block_rgb_10b >> 10u) & 1023u) >> block_rgb_shift) << 8u) +
          (((block_rgb_10b >> 20u) >> block_rgb_shift) << 16u) +
          (((weights_alpha.xxxx >> weights_shifts_low) & 1u) * 0xFF000000u);
  block_rgb_10b =
      ((weights.yyyy >> weights_shifts_low) & 1u) * rgb_10b_low.y +
      ((weights.yyyy >> weights_shifts_high) & 1u) * rgb_10b_high.y;
  block_rgb_shift = (weights_sums_log2.yyyy >> weights_shifts_low) & 1u;
  row_1 = ((block_rgb_10b & 1023u) >> block_rgb_shift) +
          ((((block_rgb_10b >> 10u) & 1023u) >> block_rgb_shift) << 8u) +
          (((block_rgb_10b >> 20u) >> block_rgb_shift) << 16u) +
          (((weights_alpha.yyyy >> weights_shifts_low) & 1u) * 0xFF000000u);
  block_rgb_10b =
      ((weights.zzzz >> weights_shifts_low) & 1u) * rgb_10b_low.z +
      ((weights.zzzz >> weights_shifts_high) & 1u) * rgb_10b_high.z;
  block_rgb_shift = (weights_sums_log2.zzzz >> weights_shifts_low) & 1u;
  row_2 = ((block_rgb_10b & 1023u) >> block_rgb_shift) +
          ((((block_rgb_10b >> 10u) & 1023u) >> block_rgb_shift) << 8u) +
          (((block_rgb_10b >> 20u) >> block_rgb_shift) << 16u) +
          (((weights_alpha.zzzz >> weights_shifts_low) & 1u) * 0xFF000000u);
  block_rgb_10b =
      ((weights.wwww >> weights_shifts_low) & 1u) * rgb_10b_low.w +
      ((weights.wwww >> weights_shifts_high) & 1u) * rgb_10b_high.w;
  block_rgb_shift = (weights_sums_log2.wwww >> weights_shifts_low) & 1u;
  row_3 = ((block_rgb_10b & 1023u) >> block_rgb_shift) +
          ((((block_rgb_10b >> 10u) & 1023u) >> block_rgb_shift) << 8u) +
          (((block_rgb_10b >> 20u) >> block_rgb_shift) << 16u) +
          (((weights_alpha.wwww >> weights_shifts_low) & 1u) * 0xFF000000u);
}

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXT1 (8bpb) blocks to 16x4 R8G8B8A8 texels.
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

  uint4 codes = uint4(blocks_01.yw, blocks_23.yw);
  // Sort the color indices so they can be used as weights for the second
  // endpoint in the opaque mode.
  uint4 weights_opaque_high = XeDXTHighColorWeights(codes);
  // Sort the color indices so bits of them can be used as endpoint weights, and
  // AND of those bits can be used as the right shift amount for mixing the two
  // colors in the punchthrough mode.
  // Initially 00 = 1:0, 01 = 0:1, 10 = 1:1, 11 = 0:0.
  // 00 = 0:0, 01 = 1:1, 10 = 0:1, 11 = 1:0.
  uint4 weights_trans = ~codes;
  // 00 = 0:0, 01 = 1:0, 10 = 0:1, 11 = 1:1.
  weights_trans ^= (weights_trans & 0x55555555u) << 1u;

  // Get endpoint RGB for mixing, as 8-bit components in 10-bit sequences.
  uint4 rgb_565 = uint4(blocks_01.xz, blocks_23.xz);
  uint4 rgb_10b_low, rgb_10b_high;
  XeDXTColorEndpointsTo8In10(rgb_565, rgb_10b_low, rgb_10b_high);

  // Get modes for each block.
  bool4 is_trans = (rgb_565 & 0xFFFFu) <= (rgb_565 >> 16u);

  // Uncompress and write the rows.
  uint3 texel_index_host = block_index << uint3(2u, 2u, 0u);
  uint texel_offset_host = XeTextureHostLinearOffset(
      texel_index_host, xe_texture_load_size_texels.y,
      xe_texture_load_host_pitch, 4u) + xe_texture_load_host_base;
  for (uint i = 0u; i < 4u; ++i) {
    uint4 row_opaque_0, row_opaque_1, row_opaque_2, row_opaque_3;
    XeDXTFourBlocksRowToRGB8(rgb_10b_low, rgb_10b_high,
                             weights_opaque_high >> (i * 8u), row_opaque_0,
                             row_opaque_1, row_opaque_2, row_opaque_3);
    row_opaque_0 |= 0xFF000000u;
    row_opaque_1 |= 0xFF000000u;
    row_opaque_2 |= 0xFF000000u;
    row_opaque_3 |= 0xFF000000u;
    uint4 row_trans_0, row_trans_1, row_trans_2, row_trans_3;
    XeDXT1FourTransBlocksRowToRGBA8(rgb_10b_low, rgb_10b_high,
                                    weights_trans >> (i * 8u), row_trans_0,
                                    row_trans_1, row_trans_2, row_trans_3);
    xe_texture_load_dest.Store4(texel_offset_host,
                                is_trans.x ? row_trans_0 : row_opaque_0);
    xe_texture_load_dest.Store4(texel_offset_host + 16u,
                                is_trans.y ? row_trans_1 : row_opaque_1);
    xe_texture_load_dest.Store4(texel_offset_host + 32u,
                                is_trans.z ? row_trans_2 : row_opaque_2);
    xe_texture_load_dest.Store4(texel_offset_host + 48u,
                                is_trans.w ? row_trans_3 : row_opaque_3);
    if (++texel_index_host.y >= xe_texture_load_size_texels.y) {
      return;
    }
    texel_offset_host += xe_texture_load_host_pitch;
  }
}
