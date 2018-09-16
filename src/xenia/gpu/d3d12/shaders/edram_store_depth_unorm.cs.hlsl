#include "edram_load_store.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Depth.
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  uint4 samples =
      (xe_edram_load_store_source.Load4(rt_offset) & 0xFFFFFFu) << 8u;
  // Stencil.
  rt_offset = xe_thread_id.y * xe_edram_rt_stencil_pitch + xe_thread_id.x * 4u +
              xe_edram_rt_stencil_offset;
  samples |= (xe_edram_load_store_source.Load(rt_offset).xxxx >>
             uint4(0u, 8u, 16u, 24u)) & 0xFFu;
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 4u;
  xe_edram_load_store_dest.Store4(
      XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index), samples);
}
