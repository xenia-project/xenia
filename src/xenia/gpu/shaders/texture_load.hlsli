#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_LOAD_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_LOAD_HLSLI_

#include "endian.hlsli"
#include "texture_address.hlsli"

cbuffer XeTextureLoadConstants : register(b0) {
  uint xe_texture_load_is_tiled_3d_endian_scale;
  // Base offset in bytes, resolution-scaled.
  uint xe_texture_load_guest_offset;
  // For tiled textures - row pitch in guest blocks, aligned to 32, unscaled.
  // For linear textures - row pitch in bytes.
  uint xe_texture_load_guest_pitch_aligned;
  // For 3D textures only (ignored otherwise) - aligned to 32, unscaled.
  uint xe_texture_load_guest_z_stride_block_rows_aligned;

  // If this is a packed mip tail, this is aligned to tile dimensions.
  // Resolution-scaled.
  uint3 xe_texture_load_size_blocks;
  // Base offset in bytes.
  uint xe_texture_load_host_offset;

  uint xe_texture_load_host_pitch;
  uint xe_texture_load_height_texels;
};

bool XeTextureLoadIsTiled() {
  #ifdef XE_TEXTURE_LOAD_RESOLUTION_SCALED
    // Only resolved textures can be resolution-scaled, and resolving is only
    // possible to a tiled destination.
    return true;
  #else
    return (xe_texture_load_is_tiled_3d_endian_scale & 1u) != 0u;
  #endif
}

bool XeTextureLoadIs3D() {
  return (xe_texture_load_is_tiled_3d_endian_scale & (1u << 1u)) != 0u;
}

uint XeTextureLoadEndian32() {
  return (xe_texture_load_is_tiled_3d_endian_scale >> 2u) & 3u;
}

uint2 XeTextureLoadResolutionScale() {
  #ifdef XE_TEXTURE_LOAD_RESOLUTION_SCALED
    return (xe_texture_load_is_tiled_3d_endian_scale.xx >> uint2(4u, 6u)) & 3u;
  #else
    return uint2(1u, 1u);
  #endif
}

// bpb and bpb_log2 are separate because bpb may be not a power of 2 (like 96).
uint XeTextureLoadGuestBlockOffset(uint3 block_index, uint bpb, uint bpb_log2) {
  #ifdef XE_TEXTURE_LOAD_RESOLUTION_SCALED
    // Only resolved textures can be resolution-scaled, and resolving is only
    // possible to a tiled destination.
    return
        xe_texture_load_guest_offset +
        XeTextureScaledTiledOffset(
            XeTextureLoadIs3D(), block_index,
            xe_texture_load_guest_pitch_aligned,
            xe_texture_load_guest_z_stride_block_rows_aligned, bpb_log2,
            XeTextureLoadResolutionScale());
  #else
    int block_offset_guest;
    [branch] if (XeTextureLoadIsTiled()) {
      [branch] if (XeTextureLoadIs3D()) {
        block_offset_guest = XeTextureTiledOffset3D(
            uint3(block_index), xe_texture_load_guest_pitch_aligned,
            xe_texture_load_guest_z_stride_block_rows_aligned, bpb_log2);
      } else {
        block_offset_guest = XeTextureTiledOffset2D(
            uint2(block_index.xy), xe_texture_load_guest_pitch_aligned,
            bpb_log2);
      }
    } else {
      block_offset_guest = XeTextureGuestLinearOffset(
          uint3(block_index), xe_texture_load_guest_pitch_aligned,
          xe_texture_load_guest_z_stride_block_rows_aligned, bpb);
    }
    return uint(int(xe_texture_load_guest_offset) + block_offset_guest);
  #endif
}

// Offset of the beginning of the odd R32G32/R32G32B32A32 load address from the
// address of the even load, for power-of-two-sized textures.
uint XeTextureLoadRightConsecutiveBlocksOffset(uint block_x, uint bpb_log2) {
  #ifdef XE_TEXTURE_LOAD_RESOLUTION_SCALED
    return XeTextureScaledRightSubUnitOffsetInConsecutivePair(
               block_x, bpb_log2, XeTextureLoadResolutionScale());
  #else
    uint offset;
    uint consecutive_blocks_log2 =
        XeTextureTiledConsecutiveBlocksLog2(bpb_log2);
    [branch] if (XeTextureLoadIsTiled()) {
      offset = XeTextureTiledOddConsecutiveBlocksOffset(bpb_log2);
    } else {
      offset = 1u << (consecutive_blocks_log2 + bpb_log2);
    }
    return offset;
  #endif
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_LOAD_HLSLI_
