#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXT1 blocks to 16x4 R8G8B8A8 texels.
  uint3 block_index = xe_thread_id << uint3(2u, 0u, 0u);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int3 texel_index_host = int3(block_index) << uint3(2u, 2u, 0u);
  int block_offset_host =
      (XeTextureHostLinearOffset(texel_index_host, xe_texture_load_host_pitch,
                                 xe_texture_load_height_texels, 4u) +
       xe_texture_load_host_offset) >> 4u;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4u;
  uint block_offset_guest =
      XeTextureLoadGuestBlockOffset(block_index, 8u, 3u) >> 4u;
  uint endian = XeTextureLoadEndian32();
  uint4 blocks_01 = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                   endian);
  // Odd 2 blocks = even 2 blocks + 32 bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2u : 1u;
  uint4 blocks_23 = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                   endian);
  uint4 end_8in10_01 = uint4(XeDXTColorEndpointsToBGR8In10(blocks_01.x),
                             XeDXTColorEndpointsToBGR8In10(blocks_01.z));
  uint4 end_8in10_23 = uint4(XeDXTColorEndpointsToBGR8In10(blocks_23.x),
                             XeDXTColorEndpointsToBGR8In10(blocks_23.z));
  bool4 is_trans = uint4(end_8in10_01.xz, end_8in10_23.xz) <=
                   uint4(end_8in10_01.yw, end_8in10_23.yw);
  uint4 weights = uint4(blocks_01.yw, blocks_23.yw);
  weights = is_trans ? XeDXT1TransWeights(weights)
                     : XeDXTHighColorWeights(weights);
  int i;
  [unroll] for (i = 0; i < 4; ++i) {
    if (i) {
      if (texel_index_host.y + i >= int(xe_texture_load_height_texels)) {
        break;
      }
      block_offset_host += elements_pitch_host;
      weights >>= 8u;
    }
    xe_texture_load_dest[block_offset_host] =
        is_trans.x ? XeDXT1TransRowToRGBA8(end_8in10_01.xy, weights.x)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_01.xy, weights.x) |
                      0xFF000000u);
    xe_texture_load_dest[block_offset_host + 1] =
        is_trans.y ? XeDXT1TransRowToRGBA8(end_8in10_01.zw, weights.y)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_01.zw, weights.y) |
                      0xFF000000u);
    xe_texture_load_dest[block_offset_host + 2] =
        is_trans.z ? XeDXT1TransRowToRGBA8(end_8in10_23.xy, weights.z)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_23.xy, weights.z) |
                      0xFF000000u);
    xe_texture_load_dest[block_offset_host + 3] =
        is_trans.w ? XeDXT1TransRowToRGBA8(end_8in10_23.zw, weights.w)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_23.zw, weights.w) |
                      0xFF000000u);
  }
}
