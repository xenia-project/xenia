#include "endian.xesli"
#include "resolve.hlsli"

RWBuffer<uint4> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 host pixels.
  uint2 pixel_index = xe_thread_id.xy << uint2(1u, 0u);
  // Group height can't cross resolve granularity, Y overflow check not needed.
  [branch] if (pixel_index.x >= XeResolveScaledSize().x) {
    return;
  }
  float4 pixel_0, pixel_1;
  XeResolveLoad2RGBAColors(
      xe_resolve_source,
      XeResolveColorCopySourcePixelAddressIntsYDuplicating(pixel_index),
      pixel_0, pixel_1);
  if (XeResolveDuplicateSecondHostPixel().x && pixel_index.x == 0u) {
    pixel_0 = pixel_1;
  }
  // Only 32_32_32_32_FLOAT color format is 128bpp.
  uint endian = XeResolveDestEndian128();
  uint dest_address = XeResolveDestPixelAddress(pixel_index, 4u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap128(asuint(pixel_0), endian);
  dest_address +=
      XeResolveDestRightConsecutiveBlocksOffset(pixel_index.x, 4u) >> 4u;
  xe_resolve_dest[dest_address] = XeEndianSwap128(asuint(pixel_1), endian);
}
