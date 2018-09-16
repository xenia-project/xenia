#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Depth.
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  uint4 depth32 = xe_edram_load_store_source.Load4(rt_offset);
  uint4 depth24_stencil = XeFloat32To20e4(depth32) << 8u;
  // Stencil.
  rt_offset = xe_thread_id.y * xe_edram_rt_stencil_pitch + xe_thread_id.x * 4u +
              xe_edram_rt_stencil_offset;
  depth24_stencil |= (xe_edram_load_store_source.Load(rt_offset).xxxx >>
                      uint4(0u, 8u, 16u, 24u)) & 0xFFu;
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 4u;
  uint edram_offset = XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index);
  // Store 24-bit depth for aliasing and checking if 32-bit depth is up to date.
  xe_edram_load_store_dest.Store4(edram_offset, depth24_stencil);
  // Store 32-bit depth so precision isn't lost when doing multipass rendering.
  xe_edram_load_store_dest.Store4(10485760u + edram_offset, depth32);
}
