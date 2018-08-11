#include "edram_load_store.hlsli"

[numthreads(40, 8, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u;
  uint4 pixels = xe_edram_load_store_source.Load4(rt_offset);
  // One tile contains 80x8 texels, and 2 rows within a 80x16 tile contain data
  // from 1 render target row rather than 1. Threads with X 0-19 are for the
  // first row, with 20-39 are for the second.
  uint2 tile_dword_index = xe_group_thread_id.xy * uint2(4u, 2u);
  [flatten] if (xe_group_thread_id.x >= 20u) {
    tile_dword_index += uint2(uint(-80), 1u);
  }
  xe_edram_load_store_dest.Store4(
      XeEDRAMOffset(xe_group_id.xy, tile_dword_index), pixels);
}
