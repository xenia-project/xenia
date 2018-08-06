#ifndef XENIA_GPU_D3D12_SHADERS_BYTE_SWAP_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_BYTE_SWAP_HLSLI_

#define XE_BYTE_SWAP_OVERLOAD(XeByteSwapType) \
XeByteSwapType XeByteSwap(XeByteSwapType v, uint endian) { \
  [flatten] if (((endian ^ (endian >> 1u)) & 1u) != 0u) { \
    v = ((v & 0x00FF00FFu) << 8u) | ((v & 0xFF00FF00u) >> 8u); \
  } \
  [flatten] if ((endian & 2u) != 0u) { \
    v = (v << 16u) | (v >> 16u); \
  } \
  return v; \
}
XE_BYTE_SWAP_OVERLOAD(uint)
XE_BYTE_SWAP_OVERLOAD(uint2)
XE_BYTE_SWAP_OVERLOAD(uint3)
XE_BYTE_SWAP_OVERLOAD(uint4)

#endif  // XENIA_GPU_D3D12_SHADERS_BYTE_SWAP_HLSLI_
