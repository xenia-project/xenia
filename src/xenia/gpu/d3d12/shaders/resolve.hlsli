#ifndef XENIA_GPU_D3D12_SHADERS_RESOLVE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_RESOLVE_HLSLI_

#include "edram.hlsli"
#include "pixel_formats.hlsli"
#include "texture_address.hlsli"

cbuffer XeResolveConstants : register(b0) {
  #ifdef XE_RESOLVE_CLEAR
    uint2 xe_resolve_clear_value;
  #endif
  // xe::gpu::draw_util::ResolveSourcePackedInfo.
  uint xe_resolve_edram_info;
  // xe::gpu::draw_util::ResolveAddressPackedInfo.
  uint xe_resolve_address_info;
  #ifndef XE_RESOLVE_CLEAR
    // Sanitized RB_COPY_DEST_INFO.
    uint xe_resolve_dest_info;
    // RB_COPY_DEST_PITCH.
    uint xe_resolve_dest_pitch;
    #ifndef XE_RESOLVE_RESOLUTION_SCALED
      uint xe_resolve_dest_base;
    #endif
  #endif
};

uint XeResolveEdramPitchTiles() {
  return xe_resolve_edram_info & ((1u << 10u) - 1u);
}

uint XeResolveEdramMsaaSamples() {
  return (xe_resolve_edram_info >> 10u) & ((1u << 2u) - 1u);
}

// Always false for non-one-to-one resolve.
bool XeResolveEdramIsDepth() {
  return (xe_resolve_edram_info & (1u << 12u)) != 0u;
}

uint XeResolveEdramBaseTiles() {
  return (xe_resolve_edram_info >> 13u) & ((1u << 12u) - 1u);
}

uint XeResolveEdramFormat() {
  return (xe_resolve_edram_info >> 25u) & ((1u << 4u) - 1u);
}

uint XeResolveEdramFormatIntsLog2() {
  return (xe_resolve_edram_info >> 29u) & 1u;
}

bool XeResolveEdramFormatIs64bpp() {
  return XeResolveEdramFormatIntsLog2() != 0u;
}

uint XeResolveEdramPixelStrideInts() {
  return 1u << (XeResolveEdramFormatIntsLog2() +
                uint(XeResolveEdramMsaaSamples() >= kXenosMsaaSamples_4X));
}

#ifdef XE_RESOLVE_RESOLUTION_SCALED
  bool XeResolveEdramDuplicateSecondHostPixel() {
    return (xe_resolve_edram_info & (1u << 30u)) != 0u;
  }
#endif

// Within 160x32, total value relative to the source EDRAM base, & 31 of * 8
// relative to the destination texture base.
uint2 XeResolveOffsetDiv8() {
  return
      (xe_resolve_address_info >> uint2(0u, 5u)) & ((1u << uint2(5u, 2u)) - 1u);
}

uint2 XeResolveOffset() {
  return XeResolveOffsetDiv8() << 3u;
}

uint2 XeResolveSizeDiv8() {
  return (xe_resolve_address_info >> uint2(7u, 18u)) & ((1u << 11u) - 1u);
}

uint2 XeResolveSize() {
  return XeResolveSizeDiv8() << 3u;
}

