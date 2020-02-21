#include "texture_tile.hlsli"

RWBuffer<uint> xe_texture_tile_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 texels.
  uint2 texture_size = XeTextureTileSizeScaled();
  uint2 texel_index = xe_thread_id.xy;
  texel_index.x *= 4u;
  [branch] if (any(texel_index >= texture_size)) {
    return;
  }

  uint4 texels = (xe_texture_tile_source.Load2(
      xe_texture_tile_host_base + texel_index.y * xe_texture_tile_host_pitch +
      texel_index.x * 2u).xxyy >> uint4(0u, 16u, 0u, 16u)) & 0xFFFFu;
  // Swap components - Xenos 16-bit packed textures are RGBA, but in Direct3D 12
  // they are BGRA.
  uint format = XeTextureTileFormat();
  if (format == 4u) {
    // k_5_6_5.
    texels = (texels & (63u << 5u)) | ((texels & 31u) << 11u) | (texels >> 11u);
  } else if (format == 5u) {
    // k_6_5_5 - GGGGG BBBBBB RRRRR to RRRRR GGGGG BBBBBB (use RBGA swizzle when
    // resolving).
    texels = ((texels & 0x07FFu) << 5u) | (texels >> 11u);
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
