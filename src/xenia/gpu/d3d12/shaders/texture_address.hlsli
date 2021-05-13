#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_

int XeTextureTiledOffset2D(int2 p, uint pitch_aligned, uint bpb_log2) {
  // https://github.com/gildor2/UModel/blob/de8fbd3bc922427ea056b7340202dcdcc19ccff5/Unreal/UnTexture.cpp#L489
  // Top bits of coordinates.
  int macro =
      ((p.x >> 5) + (p.y >> 5) * int(pitch_aligned >> 5u)) << (bpb_log2 + 7);
  // Lower bits of coordinates (result is 6-bit value).
  int micro = ((p.x & 7) + ((p.y & 0xE) << 2)) << bpb_log2;
  // Mix micro/macro + add few remaining x/y bits.
  int offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((p.y & 1) << 4);
  // Mix bits again.
  return ((offset & ~0x1FF) << 3) +  // Upper bits (offset bits [*-9]).
         ((p.y & 16) << 7) +  // Next 1 bit.
         ((offset & 0x1C0) << 2) +  // Next 3 bits (offset bits [8-6]).
         (((((p.y & 8) >> 2) + (p.x >> 3)) & 3) << 6) +  // Next 2 bits.
         (offset & 0x3F);  // Lower 6 bits (offset bits [5-0]).
}

int XeTextureTiledOffset3D(int3 p, uint pitch_aligned, uint height_aligned,
                           uint bpb_log2) {
  // Reconstructed from disassembly of XGRAPHICS::TileVolume.
  int macro_outer = ((p.y >> 4) + (p.z >> 2) * int(height_aligned >> 4u)) *
                    int(pitch_aligned >> 5u);
  int macro = ((((p.x >> 5) + macro_outer) << (bpb_log2 + 6)) & 0xFFFFFFF) << 1;
  int micro = (((p.x & 7) + ((p.y & 6) << 2)) << (bpb_log2 + 6)) >> 6;
  int offset_outer = ((p.y >> 3) + (p.z >> 2)) & 1;
  int offset1 = offset_outer + ((((p.x >> 3) + (offset_outer << 1)) & 3) << 1);
  int offset2 = ((macro + (micro & ~15)) << 1) + (micro & 15) +
                ((p.z & 3) << (bpb_log2 + 6)) + ((p.y & 1) << 4);
  int address = (offset1 & 1) << 3;
  address += (offset2 >> 6) & 7;
  address <<= 3;
  address += offset1 & ~1;
  address <<= 2;
  address += offset2 & ~511;
  address <<= 3;
  address += offset2 & 63;
  return address;
}

int XeTextureGuestLinearOffset(int3 p, uint pitch, uint height_aligned,
                               uint bpb) {
  return p.x * int(bpb) + (p.z * int(height_aligned) + p.y) * int(pitch);
}

int XeTextureHostLinearOffset(int3 p, uint pitch, uint height, uint bpb) {
  return p.x * int(bpb) + (p.z * int(height) + p.y) * int(pitch);
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
