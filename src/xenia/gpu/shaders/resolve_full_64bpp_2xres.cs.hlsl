#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest pixel.
  // Group size is the same as resolve granularity, overflow check not needed.
  float4 host_pixel_y0x0, host_pixel_y0x1, host_pixel_y1x0, host_pixel_y1x1;
  XeResolveLoadGuestPixelRGBAColorsX2(
      xe_resolve_source,
      XeResolveColorCopySourcePixelAddressInts(xe_thread_id.xy),
      host_pixel_y0x0, host_pixel_y0x1, host_pixel_y1x0, host_pixel_y1x1);
  uint4 packed_y0, packed_y1;
  XePack64bpp4Pixels(host_pixel_y0x0, host_pixel_y0x1, host_pixel_y1x0,
                     host_pixel_y1x1, XeResolveDestFormat(), packed_y0,
                     packed_y1);
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    if (xe_thread_id.x == 0u) {
      packed_y0.xy = packed_y0.zw;
      packed_y1.xy = packed_y1.zw;
    }
    if (xe_thread_id.y == 0u) {
      packed_y0 = packed_y1;
    }
  }
  uint endian = XeResolveDestEndian128();
  uint dest_address =
      XeResolveDestPixelAddress(xe_thread_id.xy, 3u) >> (4u - 2u);
  xe_resolve_dest[dest_address] = XeEndianSwap64(packed_y0, endian);
  xe_resolve_dest[dest_address + 1u] = XeEndianSwap64(packed_y1, endian);
}
