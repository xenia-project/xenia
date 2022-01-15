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
    // xe::gpu::draw_util::ResolveCopyDestPitchPackedInfo.
    uint xe_resolve_dest_pitch_aligned;
    #ifndef XE_RESOLVE_RESOLUTION_SCALED
      uint xe_resolve_dest_base;
    #endif
  #endif
};

  #ifdef XE_RESOLVE_RESOLUTION_SCALED
  cbuffer XeResolveResolutionScaleConstant : register(b1) {
    // xe::gpu::draw_util::ResolveResolutionScaleConstant.
    uint xe_resolve_resolution_scale;
  }
  #endif

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

uint2 XeResolveResolutionScale() {
#ifdef XE_RESOLVE_RESOLUTION_SCALED
  return (xe_resolve_resolution_scale.xx >> uint2(0u, 2u)) & 3u;
#else
  return uint2(1u, 1u);
#endif
}

uint XeResolveResolutionScaleArea() {
  uint2 scale = XeResolveResolutionScale();
  return scale.x * scale.y;
}

bool2 XeResolveDuplicateSecondHostPixel() {
#ifdef XE_RESOLVE_RESOLUTION_SCALED
  bool2 duplicate;
  if ((xe_resolve_edram_info & (1u << 30u)) != 0u) {
    duplicate = XeResolveResolutionScale() > uint2(1u, 1u);
  } else {
    duplicate = bool2(false, false);
  }
  return duplicate;
#else
  return false;
#endif
}

// Within 160x32 guest pixels, total value relative to the source EDRAM base,
// & 31 of * 8 relative to the destination texture base.
uint2 XeResolveUnscaledOffsetDiv8() {
  return
      (xe_resolve_address_info >> uint2(0u, 5u)) & ((1u << uint2(5u, 2u)) - 1u);
}

uint2 XeResolveUnscaledOffset() {
  return XeResolveUnscaledOffsetDiv8() << 3u;
}

uint2 XeResolveScaledOffsetDiv8() {
  return XeResolveUnscaledOffsetDiv8() * XeResolveResolutionScale();
}

uint2 XeResolveScaledOffset() {
  return XeResolveScaledOffsetDiv8() << 3u;
}

uint2 XeResolveUnscaledSizeDiv8() {
  return (xe_resolve_address_info >> uint2(7u, 18u)) & ((1u << 11u) - 1u);
}

uint2 XeResolveScaledSizeDiv8() {
  return XeResolveUnscaledSizeDiv8() * XeResolveResolutionScale();
}