#ifndef XE_RESOLVE_CLEAR
  uint XeResolveDestEndian128() {
    return xe_resolve_dest_info & ((1u << 3u) - 1u);
  }

  bool XeResolveDestIsArray() {
    return (xe_resolve_dest_info & (1u << 3u)) != 0u;
  }

  uint XeResolveDestSlice() {
    return (xe_resolve_dest_info >> 4u) & ((1u << 3u) - 1u);
  }

  uint XeResolveDestFormat() {
    return (xe_resolve_dest_info >> 7u) & ((1u << 6u) - 1u);
  }

  int XeResolveDestExpBias() {
    return int(xe_resolve_dest_info) << (32 - (16 + 6)) >> (32 - 6);
  }

  float XeResolveDestExpBiasFactor() {
    return asfloat((XeResolveDestExpBias() << 23) + asint(1.0f));
  }

  bool XeResolveDestSwap() {
    return (xe_resolve_dest_info & (1u << 24u)) != 0u;
  }

  uint XeResolveDestRowPitch() {
    return xe_resolve_dest_pitch & ((1u << 14u) - 1u);
  }

  uint XeResolveDestSlicePitch() {
    return (xe_resolve_dest_pitch >> 16u) & ((1u << 14u) - 1u);
  }

  uint XeResolveDestPixelAddress(uint2 p, uint bpp_log2) {
    p += XeResolveOffset() & 31u;
    uint address;
    uint row_pitch = XeResolveDestRowPitch();
    [branch] if (XeResolveDestIsArray()) {
      address = uint(XeTextureTiledOffset3D(int3(p, XeResolveDestSlice()),
                                            uint2(row_pitch,
                                                  XeResolveDestSlicePitch()),
                                            bpp_log2));
    } else {
      address = uint(XeTextureTiledOffset2D(int2(p), row_pitch, bpp_log2));
    }
    #ifndef XE_RESOLVE_RESOLUTION_SCALED
      address += xe_resolve_dest_base;
    #endif
    return address;
  }

  #define kXenosCopySampleSelect_0 0u
  #define kXenosCopySampleSelect_1 1u
  #define kXenosCopySampleSelect_2 2u
  #define kXenosCopySampleSelect_3 3u
  #define kXenosCopySampleSelect_01 4u
  #define kXenosCopySampleSelect_23 5u
  #define kXenosCopySampleSelect_0123 6u

  uint XeResolveSampleSelect() {
    return xe_resolve_address_info >> 29u;
  }

  uint XeResolveFirstSampleIndex() {
    uint sample_select = XeResolveSampleSelect();
    uint sample_index;
    if (sample_select <= kXenosCopySampleSelect_3) {
      sample_index = sample_select;
    } else if (sample_select == kXenosCopySampleSelect_23) {
      sample_index = 2u;
    } else {
      sample_index = 0u;
    }
    return sample_index;
  }

  // Offset to the first sample to participate in averaging (or the sample to be
  // copied if not averaging).
  uint XeResolveColorCopySourcePixelAddressInts(uint2 pixel_index) {
    return XeEdramOffsetInts(pixel_index + XeResolveOffset(),
                             XeResolveEdramBaseTiles(),
                             XeResolveEdramPitchTiles(),
                             XeResolveEdramMsaaSamples(), false,
                             XeResolveEdramFormatIntsLog2(),
                             XeResolveFirstSampleIndex());
  }

  // Not using arrays for multi-pixel functions because they are compiled to
  // indexable temps by FXC.

  void XeResolveUnpack32bpp2Samples(uint2 packed, uint format,
                                    out float4 sample_0, out float4 sample_1) {
    switch (format) {
      case kXenosColorRenderTargetFormat_8_8_8_8:
      case kXenosColorRenderTargetFormat_8_8_8_8_GAMMA:
        sample_0 = XeUnpackR8G8B8A8UNorm(packed.x);
        sample_1 = XeUnpackR8G8B8A8UNorm(packed.y);
        break;
      case kXenosColorRenderTargetFormat_2_10_10_10:
      case kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10:
        sample_0 = XeUnpackR10G10B10A2UNorm(packed.x);
        sample_1 = XeUnpackR10G10B10A2UNorm(packed.y);
        break;
      case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT:
      case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16:
        sample_0 = XeUnpackR10G10B10A2Float(packed.x);
        sample_1 = XeUnpackR10G10B10A2Float(packed.y);
        break;
      case kXenosColorRenderTargetFormat_16_16:
        sample_0 = float4(XeUnpackR16G16Edram(packed.x), 0.0f, 0.0f);
        sample_1 = float4(XeUnpackR16G16Edram(packed.y), 0.0f, 0.0f);
        break;
      case kXenosColorRenderTargetFormat_16_16_FLOAT:
        sample_0 = float4(f16tof32(packed.x >> uint2(0u, 16u)), 0.0f, 0.0f);
        sample_1 = float4(f16tof32(packed.y >> uint2(0u, 16u)), 0.0f, 0.0f);
        break;
      default:
        // Treat as 32_FLOAT.
        sample_0 = float2(asfloat(packed.x), 0.0f).xyyy;
        sample_1 = float2(asfloat(packed.y), 0.0f).xyyy;
        break;
    }
  }

  void XeResolveUnpack32bpp4Samples(uint4 packed, uint format,
                                    out float4 sample_0, out float4 sample_1,
                                    out float4 sample_2, out float4 sample_3) {
    switch (format) {
      case kXenosColorRenderTargetFormat_8_8_8_8:
      case kXenosColorRenderTargetFormat_8_8_8_8_GAMMA:
        sample_0 = XeUnpackR8G8B8A8UNorm(packed.x);
        sample_1 = XeUnpackR8G8B8A8UNorm(packed.y);
        sample_2 = XeUnpackR8G8B8A8UNorm(packed.z);
        sample_3 = XeUnpackR8G8B8A8UNorm(packed.w);
        break;
      case kXenosColorRenderTargetFormat_2_10_10_10:
      case kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10:
        sample_0 = XeUnpackR10G10B10A2UNorm(packed.x);
        sample_1 = XeUnpackR10G10B10A2UNorm(packed.y);
        sample_2 = XeUnpackR10G10B10A2UNorm(packed.z);
        sample_3 = XeUnpackR10G10B10A2UNorm(packed.w);
        break;
      case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT:
      case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16:
        sample_0 = XeUnpackR10G10B10A2Float(packed.x);
        sample_1 = XeUnpackR10G10B10A2Float(packed.y);
        sample_2 = XeUnpackR10G10B10A2Float(packed.z);
        sample_3 = XeUnpackR10G10B10A2Float(packed.w);
        break;
      case kXenosColorRenderTargetFormat_16_16:
        sample_0 = float4(XeUnpackR16G16Edram(packed.x), 0.0f, 0.0f);
        sample_1 = float4(XeUnpackR16G16Edram(packed.y), 0.0f, 0.0f);
        sample_2 = float4(XeUnpackR16G16Edram(packed.z), 0.0f, 0.0f);
        sample_3 = float4(XeUnpackR16G16Edram(packed.w), 0.0f, 0.0f);
        break;
      case kXenosColorRenderTargetFormat_16_16_FLOAT:
        sample_0 = float4(f16tof32(packed.x >> uint2(0u, 16u)), 0.0f, 0.0f);
        sample_1 = float4(f16tof32(packed.y >> uint2(0u, 16u)), 0.0f, 0.0f);
        sample_2 = float4(f16tof32(packed.z >> uint2(0u, 16u)), 0.0f, 0.0f);
        sample_3 = float4(f16tof32(packed.w >> uint2(0u, 16u)), 0.0f, 0.0f);
        break;
      default:
        // Treat as 32_FLOAT.
        sample_0 = float2(asfloat(packed.x), 0.0f).xyyy;
        sample_1 = float2(asfloat(packed.y), 0.0f).xyyy;
        sample_2 = float2(asfloat(packed.z), 0.0f).xyyy;
        sample_3 = float2(asfloat(packed.w), 0.0f).xyyy;
        break;
    }
  }

  void XeResolveUnpack32bpp8RedSamples(uint4 packed_0123, uint4 packed_4567,
                                       uint format, bool swap,
                                       out float4 samples_0123,
                                       out float4 samples_4567) {
    switch (format) {
      case kXenosColorRenderTargetFormat_8_8_8_8:
      case kXenosColorRenderTargetFormat_8_8_8_8_GAMMA: {
        uint shift = swap ? 16u : 0u;
        samples_0123 = XeUnpackR8UNormX4(packed_0123 >> shift);
        samples_4567 = XeUnpackR8UNormX4(packed_4567 >> shift);
      } break;
      case kXenosColorRenderTargetFormat_2_10_10_10:
      case kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10: {
        uint shift = swap ? 20u : 0u;
        samples_0123 = XeUnpackR10UNormX4(packed_0123 >> shift);
        samples_4567 = XeUnpackR10UNormX4(packed_4567 >> shift);
      } break;
      case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT:
      case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16: {
        uint shift = swap ? 20u : 0u;
        samples_0123 = XeUnpackR10FloatX4(packed_0123 >> shift);
        samples_4567 = XeUnpackR10FloatX4(packed_4567 >> shift);
      } break;
      case kXenosColorRenderTargetFormat_16_16:
        samples_0123 = XeUnpackR16EdramX4(packed_0123);
        samples_4567 = XeUnpackR16EdramX4(packed_4567);
        break;
      case kXenosColorRenderTargetFormat_16_16_FLOAT:
        samples_0123 = f16tof32(packed_0123);
        samples_4567 = f16tof32(packed_4567);
        break;
      default:
        // Treat as 32_FLOAT.
        samples_0123 = asfloat(packed_0123);
        samples_4567 = asfloat(packed_4567);
        break;
    }
  }

  void XeResolveUnpack64bpp2Samples(uint4 packed, uint format,
                                    out float4 sample_0, out float4 sample_1) {
    switch (format) {
      case kXenosColorRenderTargetFormat_16_16_16_16:
        sample_0 = XeUnpackR16G16B16A16Edram(packed.xy);
        sample_1 = XeUnpackR16G16B16A16Edram(packed.zw);
        break;
      case kXenosColorRenderTargetFormat_16_16_16_16_FLOAT:
        sample_0 = f16tof32(packed.xxyy >> uint2(0u, 16u).xyxy);
        sample_1 = f16tof32(packed.zzww >> uint2(0u, 16u).xyxy);
        break;
      default:
        // Treat as 32_32_FLOAT.
        sample_0 = float4(asfloat(packed.xy), 0.0f, 0.0f);
        sample_1 = float4(asfloat(packed.zw), 0.0f, 0.0f);
        break;
    }
  }

  void XeResolveUnpack64bpp4Samples(uint4 packed_01, uint4 packed_23,
                                    uint format, out float4 sample_0,
                                    out float4 sample_1, out float4 sample_2,
                                    out float4 sample_3) {
    switch (format) {
      case kXenosColorRenderTargetFormat_16_16_16_16:
        sample_0 = XeUnpackR16G16B16A16Edram(packed_01.xy);
        sample_1 = XeUnpackR16G16B16A16Edram(packed_01.zw);
        sample_2 = XeUnpackR16G16B16A16Edram(packed_23.xy);
        sample_3 = XeUnpackR16G16B16A16Edram(packed_23.zw);
        break;
      case kXenosColorRenderTargetFormat_16_16_16_16_FLOAT:
        sample_0 = f16tof32(packed_01.xxyy >> uint2(0u, 16u).xyxy);
        sample_1 = f16tof32(packed_01.zzww >> uint2(0u, 16u).xyxy);
        sample_2 = f16tof32(packed_23.xxyy >> uint2(0u, 16u).xyxy);
        sample_3 = f16tof32(packed_23.zzww >> uint2(0u, 16u).xyxy);
        break;
      default:
        // Treat as 32_32_FLOAT.
        sample_0 = float4(asfloat(packed_01.xy), 0.0f, 0.0f);
        sample_1 = float4(asfloat(packed_01.zw), 0.0f, 0.0f);
        sample_2 = float4(asfloat(packed_23.xy), 0.0f, 0.0f);
        sample_3 = float4(asfloat(packed_23.zw), 0.0f, 0.0f);
        break;
    }
  }

  void XeResolveUnpack64bpp8RedSamples(uint4 packed_01, uint4 packed_23,
                                       uint4 packed_45, uint4 packed_67,
                                       uint format, bool swap,
                                       out float4 samples_0123,
                                       out float4 samples_4567) {
    switch (format) {
      case kXenosColorRenderTargetFormat_16_16_16_16:
        samples_0123 = XeUnpackR16EdramX4(
            swap ? uint4(packed_01.yw, packed_23.yw)
                 : uint4(packed_01.xz, packed_23.xz));
        samples_4567 = XeUnpackR16EdramX4(
            swap ? uint4(packed_45.yw, packed_67.yw)
                 : uint4(packed_45.xz, packed_67.xz));
        break;
      case kXenosColorRenderTargetFormat_16_16_16_16_FLOAT:
        samples_0123 = f16tof32(swap ? uint4(packed_01.yw, packed_23.yw)
                                     : uint4(packed_01.xz, packed_23.xz));
        samples_4567 = f16tof32(swap ? uint4(packed_45.yw, packed_67.yw)
                                     : uint4(packed_45.xz, packed_67.xz));
        break;
      default:
        // Treat as 32_32_FLOAT.
        samples_0123 = asfloat(uint4(packed_01.xz, packed_23.xz));
        samples_4567 = asfloat(uint4(packed_45.xz, packed_67.xz));
        break;
    }
  }

  void XeResolveLoad2RGBAUnswappedPixelSamplesFromRaw(
      ByteAddressBuffer source, uint sample_address_bytes,
      uint pixel_stride_bytes, uint format_ints_log2, uint format,
      out float4 pixel_0, out float4 pixel_1) {
    [branch] if (format_ints_log2) {
      uint4 packed;
      [branch] if (pixel_stride_bytes == 8u) {
        packed = source.Load4(sample_address_bytes);
      } else {
        packed.xy = source.Load2(sample_address_bytes);
        packed.zw = source.Load2(sample_address_bytes + pixel_stride_bytes);
      }
      XeResolveUnpack64bpp2Samples(packed, format, pixel_0, pixel_1);
    } else {
      uint2 packed;
      [branch] if (pixel_stride_bytes == 4u) {
        packed = source.Load2(sample_address_bytes);
      } else {
        packed.x = source.Load(sample_address_bytes);
        packed.y = source.Load(sample_address_bytes + pixel_stride_bytes);
      }
      XeResolveUnpack32bpp2Samples(packed, format, pixel_0, pixel_1);
    }
  }

  void XeResolveLoad4RGBAUnswappedPixelSamplesFromRaw(
      ByteAddressBuffer source, uint sample_address_bytes,
      uint pixel_stride_bytes, uint format_ints_log2, uint format,
      out float4 pixel_0, out float4 pixel_1, out float4 pixel_2,
      out float4 pixel_3) {
    [branch] if (format_ints_log2) {
      uint4 packed_01, packed_23;
      [branch] if (pixel_stride_bytes == 8u) {
        packed_01 = source.Load4(sample_address_bytes);
        packed_23 = source.Load4(sample_address_bytes + 16u);
      } else {
        packed_01.xy = source.Load2(sample_address_bytes);
        packed_01.zw = source.Load2(sample_address_bytes + pixel_stride_bytes);
        packed_23.xy =
            source.Load2(sample_address_bytes + 2u * pixel_stride_bytes);
        packed_23.zw =
            source.Load2(sample_address_bytes + 3u * pixel_stride_bytes);
      }
      XeResolveUnpack64bpp4Samples(packed_01, packed_23, format, pixel_0,
                                   pixel_1, pixel_2, pixel_3);
    } else {
      uint4 packed;
      [branch] if (pixel_stride_bytes == 4u) {
        packed = source.Load4(sample_address_bytes);
      } else {
        packed.x = source.Load(sample_address_bytes);
        packed.y = source.Load(sample_address_bytes + pixel_stride_bytes);
        packed.z = source.Load(sample_address_bytes + 2u * pixel_stride_bytes);
        packed.w = source.Load(sample_address_bytes + 3u * pixel_stride_bytes);
      }
      XeResolveUnpack32bpp4Samples(packed, format, pixel_0, pixel_1, pixel_2,
                                   pixel_3);
    }
  }

  void XeResolveLoad8RedSwappedPixelSamplesFromRaw(
      ByteAddressBuffer source, uint sample_address_bytes,
      uint pixel_stride_bytes, uint format_ints_log2, uint format, bool swap,
      out float4 pixels_0123, out float4 pixels_4567) {
    [branch] if (format_ints_log2) {
      uint4 packed_01, packed_23, packed_45, packed_67;
      [branch] if (pixel_stride_bytes == 8u) {
        packed_01 = source.Load4(sample_address_bytes);
        packed_23 = source.Load4(sample_address_bytes + 16u);
        packed_45 = source.Load4(sample_address_bytes + 32u);
        packed_67 = source.Load4(sample_address_bytes + 48u);
      } else {
        packed_01.xy = source.Load2(sample_address_bytes);
        packed_01.zw = source.Load2(sample_address_bytes + pixel_stride_bytes);
        packed_23.xy =
            source.Load2(sample_address_bytes + 2u * pixel_stride_bytes);
        packed_23.zw =
            source.Load2(sample_address_bytes + 3u * pixel_stride_bytes);
        packed_45.xy =
            source.Load2(sample_address_bytes + 4u * pixel_stride_bytes);
        packed_45.zw =
            source.Load2(sample_address_bytes + 5u * pixel_stride_bytes);
        packed_67.xy =
            source.Load2(sample_address_bytes + 6u * pixel_stride_bytes);
        packed_67.zw =
            source.Load2(sample_address_bytes + 7u * pixel_stride_bytes);
      }
      XeResolveUnpack64bpp8RedSamples(packed_01, packed_23, packed_45,
                                      packed_67, format, swap, pixels_0123,
                                      pixels_4567);
    } else {
      uint4 packed_0123, packed_4567;
      [branch] if (pixel_stride_bytes == 4u) {
        packed_0123 = source.Load4(sample_address_bytes);
        packed_4567 = source.Load4(sample_address_bytes + 16u);
      } else {
        packed_0123.x = source.Load(sample_address_bytes);
        packed_0123.y = source.Load(sample_address_bytes + pixel_stride_bytes);
        packed_0123.z =
            source.Load(sample_address_bytes + 2u * pixel_stride_bytes);
        packed_0123.w =
            source.Load(sample_address_bytes + 3u * pixel_stride_bytes);
        packed_4567.x =
            source.Load(sample_address_bytes + 4u * pixel_stride_bytes);
        packed_4567.y =
            source.Load(sample_address_bytes + 5u * pixel_stride_bytes);
        packed_4567.z =
            source.Load(sample_address_bytes + 6u * pixel_stride_bytes);
        packed_4567.w =
            source.Load(sample_address_bytes + 7u * pixel_stride_bytes);
      }
      XeResolveUnpack32bpp8RedSamples(packed_0123, packed_4567, format, swap,
                                      pixels_0123, pixels_4567);
    }
  }

  void XeResolveLoad2RGBAColorsX1(ByteAddressBuffer source, uint address_ints,
                                  out float4 pixel_0, out float4 pixel_1) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
    uint msaa_samples = XeResolveEdramMsaaSamples();
    uint pixel_stride_bytes = XeResolveEdramPixelStrideInts() << 2u;
    uint address_bytes = address_ints << 2u;
    uint format = XeResolveEdramFormat();
    XeResolveLoad2RGBAUnswappedPixelSamplesFromRaw(source, address_bytes,
                                                   pixel_stride_bytes,
                                                   format_ints_log2, format,
                                                   pixel_0, pixel_1);
    uint sample_select = XeResolveSampleSelect();
    float exp_bias = XeResolveDestExpBiasFactor();
    [branch] if (sample_select >= kXenosCopySampleSelect_01) {
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixel_0, msaa_resolve_pixel_1;
      XeResolveLoad2RGBAUnswappedPixelSamplesFromRaw(
          source, address_bytes + (320u << format_ints_log2),
          pixel_stride_bytes, format_ints_log2, format, msaa_resolve_pixel_0,
          msaa_resolve_pixel_1);
      pixel_0 += msaa_resolve_pixel_0;
      pixel_1 += msaa_resolve_pixel_1;
      [branch] if (sample_select >= kXenosCopySampleSelect_0123) {
        exp_bias *= 0.5f;
        XeResolveLoad2RGBAUnswappedPixelSamplesFromRaw(
            source, address_bytes + (4u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, msaa_resolve_pixel_0,
            msaa_resolve_pixel_1);
        pixel_0 += msaa_resolve_pixel_0;
        pixel_1 += msaa_resolve_pixel_1;
        XeResolveLoad2RGBAUnswappedPixelSamplesFromRaw(
            source, address_bytes + (324u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, msaa_resolve_pixel_0,
            msaa_resolve_pixel_1);
        pixel_0 += msaa_resolve_pixel_0;
        pixel_1 += msaa_resolve_pixel_1;
      }
    }
    pixel_0 *= exp_bias;
    pixel_1 *= exp_bias;
    [branch] if (XeResolveDestSwap()) {
      pixel_0 = pixel_0.bgra;
      pixel_1 = pixel_1.bgra;
    }
  }

  void XeResolveLoad4RGBAColorsX1(ByteAddressBuffer source, uint address_ints,
                                  out float4 pixel_0, out float4 pixel_1,
                                  out float4 pixel_2, out float4 pixel_3) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
    uint msaa_samples = XeResolveEdramMsaaSamples();
    uint pixel_stride_bytes = XeResolveEdramPixelStrideInts() << 2u;
    uint address_bytes = address_ints << 2u;
    uint format = XeResolveEdramFormat();
    XeResolveLoad4RGBAUnswappedPixelSamplesFromRaw(source, address_bytes,
                                                   pixel_stride_bytes,
                                                   format_ints_log2, format,
                                                   pixel_0, pixel_1, pixel_2,
                                                   pixel_3);
    uint sample_select = XeResolveSampleSelect();
    float exp_bias = XeResolveDestExpBiasFactor();
    [branch] if (sample_select >= kXenosCopySampleSelect_01) {
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixel_0;
      float4 msaa_resolve_pixel_1;
      float4 msaa_resolve_pixel_2;
      float4 msaa_resolve_pixel_3;
      XeResolveLoad4RGBAUnswappedPixelSamplesFromRaw(
          source, address_bytes + (320u << format_ints_log2),
          pixel_stride_bytes, format_ints_log2, format, msaa_resolve_pixel_0,
          msaa_resolve_pixel_1, msaa_resolve_pixel_2, msaa_resolve_pixel_3);
      pixel_0 += msaa_resolve_pixel_0;
      pixel_1 += msaa_resolve_pixel_1;
      pixel_2 += msaa_resolve_pixel_2;
      pixel_3 += msaa_resolve_pixel_3;
      [branch] if (sample_select >= kXenosCopySampleSelect_0123) {
        exp_bias *= 0.5f;
        XeResolveLoad4RGBAUnswappedPixelSamplesFromRaw(
            source, address_bytes + (4u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, msaa_resolve_pixel_0,
            msaa_resolve_pixel_1, msaa_resolve_pixel_2, msaa_resolve_pixel_3);
        pixel_0 += msaa_resolve_pixel_0;
        pixel_1 += msaa_resolve_pixel_1;
        pixel_2 += msaa_resolve_pixel_2;
        pixel_3 += msaa_resolve_pixel_3;
        XeResolveLoad4RGBAUnswappedPixelSamplesFromRaw(
            source, address_bytes + (324u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, msaa_resolve_pixel_0,
            msaa_resolve_pixel_1, msaa_resolve_pixel_2, msaa_resolve_pixel_3);
        pixel_0 += msaa_resolve_pixel_0;
        pixel_1 += msaa_resolve_pixel_1;
        pixel_2 += msaa_resolve_pixel_2;
        pixel_3 += msaa_resolve_pixel_3;
      }
    }
    pixel_0 *= exp_bias;
    pixel_1 *= exp_bias;
    pixel_2 *= exp_bias;
    pixel_3 *= exp_bias;
    [branch] if (XeResolveDestSwap()) {
      pixel_0 = pixel_0.bgra;
      pixel_1 = pixel_1.bgra;
      pixel_2 = pixel_2.bgra;
      pixel_3 = pixel_3.bgra;
    }
  }

  void XeResolveLoad8RedColorsX1(ByteAddressBuffer source, uint address_ints,
                                 out float4 pixels_0123,
                                 out float4 pixels_4567) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
    uint msaa_samples = XeResolveEdramMsaaSamples();
    uint pixel_stride_bytes = XeResolveEdramPixelStrideInts() << 2u;
    uint address_bytes = address_ints << 2u;
    uint format = XeResolveEdramFormat();
    bool swap = XeResolveDestSwap();
    XeResolveLoad8RedSwappedPixelSamplesFromRaw(source, address_bytes,
                                                pixel_stride_bytes,
                                                format_ints_log2, format, swap,
                                                pixels_0123, pixels_4567);
    uint sample_select = XeResolveSampleSelect();
    float exp_bias = XeResolveDestExpBiasFactor();
    [branch] if (sample_select >= kXenosCopySampleSelect_01) {
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixels_0123, msaa_resolve_pixels_4567;
      XeResolveLoad8RedSwappedPixelSamplesFromRaw(
          source, address_bytes + (320u << format_ints_log2),
          pixel_stride_bytes, format_ints_log2, format, swap,
          msaa_resolve_pixels_0123, msaa_resolve_pixels_4567);
      pixels_0123 += msaa_resolve_pixels_0123;
      pixels_4567 += msaa_resolve_pixels_4567;
      [branch] if (sample_select >= kXenosCopySampleSelect_0123) {
        exp_bias *= 0.5f;
        XeResolveLoad8RedSwappedPixelSamplesFromRaw(
            source, address_bytes + (4u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, swap,
            msaa_resolve_pixels_0123, msaa_resolve_pixels_4567);
        pixels_0123 += msaa_resolve_pixels_0123;
        pixels_4567 += msaa_resolve_pixels_4567;
        XeResolveLoad8RedSwappedPixelSamplesFromRaw(
            source, address_bytes + (324u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, swap,
            msaa_resolve_pixels_0123, msaa_resolve_pixels_4567);
        pixels_0123 += msaa_resolve_pixels_0123;
        pixels_4567 += msaa_resolve_pixels_4567;
      }
    }
    pixels_0123 *= exp_bias;
    pixels_4567 *= exp_bias;
  }

  void XeResolveLoadGuestPixelRGBAColorsX2(Buffer<uint4> source,
                                           uint guest_address_ints,
                                           out float4 host_pixel_y0x0,
                                           out float4 host_pixel_y0x1,
                                           out float4 host_pixel_y1x0,
                                           out float4 host_pixel_y1x1) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
    uint msaa_samples = XeResolveEdramMsaaSamples();
    uint format = XeResolveEdramFormat();
    // 1 host uint4 == 1 guest uint.
    uint4 packed = source[guest_address_ints];
    [branch] if (format_ints_log2) {
      // packed is the top 2 host pixels, also load the bottom 2.
      XeResolveUnpack64bpp4Samples(packed, source[guest_address_ints + 1u],
                                   format, host_pixel_y0x0, host_pixel_y0x1,
                                   host_pixel_y1x0, host_pixel_y1x1);
    } else {
      XeResolveUnpack32bpp4Samples(packed, format, host_pixel_y0x0,
                                   host_pixel_y0x1, host_pixel_y1x0,
                                   host_pixel_y1x1);
    }
    uint sample_select = XeResolveSampleSelect();
    float exp_bias = XeResolveDestExpBiasFactor();
    [branch] if (sample_select >= kXenosCopySampleSelect_01) {
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixel_y0x0;
      float4 msaa_resolve_pixel_y0x1;
      float4 msaa_resolve_pixel_y1x0;
      float4 msaa_resolve_pixel_y1x1;
      packed = source[guest_address_ints + (80u << format_ints_log2)];
      [branch] if (format_ints_log2) {
        XeResolveUnpack64bpp4Samples(packed, source[guest_address_ints + 161u],
                                     format, msaa_resolve_pixel_y0x0,
                                     msaa_resolve_pixel_y0x1,
                                     msaa_resolve_pixel_y1x0,
                                     msaa_resolve_pixel_y1x1);
      } else {
        XeResolveUnpack32bpp4Samples(packed, format, msaa_resolve_pixel_y0x0,
                                     msaa_resolve_pixel_y0x1,
                                     msaa_resolve_pixel_y1x0,
                                     msaa_resolve_pixel_y1x1);
      }
      host_pixel_y0x0 += msaa_resolve_pixel_y0x0;
      host_pixel_y0x1 += msaa_resolve_pixel_y0x1;
      host_pixel_y1x0 += msaa_resolve_pixel_y1x0;
      host_pixel_y1x1 += msaa_resolve_pixel_y1x1;
      [branch] if (sample_select >= kXenosCopySampleSelect_0123) {
        exp_bias *= 0.5f;
        packed = source[guest_address_ints + (1u << format_ints_log2)];
        [branch] if (format_ints_log2) {
          XeResolveUnpack64bpp4Samples(packed, source[guest_address_ints + 3u],
                                       format, msaa_resolve_pixel_y0x0,
                                       msaa_resolve_pixel_y0x1,
                                       msaa_resolve_pixel_y1x0,
                                       msaa_resolve_pixel_y1x1);
        } else {
          XeResolveUnpack32bpp4Samples(packed, format, msaa_resolve_pixel_y0x0,
                                       msaa_resolve_pixel_y0x1,
                                       msaa_resolve_pixel_y1x0,
                                       msaa_resolve_pixel_y1x1);
        }
        host_pixel_y0x0 += msaa_resolve_pixel_y0x0;
        host_pixel_y0x1 += msaa_resolve_pixel_y0x1;
        host_pixel_y1x0 += msaa_resolve_pixel_y1x0;
        host_pixel_y1x1 += msaa_resolve_pixel_y1x1;
        packed = source[guest_address_ints + (81u << format_ints_log2)];
        [branch] if (format_ints_log2) {
          XeResolveUnpack64bpp4Samples(packed,
                                       source[guest_address_ints + 163u],
                                       format, msaa_resolve_pixel_y0x0,
                                       msaa_resolve_pixel_y0x1,
                                       msaa_resolve_pixel_y1x0,
                                       msaa_resolve_pixel_y1x1);
        } else {
          XeResolveUnpack32bpp4Samples(packed, format, msaa_resolve_pixel_y0x0,
                                       msaa_resolve_pixel_y0x1,
                                       msaa_resolve_pixel_y1x0,
                                       msaa_resolve_pixel_y1x1);
        }
        host_pixel_y0x0 += msaa_resolve_pixel_y0x0;
        host_pixel_y0x1 += msaa_resolve_pixel_y0x1;
        host_pixel_y1x0 += msaa_resolve_pixel_y1x0;
        host_pixel_y1x1 += msaa_resolve_pixel_y1x1;
      }
    }
    host_pixel_y0x0 *= exp_bias;
    host_pixel_y0x1 *= exp_bias;
    host_pixel_y1x0 *= exp_bias;
    host_pixel_y1x1 *= exp_bias;
    [branch] if (XeResolveDestSwap()) {
      host_pixel_y0x0 = host_pixel_y0x0.bgra;
      host_pixel_y0x1 = host_pixel_y0x1.bgra;
      host_pixel_y1x0 = host_pixel_y1x0.bgra;
      host_pixel_y1x1 = host_pixel_y1x1.bgra;
    }
  }

  void XeResolveLoadGuest2PixelsRedColorsX2(Buffer<uint4> source,
                                            uint guest_address_ints,
                                            out float4 guest_pixel_0,
                                            out float4 guest_pixel_1) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
    uint msaa_samples = XeResolveEdramMsaaSamples();
    uint guest_address_ints_1 =
        guest_address_ints + XeResolveEdramPixelStrideInts();
    uint format = XeResolveEdramFormat();
    bool swap = XeResolveDestSwap();
    // 1 host uint4 == 1 guest uint.
    uint4 packed_0 = source[guest_address_ints];
    uint4 packed_1 = source[guest_address_ints_1];
    [branch] if (format_ints_log2) {
      // packed are the top 2 host pixels of each guest pixel, also load the
      // bottom 2.
      XeResolveUnpack64bpp8RedSamples(packed_0, source[guest_address_ints + 1u],
                                      packed_1,
                                      source[guest_address_ints_1 + 1u], format,
                                      swap, guest_pixel_0, guest_pixel_1);
    } else {
      XeResolveUnpack32bpp8RedSamples(packed_0, packed_1, format, swap,
                                      guest_pixel_0, guest_pixel_1);
    }
    uint sample_select = XeResolveSampleSelect();
    float exp_bias = XeResolveDestExpBiasFactor();
    [branch] if (sample_select >= kXenosCopySampleSelect_01) {
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixel_0, msaa_resolve_pixel_1;
      packed_0 = source[guest_address_ints + (80u << format_ints_log2)];
      packed_1 = source[guest_address_ints_1 + (80u << format_ints_log2)];
      [branch] if (format_ints_log2) {
        XeResolveUnpack64bpp8RedSamples(
            packed_0, source[guest_address_ints + 161u], packed_1,
            source[guest_address_ints_1 + 161u], format, swap,
            msaa_resolve_pixel_0, msaa_resolve_pixel_1);
      } else {
        XeResolveUnpack32bpp8RedSamples(packed_0, packed_1, format, swap,
                                        msaa_resolve_pixel_0,
                                        msaa_resolve_pixel_1);
      }
      guest_pixel_0 += msaa_resolve_pixel_0;
      guest_pixel_1 += msaa_resolve_pixel_1;
      [branch] if (sample_select >= kXenosCopySampleSelect_0123) {
        exp_bias *= 0.5f;
        packed_0 = source[guest_address_ints + (1u << format_ints_log2)];
        packed_1 = source[guest_address_ints_1 + (1u << format_ints_log2)];
        [branch] if (format_ints_log2) {
          XeResolveUnpack64bpp8RedSamples(
              packed_0, source[guest_address_ints + 3u], packed_1,
              source[guest_address_ints_1 + 3u], format, swap,
              msaa_resolve_pixel_0, msaa_resolve_pixel_1);
        } else {
          XeResolveUnpack32bpp8RedSamples(packed_0, packed_1, format, swap,
                                          msaa_resolve_pixel_0,
                                          msaa_resolve_pixel_1);
        }
        guest_pixel_0 += msaa_resolve_pixel_0;
        guest_pixel_1 += msaa_resolve_pixel_1;
        packed_0 = source[guest_address_ints + (81u << format_ints_log2)];
        packed_1 = source[guest_address_ints_1 + (81u << format_ints_log2)];
        [branch] if (format_ints_log2) {
          XeResolveUnpack64bpp8RedSamples(
              packed_0, source[guest_address_ints + 163u], packed_1,
              source[guest_address_ints_1 + 163u], format, swap,
              msaa_resolve_pixel_0, msaa_resolve_pixel_1);
        } else {
          XeResolveUnpack32bpp8RedSamples(packed_0, packed_1, format, swap,
                                          msaa_resolve_pixel_0,
                                          msaa_resolve_pixel_1);
        }
        guest_pixel_0 += msaa_resolve_pixel_0;
        guest_pixel_1 += msaa_resolve_pixel_1;
      }
    }
    guest_pixel_0 *= exp_bias;
    guest_pixel_1 *= exp_bias;
  }

  uint4 XeResolveSwap4PixelsRedBlue32bpp(uint4 pixels) {
    [branch] if (XeResolveDestSwap()) {
      switch (XeResolveEdramFormat()) {
        case kXenosColorRenderTargetFormat_8_8_8_8:
        case kXenosColorRenderTargetFormat_8_8_8_8_GAMMA:
          pixels = (pixels & ~0xFF00FFu) | ((pixels & 0xFFu) << 16u) |
                   ((pixels >> 16u) & 0xFFu);
          break;
        case kXenosColorRenderTargetFormat_2_10_10_10:
        case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT:
        case kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10:
        case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16:
          pixels = (pixels & ~0x3FF003FF) | ((pixels & 0x3FFu) << 20u) |
                   ((pixels >> 20u) & 0x3FFu);
          break;
      }
    }
    return pixels;
  }

  void XeResolveSwap8PixelsRedBlue32bpp(inout uint4 pixels_0123,
                                        inout uint4 pixels_4567) {
    [branch] if (XeResolveDestSwap()) {
      switch (XeResolveEdramFormat()) {
        case kXenosColorRenderTargetFormat_8_8_8_8:
        case kXenosColorRenderTargetFormat_8_8_8_8_GAMMA:
          pixels_0123 = (pixels_0123 & ~0xFF00FFu) |
                        ((pixels_0123 & 0xFFu) << 16u) |
                        ((pixels_0123 >> 16u) & 0xFFu);
          pixels_4567 = (pixels_4567 & ~0xFF00FFu) |
                        ((pixels_4567 & 0xFFu) << 16u) |
                        ((pixels_4567 >> 16u) & 0xFFu);
          break;
        case kXenosColorRenderTargetFormat_2_10_10_10:
        case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT:
        case kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10:
        case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16:
          pixels_0123 = (pixels_0123 & ~0x3FF003FF) |
                        ((pixels_0123 & 0x3FFu) << 20u) |
                        ((pixels_0123 >> 20u) & 0x3FFu);
          pixels_4567 = (pixels_4567 & ~0x3FF003FF) |
                        ((pixels_4567 & 0x3FFu) << 20u) |
                        ((pixels_4567 >> 20u) & 0x3FFu);
          break;
      }
    }
  }

  void XeResolveSwap4PixelsRedBlue64bpp(inout uint4 pixels_01,
                                        inout uint4 pixels_23) {
    [branch] if (XeResolveDestSwap()) {
      uint format = XeResolveDestFormat();
      [branch] if (format == kXenosColorRenderTargetFormat_16_16_16_16 ||
                   format == kXenosColorRenderTargetFormat_16_16_16_16_FLOAT) {
        pixels_01 = (pixels_01 & ~0xFFFFu) | (pixels_01.yxwz & 0xFFFFu);
        pixels_23 = (pixels_23 & ~0xFFFFu) | (pixels_23.yxwz & 0xFFFFu);
      }
    }
  }
#endif

#endif  // XENIA_GPU_D3D12_SHADERS_RESOLVE_HLSLI_
