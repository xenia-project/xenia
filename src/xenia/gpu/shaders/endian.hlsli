#ifndef XENIA_GPU_D3D12_SHADERS_ENDIAN_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_ENDIAN_HLSLI_

// 2-bit (xenos::Endian) and 3-bit (xenos::Endian128).
#define kXenosEndian_None 0u
#define kXenosEndian_8in16 1u
#define kXenosEndian_8in32 2u
#define kXenosEndian_16in32 3u
// 3-bit only.
#define kXenosEndian_8in64 4u
#define kXenosEndian_8in128 5u

// Can also swap two packed 16-bit values.
#define XE_ENDIAN_SWAP_16_OVERLOAD(XeEndianSwapType) \
XeEndianSwapType XeEndianSwap16(XeEndianSwapType value, uint endian) { \
  if (endian == kXenosEndian_8in16) { \
    value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u); \
  } \
  return value; \
}
XE_ENDIAN_SWAP_16_OVERLOAD(uint)
XE_ENDIAN_SWAP_16_OVERLOAD(uint2)
XE_ENDIAN_SWAP_16_OVERLOAD(uint3)
XE_ENDIAN_SWAP_16_OVERLOAD(uint4)

// 4 + 1 version for 3x3 resolution scale resolves.
void XeEndianSwap16(inout uint4 value, inout uint value_4, uint endian) {
  if (endian == kXenosEndian_8in16) {
    value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u);
    value_4 = ((value_4 & 0x00FF00FFu) << 8u) | ((value_4 & 0xFF00FF00u) >> 8u);
  }
}

#define XE_ENDIAN_SWAP_32_OVERLOAD(XeEndianSwapType) \
XeEndianSwapType XeEndianSwap32(XeEndianSwapType value, uint endian) { \
  if (endian == kXenosEndian_8in16 || endian == kXenosEndian_8in32) { \
    value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u); \
  } \
  if (endian == kXenosEndian_8in32 || endian == kXenosEndian_16in32) { \
    value = (value << 16u) | (value >> 16u); \
  } \
  return value; \
}
XE_ENDIAN_SWAP_32_OVERLOAD(uint)
XE_ENDIAN_SWAP_32_OVERLOAD(uint2)
XE_ENDIAN_SWAP_32_OVERLOAD(uint3)
XE_ENDIAN_SWAP_32_OVERLOAD(uint4)

// 4 + 1 version for 3x3 resolution scale resolves.
void XeEndianSwap32(inout uint4 value, inout uint value_4, uint endian) {
  if (endian == kXenosEndian_8in16 || endian == kXenosEndian_8in32) {
    value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u);
    value_4 = ((value_4 & 0x00FF00FFu) << 8u) | ((value_4 & 0xFF00FF00u) >> 8u);
  }
  if (endian == kXenosEndian_8in32 || endian == kXenosEndian_16in32) {
    value = (value << 16u) | (value >> 16u);
    value_4 = (value_4 << 16u) | (value_4 >> 16u);
  }
}

uint2 XeEndianSwap64(uint2 value, uint endian) {
  if (endian == kXenosEndian_8in64) {
    value = value.yx;
    endian = kXenosEndian_8in32;
  }
  return XeEndianSwap32(value, endian);
}

uint4 XeEndianSwap64(uint4 value, uint endian) {
  if (endian == kXenosEndian_8in64) {
    value = value.yxwz;
    endian = kXenosEndian_8in32;
  }
  return XeEndianSwap32(value, endian);
}

// 2 + 2 version for 3x3 resolution scale resolves.
void XeEndianSwap64(inout uint4 value_01, inout uint4 value_23, uint endian) {
  if (endian == kXenosEndian_8in64) {
    value_01 = value_01.yxwz;
    value_23 = value_23.yxwz;
    endian = kXenosEndian_8in32;
  }
  if (endian == kXenosEndian_8in16 || endian == kXenosEndian_8in32) {
    value_01 = ((value_01 & 0x00FF00FFu) << 8u) |
               ((value_01 & 0xFF00FF00u) >> 8u);
    value_23 = ((value_23 & 0x00FF00FFu) << 8u) |
               ((value_23 & 0xFF00FF00u) >> 8u);
  }
  if (endian == kXenosEndian_8in32 || endian == kXenosEndian_16in32) {
    value_01 = (value_01 << 16u) | (value_01 >> 16u);
    value_23 = (value_23 << 16u) | (value_23 >> 16u);
  }
}

