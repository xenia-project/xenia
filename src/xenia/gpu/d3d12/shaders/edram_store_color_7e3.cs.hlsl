#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  uint4 samples_f16u32_packed = xe_edram_load_store_source.Load4(rt_offset);
  uint4 sample_0_f16u32 = samples_f16u32_packed.xxyy >> uint4(0u, 16u, 0u, 16u);
  uint4 sample_1_f16u32 = samples_f16u32_packed.zzww >> uint4(0u, 16u, 0u, 16u);
  uint2 samples_7e3_packed = uint2(
      XeFloat32To7e3(asuint(f16tof32(sample_0_f16u32))),
      XeFloat32To7e3(asuint(f16tof32(sample_1_f16u32))));
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 2u;
  xe_edram_load_store_dest.Store2(
      XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index),
      samples_7e3_packed);
}
