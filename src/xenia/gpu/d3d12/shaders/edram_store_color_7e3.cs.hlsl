#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  uint4 pixels_f16u32_packed = xe_edram_load_store_source.Load4(rt_offset);
  uint4 pixel_0_f16u32 = pixels_f16u32_packed.xxyy >> uint4(0u, 16u, 0u, 16u);
  uint4 pixel_1_f16u32 = pixels_f16u32_packed.zzww >> uint4(0u, 16u, 0u, 16u);
  uint2 pixels_7e3_packed =
      uint2(XeFloat16To7e3(pixel_0_f16u32), XeFloat16To7e3(pixel_1_f16u32));
  uint2 tile_dword_index = xe_group_thread_id.xy;
  tile_dword_index.x *= 2u;
  xe_edram_load_store_dest.Store2(
      XeEDRAMOffset(xe_group_id.xy, tile_dword_index), pixels_7e3_packed);
}
