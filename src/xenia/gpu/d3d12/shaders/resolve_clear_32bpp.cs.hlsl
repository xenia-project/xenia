#define XE_RESOLVE_CLEAR
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 samples (same as resolve granularity).
  uint2 extent_scale =
      uint2(XeResolveEdramMsaaSamples() >= uint2(kXenosMsaaSamples_4X,
                                                 kXenosMsaaSamples_2X));
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (xe_thread_id.x >= (XeResolveSizeDiv8().x << extent_scale.x)) {
    return;
  }
  uint address_int4s =
      XeEdramOffsetInts(
          (xe_thread_id.xy << uint2(3u, 0u)) +
              (XeResolveOffset() << extent_scale),
          XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
          kXenosMsaaSamples_1X, XeResolveEdramIsDepth(), 0u)
      >> 2u;
  xe_resolve_dest[address_int4s] = xe_resolve_clear_value.xxxx;
  xe_resolve_dest[address_int4s + 1u] = xe_resolve_clear_value.xxxx;
}
