#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_

#include "byte_swap.hlsli"
#include "texture_address.hlsli"

cbuffer XeTextureTileConstants : register(b0) {
  // Either from the start of the shared memory or from the start of the typed
  // UAV, in bytes (even for typed UAVs, so XeTextureTiledOffset2D is easier to
  // use).
  uint xe_texture_tile_guest_base;
  // 0:2 - endianness (up to Xin128).
  // 3:8 - guest format (primarily for 16-bit textures).
  // 9 - whether 2x resolution scale is used.
  // 10:31 - actual guest texture width.
  uint xe_texture_tile_info;
  // Origin of the written data in the destination texture. X in the lower 16
  // bits, Y in the upper.
  uint xe_texture_tile_offset;
  // Size to copy, texels with index bigger than this won't be written.
  // Width in the lower 16 bits, height in the upper.
  uint xe_texture_tile_size;
  // Byte offset to the first texel from the beginning of the source buffer.
  uint xe_texture_tile_host_base;
  // Row pitch of the source buffer.
  uint xe_texture_tile_host_pitch;
}

ByteAddressBuffer xe_texture_tile_source : register(t0);
// The target is u0, may be a raw UAV or a typed UAV depending on the format.

uint XeTextureTileEndian() {
  return xe_texture_tile_info & 7u;
}

uint XeTextureTileFormat() {
  return (xe_texture_tile_info >> 3u) & 63u;
}

uint XeTextureTileScaleLog2() {
  return (xe_texture_tile_info >> 9u) & 1u;
}

uint XeTextureTileGuestPitch() {
  return xe_texture_tile_info >> 10u;
}

uint2 XeTextureTileSizeUnscaled() {
  return (xe_texture_tile_size >> uint2(0u, 16u)) & 0xFFFFu;
}

uint2 XeTextureTileSizeScaled() {
  return XeTextureTileSizeUnscaled() << XeTextureTileScaleLog2();
}

uint4 XeTextureTileGuestAddress(uint2 texel_index_scaled, uint bpb_log2) {
  // Consecutive pixels in the host texture are written to addresses in XYZW.
  // Without 2x scaling, the addresses map to 4x1 guest pixels, so need to
  // return four separate tiled addresses.
  // With 2x scaling, they map to 2x0.5 guest pixels, so need to return
  // addresses that belong to two tiled guest pixels.
  uint scale_log2 = XeTextureTileScaleLog2();
  uint4 addresses = (xe_texture_tile_guest_base + XeTextureTiledOffset2D(
                     ((xe_texture_tile_offset >> uint2(0u, 16u)) & 0xFFFFu) +
                     (texel_index_scaled >> scale_log2),
                     XeTextureTileGuestPitch(), bpb_log2)) <<
                    (scale_log2 * 2u);
  addresses += (texel_index_scaled.y & scale_log2) << (bpb_log2 + 1u);
  if (scale_log2 != 0u) {
    uint x_odd = texel_index_scaled.x & 1u;
    addresses = x_odd ? addresses.xyyz : addresses.xxyy;
    addresses += ((uint2(0u, 1u) ^ x_odd) << bpb_log2).xyxy;
  }
  return addresses;
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_
