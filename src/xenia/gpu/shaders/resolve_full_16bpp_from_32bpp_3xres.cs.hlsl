#include "endian.hlsli"
#include "pixel_formats.hlsli"
#define XE_RESOLVE_RESOLUTION_SCALED
#include "resolve.hlsli"

RWBuffer<uint2> xe_resolve_dest : register(u0);
ByteAddressBuffer xe_resolve_source : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 guest pixels.
  // Group height is the same as resolve granularity, Y overflow check not
  // needed.
  uint2 pixel_index = xe_thread_id.xy << uint2(2u, 0u);
  [branch] if (pixel_index.x >= XeResolveSize().x) {
    return;
  }
  bool2 duplicate_second = XeResolveEdramDuplicateSecondHostPixel()
                               ? pixel_index == 0u
                               : bool2(false, false);
  uint dest_format = XeResolveDestFormat();
  uint endian = XeResolveDestEndian128();
  uint dest_address_int2s =
      (XeResolveDestPixelAddress(pixel_index, 1u) * 9u) >> 3u;
  // XYZ --- --- ---
  // W4x --- --- ---
  // yzw --- --- ---
  uint source_address_bytes =
      XeResolveColorCopySourcePixelAddressInts(pixel_index) * (9u * 4u);
  float4 host_pixel_0, host_pixel_1, host_pixel_2, host_pixel_3, host_pixel_4;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  uint4 packed_top;
  uint packed_top_4;
  XePack16bpp5PixelsInUInt41(host_pixel_0, host_pixel_1, host_pixel_2,
                             host_pixel_3, host_pixel_4, dest_format,
                             packed_top, packed_top_4);
  XeEndianSwap16(packed_top, packed_top_4, endian);
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  uint4 packed_bottom = XePack16bpp4PixelsInUInt4(host_pixel_0, host_pixel_1,
                                                  host_pixel_2, host_pixel_3,
                                                  dest_format);
  packed_bottom = XeEndianSwap16(packed_bottom, endian);
  [branch] if (duplicate_second.x) {
    packed_top.x = packed_top.y;
    packed_top.w = packed_top_4;
    packed_bottom.y = packed_bottom.z;
  }
  [branch] if (duplicate_second.y) {
    packed_top.x = packed_top.w;
    packed_top.y = packed_top_4;
    packed_top.z = packed_bottom.x;
  }
  xe_resolve_dest[dest_address_int2s] = packed_top.xz | (packed_top.yw << 16u);
  xe_resolve_dest[dest_address_int2s + 1u] =
      uint2(packed_top_4, packed_bottom.y) | (packed_bottom.xz << 16u);
  uint packed_0_8 = packed_bottom.w;
  // +++ XYZ --- ---
  // +++ W4x --- ---
  // +++ yzw --- ---
  uint source_pixel_stride_bytes =
      (9u * 4u) << uint(XeResolveEdramMsaaSamples() >= kXenosMsaaSamples_4X);
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  XePack16bpp5PixelsInUInt41(host_pixel_0, host_pixel_1, host_pixel_2,
                             host_pixel_3, host_pixel_4, dest_format,
                             packed_top, packed_top_4);
  XeEndianSwap16(packed_top, packed_top_4, endian);
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  packed_bottom = XePack16bpp4PixelsInUInt4(host_pixel_0, host_pixel_1,
                                            host_pixel_2, host_pixel_3,
                                            dest_format);
  packed_bottom = XeEndianSwap16(packed_bottom, endian);
  [branch] if (duplicate_second.y) {
    packed_top.x = packed_top.w;
    packed_top.y = packed_top_4;
    packed_top.z = packed_bottom.x;
  }
  xe_resolve_dest[dest_address_int2s + 2u] =
      uint2(packed_0_8, packed_top.y) | (packed_top.xz << 16u);
  xe_resolve_dest[dest_address_int2s + 3u] =
      uint2(packed_top.w, packed_bottom.x) |
      (uint2(packed_top_4, packed_bottom.y) << 16u);
  uint packed_1_78 = packed_bottom.z | (packed_bottom.w << 16u);
  // +++ +++ XYZ ---
  // +++ +++ W4x ---
  // +++ +++ yzw ---
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  XePack16bpp5PixelsInUInt41(host_pixel_0, host_pixel_1, host_pixel_2,
                             host_pixel_3, host_pixel_4, dest_format,
                             packed_top, packed_top_4);
  XeEndianSwap16(packed_top, packed_top_4, endian);
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  packed_bottom = XePack16bpp4PixelsInUInt4(host_pixel_0, host_pixel_1,
                                            host_pixel_2, host_pixel_3,
                                            dest_format);
  packed_bottom = XeEndianSwap16(packed_bottom, endian);
  [branch] if (duplicate_second.y) {
    packed_top.x = packed_top.w;
    packed_top.y = packed_top_4;
    packed_top.z = packed_bottom.x;
  }
  xe_resolve_dest[dest_address_int2s + 4u] =
      uint2(packed_1_78, packed_top.x | (packed_top.y << 16u));
  xe_resolve_dest[dest_address_int2s + 5u] =
      uint2(packed_top.z, packed_top_4) |
      (uint2(packed_top.w, packed_bottom.x) << 16u);
  uint2 packed_2_678 =
      uint2(packed_bottom.y | (packed_bottom.z << 16u), packed_bottom.w);
  // +++ +++ +++ XYZ
  // +++ +++ +++ W4x
  // +++ +++ +++ yzw
  source_address_bytes += source_pixel_stride_bytes;
  XeResolveLoad5Host32bppPixels(xe_resolve_source, 9u, source_address_bytes,
                                host_pixel_0, host_pixel_1, host_pixel_2,
                                host_pixel_3, host_pixel_4);
  XePack16bpp5PixelsInUInt41(host_pixel_0, host_pixel_1, host_pixel_2,
                             host_pixel_3, host_pixel_4, dest_format,
                             packed_top, packed_top_4);
  XeEndianSwap16(packed_top, packed_top_4, endian);
  XeResolveLoad4Host32bppPixels(xe_resolve_source, 9u,
                                source_address_bytes + 5u * 4u, host_pixel_0,
                                host_pixel_1, host_pixel_2, host_pixel_3);
  packed_bottom = XePack16bpp4PixelsInUInt4(host_pixel_0, host_pixel_1,
                                            host_pixel_2, host_pixel_3,
                                            dest_format);
  packed_bottom = XeEndianSwap16(packed_bottom, endian);
  [branch] if (duplicate_second.y) {
    packed_top.x = packed_top.w;
    packed_top.y = packed_top_4;
    packed_top.z = packed_bottom.x;
  }
  xe_resolve_dest[dest_address_int2s + 6u] =
      uint2(packed_2_678.x, packed_2_678.y | (packed_top.x << 16u));
  xe_resolve_dest[dest_address_int2s + 7u] =
      packed_top.yw | (uint2(packed_top.z, packed_top_4) << 16u);
  xe_resolve_dest[dest_address_int2s + 8u] =
      packed_bottom.xz | (packed_bottom.yw << 16u);
}
