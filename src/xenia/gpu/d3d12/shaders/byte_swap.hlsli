#ifndef XENIA_GPU_D3D12_SHADERS_BYTE_SWAP_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_BYTE_SWAP_HLSLI_

// These functions may accept endianness without it being masked with & 3 -
// don't use ==, <=, >= here!

#define XE_BYTE_SWAP_OVERLOAD(XeByteSwapType) \
XeByteSwapType XeByteSwap(XeByteSwapType v, uint endian) { \
  if (((endian ^ (endian >> 1u)) & 1u) != 0u) { \
    v = ((v & 0x00FF00FFu) << 8u) | ((v & 0xFF00FF00u) >> 8u); \
  } \
  if ((endian & 2u) != 0u) { \
    v = (v << 16u) | (v >> 16u); \
  } \
  return v; \
}
XE_BYTE_SWAP_OVERLOAD(uint)
XE_BYTE_SWAP_OVERLOAD(uint2)
XE_BYTE_SWAP_OVERLOAD(uint3)
XE_BYTE_SWAP_OVERLOAD(uint4)

// Can also swap two packed 16-bit values.
#define XE_BYTE_SWAP_16_OVERLOAD(XeByteSwapType) \
XeByteSwapType XeByteSwap16(XeByteSwapType v, uint endian) { \
  if (((endian ^ (endian >> 1u)) & 1u) != 0u) { \
    v = ((v & 0x00FF00FFu) << 8u) | ((v & 0xFF00FF00u) >> 8u); \
  } \
  return v; \
}
XE_BYTE_SWAP_16_OVERLOAD(uint)
XE_BYTE_SWAP_16_OVERLOAD(uint2)
XE_BYTE_SWAP_16_OVERLOAD(uint3)
XE_BYTE_SWAP_16_OVERLOAD(uint4)

uint2 XeByteSwap64(uint2 v, uint endian) {
  if ((endian & 4u) != 0u) {
    v = v.yx;
    endian = 2u;
  }
  return XeByteSwap(v, endian);
}
uint4 XeByteSwap64(uint4 v, uint endian) {
  if ((endian & 4u) != 0u) {
    v = v.yxwz;
    endian = 2u;
  }
  return XeByteSwap(v, endian);
}

uint4 XeByteSwap128(uint4 v, uint endian) {
  if ((endian & 4u) != 0u) {
    v = ((endian & 1u) != 0u) ? v.wzyx /* 8in128 */ : v.yxwz /* 8in64 */;
    endian = 2u;
  }
  return XeByteSwap(v, endian);
}

#endif  // XENIA_GPU_D3D12_SHADERS_BYTE_SWAP_HLSLI_
