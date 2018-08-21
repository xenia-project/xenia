#include "edram_load_store.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint2 tile_dword_index = xe_group_thread_id.xy;
  tile_dword_index.x *= 4u;
  uint4 pixels = xe_edram_load_store_source.Load4(
      XeEDRAMOffset(xe_group_id.xy, tile_dword_index));
  if (xe_edram_swap_red_blue != 0u) {
    // Not a very long shift, just 16 or 20.
    uint blue_shift = xe_edram_swap_red_blue >> 16u;
    uint red_mask = xe_edram_swap_red_blue & 0xFFFFu;
    uint blue_mask = red_mask << blue_shift;
    pixels = (pixels & ~(red_mask | blue_mask)) |
             ((pixels & red_mask) << blue_shift) |
             ((pixels >> blue_shift) & red_mask);
  }
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u;
  xe_edram_load_store_dest.Store4(rt_offset, pixels);
}
