#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest pixel.
  // Group size is the same as resolve granularity, overflow check not needed.
  bool2 duplicate_second = XeResolveEdramDuplicateSecondHostPixel()
                               ? xe_thread_id.xy == 0u
                               : bool2(false, false);
  uint endian = XeResolveDestEndian128();
  uint dest_address_int4s =
      (XeResolveDestPixelAddress(xe_thread_id.xy, 4u) * 9u) >> 4u;
  // XYZ
  // W4-
  // ---
  uint source_address_bytes =
      XeResolveColorCopySourcePixelAddressInts(xe_thread_id.xy) * (9u * 4u);
  float4 source_pixel_y0x0, source_pixel_y0x1, source_pixel_y0x2;
  float4 source_pixel_y1x0, source_pixel_y1x1;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                source_pixel_y0x0, source_pixel_y0x1,
                                source_pixel_y0x2, source_pixel_y1x0,
                                source_pixel_y1x1);
  // y0x2 will be stored later - need y1x2 instead for duplicate_second.y.
  uint4 store_pixel_uint_y0x0 = asuint(source_pixel_y0x0);
  uint4 store_pixel_uint_y0x1 = asuint(source_pixel_y0x1);
  uint4 store_pixel_uint_y1x0 = asuint(source_pixel_y1x0);
  uint4 store_pixel_uint_y1x1 = asuint(source_pixel_y1x1);
  [branch] if (duplicate_second.x) {
    store_pixel_uint_y0x0 = store_pixel_uint_y0x1;
    store_pixel_uint_y1x0 = store_pixel_uint_y1x1;
  }
  [branch] if (duplicate_second.y) {
    store_pixel_uint_y0x0 = store_pixel_uint_y1x0;
    store_pixel_uint_y0x1 = store_pixel_uint_y1x1;
  }
  XeEndianSwap128(store_pixel_uint_y0x0, store_pixel_uint_y0x1,
                  store_pixel_uint_y1x0, store_pixel_uint_y1x1, endian);
  xe_resolve_dest[dest_address_int4s] = store_pixel_uint_y0x0;
  xe_resolve_dest[dest_address_int4s + 1u] = store_pixel_uint_y0x1;
  xe_resolve_dest[dest_address_int4s + 3u] = store_pixel_uint_y1x0;
  xe_resolve_dest[dest_address_int4s + 4u] = store_pixel_uint_y1x1;
  // +++
  // ++X
  // YZW
  float4 source_pixel_y1x2;
  float4 source_pixel_y2x0, source_pixel_y2x1, source_pixel_y2x2;
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u,
                                source_pixel_y1x2, source_pixel_y2x0,
                                source_pixel_y2x1, source_pixel_y2x2);
  uint4 store_pixel_uint_y0x2 =
      asuint(duplicate_second.y ? source_pixel_y1x2 : source_pixel_y0x2);
  uint4 store_pixel_uint_y1x2 = asuint(source_pixel_y1x2);
  uint4 store_pixel_uint_y2x0 =
      asuint(duplicate_second.x ? source_pixel_y2x1 : source_pixel_y2x0);
  uint4 store_pixel_uint_y2x1 = asuint(source_pixel_y2x1);
  uint4 store_pixel_uint_y2x2 = asuint(source_pixel_y2x2);
  XeEndianSwap128(store_pixel_uint_y0x2, store_pixel_uint_y1x2,
                  store_pixel_uint_y2x0, store_pixel_uint_y2x1,
                  store_pixel_uint_y2x2, endian);
  xe_resolve_dest[dest_address_int4s + 2u] = store_pixel_uint_y0x2;
  xe_resolve_dest[dest_address_int4s + 5u] = store_pixel_uint_y1x2;
  xe_resolve_dest[dest_address_int4s + 6u] = store_pixel_uint_y2x0;
  xe_resolve_dest[dest_address_int4s + 7u] = store_pixel_uint_y2x1;
  xe_resolve_dest[dest_address_int4s + 8u] = store_pixel_uint_y2x2;
}
