#include "edram_load_store.hlsli"
#include "pixel_formats.hlsli"

[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Host pixels within samples in the EDRAM buffer -> samples within host
  // pixels in texture data (loading for AA resolve, not for rendering).
  // 1 group = 1 tile, 1 thread writes 8 host pixels for 2 samples
  // (horizontally).
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 2u;
  uint edram_offset = XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index);
  // 4 host pixels of the left guest pixel (1x/2x AA) / sample (4x AA).
  uint4 pixels_7e3_0 = xe_edram_load_store_source.Load4(edram_offset);
  // 4 host pixels of the right guest pixel/sample.
  uint4 pixels_7e3_1 = xe_edram_load_store_source.Load4(edram_offset + 16u);

  uint2 sample_count_log2 = XeEDRAMSampleCountLog2();
  if (sample_count_log2.x != 0u) {
    // We already have data for 2 horizontal samples - change "host pixels
    // within samples" to "samples within host pixels" horizontally.
    // pixels_7e3_0 is s0pTL s0pTR s0pBL s0pBR currently, and
    // pixels_7e3_1 is s2pTL s2pTR s2pBL s2pBR.
    // Change that to:
    // pixels_7e3_0 being s0pTL s2pTL s0pBL s2pBL and
    // pixels_7e3_1 being s0pTR s2pTR s0pBR s2pBR.
    uint2 pixels_temp = pixels_7e3_0.yw;
    pixels_7e3_0.yw = pixels_7e3_1.xz;
    pixels_7e3_1.xz = pixels_temp;
  }
  // Do similar interleaving for rows.
  uint rt_offset = ((xe_thread_id.y & ~sample_count_log2.y) * 2u +
                    (xe_thread_id.y & sample_count_log2.y)) *
                   xe_edram_rt_color_depth_pitch + xe_thread_id.x * 32u;
  uint4 pixel_0_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_0.x)));
  uint4 pixel_1_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_0.y)));
  uint4 pixels_f16u32_packed =
      uint4(pixel_0_f16u32.xz, pixel_1_f16u32.xz) |
      (uint4(pixel_0_f16u32.yw, pixel_1_f16u32.yw) << 16u);
  xe_edram_load_store_dest.Store4(rt_offset, pixels_f16u32_packed);
  pixel_0_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_1.x)));
  pixel_1_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_1.y)));
  pixels_f16u32_packed = uint4(pixel_0_f16u32.xz, pixel_1_f16u32.xz) |
                         (uint4(pixel_0_f16u32.yw, pixel_1_f16u32.yw) << 16u);
  xe_edram_load_store_dest.Store4(rt_offset + 16u, pixels_f16u32_packed);
  rt_offset += (1u << sample_count_log2.y) * xe_edram_rt_color_depth_pitch;
  pixel_0_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_0.z)));
  pixel_1_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_0.w)));
  pixels_f16u32_packed = uint4(pixel_0_f16u32.xz, pixel_1_f16u32.xz) |
                         (uint4(pixel_0_f16u32.yw, pixel_1_f16u32.yw) << 16u);
  xe_edram_load_store_dest.Store4(rt_offset, pixels_f16u32_packed);
  pixel_0_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_1.z)));
  pixel_1_f16u32 = f32tof16(asfloat(XeFloat7e3To32(pixels_7e3_1.w)));
  pixels_f16u32_packed = uint4(pixel_0_f16u32.xz, pixel_1_f16u32.xz) |
                         (uint4(pixel_0_f16u32.yw, pixel_1_f16u32.yw) << 16u);
  xe_edram_load_store_dest.Store4(rt_offset + 16u, pixels_f16u32_packed);
}
