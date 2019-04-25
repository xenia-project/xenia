#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_

// XeTiledOffset function take x/y in blocks and returns byte offsets for 4
// consecutive blocks along X.

// https://github.com/gildor2/UModel/blob/de8fbd3bc922427ea056b7340202dcdcc19ccff5/Unreal/UnTexture.cpp#L495
uint4 XeTextureTiledOffset2D(uint2 p, uint storage_width, uint bpb_log2) {
  uint4 x4 = uint4(0u, 1u, 2u, 3u) + p.xxxx;
  // Top bits of coordinates.
  uint4 macro = ((x4 >> 5u) + (p.y >> 5u) * ((storage_width + 31u) >> 5u)) <<
                (bpb_log2 + 7u);
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

// Reverse-engineered from XGRAPHICS::TileVolume in an executable.
uint4 XeTextureTiledOffset3D(uint3 p, uint2 storage_width_height,
                             uint bpb_log2) {
  storage_width_height = (storage_width_height + 31u) & ~31u;
  uint4 x4 = uint4(0u, 1u, 2u, 3u) + p.xxxx;
  uint macro_outer =
      ((p.y >> 4u) + (p.z >> 2u) * (storage_width_height.y >> 4u)) *
      (storage_width_height.x >> 5u);
  uint4 macro =
      ((((x4 >> 5u) + macro_outer) << (bpb_log2 + 6u)) & 0xFFFFFFFu) << 1u;
  uint4 micro = (((x4 & 7u) + ((p.y & 6u) << 2u)) << (bpb_log2 + 6u)) >> 6u;
  uint offset_outer = ((p.y >> 3u) + (p.z >> 2u)) & 1u;
  uint4 offset1 =
      offset_outer + ((((x4 >> 3u) + (offset_outer << 1u)) & 3u) << 1u);
  uint4 offset2 =
      ((macro + (micro & ~15u)) << 1u) + (micro & 15u) +
      ((p.z & 3u) << (bpb_log2 + 6u)) + ((p.y & 1u) << 4u);
  uint4 address = (offset1 & 1u) << 3u;
  address += uint4(int4(offset2) >> 6) & 7u;
  address <<= 3u;
  address += offset1 & ~1u;
  address <<= 2u;
  address += offset2 & ~511u;
  address <<= 3u;
  address += offset2 & 63u;
  return address;
}

uint XeTextureGuestLinearOffset(uint3 p, uint height, uint pitch, uint bpb) {
  return p.x * bpb + (p.z * ((height + 31u) & ~31u) + p.y) * pitch;
}

uint XeTextureHostLinearOffset(uint3 p, uint height, uint pitch, uint bpb) {
  return p.x * bpb + (p.z * height + p.y) * pitch;
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
