#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXT5 (16bpb) blocks to 16x4 R8G8B8A8 texels.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 16u, 4u);
  uint4 block_0 = xe_texture_load_source.Load4(block_offsets_guest.x);
  uint4 block_1 = xe_texture_load_source.Load4(block_offsets_guest.y);
  uint4 block_2 = xe_texture_load_source.Load4(block_offsets_guest.z);
  uint4 block_3 = xe_texture_load_source.Load4(block_offsets_guest.w);
  block_0 = XeByteSwap(block_0, xe_texture_load_endianness);
  block_1 = XeByteSwap(block_1, xe_texture_load_endianness);
  block_2 = XeByteSwap(block_2, xe_texture_load_endianness);
  block_3 = XeByteSwap(block_3, xe_texture_load_endianness);
  uint4 alpha_blocks_0 = uint4(block_0.x, block_1.x, block_2.x, block_3.x);
  uint4 alpha_blocks_1 = uint4(block_0.y, block_1.y, block_2.y, block_3.y);

  uint4 rgb_codes = uint4(block_0.w, block_1.w, block_2.w, block_3.w);
  // Sort the color indices so they can be used as weights for the second
  // endpoint.
  uint4 rgb_weights_high = XeDXTHighColorWeights(rgb_codes);
  // Sort the alpha indices.
  uint4 alpha_codes_r01 =
      (alpha_blocks_0 >> 16u) | ((alpha_blocks_1 & 0xFFu) << 16u);
  uint4 alpha_codes_r23 = alpha_blocks_1 >> 8u;
  uint4 alpha_weights_8step_r01 = XeDXT5High8StepAlphaWeights(alpha_codes_r01);
  uint4 alpha_weights_8step_r23 = XeDXT5High8StepAlphaWeights(alpha_codes_r23);
  uint4 alpha_weights_6step_r01 = XeDXT5High6StepAlphaWeights(alpha_codes_r01);
  uint4 alpha_weights_6step_r23 = XeDXT5High6StepAlphaWeights(alpha_codes_r23);

  // Get the endpoints for mixing, as 8-bit components in 10-bit sequences.
  uint4 rgb_565 = uint4(block_0.z, block_1.z, block_2.z, block_3.z);
  uint4 rgb_10b_low, rgb_10b_high;
  XeDXTColorEndpointsTo8In10(rgb_565, rgb_10b_low, rgb_10b_high);
  // Get the alpha endpoints.
  uint4 alpha_end_low = alpha_blocks_0 & 0xFFu;
  uint4 alpha_end_high = (alpha_blocks_0 >> 8u) & 0xFFu;

  // Uncompress and write the rows.
  uint3 texel_index_host = block_index << uint3(2u, 2u, 0u);
  uint texel_offset_host = XeTextureHostLinearOffset(
      texel_index_host, xe_texture_load_size_texels.y,
      xe_texture_load_host_pitch, 4u) + xe_texture_load_host_base;
  for (uint i = 0u; i < 4u; ++i) {
    uint4 row_0, row_1, row_2, row_3;
    XeDXTFourBlocksRowToRGB8(rgb_10b_low, rgb_10b_high,
                             rgb_weights_high >> (i * 8u),
                             row_0, row_1, row_2, row_3);
    uint4 alpha_row = XeDXT5Four8StepBlocksRowToA8(
        alpha_end_low, alpha_end_high,
        (i < 2u ? alpha_weights_8step_r01 : alpha_weights_8step_r23) >>
            ((i & 1u) * 12u),
        (i < 2u ? alpha_weights_6step_r01 : alpha_weights_6step_r23) >>
            ((i & 1u) * 12u));
    xe_texture_load_dest.Store4(
        texel_offset_host,
        row_0 | ((alpha_row.xxxx << uint4(24u, 16u, 8u, 0u)) & 0xFF000000u));
    xe_texture_load_dest.Store4(
        texel_offset_host + 16u,
        row_1 | ((alpha_row.yyyy << uint4(24u, 16u, 8u, 0u)) & 0xFF000000u));
    xe_texture_load_dest.Store4(
        texel_offset_host + 32u,
        row_2 | ((alpha_row.zzzz << uint4(24u, 16u, 8u, 0u)) & 0xFF000000u));
    xe_texture_load_dest.Store4(
        texel_offset_host + 48u,
        row_3 | ((alpha_row.wwww << uint4(24u, 16u, 8u, 0u)) & 0xFF000000u));
    if (++texel_index_host.y >= xe_texture_load_size_texels.y) {
      return;
    }
    texel_offset_host += xe_texture_load_host_pitch;
  }
}
