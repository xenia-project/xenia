#include "endian.hlsli"
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 pixels.
  uint2 pixel_index = xe_thread_id.xy << uint2(2u, 0u);
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  uint source_address_int4s =
      XeEdramOffsetInts(pixel_index + XeResolveOffset(),
                        XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
                        XeResolveEdramMsaaSamples(), false, 1u,
                        XeResolveFirstSampleIndex())
      >> 2u;
  uint4 pixels_01 = xe_resolve_source[source_address_int4s];
  uint4 pixels_23 = xe_resolve_source[source_address_int4s + 1u];
  XeResolveSwap4PixelsRedBlue64bpp(pixels_01, pixels_23);
  uint endian = XeResolveDestEndian128();
  uint dest_address = XeResolveDestPixelAddress(pixel_index, 3u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap64(pixels_01, endian);
  // Odd 2 pixels = even 2 pixels + 32 bytes when tiled.
  xe_resolve_dest[dest_address + 2u] = XeEndianSwap64(pixels_23, endian);
}
