#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 guest pixels.
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  uint2 pixel_index = xe_thread_id.xy << uint2(1u, 0u);
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  bool2 duplicate_second = XeResolveEdramDuplicateSecondHostPixel()
                               ? pixel_index == 0u
                               : bool2(false, false);
  uint dest_format = XeResolveDestFormat();
  uint endian = XeResolveDestEndian128();
  uint dest_address_int2s =
      (XeResolveDestPixelAddress(pixel_index, 2u) * 9u) >> 3u;
  // XYZ ---
  // W4- ---
  // --- ---
  uint source_address_bytes =
      XeResolveColorCopySourcePixelAddressInts(pixel_index) * (9u * 4u);
  float4 host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3, host_pixel_4;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  uint4 packed_top;
  uint packed_top_4;
  XePack32bpp5Pixels(host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3,
                     host_pixel_4, dest_format, packed_top, packed_top_4);
  XeEndianSwap32(packed_top, packed_top_4, endian);
  [branch] if (duplicate_second.x) {
    packed_top.x = packed_top.y;
    packed_top.w = packed_top_4;
  }
  [branch] if (duplicate_second.y) {
    packed_top.x = packed_top.w;
    packed_top.y = packed_top_4;
  }
  xe_resolve_dest[dest_address_int2s] = packed_top.xy;
  // +++ ---
  // ++X ---
  // YZW ---
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  uint4 packed_bottom = XePack32bpp4Pixels(host_pixel_0, host_pixel_1,
                                           host_pixel_2, host_pixel_3,
                                           dest_format);
  packed_bottom = XeEndianSwap32(packed_bottom, endian);
  [branch] if (duplicate_second.x) {
    packed_bottom.y = packed_bottom.z;
  }
  [branch] if (duplicate_second.y) {
    packed_top.z = packed_bottom.x;
  }
  xe_resolve_dest[dest_address_int2s + 1u] = packed_top.zw;
  xe_resolve_dest[dest_address_int2s + 2u] =
      uint2(packed_top_4, packed_bottom.x);
  xe_resolve_dest[dest_address_int2s + 3u] = packed_bottom.yz;
  // +++ XYZ
  // +++ W4-
  // +++ ---
  source_address_bytes +=
      (9u * 4u) << uint(XeResolveEdramMsaaSamples() >= kXenosMsaaSamples_4X);
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  XePack32bpp5Pixels(host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3,
                     host_pixel_4, dest_format, packed_top, packed_top_4);
  XeEndianSwap32(packed_top, packed_top_4, endian);
  [branch] if (duplicate_second.y) {
    packed_top.x = packed_top.w;
    packed_top.y = packed_top_4;
  }
  xe_resolve_dest[dest_address_int2s + 4u] =
      uint2(packed_bottom.w, packed_top.x);
  // +++ +++
  // +++ ++X
  // +++ YZW
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  packed_bottom = XePack32bpp4Pixels(host_pixel_0, host_pixel_1, host_pixel_2,
                                     host_pixel_3, dest_format);
  packed_bottom = XeEndianSwap32(packed_bottom, endian);
  [branch] if (duplicate_second.y) {
    packed_top.z = packed_bottom.x;
  }
  xe_resolve_dest[dest_address_int2s + 5u] = packed_top.yz;
  xe_resolve_dest[dest_address_int2s + 6u] = uint2(packed_top.w, packed_top_4);
  xe_resolve_dest[dest_address_int2s + 7u] = packed_bottom.xy;
  xe_resolve_dest[dest_address_int2s + 8u] = packed_bottom.zw;
}
