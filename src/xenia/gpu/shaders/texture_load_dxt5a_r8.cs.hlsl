#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXT5A blocks to 16x4 R8 texels.
  uint3 block_index = xe_thread_id << uint3(2, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int3 texel_index_host = int3(block_index) << int3(2, 2, 0);
  int block_offset_host =
      (XeTextureHostLinearOffset(texel_index_host, xe_texture_load_host_pitch,
                                 xe_texture_load_height_texels, 1u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 8u, 3u) >> 4;
  uint endian = XeTextureLoadEndian32();
  uint4 blocks_01 = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                   endian);
  // Odd 2 blocks = even 2 blocks + 32 bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
  uint4 blocks_23 = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                   endian);
  uint4 alpha_end_01 = (blocks_01.xxzz >> uint4(0u, 8u, 0u, 8u)) & 0xFFu;
  uint4 alpha_end_23 = (blocks_23.xxzz >> uint4(0u, 8u, 0u, 8u)) & 0xFFu;
  uint4 alpha_weights = (uint4(blocks_01.xz, blocks_23.xz) >> 16u) |
                        ((uint4(blocks_01.yw, blocks_23.yw) & 0xFFu) << 16u);
  alpha_weights.x = XeDXT5HighAlphaWeights(alpha_end_01.xy, alpha_weights.x);
  alpha_weights.y = XeDXT5HighAlphaWeights(alpha_end_01.zw, alpha_weights.y);
  alpha_weights.z = XeDXT5HighAlphaWeights(alpha_end_23.xy, alpha_weights.z);
  alpha_weights.w = XeDXT5HighAlphaWeights(alpha_end_23.zw, alpha_weights.w);
  xe_texture_load_dest[block_offset_host] = uint4(
      XeDXT5RowToA8(alpha_end_01.xy, alpha_weights.x),
      XeDXT5RowToA8(alpha_end_01.zw, alpha_weights.y),
      XeDXT5RowToA8(alpha_end_23.xy, alpha_weights.z),
      XeDXT5RowToA8(alpha_end_23.zw, alpha_weights.w));
  [branch] if (++texel_index_host.y < int(xe_texture_load_height_texels)) {
    block_offset_host += elements_pitch_host;
    alpha_weights >>= 12u;
    xe_texture_load_dest[block_offset_host] = uint4(
        XeDXT5RowToA8(alpha_end_01.xy, alpha_weights.x),
        XeDXT5RowToA8(alpha_end_01.zw, alpha_weights.y),
        XeDXT5RowToA8(alpha_end_23.xy, alpha_weights.z),
        XeDXT5RowToA8(alpha_end_23.zw, alpha_weights.w));
    [branch] if (++texel_index_host.y < int(xe_texture_load_height_texels)) {
      block_offset_host += elements_pitch_host;
      alpha_weights = uint4(blocks_01.yw, blocks_23.yw) >> 8u;
      alpha_weights.x = XeDXT5HighAlphaWeights(alpha_end_01.xy,
                                               alpha_weights.x);
      alpha_weights.y = XeDXT5HighAlphaWeights(alpha_end_01.zw,
                                               alpha_weights.y);
      alpha_weights.z = XeDXT5HighAlphaWeights(alpha_end_23.xy,
                                               alpha_weights.z);
      alpha_weights.w = XeDXT5HighAlphaWeights(alpha_end_23.zw,
                                               alpha_weights.w);
      xe_texture_load_dest[block_offset_host] = uint4(
          XeDXT5RowToA8(alpha_end_01.xy, alpha_weights.x),
          XeDXT5RowToA8(alpha_end_01.zw, alpha_weights.y),
          XeDXT5RowToA8(alpha_end_23.xy, alpha_weights.z),
          XeDXT5RowToA8(alpha_end_23.zw, alpha_weights.w));
      [branch] if (++texel_index_host.y < int(xe_texture_load_height_texels)) {
        block_offset_host += elements_pitch_host;
        alpha_weights >>= 12u;
        xe_texture_load_dest[block_offset_host] = uint4(
            XeDXT5RowToA8(alpha_end_01.xy, alpha_weights.x),
            XeDXT5RowToA8(alpha_end_01.zw, alpha_weights.y),
            XeDXT5RowToA8(alpha_end_23.xy, alpha_weights.z),
            XeDXT5RowToA8(alpha_end_23.zw, alpha_weights.w));
      }
    }
  }
}
