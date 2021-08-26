#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
Buffer<uint2> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest pixel.
  // Group size is the same as resolve granularity, overflow check not needed.
  uint host_source_address_int2s =
      (XeEdramOffsetInts(xe_thread_id.xy + XeResolveOffset(),
                         XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
                         XeResolveEdramMsaaSamples(), false, 1u,
                         XeResolveFirstSampleIndex()) * 9u) >> 1u;
  uint2 source_xy_min =
      XeResolveEdramDuplicateSecondHostPixel() ? uint2(xe_thread_id.xy == 0u)
                                               : uint2(0u, 0u);
  uint dest_format = XeResolveDestFormat();
  uint endian = XeResolveDestEndian128();
  uint dest_address_int4s =
      (XeResolveDestPixelAddress(xe_thread_id.xy, 4u) * 9u) >> 4u;
  uint x, y;
  [unroll] for (y = 0u; y < 3u; ++y) {
    uint source_y = max(y, source_xy_min.y);
    [unroll] for (x = 0u; x < 3u; ++x) {
      xe_resolve_dest[dest_address_int4s++] = XeEndianSwap128(
          asuint(XeResolveLoadHost64bppPixelFromUInt2(
                     xe_resolve_source, 9u,
                     host_source_address_int2s + source_y * 3u +
                         max(x, source_xy_min.x))),
          endian);
    }
  }
}
