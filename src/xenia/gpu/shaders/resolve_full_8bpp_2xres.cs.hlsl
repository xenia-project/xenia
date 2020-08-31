#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 guest pixels.
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  uint2 pixel_index = xe_thread_id.xy << uint2(1u, 0u);
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  float4 guest_pixel_0, guest_pixel_1;
  XeResolveLoadGuest2PixelsRedColorsX2(
      xe_resolve_source, XeResolveColorCopySourcePixelAddressInts(pixel_index),
      guest_pixel_0, guest_pixel_1);
  // Convert to R8.
  // TODO(Triang3l): Investigate formats 8_A and 8_B.
  uint2 packed = uint2(XePackR8G8B8A8UNorm(guest_pixel_0),
                       XePackR8G8B8A8UNorm(guest_pixel_1));
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    if (pixel_index.x == 0u) {
      packed.x = (packed.x & 0xFF00FF00u) | (packed.x >> 8u);
    }
    if (pixel_index.y == 0u) {
      packed = (packed & 0xFFFF0000u) | (packed >> 16u);
    }
  }
  xe_resolve_dest[XeResolveDestPixelAddress(pixel_index, 0u) >> (3u - 2u)] =
      packed;
}
