#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 guest pixels.
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  [branch] if (xe_thread_id.x >= XeResolveSizeDiv8().x) {
    return;
  }
  uint2 pixel_index = xe_thread_id.xy << uint2(3u, 0u);
  bool2 duplicate_second = XeResolveEdramDuplicateSecondHostPixel()
                               ? pixel_index == 0u
                               : bool2(false, false);
  uint dest_address_int2s =
      (XeResolveDestPixelAddress(pixel_index, 0u) * 9u) >> 3u;
  // Converting to R8.
  // TODO(Triang3l): Investigate formats 8_A and 8_B.
  // Guest pixel 0.
  // +0: 0123, 4567
  // +1: 8---, ----
  uint source_address_bytes =
      XeResolveColorCopySourcePixelAddressInts(pixel_index) * (9u * 4u);
  float4 host_pixels_0123, host_pixels_4567;
  float host_pixels_8;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.x) {
    host_pixels_0123.x = host_pixels_0123.y;
    host_pixels_0123.w = host_pixels_4567.x;
    host_pixels_4567.z = host_pixels_4567.w;
  }
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      XePackR8G8B8A8UNorm(host_pixels_0123),
      XePackR8G8B8A8UNorm(host_pixels_4567));
  uint packed_next_x = XePackR8UNorm(host_pixels_8);
  // Guest pixel 1.
  // +1: <012, 3456
  // +2: 78--, ----
  uint source_pixel_stride_bytes =
      (9u * 4u) <<
      (uint(XeResolveEdramFormatIs64bpp()) +
       uint(XeResolveEdramMsaaSamples() >= kXenosMsaaSamples_4X));
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      packed_next_x | (XePackR8G8B8UNorm(host_pixels_0123.xyz) << 8u),
      XePackR8G8B8A8UNorm(float4(host_pixels_0123.w, host_pixels_4567.xyz)));
  packed_next_x = XePackR8G8UNorm(float2(host_pixels_4567.w, host_pixels_8));
  // Guest pixel 2.
  // +2: <<01, 2345
  // +3: 678-, ----
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      packed_next_x | (XePackR8G8UNorm(host_pixels_0123.xy) << 16u),
      XePackR8G8B8A8UNorm(float4(host_pixels_0123.zw, host_pixels_4567.xy)));
  packed_next_x = XePackR8G8B8UNorm(float3(host_pixels_4567.zw, host_pixels_8));
  // Guest pixel 3.
  // +3: <<<0, 1234
  // +4: 5678, ----
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      packed_next_x | (XePackR8UNorm(host_pixels_0123.x) << 24u),
      XePackR8G8B8A8UNorm(float4(host_pixels_0123.yzw, host_pixels_4567.x)));
  packed_next_x =
      XePackR8G8B8A8UNorm(float4(host_pixels_4567.yzw, host_pixels_8));
  // Guest pixel 4.
  // +4: <<<<, 0123
  // +5: 4567, 8---
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] =
      uint2(packed_next_x, XePackR8G8B8A8UNorm(host_pixels_0123));
  packed_next_x = XePackR8G8B8A8UNorm(host_pixels_4567);
  uint packed_next_y = XePackR8UNorm(host_pixels_8);
  // Guest pixel 5.
  // +5: <<<<, <012
  // +6: 3456, 78--
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      packed_next_x,
      packed_next_y | (XePackR8G8B8UNorm(host_pixels_0123.xyz) << 8u));
  packed_next_x =
      XePackR8G8B8A8UNorm(float4(host_pixels_0123.w, host_pixels_4567.xyz));
  packed_next_y = XePackR8G8UNorm(float2(host_pixels_4567.w, host_pixels_8));
  // Guest pixel 6.
  // +6: <<<<, <<01
  // +7: 2345, 678-
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      packed_next_x,
      packed_next_y | (XePackR8G8UNorm(host_pixels_0123.xy) << 16u));
  packed_next_x =
      XePackR8G8B8A8UNorm(float4(host_pixels_0123.zw, host_pixels_4567.xy));
  packed_next_y = XePackR8G8B8UNorm(float3(host_pixels_4567.zw, host_pixels_8));
  // Guest pixel 7.
  // +7: <<<<, <<<0
  // +8: 1234, 5678
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad9RedColorsX3(xe_resolve_source, source_address_bytes,
                            host_pixels_0123, host_pixels_4567, host_pixels_8);
  [branch] if (duplicate_second.y) {
    host_pixels_0123.xyz = float3(host_pixels_0123.w, host_pixels_4567.xy);
  }
  xe_resolve_dest[dest_address_int2s++] = uint2(
      packed_next_x,
      packed_next_y | (XePackR8UNorm(host_pixels_0123.x) << 24u));
  xe_resolve_dest[dest_address_int2s++] = uint2(
      XePackR8G8B8A8UNorm(float4(host_pixels_0123.yzw, host_pixels_4567.x)),
      XePackR8G8B8A8UNorm(float4(host_pixels_4567.yzw, host_pixels_8)));
}
