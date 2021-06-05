#include "texture_load.hlsli"

Buffer<uint2> xe_texture_load_source : register(t0);
RWBuffer<uint2> xe_texture_load_dest : register(u0);

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 packed 32-bit texels with the externally provided uint ->
  // uint2 function (XE_TEXTURE_LOAD_32BPB_TO_64BPB) for converting to 64bpb -
  // useful for expansion of hendeca (10:11:11 or 11:11:10) to unorm16/snorm16.
  uint3 block_index = xe_thread_id << uint3(1, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) << int3(3, 3, 1),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y * 3, 8u) +
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
  // abcjkl
  // defmno
  // ghipqr
  // Loading:
  // ABC---
  // D-----
  // ------
  uint4 block_abcd =
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest],
                           xe_texture_load_source[block_offset_guest + 1]),
                     endian);
  xe_texture_load_dest[block_offset_host] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_abcd.x);
  xe_texture_load_dest[block_offset_host + 1] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_abcd.y);
  xe_texture_load_dest[block_offset_host + 2] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_abcd.z);
  xe_texture_load_dest[block_offset_host + elements_pitch_host] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_abcd.w);
  // abc---
  // dEF---
  // GH----
  uint4 block_efgh =
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest + 2],
                           xe_texture_load_source[block_offset_guest + 3]),
                     endian);
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 1] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_efgh.x);
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 2] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_efgh.y);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_efgh.z);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 1] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_efgh.w);
  // abcJKL
  // def---
  // ghI---
  uint4 block_ijkl =
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest + 4],
                           xe_texture_load_source[block_offset_guest + 5]),
                     endian);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 2] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_ijkl.x);
  xe_texture_load_dest[block_offset_host + 3] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_ijkl.y);
  xe_texture_load_dest[block_offset_host + 4] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_ijkl.z);
  xe_texture_load_dest[block_offset_host + 5] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_ijkl.w);
  // abcjkl
  // defMNO
  // ghiP--
  uint4 block_mnop =
      XeEndianSwap32(uint4(xe_texture_load_source[block_offset_guest + 6],
                           xe_texture_load_source[block_offset_guest + 7]),
                     endian);
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 3] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_mnop.x);
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 4] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_mnop.y);
  xe_texture_load_dest[block_offset_host + elements_pitch_host + 5] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_mnop.z);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 3] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_mnop.w);
  // abcjkl
  // defmno
  // ghipQR
  uint2 block_qr =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest + 8], endian);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 4] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_qr.x);
  xe_texture_load_dest[block_offset_host + 2 * elements_pitch_host + 5] =
      XE_TEXTURE_LOAD_32BPB_TO_64BPB(block_qr.y);
}
