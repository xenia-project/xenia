#ifndef XENIA_GPU_D3D12_SHADERS_PIXEL_FORMATS_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_PIXEL_FORMATS_HLSLI_

// Assuming the original number has only 10 bits.
uint4 XeSNorm10To16(uint4 s10) {
  uint4 signs = s10 >> 9u;
  // -512 and -511 are both -1.0, but with -512 the conversion will overflow.
  s10 = s10 == 0x200u ? 0x201u : s10;
  // Take the absolute value.
  s10 = (s10 ^ (signs ? 0x3FFu : 0u)) + signs;
  // Expand the 9-bit absolute value to 15 bits like unorm.
  s10 = (s10 << 6u) | (s10 >> 3u);
  // Apply the sign.
  return (s10 ^ (signs ? 0xFFFFu : 0u)) + signs;
}

// Assuming the original number has only 11 bits.
uint4 XeSNorm11To16(uint4 s11) {
  uint4 signs = s11 >> 10u;
  // -1024 and -1023 are both -1.0, but with -1024 the conversion will overflow.
  s11 = s11 == 0x400u ? 0x401u : s11;
  // Take the absolute value.
  s11 = (s11 ^ (signs ? 0x7FFu : 0u)) + signs;
  // Expand the 10-bit absolute value to 15 bits like unorm.
  s11 = (s11 << 5u) | (s11 >> 5u);
  // Apply the sign.
  return (s11 ^ (signs ? 0xFFFFu : 0u)) + signs;
}

// https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

uint XeFloat32To7e3(uint4 rgba_f32u32) {
  // Keep only positive integers and saturate to 31.875 (also dropping NaNs).
  // Was previously done with `asuint(clamp(asint(rgb_f32u32), 0, 0x41FF0000))`,
  // but FXC decides to ignore the uint->int cast, and negative numbers become
  // 0x41FF0000.
  rgba_f32u32.rgb = min(
      (rgba_f32u32.rgb <= 0x7FFFFFFFu) ? rgba_f32u32.rgb : (0u).xxx,
      0x41FF0000u);
  uint3 denormalized = ((rgba_f32u32.rgb & 0x7FFFFFu) | 0x800000u) >>
                       min((125u).xxx - (rgba_f32u32.rgb >> 23u), 24u);
  uint3 rgb_f10u32 =
      (rgba_f32u32.rgb < 0x3E800000u) ? denormalized
                                      : (rgba_f32u32.rgb + 0xC2000000u);
  rgb_f10u32 =
      ((rgb_f10u32 + 0x7FFFu + ((rgb_f10u32 >> 16u) & 1u)) >> 16u) & 0x3FFu;
  // Rounding alpha to the nearest integer.
  // https://docs.microsoft.com/en-us/windows/desktop/direct3d10/d3d10-graphics-programming-guide-resources-data-conversion
  return rgb_f10u32.r | (rgb_f10u32.g << 10u) | (rgb_f10u32.b << 20u) |
         (uint(round(saturate(asfloat(rgba_f32u32.a)) * 3.0)) << 30u);
}

uint4 XeFloat7e3To32(uint rgba_packed) {
  uint3 rgb_f10u32 = (rgba_packed.xxx >> uint3(0u, 10u, 20u)) & 0x3FFu;
  uint3 mantissa = rgb_f10u32 & 0x7Fu;
  uint3 exponent = rgb_f10u32 >> 7u;
  // Normalize the values for the denormalized components.
  // Exponent = 1;
  // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x80) == 0);
  bool3 is_denormalized = exponent == 0u;
  uint3 mantissa_lzcnt = (7u).xxx - firstbithigh(mantissa);
  exponent = is_denormalized ? ((1u).xxx - mantissa_lzcnt) : exponent;
  mantissa =
      is_denormalized ? ((mantissa << mantissa_lzcnt) & 0x7Fu) : mantissa;
  // Combine into 32-bit float bits and clear zeros.
  uint3 rgb_f32u32 =
      (rgb_f10u32 != 0u) ? (((exponent + 124u) << 23u) | (mantissa << 16u))
                         : (0u).xxx;
  return uint4(rgb_f32u32, asuint(float(rgba_packed >> 30u) * (1.0 / 3.0)));
}

// Based on CFloat24 from d3dref9.dll and the 6e4 code from:
// https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
// 6e4 has a different exponent bias allowing [0,512) values, 20e4 allows [0,2).
// We also can't clamp the stored value to 1 as load->store->load must be exact.

