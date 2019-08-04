#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 2u;
  uint2 samples_7e3_packed = xe_edram_load_store_source.Load2(
      XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index));
  uint4 sample_0_f16u32 =
      f32tof16(asfloat(XeFloat7e3To32(samples_7e3_packed.x)));
  uint4 sample_1_f16u32 =
      f32tof16(asfloat(XeFloat7e3To32(samples_7e3_packed.y)));
  uint4 samples_f16u32_packed =
      uint4(sample_0_f16u32.xz, sample_1_f16u32.xz) |
      (uint4(sample_0_f16u32.yw, sample_1_f16u32.yw) << 16u);
  uint rt_offset = xe_thread_id.y * xe_edram_rt_color_depth_pitch +
                   xe_thread_id.x * 16u + xe_edram_rt_color_depth_offset;
  xe_edram_load_store_dest.Store4(rt_offset, samples_f16u32_packed);
}
