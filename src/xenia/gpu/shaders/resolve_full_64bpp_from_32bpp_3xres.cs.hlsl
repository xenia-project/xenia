#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest pixel.
  // Group size is the same as resolve granularity, overflow check not needed.
  bool2 duplicate_second = XeResolveEdramDuplicateSecondHostPixel()
                               ? xe_thread_id.xy == 0u
                               : bool2(false, false);
  uint dest_format = XeResolveDestFormat();
  uint endian = XeResolveDestEndian128();
  uint dest_address_int2s =
      (XeResolveDestPixelAddress(xe_thread_id.xy, 3u) * 9u) >> 3u;
  // XYZ
  // W4-
  // ---
  uint source_address_bytes =
      XeResolveColorCopySourcePixelAddressInts(xe_thread_id.xy) * (9u * 4u);
  float4 host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3, host_pixel_4;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  uint4 packed_01, packed_23;
  uint2 packed_4;
  XePack64bpp5Pixels(host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3,
                     host_pixel_4, dest_format, packed_01, packed_23, packed_4);
  XeEndianSwap64(packed_01, packed_23, packed_4, endian);
  // 2 will be stored later - need 5 instead for duplicate_second.y.
  [branch] if (duplicate_second.x) {
    packed_01.xy = packed_01.zw;
    packed_23.zw = packed_4;
  }
  [branch] if (duplicate_second.y) {
    packed_01 = uint4(packed_23.zw, packed_4);
  }
  xe_resolve_dest[dest_address_int2s] = packed_01.xy;
  xe_resolve_dest[dest_address_int2s + 1u] = packed_01.zw;
  xe_resolve_dest[dest_address_int2s + 3u] = packed_23.zw;
  xe_resolve_dest[dest_address_int2s + 4u] = packed_4;
  // +++
  // ++X
  // YZW
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  uint4 packed_56, packed_78;
  XePack64bpp4Pixels(host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3,
                     dest_format, packed_56, packed_78);
  XeEndianSwap64(packed_56, packed_78, endian);
  [branch] if (duplicate_second.x) {
    packed_56.zw = packed_78.xy;
  }
  [branch] if (duplicate_second.y) {
    packed_23.xy = packed_56.xy;
  }
  xe_resolve_dest[dest_address_int2s + 2u] = packed_23.xy;
  xe_resolve_dest[dest_address_int2s + 5u] = packed_56.xy;
  xe_resolve_dest[dest_address_int2s + 6u] = packed_56.zw;
  xe_resolve_dest[dest_address_int2s + 7u] = packed_78.xy;
  xe_resolve_dest[dest_address_int2s + 8u] = packed_78.zw;
}