uint4 XeFloat32To20e4(uint4 f32u32) {
  // Keep only positive (high bit set means negative for both float and int) and
  // saturate to the maximum representable value near 2 (also dropping NaNs).
  f32u32 = min((f32u32 <= 0x7FFFFFFFu) ? f32u32 : (0u).xxxx, 0x3FFFFFF8u);
  uint4 denormalized = ((f32u32 & 0x7FFFFFu) | 0x800000u) >>
                       min((113u).xxxx - (f32u32 >> 23u), 24u);
  uint4 f24u32 = (f32u32 < 0x38800000u) ? denormalized : (f32u32 + 0xC8000000u);
  return ((f24u32 + 3u + ((f24u32 >> 3u) & 1u)) >> 3u) & 0xFFFFFFu;
}

uint4 XeFloat20e4To32(uint4 f24u32) {
  uint4 mantissa = f24u32 & 0xFFFFFu;
  uint4 exponent = f24u32 >> 20u;
  // Normalize the values for the denormalized components.
  // Exponent = 1;
  // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x100000) == 0);
  bool4 is_denormalized = exponent == 0u;
  uint4 mantissa_lzcnt = (20u).xxxx - firstbithigh(mantissa);
  exponent = is_denormalized ? ((1u).xxxx - mantissa_lzcnt) : exponent;
  mantissa =
      is_denormalized ? ((mantissa << mantissa_lzcnt) & 0xFFFFFu) : mantissa;
  // Combine into 32-bit float bits and clear zeros.
  return (f24u32 != 0u) ? (((exponent + 112u) << 23u) | (mantissa << 3u))
                        : (0u).xxxx;
}

// Sorts the color indices of four DXT3/DXT5 or DXT1 opaque blocks so they can
// be used as the weights for the second endpoint, from 0 to 3. To get the
// weights for the first endpoint, apply bitwise NOT to the result.
uint4 XeDXTHighColorWeights(uint4 codes) {
  // Initially 00 = 3:0, 01 = 0:3, 10 = 2:1, 11 = 1:2.
  // Swap bits. 00 = 3:0, 01 = 2:1, 10 = 0:3, 11 = 1:2.
  codes = ((codes & 0x55555555u) << 1u) | ((codes & 0xAAAAAAAAu) >> 1u);
  // Swap 10 and 11. 00 = 3:0, 01 = 2:1, 10 = 1:2, 11 = 0:3.
  return codes ^ ((codes & 0xAAAAAAAAu) >> 1u);
}

// Converts endpoint RGB (first in the low 16 bits, second in the high) of four
// DXT blocks to 8-bit, with 2 unused bits between each component to allow for
// overflow when multiplying by values up to 3 (so multiplication can be done
// for all components at once).
void XeDXTColorEndpointsTo8In10(uint4 rgb_565, out uint4 rgb_10b_low,
                                out uint4 rgb_10b_high) {
  // Converting 5:6:5 to 8:8:8 similar to how Compressonator does that.
  // https://github.com/GPUOpen-Tools/Compressonator/blob/master/Compressonator/Source/Codec/DXTC/Codec_DXTC_RGBA.cpp#L429
  rgb_10b_low = ((rgb_565 & 31u) << 23u) |
                ((rgb_565 & (7u << 2u)) << (20u - 2u)) |
                ((rgb_565 & (63u << 5u)) << (12u - 5u)) |
                ((rgb_565 & (3u << 9u)) << (10u - 9u)) |
                ((rgb_565 & (31u << 11u)) >> (11u - 3u)) |
                ((rgb_565 & (7u << 13u)) >> 13u);
  rgb_10b_high = ((rgb_565 & (31u << 16u)) << (23u - 16u)) |
                 ((rgb_565 & (7u << 18u)) << (20u - 18u)) |
                 ((rgb_565 & (63u << 21u)) >> (21u - 12u)) |
                 ((rgb_565 & (3u << 25u)) >> (25u - 10u)) |
                 ((rgb_565 & (31u << 27u)) >> (27u - 3u)) |
                 ((rgb_565 & (7u << 29u)) >> 29u);
}

