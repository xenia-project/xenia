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
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    [branch] if (xe_thread_id.x == 0u) {
      host_pixel_y0x0 = host_pixel_y0x1;
      host_pixel_y1x0 = host_pixel_y1x1;
    }
    [branch] if (xe_thread_id.y == 0u) {
      host_pixel_y0x0 = host_pixel_y1x0;
      host_pixel_y0x1 = host_pixel_y1x1;
    }
  }
  // Only 32_32_32_32_FLOAT color format is 128bpp.
  uint endian = XeResolveDestEndian128();
  uint dest_address =
      XeResolveDestPixelAddress(xe_thread_id.xy, 4u) >> (4u - 2u);
  xe_resolve_dest[dest_address] =
      XeEndianSwap128(asuint(host_pixel_y0x0), endian);
  xe_resolve_dest[dest_address + 1u] =
      XeEndianSwap128(asuint(host_pixel_y0x1), endian);
  xe_resolve_dest[dest_address + 2u] =
      XeEndianSwap128(asuint(host_pixel_y1x0), endian);
  xe_resolve_dest[dest_address + 3u] =
      XeEndianSwap128(asuint(host_pixel_y1x1), endian);
}
