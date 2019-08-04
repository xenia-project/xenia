#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXT5A (8bpb) blocks to 16x4 R8 texels.
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
  uint4 r_blocks_0 = uint4(blocks_01.xz, blocks_23.xz);
  uint4 r_blocks_1 = uint4(blocks_01.yw, blocks_23.yw);

  // Sort the codes.
  uint4 r_codes_r01 = (r_blocks_0 >> 16u) | ((r_blocks_1 & 0xFFu) << 16u);
  uint4 r_codes_r23 = r_blocks_1 >> 8u;
  uint4 r_weights_8step_r01 = XeDXT5High8StepAlphaWeights(r_codes_r01);
  uint4 r_weights_8step_r23 = XeDXT5High8StepAlphaWeights(r_codes_r23);
  uint4 r_weights_6step_r01 = XeDXT5High6StepAlphaWeights(r_codes_r01);
  uint4 r_weights_6step_r23 = XeDXT5High6StepAlphaWeights(r_codes_r23);

  // Get the endpoints for mixing.
  uint4 r_end_low = r_blocks_0 & 0xFFu;
  uint4 r_end_high = (r_blocks_0 >> 8u) & 0xFFu;

  // Uncompress and write the rows.
  uint3 texel_index_host = block_index << uint3(2u, 2u, 0u);
  uint texel_offset_host = XeTextureHostLinearOffset(
      texel_index_host, xe_texture_load_size_texels.y,
      xe_texture_load_host_pitch, 1u) + xe_texture_load_host_base;
  for (uint i = 0u; i < 4u; ++i) {
    xe_texture_load_dest.Store4(texel_offset_host, XeDXT5Four8StepBlocksRowToA8(
        r_end_low, r_end_high,
        (i < 2u ? r_weights_8step_r01 : r_weights_8step_r23) >>
            ((i & 1u) * 12u),
        (i < 2u ? r_weights_6step_r01 : r_weights_6step_r23) >>
            ((i & 1u) * 12u)));
    if (++texel_index_host.y >= xe_texture_load_size_texels.y) {
      return;
    }
    texel_offset_host += xe_texture_load_host_pitch;
  }
}
