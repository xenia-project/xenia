#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 DXN blocks to 8x4 R8G8 texels.
  uint3 block_index = xe_thread_id << uint3(1u, 0u, 0u);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int3 texel_index_host = int3(block_index) << uint3(2u, 2u, 0u);
  int block_offset_host =
      (XeTextureHostLinearOffset(texel_index_host, xe_texture_load_host_pitch,
                                 xe_texture_load_height_texels, 2u) +
       xe_texture_load_host_offset) >> 4u;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4u;
  uint block_offset_guest =
      XeTextureLoadGuestBlockOffset(block_index, 16u, 4u) >> 4u;
  uint endian = XeTextureLoadEndian32();
  uint4 block_0 = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                 endian);
  // Odd block = even block + 32 guest bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2u : 1u;
  uint4 block_1 = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                 endian);
  uint4 end_0 = (block_0.xxzz >> uint4(0u, 8u, 0u, 8u)) & 0xFFu;
  uint4 end_1 = (block_1.xxzz >> uint4(0u, 8u, 0u, 8u)) & 0xFFu;
  uint4 weights = (uint4(block_0.xz, block_1.xz) >> 16u) |
                  ((uint4(block_0.yw, block_1.yw) & 0xFFu) << 16u);
  weights = uint4(XeDXT5HighAlphaWeights(end_0.xy, weights.x),
                  XeDXT5HighAlphaWeights(end_0.zw, weights.y),
                  XeDXT5HighAlphaWeights(end_1.xy, weights.z),
                  XeDXT5HighAlphaWeights(end_1.zw, weights.w));
  xe_texture_load_dest[block_offset_host] =
      uint4(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
            XeDXT5RowToA8In16(end_1.xy, weights.z) |
                (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u));
  [branch] if (++texel_index_host.y < int(xe_texture_load_height_texels)) {
    block_offset_host += elements_pitch_host;
    weights >>= 12u;
    xe_texture_load_dest[block_offset_host] =
        uint4(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                  (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
              XeDXT5RowToA8In16(end_1.xy, weights.z) |
                  (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u));
    [branch] if (++texel_index_host.y < int(xe_texture_load_height_texels)) {
      block_offset_host += elements_pitch_host;
      weights = uint4(block_0.yw, block_1.yw) >> 8u;
      weights = uint4(XeDXT5HighAlphaWeights(end_0.xy, weights.x),
                      XeDXT5HighAlphaWeights(end_0.zw, weights.y),
                      XeDXT5HighAlphaWeights(end_1.xy, weights.z),
                      XeDXT5HighAlphaWeights(end_1.zw, weights.w));
      xe_texture_load_dest[block_offset_host] =
          uint4(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                    (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
                XeDXT5RowToA8In16(end_1.xy, weights.z) |
                    (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u));
      [branch] if (++texel_index_host.y < int(xe_texture_load_height_texels)) {
        block_offset_host += elements_pitch_host;
        weights >>= 12u;
        xe_texture_load_dest[block_offset_host] =
            uint4(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                      (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
                  XeDXT5RowToA8In16(end_1.xy, weights.z) |
                      (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u));
      }
    }
  }
}
