#include "endian.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest pixel (same byte count as in the 1x resolution shaders).
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (xe_thread_id.x >= XeResolveSize().x) {
    return;
  }
  // 1 host uint4 == 1 guest uint.
  uint guest_source_address_ints =
      XeEdramOffsetInts(xe_thread_id.xy + XeResolveOffset(),
                        XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
                        XeResolveEdramMsaaSamples(), false, 1u,
                        XeResolveFirstSampleIndex());
  uint4 guest_pixel_y0 = xe_resolve_source[guest_source_address_ints];
  uint4 guest_pixel_y1 = xe_resolve_source[guest_source_address_ints + 1u];
  XeResolveSwap4PixelsRedBlue64bpp(guest_pixel_y0, guest_pixel_y1);
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    if (xe_thread_id.x == 0u) {
      guest_pixel_y0.xy = guest_pixel_y0.zw;
      guest_pixel_y1.xy = guest_pixel_y1.zw;
    }
    if (xe_thread_id.y == 0u) {
      guest_pixel_y0 = guest_pixel_y1;
    }
  }
  uint endian = XeResolveDestEndian128();
  uint dest_address =
      XeResolveDestPixelAddress(xe_thread_id.xy, 3u) >> (4u - 2u);
  xe_resolve_dest[dest_address] = XeEndianSwap64(guest_pixel_y0, endian);
  xe_resolve_dest[dest_address + 1u] = XeEndianSwap64(guest_pixel_y1, endian);
}
