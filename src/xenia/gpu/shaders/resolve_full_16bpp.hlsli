#include "endian.hlsli"
#include "pixel_formats.xesli"
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 host pixels.
  uint2 pixel_index = xe_thread_id.xy << uint2(2u, 0u);
  // Group height can't cross resolve granularity, Y overflow check not needed.
  [branch] if (pixel_index.x >= XeResolveScaledSize().x) {
    return;
  }
  float4 pixel_0, pixel_1, pixel_2, pixel_3;
  XeResolveLoad4RGBAColors(
      xe_resolve_source,
      XeResolveColorCopySourcePixelAddressIntsYDuplicating(pixel_index),
      pixel_0, pixel_1, pixel_2, pixel_3);
  if (XeResolveDuplicateSecondHostPixel().x && pixel_index.x == 0u) {
    pixel_0 = pixel_1;
  }
  xe_resolve_dest[XeResolveDestPixelAddress(pixel_index, 1u) >> 3u] =
      XeEndianSwap16(XePack16bpp4PixelsInUInt2(pixel_0, pixel_1, pixel_2,
                                               pixel_3, XeResolveDestFormat()),
                     XeResolveDestEndian128());
}
