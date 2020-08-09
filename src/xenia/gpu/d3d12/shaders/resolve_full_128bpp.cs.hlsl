#include "endian.hlsli"
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 pixels.
  uint2 pixel_index = xe_thread_id.xy << uint2(1u, 0u);
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  float4 pixel_0, pixel_1;
  XeResolveLoad2RGBAColorsX1(
      xe_resolve_source, XeResolveColorCopySourcePixelAddressInts(pixel_index),
      pixel_0, pixel_1);
  // Only 32_32_32_32_FLOAT color format is 128bpp.
  uint endian = XeResolveDestEndian128();
  uint dest_address = XeResolveDestPixelAddress(pixel_index, 4u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap128(asuint(pixel_0), endian);
  // Odd pixel = even pixel + 16 bytes when tiled.
  xe_resolve_dest[dest_address + 2u] = XeEndianSwap128(asuint(pixel_1), endian);
}
