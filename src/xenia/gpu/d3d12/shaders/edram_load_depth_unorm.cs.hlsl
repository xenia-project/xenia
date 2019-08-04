#include "edram_load_store.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 4u;
  uint4 samples = xe_edram_load_store_source.Load4(
      XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index));
  // Depth.
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  xe_edram_load_store_dest.Store4(rt_offset, samples >> 8u);
  // Stencil.
  uint4 stencil = (samples & 0xFFu) << uint4(0u, 8u, 16u, 24u);
  stencil.xy |= stencil.zw;
  stencil.x |= stencil.y;
  rt_offset = xe_thread_id.y * xe_edram_rt_stencil_pitch + xe_thread_id.x * 4u +
              xe_edram_rt_stencil_offset;
  xe_edram_load_store_dest.Store(rt_offset, stencil.x);
}
