#ifndef XENIA_GPU_D3D12_SHADERS_PIXEL_FORMATS_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_PIXEL_FORMATS_HLSLI_

// https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

uint XeFloat16To7e3(uint4 rgba_f16u32) {
  float4 rgba_f32 = f16tof32(rgba_f16u32);
  uint3 rgb_f32u32 = asuint(rgba_f32.xyz);
  // Keep only positive integers and saturate to 31.875 (also dropping NaNs).
  // Was previously done with `asuint(clamp(asint(rgb_f32u32), 0, 0x41FF0000))`,
  // but FXC decides to ignore the uint->int cast, and negative numbers become
  // 0x41FF0000.
  rgb_f32u32 = min(rgb_f32u32 * uint3(rgb_f32u32 <= 0x7FFFFFFFu), 0x41FF0000u);
  uint3 normalized = rgb_f32u32 + 0xC2000000u;
  uint3 denormalized = ((rgb_f32u32 & 0x7FFFFFu) | 0x800000u) >>
                       ((125u).xxx - (rgb_f32u32 >> 23u));
  uint3 rgb_f10u32 = normalized + (denormalized - normalized) *
                     uint3(rgb_f32u32 < 0x3E800000u);
  rgb_f10u32 =
      ((rgb_f10u32 + 0x7FFFu + ((rgb_f10u32 >> 16u) & 1u)) >> 16u) & 0x3FFu;
  return rgb_f10u32.r | (rgb_f10u32.g << 10u) | (rgb_f10u32.b << 20u) |
         (uint(saturate(rgba_f32.a) * 3.0) << 30u);
}

uint4 XeFloat7e3To16(uint rgba_packed) {
  uint3 rgb_f10u32 = (rgba_packed.xxx >> uint3(0u, 10u, 20u)) & 0x3FFu;
  uint3 mantissa = rgb_f10u32 & 0x7Fu;
  uint3 exponent = rgb_f10u32 >> 7u;
  // Normalize the values for the denormalized components.
  // Exponent = 1;
  // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x80) == 0);
  uint3 is_denormalized = uint3(exponent == 0u);
  uint3 mantissa_lzcnt = (7u).xxx - firstbithigh(mantissa);
  exponent += ((1u).xxx - mantissa_lzcnt - exponent) * is_denormalized;
  mantissa +=
      (((mantissa << mantissa_lzcnt) & 0x7Fu) - mantissa) * is_denormalized;
  // Combine into 32-bit float bits and clear zeros.
  uint3 rgb_f32u32 = (((exponent + 124u) << 23u) | (mantissa << 16u)) *
                     uint3(rgb_f10u32 != 0u);
  return f32tof16(float4(asfloat(rgb_f32u32),
                         float(rgba_packed >> 30u) * (1.0 / 3.0)));
}

// Based on CFloat24 from d3dref9.dll and the 6e4 code from:
// https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
// 6e4 has a different exponent bias allowing [0,512) values, 20e4 allows [0,2).
// We also can't clamp the stored value to 1 as load->store->load must be exact.

uint4 XeFloat32To20e4(uint4 f32u32) {
  // Keep only positive (high bit set means negative for both float and int) and
  // saturate to the maximum representable value near 2 (also dropping NaNs).
  f32u32 = min(f32u32 * uint4(f32u32 <= 0x7FFFFFFFu), 0x3FFFFFF8u);
  uint4 normalized = f32u32 + 0xC8000000u;
  uint4 denormalized =
      ((f32u32 & 0x7FFFFFu) | 0x800000u) >> ((113u).xxxx - (f32u32 >> 23u));
  uint4 f24u32 =
      normalized + (denormalized - normalized) * uint4(f32u32 < 0x38800000u);
  return ((f24u32 + 3u + ((f24u32 >> 3u) & 1u)) >> 3u) & 0xFFFFFFu;
}

uint4 XeFloat20e4To32(uint4 f24u32) {
  uint4 mantissa = f24u32 & 0xFFFFFu;
  uint4 exponent = f24u32 >> 20u;
  // Normalize the values for the denormalized components.
  // Exponent = 1;
  // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x100000) == 0);
  uint4 is_denormalized = uint4(exponent == 0u);
  uint4 mantissa_lzcnt = (20u).xxxx - firstbithigh(mantissa);
  exponent += ((1u).xxxx - mantissa_lzcnt - exponent) * is_denormalized;
  mantissa +=
      (((mantissa << mantissa_lzcnt) & 0xFFFFFu) - mantissa) * is_denormalized;
  // Combine into 32-bit float bits and clear zeros.
  return (((exponent + 112u) << 23u) | (mantissa << 3u)) * uint4(f24u32 != 0u);
}

#endif  // XENIA_GPU_D3D12_SHADERS_PIXEL_FORMATS_HLSLI_