// Gets the colors of one row of four DXT opaque blocks. Endpoint colors can be
// obtained using XeDXTColorEndpointsTo8In10 (8 bits with 2 bits of free space
// between each), weights can be obtained using XeDXTHighColorWeights. Alpha is
// set to 0 in the result. Weights must be shifted right by 8 * row index before
// calling.
void XeDXTFourBlocksRowToRGB8(
    uint4 rgb_10b_low, uint4 rgb_10b_high, uint4 weights_high, out uint4 row_0,
    out uint4 row_1, out uint4 row_2, out uint4 row_3) {
  uint4 weights_low = ~weights_high;
  const uint4 weights_shifts = uint4(0u, 2u, 4u, 6u);
  uint4 block_row_10b_3x =
      ((weights_low.xxxx >> weights_shifts) & 3u) * rgb_10b_low.x +
      ((weights_high.xxxx >> weights_shifts) & 3u) * rgb_10b_high.x;
  row_0 = ((block_row_10b_3x & 1023u) / 3u) |
          ((((block_row_10b_3x >> 10u) & 1023u) / 3u) << 8u) |
          (((block_row_10b_3x >> 20u) / 3u) << 16u);
  block_row_10b_3x =
      ((weights_low.yyyy >> weights_shifts) & 3u) * rgb_10b_low.y +
      ((weights_high.yyyy >> weights_shifts) & 3u) * rgb_10b_high.y;
  row_1 = ((block_row_10b_3x & 1023u) / 3u) |
          ((((block_row_10b_3x >> 10u) & 1023u) / 3u) << 8u) |
          (((block_row_10b_3x >> 20u) / 3u) << 16u);
  block_row_10b_3x =
      ((weights_low.zzzz >> weights_shifts) & 3u) * rgb_10b_low.z +
      ((weights_high.zzzz >> weights_shifts) & 3u) * rgb_10b_high.z;
  row_2 = ((block_row_10b_3x & 1023u) / 3u) |
          ((((block_row_10b_3x >> 10u) & 1023u) / 3u) << 8u) |
          (((block_row_10b_3x >> 20u) / 3u) << 16u);
  block_row_10b_3x =
      ((weights_low.wwww >> weights_shifts) & 3u) * rgb_10b_low.w +
      ((weights_high.wwww >> weights_shifts) & 3u) * rgb_10b_high.w;
  row_3 = ((block_row_10b_3x & 1023u) / 3u) |
          ((((block_row_10b_3x >> 10u) & 1023u) / 3u) << 8u) |
          (((block_row_10b_3x >> 20u) / 3u) << 16u);
}

uint4 XeDXT3FourBlocksRowToA8(uint4 alpha4) {
  // (alpha4 & 0xFu) | ((alpha4 & 0xFu) << 4u) |
  // ((alpha4 & 0xF0u) << (8u - 4u)) | ((alpha4 & 0xF0u) << (12u - 4u)) |
  // ((alpha4 & 0xF00u) << (16u - 8u)) | ((alpha4 & 0xF00u) << (20u - 8u)) |
  // ((alpha4 & 0xF000u) << (24u - 12u)) | ((alpha4 & 0xF000u) << (28u - 12u))
  return (alpha4 & 0xFu) | ((alpha4 & 0xFFu) << 4u) |
         ((alpha4 & 0xFF0u) << 8u) | ((alpha4 & 0xFF00u) << 12u) |
         ((alpha4 & 0xF000u) << 16u);
}

// & 0x249249 = bits 0 of 24 bits of DXT5 alpha codes.
// & 0x492492 = bits 1 of 24 bits of DXT5 alpha codes.
// & 0x924924 = bits 2 of 24 bits of DXT5 alpha codes.

// Sorts half (24 bits) of the codes of four DXT5 alpha blocks so they can be
// used as weights for the second endpoint, from 0 to 7, in alpha0 > alpha1
// mode.
uint4 XeDXT5High8StepAlphaWeights(uint4 codes_24b) {
  // Initially 000 - first endpoint, 001 - second endpoint, 010 and above -
  // weights from 6:1 to 1:6. Need to make 001 111, and subtract 1 from 010 and
  // above.
  // Whether the bits are 000 (the first endpoint only).
  uint4 is_first = ((codes_24b & 0x249249u) | ((codes_24b & 0x492492u) >> 1u) |
                    ((codes_24b & 0x924924u) >> 2u)) ^ 0x249249u;
  // Whether the bits are 001 (the second endpoint only).
  uint4 is_second = (codes_24b & 0x249249u) & ~((codes_24b & 0x492492u) >> 1u) &
                    ~((codes_24b & 0x924924u) >> 2u);
  // Change 000 to 001 so subtracting 1 will result in 0 (and there will never
  // be overflow), subtract 1, and if the code was originally 001 (the second
  // endpoint only), make it 111.
  return ((codes_24b | is_first) - 0x249249u) |
         is_second | (is_second << 1u) | (is_second << 2u);
}

