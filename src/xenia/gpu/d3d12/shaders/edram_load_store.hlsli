#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_

cbuffer XeEDRAMLoadStoreConstants : register(b0) {
  uint xe_edram_base_tiles;
  uint xe_edram_pitch_tiles;
  uint xe_edram_rt_color_depth_pitch;
  uint xe_edram_rt_stencil_offset;
  uint xe_edram_rt_stencil_pitch;
};

ByteAddressBuffer xe_edram_load_store_source : register(t0);
RWByteAddressBuffer xe_edram_load_store_dest : register(u0);

uint XeEDRAMOffset(uint2 tile_index, uint2 tile_dword_index) {
  return (xe_edram_base_tiles + (tile_index.y * xe_edram_pitch_tiles) +
          tile_index.x) * 5120u + tile_dword_index.y * 320u +
         tile_dword_index.x * 4u;
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
