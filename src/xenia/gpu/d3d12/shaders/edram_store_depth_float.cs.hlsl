#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Depth.
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u;
  uint4 depth32 = xe_edram_load_store_source.Load4(rt_offset);
  uint4 depth24_stencil = XeFloat32To20e4(depth32);
  // Stencil.
  rt_offset = xe_edram_rt_stencil_offset +
              xe_thread_id.y * xe_edram_rt_stencil_pitch + xe_thread_id.x * 4u;
  depth24_stencil |= xe_edram_load_store_source.Load(rt_offset).xxxx >>
                     uint4(0u, 8u, 16u, 24u) << 24u;
  uint2 tile_dword_index = xe_group_thread_id.xy;
  tile_dword_index.x *= 4u;
  uint edram_offset = XeEDRAMOffset(xe_group_id.xy, tile_dword_index);
  // Store 24-bit depth for aliasing and checking if 32-bit depth is up to date.
  xe_edram_load_store_dest.Store4(edram_offset, depth24_stencil);
  // Store 32-bit depth so precision isn't lost when doing multipass rendering.
  xe_edram_load_store_dest.Store4(10485760u + edram_offset, depth32);
}
