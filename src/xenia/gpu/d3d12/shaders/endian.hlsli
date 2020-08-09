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

uint4 XeEndianSwap128(uint4 value, uint endian) {
  if (endian == kXenosEndian_8in128) {
    value = value.wzyx;
    endian = kXenosEndian_8in32;
  }
  return XeEndianSwap64(value, endian);
}

#endif  // XENIA_GPU_D3D12_SHADERS_ENDIAN_HLSLI_
