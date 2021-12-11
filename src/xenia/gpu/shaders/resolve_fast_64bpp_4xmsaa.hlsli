#include "endian.hlsli"
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint2> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 host pixels.
  uint2 pixel_index = xe_thread_id.xy << uint2(2u, 0u);
  // Group height can't cross resolve granularity, Y overflow check not needed.
  [branch] if (pixel_index.x >= XeResolveScaledSize().x) {
    return;
  }
  bool2 duplicate_second = XeResolveDuplicateSecondHostPixel();
  uint source_address_int2s =
      XeEdramOffsetInts(
          uint2(pixel_index.x, max(pixel_index.y, uint(duplicate_second.y))) +
              XeResolveScaledOffset(),
          XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
          kXenosMsaaSamples_4X, false, 1u, XeResolveFirstSampleIndex(),
          XeResolveResolutionScale())
      >> 1u;
  uint4 pixels_01, pixels_23;
  pixels_01.xy = xe_resolve_source[
      source_address_int2s +
      uint(duplicate_second.x && pixel_index.x == 0u) * 2u];
  pixels_01.zw = xe_resolve_source[source_address_int2s + 2u];
  pixels_23.xy = xe_resolve_source[source_address_int2s + 4u];
  pixels_23.zw = xe_resolve_source[source_address_int2s + 6u];
  XeResolveSwap4PixelsRedBlue64bpp(pixels_01, pixels_23);
  uint endian = XeResolveDestEndian128();
  uint dest_address = XeResolveDestPixelAddress(pixel_index, 3u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap64(pixels_01, endian);
  dest_address +=
      XeResolveDestRightConsecutiveBlocksOffset(pixel_index.x, 3u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap64(pixels_23, endian);
}
