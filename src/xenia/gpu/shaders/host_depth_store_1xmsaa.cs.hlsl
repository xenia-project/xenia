#include "edram.hlsli"
#include "host_depth_store.hlsli"

Texture2D<float> xe_host_depth_store_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 samples (same as resolve granularity).
  uint2 resolution_scale = XeHostDepthStoreResolutionScale();
  // Group height can't cross resolve granularity, Y overflow check not needed.
  [branch] if (xe_thread_id.x >= XeHostDepthStoreScaledWidthDiv8()) {
    return;
  }
  uint2 pixel_index =
      XeHostDepthStoreScaledOrigin() + (xe_thread_id.xy << uint2(3u, 0u));
  uint edram_address_int4s =
      XeEdramOffsetInts(pixel_index, 0u, XeHostDepthStorePitchTiles(),
                        kXenosMsaaSamples_1X, false, 0u, 0u,
                        XeHostDepthStoreResolutionScale())
      >> 2u;
  int3 source_pixel_index = int3(pixel_index, 0);
  xe_host_depth_store_dest[edram_address_int4s] = asuint(float4(
      xe_host_depth_store_source.Load(source_pixel_index),
      xe_host_depth_store_source.Load(source_pixel_index + int3(1, 0, 0)),
      xe_host_depth_store_source.Load(source_pixel_index + int3(2, 0, 0)),
      xe_host_depth_store_source.Load(source_pixel_index + int3(3, 0, 0))));
  xe_host_depth_store_dest[edram_address_int4s + 1u] = asuint(float4(
      xe_host_depth_store_source.Load(source_pixel_index + int3(4, 0, 0)),
      xe_host_depth_store_source.Load(source_pixel_index + int3(5, 0, 0)),
      xe_host_depth_store_source.Load(source_pixel_index + int3(6, 0, 0)),
      xe_host_depth_store_source.Load(source_pixel_index + int3(7, 0, 0))));
}
