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
  // 10:18 - align(guest texture pitch, 32) / 32.
  // 19:27 - align(guest texture height, 32) / 32 for 3D, 0 for 2D.
  uint xe_texture_tile_info;
  // Origin of the written data in the destination texture (for anything bigger,
  // adjust the base address with 32x32x8 granularity).
  // 0:4 - X & 31.
  // 5:9 - Y & 31.
  // 10:12 - Z & 7.
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

uint2 XeTextureTileGuestPitch() {
  return ((xe_texture_tile_info >> uint2(10u, 19u)) & 511u) << 5u;
}

uint3 XeTextureTileOffset() {
  return (xe_texture_tile_offset >> uint3(0u, 5u, 10u)) & uint3(31u, 31u, 7u);
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
  uint3 texel_index_guest = XeTextureTileOffset();
  texel_index_guest.xy += texel_index_scaled >> scale_log2;
  uint2 pitch = XeTextureTileGuestPitch();
  uint4 addresses;
  if (pitch.y != 0u) {
    addresses = XeTextureTiledOffset3D(texel_index_guest, pitch, bpb_log2);
  } else {
    addresses = XeTextureTiledOffset2D(texel_index_guest.xy, pitch.x, bpb_log2);
  }
  addresses = ((xe_texture_tile_guest_base + addresses) << (scale_log2 * 2u)) +
              ((texel_index_scaled.y & scale_log2) << (bpb_log2 + 1u));
  if (scale_log2 != 0u) {
    uint x_odd = texel_index_scaled.x & 1u;
    addresses = x_odd ? addresses.xyyz : addresses.xxyy;
    addresses += ((uint2(0u, 1u) ^ x_odd) << bpb_log2).xyxy;
  }
  return addresses;
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_
