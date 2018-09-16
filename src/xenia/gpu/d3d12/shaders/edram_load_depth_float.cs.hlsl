#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 4u;
  uint edram_offset = XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index);
  uint4 depth24_stencil = xe_edram_load_store_source.Load4(edram_offset);
  uint4 depth24 = depth24_stencil >> 8u;
  uint4 depth32 = xe_edram_load_store_source.Load4(10485760u + edram_offset);
  // Depth. If the stored 32-bit depth converted to 24-bit is the same as the
  // stored 24-bit depth, load the 32-bit value because it has more precision
  // (and multipass rendering is possible), if it's not, convert the 24-bit
  // depth because it was overwritten by aliasing.
  uint4 depth24to32 = XeFloat20e4To32(depth24);
  uint4 depth = depth24to32 + (depth32 - depth24to32) *
                uint4(XeFloat32To20e4(depth32) == depth24);
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  xe_edram_load_store_dest.Store4(rt_offset, depth);
  // Stencil.
  uint4 stencil = (depth24_stencil & 0xFFu) << uint4(0u, 8u, 16u, 24u);
  stencil.xy |= stencil.zw;
  stencil.x |= stencil.y;
  rt_offset = xe_thread_id.y * xe_edram_rt_stencil_pitch + xe_thread_id.x * 4u +
              xe_edram_rt_stencil_offset;
  xe_edram_load_store_dest.Store(rt_offset, stencil.x);
}
