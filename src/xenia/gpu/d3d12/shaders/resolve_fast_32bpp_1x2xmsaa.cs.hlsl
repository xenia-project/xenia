#include "endian.hlsli"
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint4> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 pixels.
  uint2 pixel_index = xe_thread_id.xy << uint2(3u, 0u);
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  uint source_address_int4s =
      XeEdramOffsetInts(pixel_index + XeResolveOffset(),
                        XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
                        XeResolveEdramMsaaSamples(), XeResolveEdramIsDepth(),
                        0u, XeResolveFirstSampleIndex())
      >> 2u;
  uint4 pixels_0123 = xe_resolve_source[source_address_int4s];
  uint4 pixels_4567 = xe_resolve_source[source_address_int4s + 1u];
  XeResolveSwap8PixelsRedBlue32bpp(pixels_0123, pixels_4567);
  uint endian = XeResolveDestEndian128();
  uint dest_address = XeResolveDestPixelAddress(pixel_index, 2u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap32(pixels_0123, endian);
  // Odd 4 pixels = even 4 pixels + 32 bytes when tiled.
  xe_resolve_dest[dest_address + 2u] = XeEndianSwap32(pixels_4567, endian);
}
