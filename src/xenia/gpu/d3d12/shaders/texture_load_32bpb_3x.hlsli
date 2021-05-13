#include "texture_load.hlsli"

Buffer<uint2> xe_texture_load_source : register(t0);
RWBuffer<uint2> xe_texture_load_dest : register(u0);

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 blocks passed through an externally provided uint2
  // transformation function (XE_TEXTURE_LOAD_32BPB_TRANSFORM).
  uint3 block_index = xe_thread_id << uint3(1, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) * int3(3, 3, 1),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y * 3, 4u) +
       xe_texture_load_host_offset) >> 3;
  int elements_pitch_host = xe_texture_load_host_pitch >> 3;
  int block_offset_guest =
      (XeTextureLoadGuestBlockOffset(int3(block_index), 4u, 2u) * 9) >> 3;
  uint endian = XeTextureLoadEndian32();
  // abc jkl
  // def mno
  // ghi pqr
  // from:
  // ab|cd|ef|gh|ij|kl|mn|op|qr
  // to:
  // ab|cj|kl
  // de|fm|no
  // gh|ip|qr
  uint4 block_abcd = XE_TEXTURE_LOAD_32BPB_TRANSFORM(
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest],
                           xe_texture_load_source[block_offset_guest + 1]),
                     endian));
  // AB|--|--
  // --|--|--
  // --|--|--
  xe_texture_load_dest[block_offset_host] = block_abcd.xy;
  uint4 block_efgh = XE_TEXTURE_LOAD_32BPB_TRANSFORM(
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest + 2],
                           xe_texture_load_source[block_offset_guest + 3]),
                     endian));
  // ab|--|--
  // DE|--|--
  // GH|--|--
  xe_texture_load_dest[block_offset_host + elements_pitch_host] =
      uint2(block_abcd.w, block_efgh.x);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host] =
      block_efgh.zw;
  uint4 block_ijkl = XE_TEXTURE_LOAD_32BPB_TRANSFORM(
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest + 4],
                           xe_texture_load_source[block_offset_guest + 5]),
                     endian));
  // ab|CJ|KL
  // de|--|--
  // gh|--|--
  xe_texture_load_dest[block_offset_host + 1] =
      uint2(block_abcd.z, block_ijkl.y);
  xe_texture_load_dest[block_offset_host + 2] = block_ijkl.zw;
  uint4 block_mnop = XE_TEXTURE_LOAD_32BPB_TRANSFORM(
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest + 6],
                           xe_texture_load_source[block_offset_guest + 7]),
                     endian));
  // ab|cj|kl
  // de|FM|NO
  // gh|IP|--
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 1] =
      uint2(block_efgh.y, block_mnop.x);
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 2] =
      block_mnop.yz;
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 1] =
      uint2(block_ijkl.x, block_mnop.w);
  // ab|cj|kl
  // de|fm|no
  // gh|ip|QR
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 2] =
      XE_TEXTURE_LOAD_32BPB_TRANSFORM(XeEndianSwap32(
          xe_texture_load_source[block_offset_guest + 8], endian));
}