uint2 XeResolveScaledSize() {
  return XeResolveScaledSizeDiv8() << 3u;
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

  uint XeResolveDestRowPitchAlignedDiv32() {
    return xe_resolve_dest_pitch_aligned & ((1u << 10u) - 1u);
  }

  uint XeResolveDestRowPitchAligned() {
    return XeResolveDestRowPitchAlignedDiv32() << 5u;
  }

  uint XeResolveDestSlicePitchAlignedDiv32() {
    return (xe_resolve_dest_pitch_aligned >> 10u) & ((1u << 10u) - 1u);
  }

  uint XeResolveDestSlicePitchAligned() {
    return XeResolveDestSlicePitchAlignedDiv32() << 5u;
  }

  uint XeResolveDestPixelAddress(uint2 p, uint bpp_log2) {
    uint2 resolution_scale = XeResolveResolutionScale();
    p += (XeResolveUnscaledOffset() & 31u) * resolution_scale;
    uint address;
    uint row_pitch = XeResolveDestRowPitchAligned();
    #ifdef XE_RESOLVE_RESOLUTION_SCALED
      address = XeTextureScaledTiledOffset(XeResolveDestIsArray(),
                                           uint3(p, XeResolveDestSlice()),
                                           row_pitch,
                                           XeResolveDestSlicePitchAligned(),
                                           bpp_log2, resolution_scale);
    #else
      [branch] if (XeResolveDestIsArray()) {
        address = uint(XeTextureTiledOffset3D(
                           int3(p, XeResolveDestSlice()), row_pitch,
                           XeResolveDestSlicePitchAligned(), bpp_log2));
      } else {
        address = uint(XeTextureTiledOffset2D(int2(p), row_pitch, bpp_log2));
      }
      address += xe_resolve_dest_base;
    #endif
    return address;
  }


  // Offset of the beginning of the odd R32G32/R32G32B32A32 store address from
  // the address of the even store.
  uint XeResolveDestRightConsecutiveBlocksOffset(uint x, uint bpp_log2) {
    #ifdef XE_RESOLVE_RESOLUTION_SCALED
      return XeTextureScaledRightSubUnitOffsetInConsecutivePair(
                 x, bpp_log2, XeResolveResolutionScale());
    #else
      return XeTextureTiledOddConsecutiveBlocksOffset(bpp_log2);
    #endif
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
  uint XeResolveColorCopySourcePixelAddressIntsYDuplicating(uint2 pixel_index) {
    return
        XeEdramOffsetInts(
            uint2(pixel_index.x,
                  max(pixel_index.y,
                      uint(XeResolveDuplicateSecondHostPixel().y))) +
            XeResolveScaledOffset(),
            XeResolveEdramBaseTiles(), XeResolveEdramPitchTiles(),
            XeResolveEdramMsaaSamples(), false, XeResolveEdramFormatIntsLog2(),
            XeResolveFirstSampleIndex(), XeResolveResolutionScale());
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

  void XeResolveUnpack64bpp8RedUnswappedSamples(
      uint4 packed_0123, uint4 packed_4567, uint format,
      out float4 samples_0123, out float4 samples_4567) {
    switch (format) {
      case kXenosColorRenderTargetFormat_16_16_16_16:
        samples_0123 = XeUnpackR16EdramX4(packed_0123);
        samples_4567 = XeUnpackR16EdramX4(packed_4567);
        break;
      case kXenosColorRenderTargetFormat_16_16_16_16_FLOAT:
        samples_0123 = f16tof32(packed_0123);
        samples_4567 = f16tof32(packed_4567);
        break;
      default:
        // Treat as 32_32_FLOAT.
        samples_0123 = asfloat(packed_0123);
        samples_4567 = asfloat(packed_4567);
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

  // For red/blue swapping for 64bpp, pre-add 4 to sample_address_bytes.
  void XeResolveLoad8RedPixelSamplesFromRaw(
      ByteAddressBuffer source, uint sample_address_bytes,
      uint pixel_stride_bytes, uint format_ints_log2, uint format,
      bool swap_32bpp, out float4 pixels_0123, out float4 pixels_4567) {
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
    [branch] if (format_ints_log2) {
      XeResolveUnpack64bpp8RedUnswappedSamples(packed_0123, packed_4567, format,
                                               pixels_0123, pixels_4567);
    } else {
      XeResolveUnpack32bpp8RedSamples(packed_0123, packed_4567, format,
                                      swap_32bpp, pixels_0123, pixels_4567);
    }
  }

  void XeResolveLoad2RGBAColors(ByteAddressBuffer source, uint address_ints,
                                out float4 pixel_0, out float4 pixel_1) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
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
      uint tile_row_stride = 320u * XeResolveResolutionScale().x;
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixel_0, msaa_resolve_pixel_1;
      XeResolveLoad2RGBAUnswappedPixelSamplesFromRaw(
          source, address_bytes + tile_row_stride, pixel_stride_bytes,
          format_ints_log2, format, msaa_resolve_pixel_0, msaa_resolve_pixel_1);
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
            source,
            address_bytes + (tile_row_stride + (4u << format_ints_log2)),
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

  void XeResolveLoad4RGBAColors(ByteAddressBuffer source, uint address_ints,
                                out float4 pixel_0, out float4 pixel_1,
                                out float4 pixel_2, out float4 pixel_3) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
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
      uint tile_row_stride = 320u * XeResolveResolutionScale().x;
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixel_0;
      float4 msaa_resolve_pixel_1;
      float4 msaa_resolve_pixel_2;
      float4 msaa_resolve_pixel_3;
      XeResolveLoad4RGBAUnswappedPixelSamplesFromRaw(
          source,
          address_bytes + tile_row_stride, pixel_stride_bytes, format_ints_log2,
          format, msaa_resolve_pixel_0, msaa_resolve_pixel_1,
          msaa_resolve_pixel_2, msaa_resolve_pixel_3);
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
            source,
            address_bytes + (tile_row_stride + (4u << format_ints_log2)),
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

  void XeResolveLoad8RedColors(ByteAddressBuffer source, uint address_ints,
                               out float4 pixels_0123, out float4 pixels_4567) {
    uint format_ints_log2 = XeResolveEdramFormatIntsLog2();
    uint pixel_stride_bytes = XeResolveEdramPixelStrideInts() << 2u;
    uint address_bytes = address_ints << 2u;
    uint format = XeResolveEdramFormat();
    bool swap = XeResolveDestSwap();
    [branch] if (format_ints_log2 && swap) {
      // Likely want to load the blue part from the right half.
      address_bytes += 4u;
    }
    XeResolveLoad8RedPixelSamplesFromRaw(source, address_bytes,
                                         pixel_stride_bytes, format_ints_log2,
                                         format, swap, pixels_0123,
                                         pixels_4567);
    uint sample_select = XeResolveSampleSelect();
    float exp_bias = XeResolveDestExpBiasFactor();
    [branch] if (sample_select >= kXenosCopySampleSelect_01) {
      uint tile_row_stride = 320u * XeResolveResolutionScale().x;
      // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
      exp_bias *= 0.5f;
      float4 msaa_resolve_pixels_0123, msaa_resolve_pixels_4567;
      XeResolveLoad8RedPixelSamplesFromRaw(
          source, address_bytes + tile_row_stride, pixel_stride_bytes,
          format_ints_log2, format, swap, msaa_resolve_pixels_0123,
          msaa_resolve_pixels_4567);
      pixels_0123 += msaa_resolve_pixels_0123;
      pixels_4567 += msaa_resolve_pixels_4567;
      [branch] if (sample_select >= kXenosCopySampleSelect_0123) {
        exp_bias *= 0.5f;
        XeResolveLoad8RedPixelSamplesFromRaw(
            source, address_bytes + (4u << format_ints_log2),
            pixel_stride_bytes, format_ints_log2, format, swap,
            msaa_resolve_pixels_0123, msaa_resolve_pixels_4567);
        pixels_0123 += msaa_resolve_pixels_0123;
        pixels_4567 += msaa_resolve_pixels_4567;
        XeResolveLoad8RedPixelSamplesFromRaw(
            source,
            address_bytes + (tile_row_stride + (4u << format_ints_log2)),
            pixel_stride_bytes, format_ints_log2, format, swap,
            msaa_resolve_pixels_0123, msaa_resolve_pixels_4567);
        pixels_0123 += msaa_resolve_pixels_0123;
        pixels_4567 += msaa_resolve_pixels_4567;
      }
    }
    pixels_0123 *= exp_bias;
    pixels_4567 *= exp_bias;
  }

  uint4 XeResolveSwapRedBlue_8_8_8_8(uint4 pixels) {
    return (pixels & ~0xFF00FFu) | ((pixels & 0xFFu) << 16u) |
           ((pixels >> 16u) & 0xFFu);
  }

  uint4 XeResolveSwapRedBlue_2_10_10_10(uint4 pixels) {
    return (pixels & ~0x3FF003FF) | ((pixels & 0x3FFu) << 20u) |
           ((pixels >> 20u) & 0x3FFu);
  }

  void XeResolveSwap8PixelsRedBlue32bpp(inout uint4 pixels_0123,
                                        inout uint4 pixels_4567) {
    [branch] if (XeResolveDestSwap()) {
      switch (XeResolveEdramFormat()) {
        case kXenosColorRenderTargetFormat_8_8_8_8:
        case kXenosColorRenderTargetFormat_8_8_8_8_GAMMA:
          pixels_0123 = XeResolveSwapRedBlue_8_8_8_8(pixels_0123);
          pixels_4567 = XeResolveSwapRedBlue_8_8_8_8(pixels_4567);
          break;
        case kXenosColorRenderTargetFormat_2_10_10_10:
        case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT:
        case kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10:
        case kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16:
          pixels_0123 = XeResolveSwapRedBlue_2_10_10_10(pixels_0123);
          pixels_4567 = XeResolveSwapRedBlue_2_10_10_10(pixels_4567);
          break;
      }
    }
  }

  void XeResolveSwap4PixelsRedBlue64bpp(inout uint4 pixels_01,
                                        inout uint4 pixels_23) {
    [branch] if (XeResolveDestSwap()) {
      uint format = XeResolveEdramFormat();
      [branch] if (format == kXenosColorRenderTargetFormat_16_16_16_16 ||
                   format == kXenosColorRenderTargetFormat_16_16_16_16_FLOAT) {
        pixels_01 = (pixels_01 & ~0xFFFFu) | (pixels_01.yxwz & 0xFFFFu);
        pixels_23 = (pixels_23 & ~0xFFFFu) | (pixels_23.yxwz & 0xFFFFu);
      }
    }
  }
#endif

#endif  // XENIA_GPU_D3D12_SHADERS_RESOLVE_HLSLI_
