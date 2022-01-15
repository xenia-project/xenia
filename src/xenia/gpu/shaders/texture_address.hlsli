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

// Log2 of the number of blocks always laid out consecutively in memory along
// the horizontal axis.
uint XeTextureTiledConsecutiveBlocksLog2(uint bpb_log2) {
  // 1bpb and 2bpb - 8.
  // 4bpb - 4.
  // 8bpb - 2.
  // 16bpb - 1.
  return min(4u - bpb_log2, 3u);
}

// Odd sequences of consecutive blocks along the horizontal axis are placed at a
// fixed offset in memory from the preceding even ones. Returns the distance
// between the beginnings of the even and its corresponding odd sequences.
uint XeTextureTiledOddConsecutiveBlocksOffset(uint bpb_log2) {
  return bpb_log2 >= 2u ? 32u : 64u;
}

// For shaders to be able to copy multiple horizontally adjacent pixels in the
// same way regardless of the resolution scale chosen, scaling is done at Nx1
// granularity where N matches the number of pixels that are consecutive with
// guest tiling, rather than within individual guest pixels:
// - 1bpp - 8x1 host pixels (can copy via R32G32_UINT)
// - 2bpp - 8x1 host pixels (can copy via R32G32B32A32_UINT)
// - 4bpp - 4x1 host pixels
// - 8bpp - 2x1 host pixels
// - 16bpp - 1x1 host pixels
// For better access locality, because compute shaders in Xenia usually have 2D
// thread groups, host Nx1 sub-units are scaled within guest Nx1 units in a
// column-major way.
// So, for example, in a 2bpp texture with 2x2 resolution scale, 16 guest bytes,
// or 64 host bytes, contain:
// - 16 host bytes - 8x1 top-left portion
// - 16 host bytes - 8x1 bottom-left portion
// - 16 host bytes - 8x1 top-right portion
// - 16 host bytes - 8x1 bottom-right portion
// This function is used only for non-negative positions within a texture, so
// for simplicity, especially of the division involved, assuming everything is
// unsigned.
uint XeTextureScaledTiledOffset(bool is_3d, uint3 p, uint pitch_aligned,
                                uint height_aligned, uint bpb_log2,
                                uint2 scale) {
  uint unit_width_log2 = XeTextureTiledConsecutiveBlocksLog2(bpb_log2);
  // Global host X coordinate in host Nx1 sub-units.
  uint x_subunits = p.x >> unit_width_log2;
  // Global guest XY coordinate in guest Nx1 units.
  uint2 xy_unit_guest = uint2(x_subunits, p.y) / scale;
  // Global guest XYZ coordinate of the beginning of the Nx1 unit.
  uint3 unit_guest_origin =
      uint3(xy_unit_guest.x << unit_width_log2, xy_unit_guest.y, p.z);
  // Global guest linear address of the beginning of Nx1 unit in bytes.
  uint unit_guest_address;
  [branch] if (is_3d) {
    unit_guest_address =
        uint(XeTextureTiledOffset3D(int3(unit_guest_origin), pitch_aligned,
                                    height_aligned, bpb_log2));
  } else {
    unit_guest_address =
        uint(XeTextureTiledOffset2D(int2(unit_guest_origin.xy), pitch_aligned,
                                    bpb_log2));
  }
  // Unit-local host XY index of the host Nx1 sub-unit.
  // Also see XeTextureScaledRightSubUnitOffsetInConsecutivePair for common
  // subexpression elimination information as this remainder calculation is done
  // there too.
  uint2 unit_subunit = uint2(x_subunits, p.y) - xy_unit_guest * scale;
  // Combine:
  // - Guest global unit address.
  // - Host unit-local sub-unit index.
  // - Host pixel within a sub-unit (if the offset is requested at a smaller
  //   granularity than a whole sub-unit).
  return unit_guest_address * (scale.x * scale.y) +
         ((((unit_subunit.x * scale.y + unit_subunit.y) << unit_width_log2) +
           (p.x & ((1u << unit_width_log2) - 1u)))
          << bpb_log2);
}

// Offset of the beginning of next host sub-unit along the horizontal axis
// within a pair of guest units.
// x must be a multiple of 1 << (XeTextureTiledConsecutiveBlocksLog2 + 1) - to
// go from one pair of consecutive blocks to another, full tiled offset
// recalculation is required.
uint XeTextureScaledRightSubUnitOffsetInConsecutivePair(uint x, uint bpb_log2,
                                                        uint2 scale) {
  uint right_sub_unit_offset_columns;
  uint tiled_consecutive_offset =
      XeTextureTiledOddConsecutiveBlocksOffset(bpb_log2);
  [branch] if (scale.x > 1u) {
    uint subunit_width_log2 = XeTextureTiledConsecutiveBlocksLog2(bpb_log2);
    uint subunit_size_log2 = subunit_width_log2 + bpb_log2;
    // While % can be used here to take the modulo, for better common
    // subexpression elimination between this function and
    // XeTextureScaledTiledOffset when both are used, taking the remainder the
    // same way.
    uint x_subunits = x >> subunit_width_log2;
    uint unit_subunit_x = x_subunits - (x_subunits / scale.x) * scale.x;
    if (unit_subunit_x + 1u == scale.x) {
      // The next host sub-unit is in the other, odd guest unit.
      right_sub_unit_offset_columns = tiled_consecutive_offset * scale.x -
                                      (unit_subunit_x << subunit_size_log2);
    } else {
      // The next host sub-unit is in the same guest unit.
      right_sub_unit_offset_columns = 1u << subunit_size_log2;
    }
  } else {
    right_sub_unit_offset_columns = tiled_consecutive_offset;
  }
  // The layout of sub-units within one unit is column-major.
  return right_sub_unit_offset_columns * scale.y;
}

int XeTextureGuestLinearOffset(int3 p, uint pitch, uint height_aligned,
                               uint bpb) {
  return p.x * int(bpb) + (p.z * int(height_aligned) + p.y) * int(pitch);
}

int XeTextureHostLinearOffset(int3 p, uint pitch, uint height, uint bpb) {
  return p.x * int(bpb) + (p.z * int(height) + p.y) * int(pitch);
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_ADDRESS_HLSLI_
