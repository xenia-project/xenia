#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
Buffer<uint2> xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 guest pixels.
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  uint2 pixel_index = xe_thread_id.xy << uint2(2u, 0u);
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  uint host_source_address_int2s =
      (XeEdramOffsetInts(pixel_index + XeResolveOffset(),
                         XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
                         XeResolveEdramMsaaSamples(), false, 1u,
                         XeResolveFirstSampleIndex()) * 9u) >> 1u;
  uint source_pixel_stride_int2s =
      9u << uint(XeResolveEdramMsaaSamples() >= kXenosMsaaSamples_4X);
  uint2 source_xy_min =
      XeResolveEdramDuplicateSecondHostPixel() ? uint2(pixel_index == 0u)
                                               : uint2(0u, 0u);
  uint dest_format = XeResolveDestFormat();
  uint endian = XeResolveDestEndian128();
  uint dest_address_int2s =
      (XeResolveDestPixelAddress(pixel_index, 1u) * 9u) >> 3u;
  uint p, x, y;
  uint host_pixel_index = 0u;
  float4 host_pixel_previous = (0.0f).xxxx;
  uint2 host_pixels_packed = (0u).xx;
  [unroll] for (p = 0u; p < 4u; ++p) {
    [unroll] for (y = 0u; y < 3u; ++y) {
      uint source_y = max(y, source_xy_min.y);
      [unroll] for (x = 0u; x < 3u; ++x) {
        float4 host_pixel = XeResolveLoadHost64bppPixelFromUInt2(
            xe_resolve_source, 9u,
            host_source_address_int2s + source_y * 3u + max(x, source_xy_min.x));
        if (host_pixel_index & 1u) {
          // High 16 bits within 32 bits - convert and push.
          host_pixels_packed.x = host_pixels_packed.y;
          host_pixels_packed.y = XePack16bpp2PixelsInUInt(
              host_pixel_previous, host_pixel, dest_format);
          if (host_pixel_index & 2u) {
            // Write four host pixels.
            xe_resolve_dest[dest_address_int2s++] =
                XeEndianSwap16(host_pixels_packed, endian);
          }
        } else {
          // Low 16 bits within 32 bits.
          host_pixel_previous = host_pixel;
        }
        ++host_pixel_index;
      }
    }
    host_source_address_int2s += source_pixel_stride_int2s;
  }
}
