#include "edram.hlsli"
#include "host_depth_store.hlsli"

Texture2DMS<float> xe_host_depth_store_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 samples (8x0.5 pixels, resolve granularity is 8 pixels).
  uint resolution_scale = XeHostDepthStoreResolutionScale();
  // Group height is aligned to resolve granularity, Y overflow check not
  // needed.
  [branch] if (xe_thread_id.x >
               XeHostDepthStoreWidthDiv8Minus1() * resolution_scale) {
    return;
  }
  uint2 pixel_index = XeHostDepthStoreOrigin() * resolution_scale +
                      uint2(xe_thread_id.x << 3u, xe_thread_id.y >> 1u);
  uint dest_sample_index = xe_thread_id.y & 1u;
  uint edram_address_int4s =
      XeEdramOffsetInts(pixel_index, 0u, XeHostDepthStorePitchTiles(),
                        kXenosMsaaSamples_2X, false, 0u, dest_sample_index,
                        resolution_scale)
      >> 2u;
  // Top and bottom to Direct3D 10.1+ top 1 and bottom 0 (for 2x) or top-left 0
  // and bottom-right 3 (for 4x).
  int source_sample_index =
      XeHostDepthStoreMsaa2xSupported() ? (dest_sample_index ? 0u : 1u)
                                        : (dest_sample_index ? 3u : 0u);
  xe_host_depth_store_dest[edram_address_int4s] = asuint(float4(
      xe_host_depth_store_source.Load(int2(pixel_index), source_sample_index),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(1, 0),
                                      source_sample_index),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(2, 0),
                                      source_sample_index),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(3, 0),
                                      source_sample_index)));
  xe_host_depth_store_dest[edram_address_int4s + 1u] = asuint(float4(
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(4, 0),
                                      source_sample_index),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(5, 0),
                                      source_sample_index),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(6, 0),
                                      source_sample_index),
      xe_host_depth_store_source.Load(int2(pixel_index) + int2(7, 0),
                                      source_sample_index)));
}
