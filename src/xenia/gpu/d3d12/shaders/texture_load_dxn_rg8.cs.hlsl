#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXN (16bpb) blocks to 16x4 R8G8 texels.
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
  uint4 r_blocks_0 = uint4(block_0.x, block_1.x, block_2.x, block_3.x);
  uint4 r_blocks_1 = uint4(block_0.y, block_1.y, block_2.y, block_3.y);
  uint4 g_blocks_0 = uint4(block_0.z, block_1.z, block_2.z, block_3.z);
  uint4 g_blocks_1 = uint4(block_0.w, block_1.w, block_2.w, block_3.w);

  // Sort the codes.
  uint4 r_codes_r01 = (r_blocks_0 >> 16u) | ((r_blocks_1 & 0xFFu) << 16u);
  uint4 r_codes_r23 = r_blocks_1 >> 8u;
  uint4 r_weights_8step_r01 = XeDXT5High8StepAlphaWeights(r_codes_r01);
  uint4 r_weights_8step_r23 = XeDXT5High8StepAlphaWeights(r_codes_r23);
  uint4 r_weights_6step_r01 = XeDXT5High6StepAlphaWeights(r_codes_r01);
  uint4 r_weights_6step_r23 = XeDXT5High6StepAlphaWeights(r_codes_r23);
  uint4 g_codes_r01 = (g_blocks_0 >> 16u) | ((g_blocks_1 & 0xFFu) << 16u);
  uint4 g_codes_r23 = g_blocks_1 >> 8u;
  uint4 g_weights_8step_r01 = XeDXT5High8StepAlphaWeights(g_codes_r01);
  uint4 g_weights_8step_r23 = XeDXT5High8StepAlphaWeights(g_codes_r23);
  uint4 g_weights_6step_r01 = XeDXT5High6StepAlphaWeights(g_codes_r01);
  uint4 g_weights_6step_r23 = XeDXT5High6StepAlphaWeights(g_codes_r23);

  // Get the endpoints for mixing.
  uint4 r_end_low = r_blocks_0 & 0xFFu;
  uint4 r_end_high = (r_blocks_0 >> 8u) & 0xFFu;
  uint4 g_end_low = g_blocks_0 & 0xFFu;
  uint4 g_end_high = (g_blocks_0 >> 8u) & 0xFFu;

  // Uncompress and write the rows.
  uint3 texel_index_host = block_index << uint3(2u, 2u, 0u);
  uint texel_offset_host = XeTextureHostLinearOffset(
      texel_index_host, xe_texture_load_size_texels.y,
      xe_texture_load_host_pitch, 2u) + xe_texture_load_host_base;
  for (uint i = 0u; i < 4u; ++i) {
    uint4 r_row = XeDXT5Four8StepBlocksRowToA8(
        r_end_low, r_end_high,
        (i < 2u ? r_weights_8step_r01 : r_weights_8step_r23) >>
            ((i & 1u) * 12u),
        (i < 2u ? r_weights_6step_r01 : r_weights_6step_r23) >>
            ((i & 1u) * 12u));
    uint4 g_row = XeDXT5Four8StepBlocksRowToA8(
        g_end_low, g_end_high,
        (i < 2u ? g_weights_8step_r01 : g_weights_8step_r23) >>
            ((i & 1u) * 12u),
        (i < 2u ? g_weights_6step_r01 : g_weights_6step_r23) >>
            ((i & 1u) * 12u));
    uint4 rg_row_half =
        ((r_row.xxyy >> uint4(0u, 16u, 0u, 16u)) & 0xFFu) |
        (((r_row.xxyy >> uint4(8u, 24u, 8u, 24u)) & 0xFFu) << 16u) |
        (((g_row.xxyy >> uint4(0u, 16u, 0u, 16u)) & 0xFFu) << 8u) |
        (((g_row.xxyy >> uint4(8u, 24u, 8u, 24u)) & 0xFFu) << 24u);
    xe_texture_load_dest.Store4(texel_offset_host, rg_row_half);
    rg_row_half =
        ((r_row.zzww >> uint4(0u, 16u, 0u, 16u)) & 0xFFu) |
        (((r_row.zzww >> uint4(8u, 24u, 8u, 24u)) & 0xFFu) << 16u) |
        (((g_row.zzww >> uint4(0u, 16u, 0u, 16u)) & 0xFFu) << 8u) |
        (((g_row.zzww >> uint4(8u, 24u, 8u, 24u)) & 0xFFu) << 24u);
    xe_texture_load_dest.Store4(texel_offset_host + 16u, rg_row_half);
    if (++texel_index_host.y >= xe_texture_load_size_texels.y) {
      return;
    }
    texel_offset_host += xe_texture_load_host_pitch;
  }
}
