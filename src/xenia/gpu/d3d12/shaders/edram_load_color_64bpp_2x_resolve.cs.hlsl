#include "edram_load_store.hlsli"

[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Host pixels within samples in the EDRAM buffer -> samples within host
  // pixels in texture data (loading for AA resolve, not for rendering).
  // 1 group = 1 tile, 1 thread writes 8 host pixels for 2 samples
  // (horizontally).
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 2u;
  uint4 edram_offsets;
  edram_offsets.x = XeEDRAMOffset64bpp(xe_group_id.xy, tile_sample_index);
  edram_offsets.yzw = uint3(16u, 32u, 48u) + edram_offsets.x;
  // 4 host pixels of the left guest pixel (1x/2x AA) / sample (4x AA).
  uint4 pixels_0_01 = xe_edram_load_store_source.Load4(edram_offsets.x);
  uint4 pixels_0_23 = xe_edram_load_store_source.Load4(edram_offsets.y);
  // 4 host pixels of the right guest pixel/sample.
  uint4 pixels_1_01 = xe_edram_load_store_source.Load4(edram_offsets.z);
  uint4 pixels_1_23 = xe_edram_load_store_source.Load4(edram_offsets.w);

  uint2 sample_count_log2 = XeEDRAMSampleCountLog2();
  [branch] if (sample_count_log2.x != 0u) {
    // We already have data for 2 horizontal samples - change "host pixels
    // within samples" to "samples within host pixels" horizontally.
    // pixels_0 is 01: s0pTL s0pTR, 23: s0pBL s0pBR currently, and
    // pixels_1 is 01: s2pTL s2pTR, 23: s2pBL s2pBR.
    // Change that to:
    // pixels_0 being 01: s0pTL s2pTL, 23: s0pBL s2pBL and
    // pixels_1 being 01: s0pTR s2pTR, 23: s0pBR s2pBR.
    uint4 pixels_temp = uint4(pixels_0_01.zw, pixels_0_23.zw);
    pixels_0_01.zw = pixels_1_01.xy;
    pixels_0_23.zw = pixels_1_23.xy;
    pixels_1_01.xy = pixels_temp.xy;
    pixels_1_23.xy = pixels_temp.zw;
  }
  // Do similar interleaving for rows.
  uint rt_offset = ((xe_thread_id.y & ~sample_count_log2.y) * 2u +
                    (xe_thread_id.y & sample_count_log2.y)) *
                   xe_edram_rt_color_depth_pitch + xe_thread_id.x * 32u;
  xe_edram_load_store_dest.Store4(rt_offset, pixels_0_01);
  xe_edram_load_store_dest.Store4(rt_offset + 16u, pixels_1_01);
  rt_offset += (1u << sample_count_log2.y) * xe_edram_rt_color_depth_pitch;
  xe_edram_load_store_dest.Store4(rt_offset, pixels_0_23);
  xe_edram_load_store_dest.Store4(rt_offset + 16u, pixels_1_23);
}
