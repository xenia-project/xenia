#include "texture_tile.hlsli"

RWBuffer<uint> xe_texture_tile_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 R8G8B8A8 texels to R5G5B5A1 or R4G4B4A4.
  uint2 texture_size = XeTextureTileSizeScaled();
  uint2 texel_index = xe_thread_id.xy;
  texel_index.x *= 4u;
  [branch] if (any(texel_index >= texture_size)) {
    return;
  }

  uint4 texels = xe_texture_tile_source.Load4(
      xe_texture_tile_host_base + texel_index.y * xe_texture_tile_host_pitch +
      texel_index.x * 4u);
  uint format = XeTextureTileFormat();
  if (format == 3u) {
    // k_1_5_5_5.
    texels = ((texels & (31u << 3u)) >> 3u) |
             ((texels & (31u << 11u)) >> (11u - 5u)) |
             ((texels & (31u << 19u)) >> (19u - 10u)) |
             ((texels & (1u << 31u)) >> (31u - 15u));
  } else if (format == 15u) {
    // k_4_4_4_4.
    texels = ((texels & (15u << 4u)) >> 4u) |
             ((texels & (15u << 12u)) >> (12u - 4u)) |
             ((texels & (15u << 20u)) >> (20u - 8u)) |
             ((texels & (15u << 28u)) >> (28u - 12u));
  }
  texels = XeByteSwap16(texels, XeTextureTileEndian());

  uint4 texel_addresses = XeTextureTileGuestAddress(texel_index, 1u) >> 1u;
  xe_texture_tile_dest[texel_addresses.x] = texels.x;
  bool3 texels_inside = uint3(1u, 2u, 3u) + texel_index.x < texture_size.x;
  [branch] if (texels_inside.x) {
    xe_texture_tile_dest[texel_addresses.y] = texels.y;
    [branch] if (texels_inside.y) {
      xe_texture_tile_dest[texel_addresses.z] = texels.z;
      [branch] if (texels_inside.z) {
        xe_texture_tile_dest[texel_addresses.w] = texels.w;
      }
    }
  }
}
