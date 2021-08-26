#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest pixel (2x faster than 1 thread = 8 guest pixels).
  // Group size is the same as resolve granularity, overflow check not needed.
  float4 host_pixel_y0x0, host_pixel_y0x1, host_pixel_y1x0, host_pixel_y1x1;
  XeResolveLoadGuestPixelRGBAColorsX2(
      xe_resolve_source,
      XeResolveColorCopySourcePixelAddressInts(xe_thread_id.xy),
      host_pixel_y0x0, host_pixel_y0x1, host_pixel_y1x0, host_pixel_y1x1);
  uint4 packed = XePack32bpp4Pixels(host_pixel_y0x0, host_pixel_y0x1,
                                    host_pixel_y1x0, host_pixel_y1x1,
                                    XeResolveDestFormat());
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    if (xe_thread_id.x == 0u) {
      packed.xz = packed.yw;
    }
    if (xe_thread_id.y == 0u) {
      packed.xy = packed.zw;
    }
  }
  xe_resolve_dest[XeResolveDestPixelAddress(xe_thread_id.xy, 2u) >> (4u - 2u)] =
      XeEndianSwap32(packed, XeResolveDestEndian128());
}
