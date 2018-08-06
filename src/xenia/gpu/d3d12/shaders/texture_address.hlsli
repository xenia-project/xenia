#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_

// XeTiledOffset function take x/y in blocks and returns byte offsets for 4
// consecutive blocks along X.

// https://github.com/gildor2/UModel/blob/de8fbd3bc922427ea056b7340202dcdcc19ccff5/Unreal/UnTexture.cpp#L495
uint4 XeTextureTiledOffset2D(uint2 p, uint width, uint bpb_log2) {
  uint4 x4 = uint4(0u, 1u, 2u, 3u) + p.xxxx;
  // Top bits of coordinates.
  uint4 macro =
      ((x4 >> 5u) + (p.y >> 5u) * ((width + 31u) >> 5u)) << (bpb_log2 + 7u);
  // Lower bits of coordinates (result is 6-bit value).
  uint4 micro = ((x4 & 7u) + ((p.y & 0xEu) << 2u)) << bpb_log2;
  // Mix micro/macro + add few remaining x/y bits.
  uint4 offset =
      macro + ((micro & ~0xFu) << 1u) + (micro & 0xFu) + ((p.y & 1u) << 4u);
  // Mix bits again.
  return ((offset & ~0x1FFu) << 3u) +  // Upper bits (offset bits [*-9]).
         ((p.y & 16u) << 7u) +  // Next 1 bit.
         ((offset & 0x1C0u) << 2u) +  // Next 3 bits (offset bits [8-6]).
         ((((x4 >> 3u) + ((p.y & 8u) >> 2u)) & 3u) << 6u) +  // Next 2 bits.
         (offset & 0x3Fu);  // Lower 6 bits (offset bits [5-0]).
}

// Reverse-engineered from an executable.
// The base/micro/macro names were chosen pretty much at random and don't have
// the same meaning as in TiledOffset2D.
uint4 XeTextureTiledOffset3D(uint3 p, uint2 width_height, uint bpb_log2) {
  uint4 x4 = uint4(0u, 1u, 2u, 3u) + p.xxxx;
  uint2 aligned_size = (width_height + 31u) & ~31u;
  uint base = ((p.z >> 2u) * (aligned_size.y >> 4u) + (p.y >> 4u)) *
              (aligned_size.x >> 5u);
  uint4 micro = (((p.z >> 2u) + (p.y >> 3u)) & 1u).xxxx;
  micro += (((micro << 1u) + (x4 >> 3u)) & 3u) << 1u;
  uint4 macro = (((x4 & 7u) + ((p.y & 6u) << 2u)) << (bpb_log2 + 6u)) >> 6u;
  macro = (((((((x4 >> 5u) + base) << (bpb_log2 + 6u)) & 0xFFFFFFFu) << 1u) +
            (macro & ~15u)) << 1u) + (macro & 15u) +
            ((p.z & 3u) << (bpb_log2 + 6u)) + ((p.y & 1u) << 4u);
  return ((((((((macro >> 6u) & 7u) + ((micro & 1u) << 3u)) << 3u) +
             (micro & ~1u)) << 2u) + (macro & ~511u)) << 3u) + (macro & 63u);
}

uint XeTextureGuestLinearOffset(uint3 p, uint height, uint pitch, uint bpb) {
  return p.x * bpb + (p.z * ((height + 31u) & ~31u) + p.y) * pitch;
}

uint XeTextureHostLinearOffset(uint3 p, uint height, uint pitch, uint bpb) {
  return p.x * bpb + (p.z * height + p.y) * pitch;
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