// 2 + 2 + 1 version for 3x3 resolution scale resolves.
void XeEndianSwap64(inout uint4 value_01, inout uint4 value_23,
                    inout uint2 value_4, uint endian) {
  if (endian == kXenosEndian_8in64) {
    value_01 = value_01.yxwz;
    value_23 = value_23.yxwz;
    value_4 = value_4.yx;
    endian = kXenosEndian_8in32;
  }
  if (endian == kXenosEndian_8in16 || endian == kXenosEndian_8in32) {
    value_01 = ((value_01 & 0x00FF00FFu) << 8u) |
               ((value_01 & 0xFF00FF00u) >> 8u);
    value_23 = ((value_23 & 0x00FF00FFu) << 8u) |
               ((value_23 & 0xFF00FF00u) >> 8u);
    value_4 = ((value_4 & 0x00FF00FFu) << 8u) | ((value_4 & 0xFF00FF00u) >> 8u);
  }
  if (endian == kXenosEndian_8in32 || endian == kXenosEndian_16in32) {
    value_01 = (value_01 << 16u) | (value_01 >> 16u);
    value_23 = (value_23 << 16u) | (value_23 >> 16u);
    value_4 = (value_4 << 16u) | (value_4 >> 16u);
  }
}

uint4 XeEndianSwap128(uint4 value, uint endian) {
  if (endian == kXenosEndian_8in128) {
    value = value.wzyx;
    endian = kXenosEndian_8in32;
  }
  return XeEndianSwap64(value, endian);
}

// 4-value version for 3x3 resolution scale resolves.
void XeEndianSwap128(inout uint4 value_0, inout uint4 value_1,
                     inout uint4 value_2, inout uint4 value_3, uint endian) {
  if (endian == kXenosEndian_8in128) {
    value_0 = value_0.wzyx;
    value_1 = value_1.wzyx;
    value_2 = value_2.wzyx;
    value_3 = value_3.wzyx;
    endian = kXenosEndian_8in32;
  }
  if (endian == kXenosEndian_8in64) {
    value_0 = value_0.yxwz;
    value_1 = value_1.yxwz;
    value_2 = value_2.yxwz;
    value_3 = value_3.yxwz;
    endian = kXenosEndian_8in32;
  }
  if (endian == kXenosEndian_8in16 || endian == kXenosEndian_8in32) {
    value_0 = ((value_0 & 0x00FF00FFu) << 8u) | ((value_0 & 0xFF00FF00u) >> 8u);
    value_1 = ((value_1 & 0x00FF00FFu) << 8u) | ((value_1 & 0xFF00FF00u) >> 8u);
    value_2 = ((value_2 & 0x00FF00FFu) << 8u) | ((value_2 & 0xFF00FF00u) >> 8u);
    value_3 = ((value_3 & 0x00FF00FFu) << 8u) | ((value_3 & 0xFF00FF00u) >> 8u);
  }
  if (endian == kXenosEndian_8in32 || endian == kXenosEndian_16in32) {
    value_0 = (value_0 << 16u) | (value_0 >> 16u);
    value_1 = (value_1 << 16u) | (value_1 >> 16u);
    value_2 = (value_2 << 16u) | (value_2 >> 16u);
    value_3 = (value_3 << 16u) | (value_3 >> 16u);
  }
}

// 5-value version for 3x3 resolution scale resolves.
void XeEndianSwap128(inout uint4 value_0, inout uint4 value_1,
                     inout uint4 value_2, inout uint4 value_3,
                     inout uint4 value_4, uint endian) {
  if (endian == kXenosEndian_8in128) {
    value_0 = value_0.wzyx;
    value_1 = value_1.wzyx;
    value_2 = value_2.wzyx;
    value_3 = value_3.wzyx;
    value_4 = value_4.wzyx;
    endian = kXenosEndian_8in32;
  }
  if (endian == kXenosEndian_8in64) {
    value_0 = value_0.yxwz;
    value_1 = value_1.yxwz;
    value_2 = value_2.yxwz;
    value_3 = value_3.yxwz;
    value_4 = value_4.yxwz;
    endian = kXenosEndian_8in32;
  }
  if (endian == kXenosEndian_8in16 || endian == kXenosEndian_8in32) {
    value_0 = ((value_0 & 0x00FF00FFu) << 8u) | ((value_0 & 0xFF00FF00u) >> 8u);
    value_1 = ((value_1 & 0x00FF00FFu) << 8u) | ((value_1 & 0xFF00FF00u) >> 8u);
    value_2 = ((value_2 & 0x00FF00FFu) << 8u) | ((value_2 & 0xFF00FF00u) >> 8u);
    value_3 = ((value_3 & 0x00FF00FFu) << 8u) | ((value_3 & 0xFF00FF00u) >> 8u);
    value_4 = ((value_4 & 0x00FF00FFu) << 8u) | ((value_4 & 0xFF00FF00u) >> 8u);
  }
  if (endian == kXenosEndian_8in32 || endian == kXenosEndian_16in32) {
    value_0 = (value_0 << 16u) | (value_0 >> 16u);
    value_1 = (value_1 << 16u) | (value_1 >> 16u);
    value_2 = (value_2 << 16u) | (value_2 >> 16u);
    value_3 = (value_3 << 16u) | (value_3 >> 16u);
    value_4 = (value_4 << 16u) | (value_4 >> 16u);
  }
}

#endif  // XENIA_GPU_D3D12_SHADERS_ENDIAN_HLSLI_
