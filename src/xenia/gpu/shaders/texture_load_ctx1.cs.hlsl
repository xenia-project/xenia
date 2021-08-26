#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

// http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
// CXT1 is like DXT3/5 color, but 2-component and with 8:8 endpoints rather than
// 5:6:5.
//
// Dword 1:
// rrrrrrrrgggggggg
// RRRRRRRRGGGGGGGG
// (R is in the higher bits, according to how this format is used in Halo 3).
// Dword 2:
// AA BB CC DD
// EE FF GG HH
// II JJ KK LL
// MM NN OO PP

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 CTX1 blocks to 16x4 R8G8 texels.
  uint3 block_index = xe_thread_id << uint3(2, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int3 texel_index_host = int3(block_index) << int3(2, 2, 0);
  int block_offset_host =
      (XeTextureHostLinearOffset(texel_index_host, xe_texture_load_host_pitch,
                                 xe_texture_load_height_texels, 2u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 8u, 3u) >> 4;
  uint endian = XeTextureLoadEndian32();
  int i;
  [unroll] for (i = 0; i < 2; ++i) {
    if (i) {
      ++block_offset_host;
      // Odd 2 blocks = even 2 blocks + 32 bytes when tiled.
      block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
    }
    // Two blocks.
    uint4 blocks = XeEndianSwap32(xe_texture_load_source[block_offset_guest],
                                  endian);
    // Unpack the endpoints as 0x00g000r0 0x00G000R0 0x00g100r1 0x00G100R1 so
    // they can be multiplied by their weights allowing overflow.
    uint4 end_8in16;
    end_8in16.xz = ((blocks.xz >> 8u) & 0xFFu) | ((blocks.xz & 0xFFu) << 16u);
    end_8in16.yw = (blocks.xz >> 24u) | (blocks.xz & 0xFF0000u);
    uint2 weights_high = XeDXTHighColorWeights(blocks.yw);
    xe_texture_load_dest[block_offset_host] =
        XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high);
    [branch] if (texel_index_host.y + 1 < int(xe_texture_load_height_texels)) {
      xe_texture_load_dest[block_offset_host + elements_pitch_host] =
          XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high >> 8u);
      [branch] if (texel_index_host.y + 2 <
                       int(xe_texture_load_height_texels)) {
        xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host] =
            XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high >> 16u);
        [branch] if (texel_index_host.y + 3 <
                         int(xe_texture_load_height_texels)) {
          xe_texture_load_dest[block_offset_host + 3 * elements_pitch_host] =
              XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high >> 24u);
        }
      }
    }
  }
}
