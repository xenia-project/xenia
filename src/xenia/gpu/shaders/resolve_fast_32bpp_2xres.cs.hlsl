#include "endian.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 guest pixels (same byte count as in the 1x resolution
  // shaders).
  uint2 pixel_index = xe_thread_id.xy << uint2(1u, 0u);
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  uint msaa_samples = XeResolveEdramMsaaSamples();
  // 1 host uint4 == 1 guest uint.
  uint guest_source_address_ints =
      XeEdramOffsetInts(pixel_index + XeResolveOffset(),
                        XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
                        msaa_samples, XeResolveEdramIsDepth(), 0u,
                        XeResolveFirstSampleIndex());
  uint4 guest_pixel_0 = xe_resolve_source[guest_source_address_ints];
  uint4 guest_pixel_1 =
      xe_resolve_source[guest_source_address_ints +
                        (msaa_samples >= kXenosMsaaSamples_4X ? 2u : 1u)];
  XeResolveSwap8PixelsRedBlue32bpp(guest_pixel_0, guest_pixel_1);
  [branch] if (XeResolveEdramDuplicateSecondHostPixel()) {
    if (pixel_index.x == 0u) {
      guest_pixel_0.xz = guest_pixel_0.yw;
    }
    if (pixel_index.y == 0u) {
      guest_pixel_0.xy = guest_pixel_0.zw;
      guest_pixel_1.xy = guest_pixel_1.zw;
    }
  }
  uint endian = XeResolveDestEndian128();
  uint dest_address = XeResolveDestPixelAddress(pixel_index, 2u) >> (4u - 2u);
  xe_resolve_dest[dest_address] = XeEndianSwap32(guest_pixel_0, endian);
  xe_resolve_dest[dest_address + 1u] = XeEndianSwap32(guest_pixel_1, endian);
}
