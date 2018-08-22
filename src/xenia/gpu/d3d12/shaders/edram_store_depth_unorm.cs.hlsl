#include "edram_load_store.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Depth.
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  uint4 pixels =
      (xe_edram_load_store_source.Load4(rt_offset) & 0xFFFFFFu) << 8u;
  // Stencil.
  rt_offset = xe_thread_id.y * xe_edram_rt_stencil_pitch + xe_thread_id.x * 4u +
              xe_edram_rt_stencil_offset;
  pixels |= (xe_edram_load_store_source.Load(rt_offset).xxxx >>
             uint4(0u, 8u, 16u, 24u)) & 0xFFu;
  uint2 tile_dword_index = xe_group_thread_id.xy;
  tile_dword_index.x *= 4u;
  xe_edram_load_store_dest.Store4(
      XeEDRAMOffset(xe_group_id.xy, tile_dword_index), pixels);
}