// Sorts half (24 bits) of the codes of four DXT5 alpha blocks so they can be
// used as weights for the second endpoint, from 0 to 5, in alpha0 <= alpha1
// mode, except for 110 and 111 which represent 0 and 1 constants.
uint4 XeDXT5High6StepAlphaWeights(uint4 codes_24b) {
  // Initially:
  // 000 - first endpoint.
  // 001 - second endpoint.
  // 010 - 4:1.
  // 011 - 3:2.
  // 100 - 2:3.
  // 101 - 1:4.
  // 110 - constant 0.
  // 111 - constant 1.
  // Create 3-bit masks (111 or 000) of whether the codes represent 0 or 1
  // constants to keep them 110 and 111 later.
  uint4 is_constant = codes_24b & 0x492492u & ((codes_24b & 0x924924u) >> 1u);
  is_constant |= (is_constant << 1u) | (is_constant >> 1u);
  // Store the codes for the constants (110 or 111), or 0 if not a constant.
  uint4 constant_values =
      ((codes_24b & 0x249249u) | (0x492492u | 0x924924u)) & is_constant;
  // Need to make 001 101, and subtract 1 from 010 and above (constants will be
  // handled separately later).
  // Whether the bits are 000 (the first endpoint only).
  uint4 is_first = ((codes_24b & 0x249249u) | ((codes_24b & 0x492492u) >> 1u) |
                    ((codes_24b & 0x924924u) >> 2u)) ^ 0x249249u;
  // Whether the bits are 001 (the second endpoint only).
  uint4 is_second = (codes_24b & 0x249249u) & ~((codes_24b & 0x492492u) >> 1u) &
                    ~((codes_24b & 0x924924u) >> 2u);
  // Change 000 to 001 so subtracting 1 will result in 0 (and there will never
  // be overflow), subtract 1, and if the code was originally 001 (the second
  // endpoint only), make it 101.
  codes_24b =
      ((codes_24b | is_first) - 0x249249u) | is_second | (is_second << 2u);
  // Make constants 110 and 111 again (they are 101 and 110 now).
  return (codes_24b & ~is_constant) | constant_values;
}

uint4 XeDXT5Four8StepBlocksRowToA8(uint4 end_low, uint4 end_high,
                                   uint4 weights_8step, uint4 weights_6step) {
  // Choose the mode.
  bool4 is_6step = end_low <= end_high;
  uint4 weights_high = is_6step ? weights_6step : weights_8step;
  uint4 weight_max = is_6step ? (5u.xxxx) : (7u.xxxx);
  // In the 6-step mode, make a mask for whether the weights are constants.
  uint4 is_constant = is_6step ? (weights_6step & 0x492u &
                                  ((weights_6step & 0x924u) >> 1u))
                               : (0u).xxxx;
  is_constant |= (is_constant << 1u) | (is_constant >> 1u);
  // Get the weights for the first endpoint and remove constant from the
  // interpolation (set weights of the endpoints to 0 for them). First need to
  // zero the weights of the second endpoint so 6 or 7 won't be subtracted from
  // 5 while getting the weights of the first endpoint.
  weights_high &= ~is_constant;
  uint4 weights_low = ((weight_max * 0x249u) - weights_high) & ~is_constant;
  // Interpolate.
  uint4 row =
      ((end_low * (weights_low & 7u) +
        end_high * (weights_high & 7u)) / weight_max) |
      (((end_low * ((weights_low >> 3u) & 7u) +
         end_high * ((weights_high >> 3u) & 7u)) / weight_max) << 8u) |
      (((end_low * ((weights_low >> 6u) & 7u) +
         end_high * ((weights_high >> 6u) & 7u)) / weight_max) << 16u) |
      (((end_low * ((weights_low >> 9u) & 7u) +
         end_high * ((weights_high >> 9u) & 7u)) / weight_max) << 24u);
  // Get the constant values as 1 bit per pixel separated by 7 bits.
  uint4 constant_values = weights_6step & is_constant;
  constant_values = (constant_values & 1u) |
                    ((constant_values & (1u << 3u)) << (8u - 3u)) |
                    ((constant_values & (1u << 6u)) << (16u - 6u)) |
                    ((constant_values & (1u << 9u)) << (24u - 9u));
  // Add constant 1 where needed.
  return row + constant_values * 0xFFu;
}

#endif  // XENIA_GPU_D3D12_SHADERS_PIXEL_FORMATS_HLSLI_
