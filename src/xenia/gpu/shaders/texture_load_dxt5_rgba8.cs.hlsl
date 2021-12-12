#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 DXT5 blocks to 8x4 R8G8B8A8 texels.
  uint3 block_index = xe_thread_id << uint3(1u, 0u, 0u);
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
      XeTextureLoadGuestBlockOffset(block_index, 16u, 4u) >> 4u;
  uint endian = XeTextureLoadEndian32();
  uint i;
  [unroll] for (i = 0u; i < 2u; ++i) {
    if (i) {
      ++block_offset_host;
      // Odd block = even block + 32 guest bytes when tiled.
      block_offset_guest += XeTextureLoadIsTiled() ? 2u : 1u;
    }
    uint4 block = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                 endian);
    uint2 bgr_end_8in10 = XeDXTColorEndpointsToBGR8In10(block.z);
    // Sort the color indices so they can be used as weights for the second
    // endpoint.
    uint bgr_weights = XeDXTHighColorWeights(block.w);
    uint2 alpha_end = (block.xx >> uint2(0u, 8u)) & 0xFFu;
    uint alpha_weights = XeDXT5HighAlphaWeights(
        alpha_end, (block.x >> 16u) | ((block.y & 0xFFu) << 16u));
    xe_texture_load_dest[block_offset_host] =
        XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights) |
        ((XeDXT5RowToA8(alpha_end, alpha_weights).xxxx <<
              uint4(24u, 16u, 8u, 0u))
         & 0xFF000000u);
    [branch] if (texel_index_host.y + 1 < int(xe_texture_load_height_texels)) {
      xe_texture_load_dest[block_offset_host + elements_pitch_host] =
          XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 8u) |
          ((XeDXT5RowToA8(alpha_end, alpha_weights >> 12u).xxxx <<
                uint4(24u, 16u, 8u, 0u))
           & 0xFF000000u);
      [branch] if (texel_index_host.y + 2 <
                       int(xe_texture_load_height_texels)) {
        alpha_weights = XeDXT5HighAlphaWeights(alpha_end, block.y >> 8u);
        xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host] =
            XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 16u) |
            ((XeDXT5RowToA8(alpha_end, alpha_weights).xxxx <<
                  uint4(24u, 16u, 8u, 0u))
             & 0xFF000000u);
        [branch] if (texel_index_host.y + 3 <
                         int(xe_texture_load_height_texels)) {
          xe_texture_load_dest[block_offset_host + 3 * elements_pitch_host] =
              XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 24u) |
              ((XeDXT5RowToA8(alpha_end, alpha_weights >> 12u).xxxx <<
                    uint4(24u, 16u, 8u, 0u))
               & 0xFF000000u);
        }
      }
    }
  }
}
