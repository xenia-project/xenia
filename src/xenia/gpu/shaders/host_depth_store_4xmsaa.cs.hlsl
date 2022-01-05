#include "edram.hlsli"
#include "host_depth_store.hlsli"

Texture2DMS<float> xe_host_depth_store_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 samples (4x0.5 pixels, resolve granularity is 8 pixels).
  // Group height can't cross resolve granularity, Y overflow check not needed.
  [branch] if ((xe_thread_id.x >> 1u) >= XeHostDepthStoreScaledWidthDiv8()) {
    return;
  }
  uint2 pixel_index = XeHostDepthStoreScaledOrigin() +
                      uint2(xe_thread_id.x << 2u, xe_thread_id.y >> 1u);
  // For simplicity, passing samples directly, not pixels, to XeEdramOffsetInts.
  uint edram_address_int4s =
      XeEdramOffsetInts((pixel_index << 1u) | (xe_thread_id.xy & 1u), 0u,
                        XeHostDepthStorePitchTiles(), kXenosMsaaSamples_1X,
                        false, 0u, 0u, XeHostDepthStoreResolutionScale())
      >> 2u;
  // Render target horizontal sample in bit 0, vertical sample in bit 1.
  int source_sample_left = int((xe_thread_id.y & 1u) << 1u);
  int source_sample_right = source_sample_left + 1;
  xe_host_depth_store_dest[edram_address_int4s] = asuint(float4(
      xe_host_depth_store_source.Load(int2(pixel_index), source_sample_left),
      xe_host_depth_store_source.Load(int2(pixel_index), source_sample_right),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(1, 0),
                                      source_sample_left),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(1, 0),
                                      source_sample_right)));
  xe_host_depth_store_dest[edram_address_int4s + 1u] = asuint(float4(
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(2, 0),
                                      source_sample_left),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(2, 0),
                                      source_sample_right),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(3, 0),
                                      source_sample_left),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(3, 0),
                                      source_sample_right)));
}
