#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
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
  uint2 packed = XePack16bpp4PixelsInUInt2(host_pixel_y0x0, host_pixel_y0x1,
                                           host_pixel_y1x0, host_pixel_y1x1,
                                           XeResolveDestFormat());
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    if (xe_thread_id.x == 0u) {
      packed = (packed & 0xFFFF0000u) | (packed >> 16u);
    }
    if (xe_thread_id.y == 0u) {
      packed.x = packed.y;
    }
  }
  xe_resolve_dest[XeResolveDestPixelAddress(xe_thread_id.xy, 1u) >> (3u - 2u)] =
      XeEndianSwap16(packed, XeResolveDestEndian128());
}
