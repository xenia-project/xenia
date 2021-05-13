#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 DXT3 blocks to 8x4 R8G8B8A8 texels.
  uint3 block_index = xe_thread_id << uint3(1, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int3 texel_index_host = int3(block_index) << int3(2, 2, 0);
  int block_offset_host =
      (XeTextureHostLinearOffset(texel_index_host, xe_texture_load_host_pitch,
                                 xe_texture_load_height_texels, 4u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 16u, 4u) >> 4;
  uint endian = XeTextureLoadEndian32();
  int i;
  [unroll] for (i = 0; i < 2; ++i) {
    if (i) {
      ++block_offset_host;
      // Odd block = even block + 32 guest bytes when tiled.
      block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
    }
    uint4 block = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                 endian);
    uint2 bgr_end_8in10 = XeDXTColorEndpointsToBGR8In10(block.z);
    // Sort the color indices so they can be used as weights for the second
    // endpoint.
    uint bgr_weights = XeDXTHighColorWeights(block.w);
    xe_texture_load_dest[block_offset_host] =
        XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights) +
        ((block.xxxx >> uint4(0u, 4u, 8u, 12u)) & 0xFu) * 0x11000000u;
    [branch] if (texel_index_host.y + 1 < int(xe_texture_load_height_texels)) {
      xe_texture_load_dest[block_offset_host + elements_pitch_host] =
          XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 8u) +
          ((block.xxxx >> uint4(16u, 20u, 24u, 28u)) & 0xFu) * 0x11000000u;
      [branch] if (texel_index_host.y + 2 <
                       int(xe_texture_load_height_texels)) {
        xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host] =
            XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 16u) +
            ((block.yyyy >> uint4(0u, 4u, 8u, 12u)) & 0xFu) * 0x11000000u;
        [branch] if (texel_index_host.y + 3 <
                         int(xe_texture_load_height_texels)) {
          xe_texture_load_dest[block_offset_host + 3 * elements_pitch_host] =
              XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 24u) +
              ((block.yyyy >> uint4(16u, 20u, 24u, 28u)) & 0xFu) * 0x11000000u;
        }
      }
    }
  }
}
