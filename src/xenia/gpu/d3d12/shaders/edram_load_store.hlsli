#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_

cbuffer XeEDRAMLoadStoreConstants : register(b0) {
  uint xe_edram_base_tiles;
  uint xe_edram_pitch_tiles;
  uint xe_edram_rt_color_depth_pitch;
  uint xe_edram_rt_stencil_offset_or_swap_red_blue;
  uint xe_edram_rt_stencil_pitch;
};
#define xe_edram_rt_stencil_offset xe_edram_rt_stencil_offset_or_swap_red_blue
// For loads only. How exactly it's handled depends on the specific load shader,
// but 0 always means red and blue shouldn't be swapped.
#define xe_edram_swap_red_blue xe_edram_rt_stencil_offset_or_swap_red_blue

ByteAddressBuffer xe_edram_load_store_source : register(t0);
RWByteAddressBuffer xe_edram_load_store_dest : register(u0);

uint XeEDRAMOffset(uint2 tile_index, uint2 tile_dword_index) {
  return (xe_edram_base_tiles + (tile_index.y * xe_edram_pitch_tiles) +
          tile_index.x) * 5120u + tile_dword_index.y * 320u +
         tile_dword_index.x * 4u;
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
