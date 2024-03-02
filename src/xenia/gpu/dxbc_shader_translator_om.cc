/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/texture_cache.h"

DEFINE_bool(use_fuzzy_alpha_epsilon, false,
            "Use approximate compare for alpha values to prevent flickering on "
            "NVIDIA graphics cards",
            "GPU");

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::StartPixelShader_LoadROVParameters() {
  bool any_color_targets_written = current_shader().writes_color_targets() != 0;

  // ***************************************************************************
  // Get EDRAM offsets for the pixel:
  // system_temp_rov_params_.y - for depth (absolute).
  // system_temp_rov_params_.z - for 32bpp color (base-relative).
  // system_temp_rov_params_.w - for 64bpp color (base-relative).
  // ***************************************************************************

  // For now, while we don't know the encoding of 64bpp render targets when
  // interpreted as 32bpp (no game has been seen reinterpreting between the two
  // yet), for consistency with the conventional render target logic and to have
  // the same resolve logic for both, storing 64bpp color as 40x16 samples
  // (multiplied by the resolution scale) per 1280-byte tile. It's also
  // convenient to use 40x16 granularity in the calculations here because depth
  // render targets have 40-sample halves swapped as opposed to color in each
  // tile, and reinterpretation between depth and color is common for depth /
  // stencil reloading into the EDRAM (such as in the background of the main
  // menu of 4D5307E6).

  // Convert the host pixel position to integer to system_temp_rov_params_.xy.
  // system_temp_rov_params_.x = X host pixel position as uint
  // system_temp_rov_params_.y = Y host pixel position as uint
  in_position_used_ |= 0b0011;
  a_.OpFToU(dxbc::Dest::R(system_temp_rov_params_, 0b0011),
            dxbc::Src::V1D(in_reg_ps_position_));
  // Convert the position from pixels to samples.
  // system_temp_rov_params_.x = X sample 0 position
  // system_temp_rov_params_.y = Y sample 0 position
  a_.OpIShL(
      dxbc::Dest::R(system_temp_rov_params_, 0b0011),
      dxbc::Src::R(system_temp_rov_params_),
      LoadSystemConstant(SystemConstants::Index::kSampleCountLog2,
                         offsetof(SystemConstants, sample_count_log2), 0b0100));
  // For cases of both color and depth:
  //   Get 40 x 16 x resolution scale 32bpp half-tile or 40x16 64bpp tile index
  //   to system_temp_rov_params_.zw, and put the sample index within such a
  //   region in system_temp_rov_params_.xy.
  //   Working with 40x16-sample portions for 64bpp and for swapping for depth -
  //   dividing by 40, not by 80.
  // For depth-only:
  //   Same, but for full 80x16 tiles, not 40x16 half-tiles.
  uint32_t tile_width =
      xenos::kEdramTileWidthSamples * draw_resolution_scale_x_;
  uint32_t tile_or_tile_half_width =
      tile_width >> uint32_t(any_color_targets_written);
  uint32_t tile_height =
      xenos::kEdramTileHeightSamples * draw_resolution_scale_y_;
  // system_temp_rov_params_.x = X sample 0 position within the half-tile or
  //                             tile
  // system_temp_rov_params_.y = Y sample 0 position within the (half-)tile
  // system_temp_rov_params_.z = X half-tile or tile position
  // system_temp_rov_params_.w = Y tile position
  a_.OpUDiv(dxbc::Dest::R(system_temp_rov_params_, 0b1100),
            dxbc::Dest::R(system_temp_rov_params_, 0b0011),
            dxbc::Src::R(system_temp_rov_params_, 0b01000100),
            dxbc::Src::LU(tile_or_tile_half_width, tile_height,
                          tile_or_tile_half_width, tile_height));

  // Convert the Y sample 0 position within the half-tile or tile to the dword
  // offset of the row within a 80x16 32bpp tile or a 40x16 64bpp half-tile to
  // system_temp_rov_params_.y.
  // system_temp_rov_params_.x = X sample 0 position within the half-tile or
  //                             tile
  // system_temp_rov_params_.y = Y sample 0 row dword offset within the
  //                             80x16-dword tile
  // system_temp_rov_params_.z = X half-tile position
  // system_temp_rov_params_.w = Y tile position
  a_.OpUMul(dxbc::Dest::Null(), dxbc::Dest::R(system_temp_rov_params_, 0b0010),
            dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
            dxbc::Src::LU(tile_width));

  uint32_t tile_size = tile_width * tile_height;
  uint32_t tile_half_width = tile_width >> 1;
  if (any_color_targets_written) {
    // Depth, 32bpp color, 64bpp color are all needed.

    // X sample 0 position within in the half-tile in system_temp_rov_params_.x,
    // for 64bpp, will be used directly as sample X the within the 80x16-dword
    // region, but for 32bpp color and depth, 40x16 half-tile index within the
    // 80x16 tile - system_temp_rov_params_.z & 1 - will also be taken into
    // account when calculating the X (directly for color, flipped for depth).

    uint32_t rov_address_temp = PushSystemTemp();

    // Multiply the Y tile position by the surface tile pitch in dwords to get
    // the address of the origin of the row of tiles within a 32bpp surface in
    // dwords (later it needs to be multiplied by 2 for 64bpp).
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = Y sample 0 row dword offset within the
    //                             80x16-dword tile
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = Y tile row dword origin in a 32bpp surface
    a_.OpUMul(
        dxbc::Dest::Null(), dxbc::Dest::R(system_temp_rov_params_, 0b1000),
        dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kWWWW),
        LoadSystemConstant(
            SystemConstants::Index::kEdram32bppTilePitchDwordsScaled,
            offsetof(SystemConstants, edram_32bpp_tile_pitch_dwords_scaled),
            dxbc::Src::kXXXX));

    // Get the 32bpp tile X position within the row of tiles to
    // rov_address_temp.x.
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = Y sample 0 row dword offset within the
    //                             80x16-dword tile
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = Y tile row dword origin in a 32bpp surface
    // rov_address_temp.x = X 32bpp tile position
    a_.OpUShR(dxbc::Dest::R(rov_address_temp, 0b0001),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ),
              dxbc::Src::LU(1));
    // Get the dword offset of the beginning of the row of samples within a row
    // of 32bpp 80x16 tiles to rov_address_temp.x.
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = Y sample 0 row dword offset within the
    //                             80x16-dword tile
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = Y tile row dword origin in a 32bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a row of 32bpp tiles
    a_.OpUMAd(dxbc::Dest::R(rov_address_temp, 0b0001),
              dxbc::Src::R(rov_address_temp, dxbc::Src::kXXXX),
              dxbc::Src::LU(tile_size),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY));
    // Get the dword offset of the beginning of the row of samples within a
    // 32bpp surface to rov_address_temp.x.
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = Y sample 0 row dword offset within the
    //                             80x16-dword tile
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = Y tile row dword origin in a 32bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a 32bpp surface
    a_.OpIAdd(dxbc::Dest::R(rov_address_temp, 0b0001),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kWWWW),
              dxbc::Src::R(rov_address_temp, dxbc::Src::kXXXX));

    // Get the dword offset of the beginning of the row of samples within a row
    // of 64bpp 80x16 tiles to system_temp_rov_params_.y (last time the
    // tile-local Y offset is needed).
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = dword offset of the beginning of the row of
    //                             samples within a row of 64bpp tiles
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = Y tile row dword origin in a 32bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a 32bpp surface
    a_.OpUMAd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ),
              dxbc::Src::LU(tile_size),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY));
    // Get the dword offset of the beginning of the row of samples within a
    // 64bpp surface to system_temp_rov_params_.w (last time the Y tile row
    // offset is needed).
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = free
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = dword offset of the beginning of the row of
    //                             samples within a 64bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a 32bpp surface
    a_.OpUMAd(dxbc::Dest::R(system_temp_rov_params_, 0b1000),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kWWWW),
              dxbc::Src::LU(2),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY));

    // Get the final offset of the sample 0 within a 64bpp surface to
    // system_temp_rov_params_.w.
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.z = X half-tile position
    // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a 32bpp surface
    a_.OpUMAd(dxbc::Dest::R(system_temp_rov_params_, 0b1000),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
              dxbc::Src::LU(2),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kWWWW));

    // Get the half-tile index within the tile to system_temp_rov_params_.y
    // (last time the X half-tile position is needed).
    // system_temp_rov_params_.x = X sample 0 position within the half-tile
    // system_temp_rov_params_.y = half-tile index within the tile
    // system_temp_rov_params_.z = free
    // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a 32bpp surface
    a_.OpAnd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ),
             dxbc::Src::LU(1));

    // Get the X position within the 32bpp tile to system_temp_rov_params_.z
    // (last time the X position within the half-tile is needed).
    // system_temp_rov_params_.x = free
    // system_temp_rov_params_.y = half-tile index within the tile
    // system_temp_rov_params_.z = X sample 0 position within the tile
    // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface
    // rov_address_temp.x = dword offset of the beginning of the row of samples
    //                      within a 32bpp surface
    a_.OpUMAd(dxbc::Dest::R(system_temp_rov_params_, 0b0100),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::LU(tile_half_width),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX));
    // Get the final offset of the sample 0 within a 32bpp color surface to
    // system_temp_rov_params_.z (last time the 32bpp row offset is needed).
    // system_temp_rov_params_.y = half-tile index within the tile
    // system_temp_rov_params_.z = dword sample 0 offset within a 32bpp surface
    // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface
    // rov_address_temp.x = free
    a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0100),
              dxbc::Src::R(rov_address_temp, dxbc::Src::kXXXX),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ));

    // Flip the 40x16 half-tiles for depth / stencil as opposed to 32bpp color -
    // get the dword offset to add for flipping to system_temp_rov_params_.y.
    // system_temp_rov_params_.y = depth half-tile flipping offset
    // system_temp_rov_params_.z = dword sample 0 offset within a 32bpp surface
    // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface
    a_.OpMovC(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::LI(-int32_t(tile_half_width)),
              dxbc::Src::LI(int32_t(tile_half_width)));
    // Flip the 40x16 half-tiles for depth / stencil as opposed to 32bpp color -
    // get the final offset of the sample 0 within a 32bpp depth / stencil
    // surface to system_temp_rov_params_.y.
    // system_temp_rov_params_.y = dword sample 0 offset within depth / stencil
    // system_temp_rov_params_.z = dword sample 0 offset within a 32bpp surface
    // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface
    a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY));

    // Release rov_address_temp.
    PopSystemTemp();
  } else {
    // Simpler logic for depth-only, not involving half-tile indices (flipping
    // half-tiles via comparison).

    // Get the dword offset of the beginning of the row of samples within a row
    // of 32bpp 80x16 tiles to system_temp_rov_params_.z (last time the X tile
    // position is needed).
    // system_temp_rov_params_.x = X sample 0 position within the tile
    // system_temp_rov_params_.y = Y sample 0 row dword offset within the
    //                             80x16-dword tile
    // system_temp_rov_params_.z = dword offset of the beginning of the row of
    //                             samples within a row of 32bpp tiles
    // system_temp_rov_params_.w = Y tile position
    a_.OpUMAd(dxbc::Dest::R(system_temp_rov_params_, 0b0100),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ),
              dxbc::Src::LU(tile_size),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY));
    // Get the dword offset of the beginning of the row of samples within a
    // 32bpp surface to system_temp_rov_params_.y (last time anything Y-related
    // is needed, as well as the sample row offset within the tile row).
    // system_temp_rov_params_.x = X sample 0 position within the tile
    // system_temp_rov_params_.y = dword offset of the beginning of the row of
    //                             samples within a 32bpp surface
    // system_temp_rov_params_.z = free
    // system_temp_rov_params_.w = free
    a_.OpUMAd(
        dxbc::Dest::R(system_temp_rov_params_, 0b0010),
        dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kWWWW),
        LoadSystemConstant(
            SystemConstants::Index::kEdram32bppTilePitchDwordsScaled,
            offsetof(SystemConstants, edram_32bpp_tile_pitch_dwords_scaled),
            dxbc::Src::kXXXX),
        dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ));
    // Add the tile-local X to the depth offset in system_temp_rov_params_.y.
    // system_temp_rov_params_.x = X sample 0 position within the tile
    // system_temp_rov_params_.y = dword sample 0 offset within a 32bpp surface
    a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX));
    // Flip the 40x16 half-tiles for depth / stencil as opposed to 32bpp color -
    // check in which half-tile the pixel is in to system_temp_rov_params_.x.
    // system_temp_rov_params_.x = free
    // system_temp_rov_params_.y = dword sample 0 offset within a 32bpp surface
    // system_temp_rov_params_.z = 0xFFFFFFFF if in the right half-tile, 0
    //                             otherwise
    a_.OpUGE(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::LU(tile_half_width));
    // Flip the 40x16 half-tiles for depth / stencil as opposed to 32bpp color -
    // get the dword offset to add for flipping to system_temp_rov_params_.x.
    // system_temp_rov_params_.x = depth half-tile flipping offset
    // system_temp_rov_params_.y = dword sample 0 offset within a 32bpp surface
    a_.OpMovC(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
              dxbc::Src::LI(-int32_t(tile_half_width)),
              dxbc::Src::LI(int32_t(tile_half_width)));
    // Flip the 40x16 half-tiles for depth / stencil as opposed to 32bpp color -
    // get the final offset of the sample 0 within a 32bpp depth / stencil
    // surface to system_temp_rov_params_.y.
    // system_temp_rov_params_.x = free
    // system_temp_rov_params_.y = dword sample 0 offset within depth / stencil
    a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX));
  }

  // Add the EDRAM base for depth/stencil.
  // system_temp_rov_params_.y = non-wrapped EDRAM depth / stencil address
  // system_temp_rov_params_.z = dword sample 0 offset within a 32bpp surface if
  //                             needed
  // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface if
  //                             needed
  a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
            dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
            LoadSystemConstant(
                SystemConstants::Index::kEdramDepthBaseDwordsScaled,
                offsetof(SystemConstants, edram_depth_base_dwords_scaled),
                dxbc::Src::kXXXX));
  // Wrap EDRAM addressing for depth/stencil.
  // system_temp_rov_params_.y = EDRAM depth / stencil address
  // system_temp_rov_params_.z = dword sample 0 offset within a 32bpp surface if
  //                             needed
  // system_temp_rov_params_.w = dword sample 0 offset within a 64bpp surface if
  //                             needed
  a_.OpUDiv(dxbc::Dest::Null(), dxbc::Dest::R(system_temp_rov_params_, 0b0010),
            dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
            dxbc::Src::LU(tile_size * xenos::kEdramTileCount));

  // ***************************************************************************
  // Sample coverage to system_temp_rov_params_.x.
  // ***************************************************************************

  // Using ForcedSampleCount of 4 (2 is not supported on Nvidia), so for 2x
  // MSAA, handling samples 0 and 3 (upper-left and lower-right) as 0 and 1.

  // Check if 4x MSAA is enabled.
  a_.OpIf(true, LoadSystemConstant(SystemConstants::Index::kSampleCountLog2,
                                   offsetof(SystemConstants, sample_count_log2),
                                   dxbc::Src::kXXXX));
  {
    // Copy the 4x AA coverage to system_temp_rov_params_.x, making top-right
    // the sample [2] and bottom-left the sample [1] (the opposite of Direct3D
    // 12), because on the Xbox 360, 2x MSAA doubles the storage height, 4x MSAA
    // doubles the storage width.
    // Flip samples in bits 0:1 to bits 29:30.
    a_.OpBFRev(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
               dxbc::Src::VCoverage());
    a_.OpUShR(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
              dxbc::Src::LU(29));
    a_.OpBFI(dxbc::Dest::R(system_temp_rov_params_, 0b0001), dxbc::Src::LU(2),
             dxbc::Src::LU(1),
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::VCoverage());
  }
  // Handle 1 or 2 samples.
  a_.OpElse();
  {
    // Extract sample 3 coverage, which will be used as sample 1.
    a_.OpUBFE(dxbc::Dest::R(system_temp_rov_params_, 0b0001), dxbc::Src::LU(1),
              dxbc::Src::LU(3), dxbc::Src::VCoverage());
    // Combine coverage of samples 0 (in bit 0 of vCoverage) and 3 (in bit 0 of
    // system_temp_rov_params_.x).
    a_.OpBFI(dxbc::Dest::R(system_temp_rov_params_, 0b0001), dxbc::Src::LU(31),
             dxbc::Src::LU(1),
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::VCoverage());
  }
  // Close the 4x MSAA conditional.
  a_.OpEndIf();
}

void DxbcShaderTranslator::ROV_DepthStencilTest() {
  uint32_t temp = PushSystemTemp();
  dxbc::Dest temp_x_dest(dxbc::Dest::R(temp, 0b0001));
  dxbc::Src temp_x_src(dxbc::Src::R(temp, dxbc::Src::kXXXX));
  dxbc::Dest temp_y_dest(dxbc::Dest::R(temp, 0b0010));
  dxbc::Src temp_y_src(dxbc::Src::R(temp, dxbc::Src::kYYYY));
  dxbc::Dest temp_z_dest(dxbc::Dest::R(temp, 0b0100));
  dxbc::Src temp_z_src(dxbc::Src::R(temp, dxbc::Src::kZZZZ));
  dxbc::Dest temp_w_dest(dxbc::Dest::R(temp, 0b1000));
  dxbc::Src temp_w_src(dxbc::Src::R(temp, dxbc::Src::kWWWW));

  // Check whether depth/stencil is enabled.
  // temp.x = kSysFlag_ROVDepthStencil
  a_.OpAnd(temp_x_dest, LoadFlagsSystemConstant(),
           dxbc::Src::LU(kSysFlag_ROVDepthStencil));
  // Open the depth/stencil enabled conditional.
  // temp.x = free
  a_.OpIf(true, temp_x_src);

  bool shader_writes_depth = current_shader().writes_depth();
  bool depth_stencil_early = ROV_IsDepthStencilEarly();

  dxbc::Src z_ddx_src(dxbc::Src::LF(0.0f)), z_ddy_src(dxbc::Src::LF(0.0f));

  if (shader_writes_depth) {
    // Convert the shader-generated depth to 24-bit, using temp.x as
    // temporary. oDepth is already written by StoreResult with saturation,
    // no need to clamp here. Adreno 200 doesn't have PA_SC_VPORT_ZMIN/ZMAX,
    // so likely there's no need to clamp to the viewport depth bounds.
    ROV_DepthTo24Bit(system_temp_depth_stencil_, 0, system_temp_depth_stencil_,
                     0, temp, 0);
  } else {
    dxbc::Src in_position_z(
        dxbc::Src::V1D(in_reg_ps_position_, dxbc::Src::kZZZZ));
    // Get the derivatives of the screen-space (but not clamped to the viewport
    // depth bounds yet - this happens after the pixel shader in Direct3D 11+;
    // also linear within the triangle - thus constant derivatives along the
    // triangle) Z for calculating per-sample depth values and the slope-scaled
    // polygon offset.
    // We're using derivatives instead of eval_sample_index for various reasons:
    // - eval_sample_index doesn't work with SV_Position - need to use an
    //   additional interpolant.
    // - On AMD, eval_sample_index is actually implemented via calculation and
    //   scaling of derivatives of barycentric coordinates, therefore there's no
    //   advantage of using it there.
    // - eval_sample_index is (inconsistently, but often) one of the sources of
    //   the infamous AMD shader compiler crashes when ROV is used in Xenia, in
    //   addition to shader compiler crashes on WARP.
    if (depth_stencil_early) {
      z_ddx_src = dxbc::Src::R(temp, dxbc::Src::kXXXX);
      z_ddy_src = dxbc::Src::R(temp, dxbc::Src::kYYYY);
      // temp.x = ddx(z)
      // temp.y = ddy(z)
      in_position_used_ |= 0b0100;
      a_.OpDerivRTXCoarse(temp_x_dest, in_position_z);
      a_.OpDerivRTYCoarse(temp_y_dest, in_position_z);
    } else {
      // For late depth / stencil testing, derivatives are calculated in the
      // beginning of the shader before any return statement is possibly
      // reached, and written to system_temp_depth_stencil_.xy.
      assert_true(system_temp_depth_stencil_ != UINT32_MAX);
      z_ddx_src = dxbc::Src::R(system_temp_depth_stencil_, dxbc::Src::kXXXX);
      z_ddy_src = dxbc::Src::R(system_temp_depth_stencil_, dxbc::Src::kYYYY);
    }
    // Get the maximum depth slope for polygon offset.
    // https://docs.microsoft.com/en-us/windows/desktop/direct3d9/depth-bias
    // temp.x if early = ddx(z)
    // temp.y if early = ddy(z)
    // temp.z = max(|ddx(z)|, |ddy(z)|)
    a_.OpMax(temp_z_dest, z_ddx_src.Abs(), z_ddy_src.Abs());
    // Calculate the depth bias for the needed faceness.
    in_front_face_used_ = true;
    a_.OpIf(true, dxbc::Src::V1D(in_reg_ps_front_face_sample_index_,
                                 dxbc::Src::kXXXX));
    // temp.x if early = ddx(z)
    // temp.y if early = ddy(z)
    // temp.z = front face polygon offset
    // temp.w = free
    a_.OpMAd(
        temp_z_dest, temp_z_src,
        LoadSystemConstant(SystemConstants::Index::kEdramPolyOffsetFront,
                           offsetof(SystemConstants, edram_poly_offset_front),
                           dxbc::Src::kXXXX),
        LoadSystemConstant(SystemConstants::Index::kEdramPolyOffsetFront,
                           offsetof(SystemConstants, edram_poly_offset_front),
                           dxbc::Src::kYYYY));
    a_.OpElse();
    // temp.x if early = ddx(z)
    // temp.y if early = ddy(z)
    // temp.z = back face polygon offset
    // temp.w = free
    a_.OpMAd(
        temp_z_dest, temp_z_src,
        LoadSystemConstant(SystemConstants::Index::kEdramPolyOffsetBack,
                           offsetof(SystemConstants, edram_poly_offset_back),
                           dxbc::Src::kXXXX),
        LoadSystemConstant(SystemConstants::Index::kEdramPolyOffsetBack,
                           offsetof(SystemConstants, edram_poly_offset_back),
                           dxbc::Src::kYYYY));
    a_.OpEndIf();
    // Apply the post-clip and post-viewport polygon offset to the fragment's
    // depth. Not clamping yet as this is at the center, which is not
    // necessarily covered and not necessarily inside the bounds - derivatives
    // scaled by sample positions will be added to this value, and it must be
    // linear.
    // temp.x if early = ddx(z)
    // temp.y if early = ddy(z)
    // temp.z = biased depth in the center
    in_position_used_ |= 0b0100;
    a_.OpAdd(temp_z_dest, temp_z_src, in_position_z);
  }

  for (uint32_t i = 0; i < 4; ++i) {
    // With early depth/stencil, depth/stencil writing may be deferred to the
    // end of the shader to prevent writing in case something (like alpha test,
    // which is dynamic GPU state) discards the pixel. So, write directly to the
    // persistent register, system_temp_depth_stencil_, instead of a local
    // temporary register.
    dxbc::Dest sample_depth_stencil_dest(
        depth_stencil_early ? dxbc::Dest::R(system_temp_depth_stencil_, 1 << i)
                            : temp_w_dest);
    dxbc::Src sample_depth_stencil_src(
        depth_stencil_early ? dxbc::Src::R(system_temp_depth_stencil_).Select(i)
                            : temp_w_src);

    // Get if the current sample is covered.
    // temp.x if no oDepth and early = ddx(z)
    // temp.y if no oDepth and early = ddy(z)
    // temp.z if no oDepth = biased depth in the center
    // temp.w = coverage of the current sample
    a_.OpAnd(temp_w_dest,
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::LU(1 << i));
    // Check if the current sample is covered.
    // temp.x if no oDepth and early = ddx(z)
    // temp.y if no oDepth and early = ddy(z)
    // temp.z if no oDepth = biased depth in the center
    // temp.w = free
    a_.OpIf(true, temp_w_src);

    uint32_t sample_temp = PushSystemTemp();
    dxbc::Dest sample_temp_x_dest(dxbc::Dest::R(sample_temp, 0b0001));
    dxbc::Src sample_temp_x_src(dxbc::Src::R(sample_temp, dxbc::Src::kXXXX));
    dxbc::Dest sample_temp_y_dest(dxbc::Dest::R(sample_temp, 0b0010));
    dxbc::Src sample_temp_y_src(dxbc::Src::R(sample_temp, dxbc::Src::kYYYY));
    dxbc::Dest sample_temp_z_dest(dxbc::Dest::R(sample_temp, 0b0100));
    dxbc::Src sample_temp_z_src(dxbc::Src::R(sample_temp, dxbc::Src::kZZZZ));
    dxbc::Dest sample_temp_w_dest(dxbc::Dest::R(sample_temp, 0b1000));
    dxbc::Src sample_temp_w_src(dxbc::Src::R(sample_temp, dxbc::Src::kWWWW));

    if (shader_writes_depth) {
      // Copy the 24-bit depth common to all samples to sample_depth_stencil.
      // temp.w = shader-generated 24-bit depth
      assert_false(depth_stencil_early);
      a_.OpMov(sample_depth_stencil_dest,
               dxbc::Src::R(system_temp_depth_stencil_, dxbc::Src::kXXXX));
    } else {
      // Adreno 200 doesn't have PA_SC_VPORT_ZMIN/ZMAX, so likely there's no
      // need to clamp to the viewport depth bounds, just to 0...1 - thus only
      // saturating in the end of the per-sample depth calculation.
      switch (i) {
        case 0:
          // First sample - off-center for MSAA, in the center without it.
          // Using ForcedSampleCount 4 for both 2x and 4x MSAA because
          // ForcedSampleCount 2 is not supported on Nvidia, thus the position
          // of the top-left sample (0 in Xenia) is always that of the top-left
          // sample of host 4x MSAA.
          // Calculate the depth in the sample 0 for 2x or 4x MSAA.
          // temp.x if early = ddx(z)
          // temp.y if early = ddy(z)
          // temp.z = biased depth in the center
          // temp.w if late = unsaturated sample 0 depth at 4x MSAA
          a_.OpMAd(
              sample_depth_stencil_dest, z_ddx_src,
              dxbc::Src::LF(draw_util::kD3D10StandardSamplePositions4x[0][0] *
                            (1.0f / 16.0f)),
              temp_z_src);
          a_.OpMAd(
              sample_depth_stencil_dest, z_ddy_src,
              dxbc::Src::LF(draw_util::kD3D10StandardSamplePositions4x[0][1] *
                            (1.0f / 16.0f)),
              sample_depth_stencil_src);
          // Choose between the sample and the center depth depending on whether
          // at least 2x MSAA is enabled and saturate.
          // temp.x if early = ddx(z)
          // temp.y if early = ddy(z)
          // temp.z = biased depth in the center
          // temp.w if late = sample 0 depth
          a_.OpMovC(
              sample_depth_stencil_dest,
              LoadSystemConstant(SystemConstants::Index::kSampleCountLog2,
                                 offsetof(SystemConstants, sample_count_log2),
                                 dxbc::Src::kYYYY),
              sample_depth_stencil_src, temp_z_src, true);
          break;
        case 1:
          // - 2x MSAA: Bottom sample -> bottom-right (3) with Direct3D 11's
          //   ForcedSampleCount 4.
          // - 4x MSAA: Bottom-left Xenia sample -> Direct3D 11 sample 2.
          // Check if 4x MSAA is used.
          a_.OpIf(true, LoadSystemConstant(
                            SystemConstants::Index::kSampleCountLog2,
                            offsetof(SystemConstants, sample_count_log2),
                            dxbc::Src::kXXXX));
          // 4x MSAA.
          // temp.x if early = ddx(z)
          // temp.y if early = ddy(z)
          // temp.z = biased depth in the center
          // temp.w if late = saturated sample 1 depth at 4x MSAA
          a_.OpMAd(
              sample_depth_stencil_dest, z_ddx_src,
              dxbc::Src::LF(draw_util::kD3D10StandardSamplePositions4x[2][0] *
                            (1.0f / 16.0f)),
              temp_z_src);
          a_.OpMAd(
              sample_depth_stencil_dest, z_ddy_src,
              dxbc::Src::LF(draw_util::kD3D10StandardSamplePositions4x[2][1] *
                            (1.0f / 16.0f)),
              sample_depth_stencil_src, true);
          a_.OpElse();
          // 2x MSAA as ForcedSampleCount 4 on the host.
          // temp.x if early = ddx(z)
          // temp.y if early = ddy(z)
          // temp.z = biased depth in the center
          // temp.w if late = saturated sample 1 depth at 2x MSAA
          a_.OpMAd(
              sample_depth_stencil_dest, z_ddx_src,
              dxbc::Src::LF(draw_util::kD3D10StandardSamplePositions4x[3][0] *
                            (1.0f / 16.0f)),
              temp_z_src);
          a_.OpMAd(
              sample_depth_stencil_dest, z_ddy_src,
              dxbc::Src::LF(draw_util::kD3D10StandardSamplePositions4x[3][1] *
                            (1.0f / 16.0f)),
              sample_depth_stencil_src, true);
          a_.OpEndIf();
          break;
        default: {
          // Xenia samples 2 and 3 (top-right and bottom-right) -> Direct3D 11
          // samples 1 and 3.
          // temp.x if early = ddx(z)
          // temp.y if early = ddy(z)
          // temp.z = biased depth in the center
          // temp.w if late = saturated sample 2 or 3 depth
          const int8_t* sample_position =
              draw_util::kD3D10StandardSamplePositions4x[i ^
                                                         (((i & 1) ^ (i >> 1)) *
                                                          0b11)];
          a_.OpMAd(sample_depth_stencil_dest, z_ddx_src,
                   dxbc::Src::LF(sample_position[0] * (1.0f / 16.0f)),
                   temp_z_src);
          a_.OpMAd(sample_depth_stencil_dest, z_ddy_src,
                   dxbc::Src::LF(sample_position[1] * (1.0f / 16.0f)),
                   sample_depth_stencil_src, true);
        } break;
      }
      // Convert the sample's depth to 24-bit, using sample_temp.x as a
      // temporary.
      // temp.x if early = ddx(z)
      // temp.y if early = ddy(z)
      // temp.z = biased depth in the center
      // temp.w if late = sample's 24-bit Z
      ROV_DepthTo24Bit(sample_depth_stencil_src.index_1d_.index_,
                       sample_depth_stencil_src.swizzle_ & 3,
                       sample_depth_stencil_src.index_1d_.index_,
                       sample_depth_stencil_src.swizzle_ & 3, sample_temp, 0);
    }

    // Load the old depth/stencil value.
    // sample_temp.x = old depth/stencil
    if (uav_index_edram_ == kBindingIndexUnallocated) {
      uav_index_edram_ = uav_count_++;
    }
    a_.OpLdUAVTyped(
        sample_temp_x_dest,
        dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY), 1,
        dxbc::Src::U(uav_index_edram_, uint32_t(UAVRegister::kEdram),
                     dxbc::Src::kXXXX));

    // Depth test.

    // Extract the old depth part to sample_depth_stencil.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    a_.OpUShR(sample_temp_y_dest, sample_temp_x_src, dxbc::Src::LU(8));
    // Get the difference between the new and the old depth, > 0 - greater,
    // == 0 - equal, < 0 - less.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    // sample_temp.z = depth difference
    a_.OpIAdd(sample_temp_z_dest, sample_depth_stencil_src, -sample_temp_y_src);
    // Check if the depth is "less" or "greater or equal".
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    // sample_temp.z = depth difference
    // sample_temp.w = depth difference less than 0
    a_.OpILT(sample_temp_w_dest, sample_temp_z_src, dxbc::Src::LI(0));
    // Choose the passed depth function bits for "less" or for "greater".
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    // sample_temp.z = depth difference
    // sample_temp.w = depth function passed bits for "less" or "greater"
    a_.OpMovC(sample_temp_w_dest, sample_temp_w_src,
              dxbc::Src::LU(kSysFlag_ROVDepthPassIfLess),
              dxbc::Src::LU(kSysFlag_ROVDepthPassIfGreater));
    // Do the "equal" testing.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    // sample_temp.z = depth function passed bits
    // sample_temp.w = free
    a_.OpMovC(sample_temp_z_dest, sample_temp_z_src, sample_temp_w_src,
              dxbc::Src::LU(kSysFlag_ROVDepthPassIfEqual));
    // Mask the resulting bits with the ones that should pass.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    // sample_temp.z = masked depth function passed bits
    a_.OpAnd(sample_temp_z_dest, sample_temp_z_src, LoadFlagsSystemConstant());
    // Check if depth test has passed.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = old depth
    // sample_temp.z = free
    a_.OpIf(true, sample_temp_z_src);
    {
      // Extract the depth write flag.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = old depth
      // sample_temp.z = depth write mask
      a_.OpAnd(sample_temp_z_dest, LoadFlagsSystemConstant(),
               dxbc::Src::LU(kSysFlag_ROVDepthWrite));
      // If depth writing is disabled, don't change the depth.
      // temp.x if no oDepth and early = ddx(z)
      // temp.y if no oDepth and early = ddy(z)
      // temp.z if no oDepth = biased depth in the center
      // temp.w if late = resulting sample depth after the depth test
      // sample_temp.x = old depth/stencil
      // sample_temp.y = free
      // sample_temp.z = free
      a_.OpMovC(sample_depth_stencil_dest, sample_temp_z_src,
                sample_depth_stencil_src, sample_temp_y_src);
    }
    // Depth test has failed.
    a_.OpElse();
    {
      // Exclude the bit from the covered sample mask.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = old depth
      a_.OpAnd(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
               dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
               dxbc::Src::LU(~uint32_t(1 << i)));
    }
    a_.OpEndIf();
    // Create packed depth/stencil, with the stencil value unchanged at this
    // point.
    // temp.x if no oDepth and early = ddx(z)
    // temp.y if no oDepth and early = ddy(z)
    // temp.z if no oDepth = biased depth in the center
    // temp.w if late = resulting sample depth, current resulting stencil
    // sample_temp.x = old depth/stencil
    a_.OpBFI(sample_depth_stencil_dest, dxbc::Src::LU(24), dxbc::Src::LU(8),
             sample_depth_stencil_src, sample_temp_x_src);

    // Stencil test.

    // Extract the stencil test bit.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = stencil test enabled
    a_.OpAnd(sample_temp_y_dest, LoadFlagsSystemConstant(),
             dxbc::Src::LU(kSysFlag_ROVStencilTest));
    // Check if stencil test is enabled.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = free
    a_.OpIf(true, sample_temp_y_src);
    {
      // Check the current face to get the reference and apply the read mask.
      in_front_face_used_ = true;
      a_.OpIf(true, dxbc::Src::V1D(in_reg_ps_front_face_sample_index_,
                                   dxbc::Src::kXXXX));
      for (uint32_t j = 0; j < 2; ++j) {
        if (j) {
          // Go to the back face.
          a_.OpElse();
        }
        dxbc::Src stencil_read_mask_src(LoadSystemConstant(
            SystemConstants::Index::kEdramStencil,
            j ? offsetof(SystemConstants, edram_stencil_back_read_mask)
              : offsetof(SystemConstants, edram_stencil_front_read_mask),
            dxbc::Src::kXXXX));
        // Read-mask the stencil reference.
        // sample_temp.x = old depth/stencil
        // sample_temp.y = read-masked stencil reference
        a_.OpAnd(
            sample_temp_y_dest,
            LoadSystemConstant(
                SystemConstants::Index::kEdramStencil,
                j ? offsetof(SystemConstants, edram_stencil_back_reference)
                  : offsetof(SystemConstants, edram_stencil_front_reference),
                dxbc::Src::kXXXX),
            stencil_read_mask_src);
        // Read-mask the old stencil value (also dropping the depth bits).
        // sample_temp.x = old depth/stencil
        // sample_temp.y = read-masked stencil reference
        // sample_temp.z = read-masked old stencil
        a_.OpAnd(sample_temp_z_dest, sample_temp_x_src, stencil_read_mask_src);
      }
      // Close the face check.
      a_.OpEndIf();
      // Get the difference between the stencil reference and the old stencil,
      // > 0 - greater, == 0 - equal, < 0 - less.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = stencil difference
      // sample_temp.z = free
      a_.OpIAdd(sample_temp_y_dest, sample_temp_y_src, -sample_temp_z_src);
      // Check if the stencil is "less" or "greater or equal".
      // sample_temp.x = old depth/stencil
      // sample_temp.y = stencil difference
      // sample_temp.z = stencil difference less than 0
      a_.OpILT(sample_temp_z_dest, sample_temp_y_src, dxbc::Src::LI(0));
      // Choose the passed depth function bits for "less" or for "greater".
      // sample_temp.x = old depth/stencil
      // sample_temp.y = stencil difference
      // sample_temp.z = stencil function passed bits for "less" or "greater"
      a_.OpMovC(sample_temp_z_dest, sample_temp_z_src,
                dxbc::Src::LU(uint32_t(xenos::CompareFunction::kLess)),
                dxbc::Src::LU(uint32_t(xenos::CompareFunction::kGreater)));
      // Do the "equal" testing.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = stencil function passed bits
      // sample_temp.z = free
      a_.OpMovC(sample_temp_y_dest, sample_temp_y_src, sample_temp_z_src,
                dxbc::Src::LU(uint32_t(xenos::CompareFunction::kEqual)));
      // Get the comparison function and the operations for the current face.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = stencil function passed bits
      // sample_temp.z = stencil function and operations
      in_front_face_used_ = true;
      a_.OpMovC(
          sample_temp_z_dest,
          dxbc::Src::V1D(in_reg_ps_front_face_sample_index_, dxbc::Src::kXXXX),
          LoadSystemConstant(
              SystemConstants::Index::kEdramStencil,
              offsetof(SystemConstants, edram_stencil_front_func_ops),
              dxbc::Src::kXXXX),
          LoadSystemConstant(
              SystemConstants::Index::kEdramStencil,
              offsetof(SystemConstants, edram_stencil_back_func_ops),
              dxbc::Src::kXXXX));
      // Mask the resulting bits with the ones that should pass (the comparison
      // function is in the low 3 bits of the constant, and only ANDing 3-bit
      // values with it, so safe not to UBFE the function).
      // sample_temp.x = old depth/stencil
      // sample_temp.y = stencil test result
      // sample_temp.z = stencil function and operations
      a_.OpAnd(sample_temp_y_dest, sample_temp_y_src, sample_temp_z_src);
      // Handle passing and failure of the stencil test, to choose the operation
      // and to discard the sample.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = free
      // sample_temp.z = stencil function and operations
      a_.OpIf(true, sample_temp_y_src);
      {
        // Check if depth test has passed for this sample (the sample will only
        // be processed if it's covered, so the only thing that could unset the
        // bit at this point that matters is the depth test).
        // sample_temp.x = old depth/stencil
        // sample_temp.y = depth test result
        // sample_temp.z = stencil function and operations
        a_.OpAnd(sample_temp_y_dest,
                 dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
                 dxbc::Src::LU(1 << i));
        // Choose the bit offset of the stencil operation.
        // sample_temp.x = old depth/stencil
        // sample_temp.y = sample operation offset
        // sample_temp.z = stencil function and operations
        a_.OpMovC(sample_temp_y_dest, sample_temp_y_src, dxbc::Src::LU(6),
                  dxbc::Src::LU(9));
        // Extract the stencil operation.
        // sample_temp.x = old depth/stencil
        // sample_temp.y = stencil operation
        // sample_temp.z = free
        a_.OpUBFE(sample_temp_y_dest, dxbc::Src::LU(3), sample_temp_y_src,
                  sample_temp_z_src);
      }
      // Stencil test has failed.
      a_.OpElse();
      {
        // Extract the stencil fail operation.
        // sample_temp.x = old depth/stencil
        // sample_temp.y = stencil operation
        // sample_temp.z = free
        a_.OpUBFE(sample_temp_y_dest, dxbc::Src::LU(3), dxbc::Src::LU(3),
                  sample_temp_z_src);
        // Exclude the bit from the covered sample mask.
        // sample_temp.x = old depth/stencil
        // sample_temp.y = stencil operation
        a_.OpAnd(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
                 dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
                 dxbc::Src::LU(~uint32_t(1 << i)));
      }
      // Close the stencil pass check.
      a_.OpEndIf();

      // Open the stencil operation switch for writing the new stencil (not
      // caring about bits 8:31).
      // sample_temp.x = old depth/stencil
      // sample_temp.y = will contain unmasked new stencil in 0:7 and junk above
      a_.OpSwitch(sample_temp_y_src);
      {
        // Zero.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kZero)));
        a_.OpMov(sample_temp_y_dest, dxbc::Src::LU(0));
        a_.OpBreak();
        // Replace.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kReplace)));
        in_front_face_used_ = true;
        a_.OpMovC(sample_temp_y_dest,
                  dxbc::Src::V1D(in_reg_ps_front_face_sample_index_,
                                 dxbc::Src::kXXXX),
                  LoadSystemConstant(
                      SystemConstants::Index::kEdramStencil,
                      offsetof(SystemConstants, edram_stencil_front_reference),
                      dxbc::Src::kXXXX),
                  LoadSystemConstant(
                      SystemConstants::Index::kEdramStencil,
                      offsetof(SystemConstants, edram_stencil_back_reference),
                      dxbc::Src::kXXXX));
        a_.OpBreak();
        // Increment and clamp.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kIncrementClamp)));
        {
          // Clear the upper bits for saturation.
          a_.OpAnd(sample_temp_y_dest, sample_temp_x_src,
                   dxbc::Src::LU(UINT8_MAX));
          // Increment.
          a_.OpIAdd(sample_temp_y_dest, sample_temp_y_src, dxbc::Src::LI(1));
          // Clamp.
          a_.OpIMin(sample_temp_y_dest, sample_temp_y_src,
                    dxbc::Src::LI(UINT8_MAX));
        }
        a_.OpBreak();
        // Decrement and clamp.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kDecrementClamp)));
        {
          // Clear the upper bits for saturation.
          a_.OpAnd(sample_temp_y_dest, sample_temp_x_src,
                   dxbc::Src::LU(UINT8_MAX));
          // Increment.
          a_.OpIAdd(sample_temp_y_dest, sample_temp_y_src, dxbc::Src::LI(-1));
          // Clamp.
          a_.OpIMax(sample_temp_y_dest, sample_temp_y_src, dxbc::Src::LI(0));
        }
        a_.OpBreak();
        // Invert.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kInvert)));
        a_.OpNot(sample_temp_y_dest, sample_temp_x_src);
        a_.OpBreak();
        // Increment and wrap.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kIncrementWrap)));
        a_.OpIAdd(sample_temp_y_dest, sample_temp_x_src, dxbc::Src::LI(1));
        a_.OpBreak();
        // Decrement and wrap.
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::StencilOp::kDecrementWrap)));
        a_.OpIAdd(sample_temp_y_dest, sample_temp_x_src, dxbc::Src::LI(-1));
        a_.OpBreak();
        // Keep.
        a_.OpDefault();
        a_.OpMov(sample_temp_y_dest, sample_temp_x_src);
        a_.OpBreak();
      }
      // Close the new stencil switch.
      a_.OpEndSwitch();

      // Select the stencil write mask for the face.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = unmasked new stencil in 0:7 and junk above
      // sample_temp.z = stencil write mask
      in_front_face_used_ = true;
      a_.OpMovC(
          sample_temp_z_dest,
          dxbc::Src::V1D(in_reg_ps_front_face_sample_index_, dxbc::Src::kXXXX),
          LoadSystemConstant(
              SystemConstants::Index::kEdramStencil,
              offsetof(SystemConstants, edram_stencil_front_write_mask),
              dxbc::Src::kXXXX),
          LoadSystemConstant(
              SystemConstants::Index::kEdramStencil,
              offsetof(SystemConstants, edram_stencil_back_write_mask),
              dxbc::Src::kXXXX));
      // Apply the write mask to the new stencil, also dropping the upper 24
      // bits.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = masked new stencil
      // sample_temp.z = stencil write mask
      a_.OpAnd(sample_temp_y_dest, sample_temp_y_src, sample_temp_z_src);
      // Invert the write mask for keeping the old stencil and the depth bits.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = masked new stencil
      // sample_temp.z = inverted stencil write mask
      a_.OpNot(sample_temp_z_dest, sample_temp_z_src);
      // Remove the bits that will be replaced from the combined depth/stencil
      // before inserting their new values.
      // sample_temp.x = old depth/stencil
      // sample_temp.y = masked new stencil
      // sample_temp.z = free
      // temp.x if no oDepth and early = ddx(z)
      // temp.y if no oDepth and early = ddy(z)
      // temp.z if no oDepth = biased depth in the center
      // temp.w if late = resulting sample depth, inverse-write-masked old
      //                  stencil
      a_.OpAnd(sample_depth_stencil_dest, sample_depth_stencil_src,
               sample_temp_z_src);
      // Merge the old and the new stencil.
      // temp.x if no oDepth and early = ddx(z)
      // temp.y if no oDepth and early = ddy(z)
      // temp.z if no oDepth = biased depth in the center
      // temp.w if late = resulting sample depth/stencil
      // sample_temp.x = old depth/stencil
      // sample_temp.y = free
      a_.OpOr(sample_depth_stencil_dest, sample_depth_stencil_src,
              sample_temp_y_src);
    }
    // Close the stencil test check.
    a_.OpEndIf();

    // Check if the depth/stencil has failed not to modify the depth if it has.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = whether depth/stencil has passed for this sample
    a_.OpAnd(sample_temp_y_dest,
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::LU(1 << i));
    // If the depth/stencil test has failed, don't change the depth.
    // sample_temp.x = old depth/stencil
    // sample_temp.y = free
    a_.OpIf(false, sample_temp_y_src);
    {
      // Copy the new stencil over the old depth.
      // temp.x if no oDepth and early = ddx(z)
      // temp.y if no oDepth and early = ddy(z)
      // temp.z if no oDepth = biased depth in the center
      // temp.w if late = resulting sample depth/stencil
      a_.OpBFI(sample_depth_stencil_dest, dxbc::Src::LU(8), dxbc::Src::LU(0),
               sample_depth_stencil_src, sample_temp_x_src);
    }
    // Close the depth/stencil passing check.
    a_.OpEndIf();
    // Check if the new depth/stencil is different, and thus needs to be
    // written.
    // sample_temp.x = old depth/stencil
    a_.OpINE(sample_temp_x_dest, sample_depth_stencil_src, sample_temp_x_src);
    if (depth_stencil_early &&
        !current_shader().implicit_early_z_write_allowed()) {
      // Set the sample bit in bits 4:7 of system_temp_rov_params_.x - always
      // need to write late in this shader, as it may do something like
      // explicitly killing pixels.
      a_.OpBFI(dxbc::Dest::R(system_temp_rov_params_, 0b0001), dxbc::Src::LU(1),
               dxbc::Src::LU(4 + i), sample_temp_x_src,
               dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX));
    } else {
      // Check if need to write.
      // sample_temp.x = free
      a_.OpIf(true, sample_temp_x_src);
      {
        if (depth_stencil_early) {
          // Get if early depth/stencil write is enabled.
          // sample_temp.x = whether early depth/stencil write is enabled
          a_.OpAnd(sample_temp_x_dest, LoadFlagsSystemConstant(),
                   dxbc::Src::LU(kSysFlag_ROVDepthStencilEarlyWrite));
          // Check if need to write early.
          // sample_temp.x = free
          a_.OpIf(true, sample_temp_x_src);
        }
        // Write the new depth/stencil.
        if (uav_index_edram_ == kBindingIndexUnallocated) {
          uav_index_edram_ = uav_count_++;
        }
        a_.OpStoreUAVTyped(
            dxbc::Dest::U(uav_index_edram_, uint32_t(UAVRegister::kEdram)),
            dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY), 1,
            sample_depth_stencil_src);
        if (depth_stencil_early) {
          // Need to still run the shader to know whether to write the
          // depth/stencil value.
          a_.OpElse();
          // Set the sample bit in bits 4:7 of system_temp_rov_params_.x if need
          // to write later (after checking if the sample is not discarded by a
          // kill instruction, alphatest or alpha-to-coverage).
          a_.OpOr(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
                  dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
                  dxbc::Src::LU(1 << (4 + i)));
          // Close the early depth/stencil check.
          a_.OpEndIf();
        }
      }
      // Close the write check.
      a_.OpEndIf();
    }

    // Release sample_temp.
    PopSystemTemp();

    // Close the sample conditional.
    a_.OpEndIf();

    // Go to the next sample (samples are at +0, +(80*scale_x), +1,
    // +(80*scale_x+1), so need to do +(80*scale_x), -(80*scale_x-1),
    // +(80*scale_x) and -(80*scale_x+1) after each sample).
    uint32_t tile_width =
        xenos::kEdramTileWidthSamples * draw_resolution_scale_x_;
    a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::LI((i & 1) ? -int32_t(tile_width) + 2 - i
                                    : int32_t(tile_width)));
  }

  if (ROV_IsDepthStencilEarly()) {
    // Check if safe to discard the whole 2x2 quad early, without running the
    // translated pixel shader, by checking if coverage is 0 in all pixels in
    // the quad and if there are no samples which failed the depth test, but
    // where stencil was modified and needs to be written in the end. Must
    // reject at 2x2 quad granularity because texture fetches need derivatives.

    // temp.x = coverage | deferred depth/stencil write
    a_.OpAnd(dxbc::Dest::R(temp, 0b0001),
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::LU(0b11111111));
    // temp.x = 1.0 if any sample is covered or potentially needs stencil write
    // in the end of the shader in the current pixel
    a_.OpMovC(dxbc::Dest::R(temp, 0b0001), dxbc::Src::R(temp, dxbc::Src::kXXXX),
              dxbc::Src::LF(1.0f), dxbc::Src::LF(0.0f));
    // temp.x = 1.0 if any sample is covered or potentially needs stencil write
    // in the end of the shader in the current pixel
    // temp.y = non-zero if anything is covered in the pixel across X
    a_.OpDerivRTXFine(dxbc::Dest::R(temp, 0b0010),
                      dxbc::Src::R(temp, dxbc::Src::kXXXX));
    // temp.x = 1.0 if anything is covered in the current half of the quad
    // temp.y = free
    a_.OpMovC(dxbc::Dest::R(temp, 0b0001), dxbc::Src::R(temp, dxbc::Src::kYYYY),
              dxbc::Src::LF(1.0f), dxbc::Src::R(temp, dxbc::Src::kXXXX));
    // temp.x = 1.0 if anything is covered in the current half of the quad
    // temp.y = non-zero if anything is covered in the two pixels across Y
    a_.OpDerivRTYCoarse(dxbc::Dest::R(temp, 0b0010),
                        dxbc::Src::R(temp, dxbc::Src::kXXXX));
    // temp.x = 1.0 if anything is covered in the current whole quad
    // temp.y = free
    a_.OpMovC(dxbc::Dest::R(temp, 0b0001), dxbc::Src::R(temp, dxbc::Src::kYYYY),
              dxbc::Src::LF(1.0f), dxbc::Src::R(temp, dxbc::Src::kXXXX));
    // End the shader if nothing is covered in the 2x2 quad after early
    // depth/stencil.
    // temp.x = free
    a_.OpRetC(false, dxbc::Src::R(temp, dxbc::Src::kXXXX));
  }

  // Close the large depth/stencil conditional.
  a_.OpEndIf();

  // Release temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::ROV_UnpackColor(
    uint32_t rt_index, uint32_t packed_temp, uint32_t packed_temp_components,
    uint32_t color_temp, uint32_t temp1, uint32_t temp1_component,
    uint32_t temp2, uint32_t temp2_component) {
  assert_true(color_temp != packed_temp || packed_temp_components == 0);

  dxbc::Src packed_temp_low(
      dxbc::Src::R(packed_temp).Select(packed_temp_components));
  dxbc::Dest temp1_dest(dxbc::Dest::R(temp1, 1 << temp1_component));
  dxbc::Src temp1_src(dxbc::Src::R(temp1).Select(temp1_component));
  dxbc::Dest temp2_dest(dxbc::Dest::R(temp2, 1 << temp2_component));
  dxbc::Src temp2_src(dxbc::Src::R(temp2).Select(temp2_component));

  // Break register dependencies and initialize if there are not enough
  // components. The rest of the function will write at least RG (k_32_FLOAT and
  // k_32_32_FLOAT handled with the same default label), and if packed_temp is
  // the same as color_temp, the packed color won't be touched.
  a_.OpMov(dxbc::Dest::R(color_temp, 0b1100),
           dxbc::Src::LF(0.0f, 0.0f, 0.0f, 1.0f));

  // Choose the packing based on the render target's format.
  a_.OpSwitch(
      LoadSystemConstant(SystemConstants::Index::kEdramRTFormatFlags,
                         offsetof(SystemConstants, edram_rt_format_flags) +
                             sizeof(uint32_t) * rt_index,
                         dxbc::Src::kXXXX));

  // ***************************************************************************
  // k_8_8_8_8
  // k_8_8_8_8_GAMMA
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
        i ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
          : xenos::ColorRenderTargetFormat::k_8_8_8_8)));
    // Unpack the components.
    a_.OpUBFE(dxbc::Dest::R(color_temp), dxbc::Src::LU(8),
              dxbc::Src::LU(0, 8, 16, 24), packed_temp_low);
    // Convert from fixed-point.
    a_.OpUToF(dxbc::Dest::R(color_temp), dxbc::Src::R(color_temp));
    // Normalize.
    a_.OpMul(dxbc::Dest::R(color_temp), dxbc::Src::R(color_temp),
             dxbc::Src::LF(1.0f / 255.0f));
    if (i) {
      for (uint32_t j = 0; j < 3; ++j) {
        PWLGammaToLinear(color_temp, j, color_temp, j, true, temp1,
                         temp1_component, temp2, temp2_component);
      }
    }
    a_.OpBreak();
  }

  // ***************************************************************************
  // k_2_10_10_10
  // k_2_10_10_10_AS_10_10_10_10
  // ***************************************************************************
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10)));
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10)));
  {
    // Unpack the components.
    a_.OpUBFE(dxbc::Dest::R(color_temp), dxbc::Src::LU(10, 10, 10, 2),
              dxbc::Src::LU(0, 10, 20, 30), packed_temp_low);
    // Convert from fixed-point.
    a_.OpUToF(dxbc::Dest::R(color_temp), dxbc::Src::R(color_temp));
    // Normalize.
    a_.OpMul(dxbc::Dest::R(color_temp), dxbc::Src::R(color_temp),
             dxbc::Src::LF(1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f,
                           1.0f / 3.0f));
  }
  a_.OpBreak();

  // ***************************************************************************
  // k_2_10_10_10_FLOAT
  // k_2_10_10_10_FLOAT_AS_16_16_16_16
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT)));
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16)));
  {
    // Unpack the alpha.
    a_.OpUBFE(dxbc::Dest::R(color_temp, 0b1000), dxbc::Src::LU(2),
              dxbc::Src::LU(30), packed_temp_low);
    // Convert the alpha from fixed-point.
    a_.OpUToF(dxbc::Dest::R(color_temp, 0b1000),
              dxbc::Src::R(color_temp, dxbc::Src::kWWWW));
    // Normalize the alpha.
    a_.OpMul(dxbc::Dest::R(color_temp, 0b1000),
             dxbc::Src::R(color_temp, dxbc::Src::kWWWW),
             dxbc::Src::LF(1.0f / 3.0f));
    // Process the components in reverse order because color_temp.r stores the
    // packed color which shouldn't be touched until G and B are converted if
    // packed_temp and color_temp are the same.
    for (int32_t i = 2; i >= 0; --i) {
      Float7e3To32(a_, dxbc::Dest::R(color_temp, 1 << i), packed_temp,
                   packed_temp_components, i * 10, color_temp, i, temp1,
                   temp1_component);
    }
  }
  a_.OpBreak();

  // ***************************************************************************
  // k_16_16
  // k_16_16_16_16 (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
        i ? xenos::ColorRenderTargetFormat::k_16_16_16_16
          : xenos::ColorRenderTargetFormat::k_16_16)));
    dxbc::Dest color_components_dest(
        dxbc::Dest::R(color_temp, i ? 0b1111 : 0b0011));
    // Unpack the components.
    a_.OpIBFE(color_components_dest, dxbc::Src::LU(16),
              dxbc::Src::LU(0, 16, 0, 16),
              dxbc::Src::R(packed_temp,
                           0b01010000 + packed_temp_components * 0b01010101));
    // Convert from fixed-point.
    a_.OpIToF(color_components_dest, dxbc::Src::R(color_temp));
    // Normalize.
    a_.OpMul(color_components_dest, dxbc::Src::R(color_temp),
             dxbc::Src::LF(32.0f / 32767.0f));
    a_.OpBreak();
  }

  // ***************************************************************************
  // k_16_16_FLOAT
  // k_16_16_16_16_FLOAT (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
        i ? xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT
          : xenos::ColorRenderTargetFormat::k_16_16_FLOAT)));
    dxbc::Dest color_components_dest(
        dxbc::Dest::R(color_temp, i ? 0b1111 : 0b0011));
    // Unpack the components.
    a_.OpUBFE(color_components_dest, dxbc::Src::LU(16),
              dxbc::Src::LU(0, 16, 0, 16),
              dxbc::Src::R(packed_temp,
                           0b01010000 + packed_temp_components * 0b01010101));
    // Convert from 16-bit float.
    a_.OpF16ToF32(color_components_dest, dxbc::Src::R(color_temp));
    a_.OpBreak();
  }

  if (packed_temp != color_temp) {
    // Assume k_32_FLOAT or k_32_32_FLOAT for the rest.
    a_.OpDefault();
    a_.OpMov(
        dxbc::Dest::R(color_temp, 0b0011),
        dxbc::Src::R(packed_temp, 0b0100 + packed_temp_components * 0b0101));
    a_.OpBreak();
  }

  a_.OpEndSwitch();
}

void DxbcShaderTranslator::ROV_PackPreClampedColor(
    uint32_t rt_index, uint32_t color_temp, uint32_t packed_temp,
    uint32_t packed_temp_components, uint32_t temp1, uint32_t temp1_component,
    uint32_t temp2, uint32_t temp2_component) {
  // Packing normalized formats according to the Direct3D 11.3 functional
  // specification, but assuming clamping was done by the caller.

  assert_true(color_temp != packed_temp || packed_temp_components == 0);

  dxbc::Dest packed_dest_low(
      dxbc::Dest::R(packed_temp, 1 << packed_temp_components));
  dxbc::Src packed_src_low(
      dxbc::Src::R(packed_temp).Select(packed_temp_components));
  dxbc::Dest temp1_dest(dxbc::Dest::R(temp1, 1 << temp1_component));
  dxbc::Src temp1_src(dxbc::Src::R(temp1).Select(temp1_component));
  dxbc::Dest temp2_dest(dxbc::Dest::R(temp2, 1 << temp2_component));
  dxbc::Src temp2_src(dxbc::Src::R(temp2).Select(temp2_component));

  // Break register dependency after 32bpp cases.
  a_.OpMov(dxbc::Dest::R(packed_temp, 1 << (packed_temp_components + 1)),
           dxbc::Src::LU(0));

  // Choose the packing based on the render target's format.
  a_.OpSwitch(
      LoadSystemConstant(SystemConstants::Index::kEdramRTFormatFlags,
                         offsetof(SystemConstants, edram_rt_format_flags) +
                             sizeof(uint32_t) * rt_index,
                         dxbc::Src::kXXXX));

  // ***************************************************************************
  // k_8_8_8_8
  // k_8_8_8_8_GAMMA
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
        i ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
          : xenos::ColorRenderTargetFormat::k_8_8_8_8)));
    for (uint32_t j = 0; j < 4; ++j) {
      if (i && j < 3) {
        PreSaturatedLinearToPWLGamma(temp1, temp1_component, color_temp, j,
                                     temp1, temp1_component, temp2,
                                     temp2_component);
        // Denormalize and add 0.5 for rounding.
        a_.OpMAd(temp1_dest, temp1_src, dxbc::Src::LF(255.0f),
                 dxbc::Src::LF(0.5f));
      } else {
        // Denormalize and add 0.5 for rounding.
        a_.OpMAd(temp1_dest, dxbc::Src::R(color_temp).Select(j),
                 dxbc::Src::LF(255.0f), dxbc::Src::LF(0.5f));
      }
      // Convert to fixed-point.
      a_.OpFToU(j ? temp1_dest : packed_dest_low, temp1_src);
      // Pack the upper components.
      if (j) {
        a_.OpBFI(packed_dest_low, dxbc::Src::LU(8), dxbc::Src::LU(j * 8),
                 temp1_src, packed_src_low);
      }
    }
    a_.OpBreak();
  }

  // ***************************************************************************
  // k_2_10_10_10
  // k_2_10_10_10_AS_10_10_10_10
  // ***************************************************************************
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10)));
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10)));
  for (uint32_t i = 0; i < 4; ++i) {
    // Denormalize and convert to fixed-point.
    a_.OpMAd(temp1_dest, dxbc::Src::R(color_temp).Select(i),
             dxbc::Src::LF(i < 3 ? 1023.0f : 3.0f), dxbc::Src::LF(0.5f));
    a_.OpFToU(i ? temp1_dest : packed_dest_low, temp1_src);
    // Pack the upper components.
    if (i) {
      a_.OpBFI(packed_dest_low, dxbc::Src::LU(i < 3 ? 10 : 2),
               dxbc::Src::LU(i * 10), temp1_src, packed_src_low);
    }
  }
  a_.OpBreak();

  // ***************************************************************************
  // k_2_10_10_10_FLOAT
  // k_2_10_10_10_FLOAT_AS_16_16_16_16
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT)));
  a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
      xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16)));
  {
    // Convert red directly to the destination, which may be the same as the
    // source, but PreClampedFloat32To7e3 allows that.
    PreClampedFloat32To7e3(a_, packed_temp, packed_temp_components, color_temp,
                           0, temp1, temp1_component);
    for (uint32_t i = 1; i < 3; ++i) {
      // Convert green and blue to a temporary register and insert them into the
      // result.
      PreClampedFloat32To7e3(a_, temp1, temp1_component, color_temp, i, temp2,
                             temp2_component);
      a_.OpBFI(packed_dest_low, dxbc::Src::LU(10), dxbc::Src::LU(i * 10),
               temp1_src, packed_src_low);
    }
    // Denormalize the alpha and convert it to fixed-point.
    a_.OpMAd(temp1_dest, dxbc::Src::R(color_temp, dxbc::Src::kWWWW),
             dxbc::Src::LF(3.0f), dxbc::Src::LF(0.5f));
    a_.OpFToU(temp1_dest, temp1_src);
    // Pack the alpha.
    a_.OpBFI(packed_dest_low, dxbc::Src::LU(2), dxbc::Src::LU(30), temp1_src,
             packed_src_low);
  }
  a_.OpBreak();

  // ***************************************************************************
  // k_16_16
  // k_16_16_16_16 (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
        i ? xenos::ColorRenderTargetFormat::k_16_16_16_16
          : xenos::ColorRenderTargetFormat::k_16_16)));
    for (uint32_t j = 0; j < (uint32_t(2) << i); ++j) {
      // Denormalize and convert to fixed-point, making 0.5 with the proper sign
      // in temp2.
      a_.OpGE(temp2_dest, dxbc::Src::R(color_temp).Select(j),
              dxbc::Src::LF(0.0f));
      a_.OpMovC(temp2_dest, temp2_src, dxbc::Src::LF(0.5f),
                dxbc::Src::LF(-0.5f));
      a_.OpMAd(temp1_dest, dxbc::Src::R(color_temp).Select(j),
               dxbc::Src::LF(32767.0f / 32.0f), temp2_src);
      dxbc::Dest packed_dest_half(
          dxbc::Dest::R(packed_temp, 1 << (packed_temp_components + (j >> 1))));
      // Convert to fixed-point.
      a_.OpFToI((j & 1) ? temp1_dest : packed_dest_half, temp1_src);
      // Pack green or alpha.
      if (j & 1) {
        a_.OpBFI(packed_dest_half, dxbc::Src::LU(16), dxbc::Src::LU(16),
                 temp1_src,
                 dxbc::Src::R(packed_temp)
                     .Select(packed_temp_components + (j >> 1)));
      }
    }
    a_.OpBreak();
  }

  // ***************************************************************************
  // k_16_16_FLOAT
  // k_16_16_16_16_FLOAT (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    a_.OpCase(dxbc::Src::LU(RenderTargetCache::AddPSIColorFormatFlags(
        i ? xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT
          : xenos::ColorRenderTargetFormat::k_16_16_FLOAT)));
    for (uint32_t j = 0; j < (uint32_t(2) << i); ++j) {
      dxbc::Dest packed_dest_half(
          dxbc::Dest::R(packed_temp, 1 << (packed_temp_components + (j >> 1))));
      // Convert to 16-bit float.
      a_.OpF32ToF16((j & 1) ? temp1_dest : packed_dest_half,
                    dxbc::Src::R(color_temp).Select(j));
      // Pack green or alpha.
      if (j & 1) {
        a_.OpBFI(packed_dest_half, dxbc::Src::LU(16), dxbc::Src::LU(16),
                 temp1_src,
                 dxbc::Src::R(packed_temp)
                     .Select(packed_temp_components + (j >> 1)));
      }
    }
    a_.OpBreak();
  }

  if (packed_temp != color_temp) {
    // Assume k_32_FLOAT or k_32_32_FLOAT for the rest.
    a_.OpDefault();
    a_.OpMov(dxbc::Dest::R(packed_temp, 0b11 << packed_temp_components),
             dxbc::Src::R(color_temp, 0b0100 << (packed_temp_components * 2)));
    a_.OpBreak();
  }

  a_.OpEndSwitch();
}

void DxbcShaderTranslator::ROV_HandleColorBlendFactorCases(
    uint32_t src_temp, uint32_t dst_temp, uint32_t factor_temp) {
  dxbc::Dest factor_dest(dxbc::Dest::R(factor_temp, 0b0111));
  dxbc::Src one_src(dxbc::Src::LF(1.0f));

  // kOne.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOne)));
  a_.OpMov(factor_dest, one_src);
  a_.OpBreak();

  // kSrcColor
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kSrcColor)));
  if (factor_temp != src_temp) {
    a_.OpMov(factor_dest, dxbc::Src::R(src_temp));
  }
  a_.OpBreak();

  // kOneMinusSrcColor
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusSrcColor)));
  a_.OpAdd(factor_dest, one_src, -dxbc::Src::R(src_temp));
  a_.OpBreak();

  // kSrcAlpha
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kSrcAlpha)));
  a_.OpMov(factor_dest, dxbc::Src::R(src_temp, dxbc::Src::kWWWW));
  a_.OpBreak();

  // kOneMinusSrcAlpha
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusSrcAlpha)));
  a_.OpAdd(factor_dest, one_src, -dxbc::Src::R(src_temp, dxbc::Src::kWWWW));
  a_.OpBreak();

  // kDstColor
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kDstColor)));
  if (factor_temp != dst_temp) {
    a_.OpMov(factor_dest, dxbc::Src::R(dst_temp));
  }
  a_.OpBreak();

  // kOneMinusDstColor
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusDstColor)));
  a_.OpAdd(factor_dest, one_src, -dxbc::Src::R(dst_temp));
  a_.OpBreak();

  // kDstAlpha
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kDstAlpha)));
  a_.OpMov(factor_dest, dxbc::Src::R(dst_temp, dxbc::Src::kWWWW));
  a_.OpBreak();

  // kOneMinusDstAlpha
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusDstAlpha)));
  a_.OpAdd(factor_dest, one_src, -dxbc::Src::R(dst_temp, dxbc::Src::kWWWW));
  a_.OpBreak();

  // Factors involving the constant.

  // kConstantColor
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kConstantColor)));
  a_.OpMov(factor_dest,
           LoadSystemConstant(SystemConstants::Index::kEdramBlendConstant,
                              offsetof(SystemConstants, edram_blend_constant),
                              dxbc::Src::kXYZW));
  a_.OpBreak();

  // kOneMinusConstantColor
  a_.OpCase(
      dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusConstantColor)));
  a_.OpAdd(factor_dest, one_src,
           -LoadSystemConstant(SystemConstants::Index::kEdramBlendConstant,
                               offsetof(SystemConstants, edram_blend_constant),
                               dxbc::Src::kXYZW));
  a_.OpBreak();

  // kConstantAlpha
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kConstantAlpha)));
  a_.OpMov(factor_dest,
           LoadSystemConstant(SystemConstants::Index::kEdramBlendConstant,
                              offsetof(SystemConstants, edram_blend_constant),
                              dxbc::Src::kWWWW));
  a_.OpBreak();

  // kOneMinusConstantAlpha
  a_.OpCase(
      dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusConstantAlpha)));
  a_.OpAdd(factor_dest, one_src,
           -LoadSystemConstant(SystemConstants::Index::kEdramBlendConstant,
                               offsetof(SystemConstants, edram_blend_constant),
                               dxbc::Src::kWWWW));
  a_.OpBreak();

  // kSrcAlphaSaturate
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kSrcAlphaSaturate)));
  a_.OpAdd(dxbc::Dest::R(factor_temp, 0b0001), one_src,
           -dxbc::Src::R(dst_temp, dxbc::Src::kWWWW));
  a_.OpMin(factor_dest, dxbc::Src::R(src_temp, dxbc::Src::kWWWW),
           dxbc::Src::R(factor_temp, dxbc::Src::kXXXX));
  a_.OpBreak();

  // kZero default.
  a_.OpDefault();
  a_.OpMov(factor_dest, dxbc::Src::LF(0.0f));
  a_.OpBreak();
}

void DxbcShaderTranslator::ROV_HandleAlphaBlendFactorCases(
    uint32_t src_temp, uint32_t dst_temp, uint32_t factor_temp,
    uint32_t factor_component) {
  dxbc::Dest factor_dest(dxbc::Dest::R(factor_temp, 1 << factor_component));
  dxbc::Src one_src(dxbc::Src::LF(1.0f));

  // kOne, kSrcAlphaSaturate.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOne)));
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kSrcAlphaSaturate)));
  a_.OpMov(factor_dest, one_src);
  a_.OpBreak();

  // kSrcColor, kSrcAlpha.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kSrcColor)));
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kSrcAlpha)));
  if (factor_temp != src_temp || factor_component != 3) {
    a_.OpMov(factor_dest, dxbc::Src::R(src_temp, dxbc::Src::kWWWW));
  }
  a_.OpBreak();

  // kOneMinusSrcColor, kOneMinusSrcAlpha.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusSrcColor)));
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusSrcAlpha)));
  a_.OpAdd(factor_dest, one_src, -dxbc::Src::R(src_temp, dxbc::Src::kWWWW));
  a_.OpBreak();

  // kDstColor, kDstAlpha.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kDstColor)));
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kDstAlpha)));
  if (factor_temp != dst_temp || factor_component != 3) {
    a_.OpMov(factor_dest, dxbc::Src::R(dst_temp, dxbc::Src::kWWWW));
  }
  a_.OpBreak();

  // kOneMinusDstColor, kOneMinusDstAlpha.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusDstColor)));
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusDstAlpha)));
  a_.OpAdd(factor_dest, one_src, -dxbc::Src::R(dst_temp, dxbc::Src::kWWWW));
  a_.OpBreak();

  // Factors involving the constant.

  // kConstantColor, kConstantAlpha.
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kConstantColor)));
  a_.OpCase(dxbc::Src::LU(uint32_t(xenos::BlendFactor::kConstantAlpha)));
  a_.OpMov(factor_dest,
           LoadSystemConstant(SystemConstants::Index::kEdramBlendConstant,
                              offsetof(SystemConstants, edram_blend_constant),
                              dxbc::Src::kWWWW));
  a_.OpBreak();

  // kOneMinusConstantColor, kOneMinusConstantAlpha.
  a_.OpCase(
      dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusConstantColor)));
  a_.OpCase(
      dxbc::Src::LU(uint32_t(xenos::BlendFactor::kOneMinusConstantAlpha)));
  a_.OpAdd(factor_dest, one_src,
           -LoadSystemConstant(SystemConstants::Index::kEdramBlendConstant,
                               offsetof(SystemConstants, edram_blend_constant),
                               dxbc::Src::kWWWW));
  a_.OpBreak();

  // kZero default.
  a_.OpDefault();
  a_.OpMov(factor_dest, dxbc::Src::LF(0.0f));
  a_.OpBreak();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs() {
  uint32_t shader_writes_color_targets =
      current_shader().writes_color_targets();
  if (!shader_writes_color_targets) {
    return;
  }

  uint32_t gamma_temp = PushSystemTemp();
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(shader_writes_color_targets & (1 << i))) {
      continue;
    }
    uint32_t system_temp_color = system_temps_color_[i];
    // Apply the exponent bias after alpha to coverage because it needs the
    // unbiased alpha from the shader.
    a_.OpMul(dxbc::Dest::R(system_temp_color), dxbc::Src::R(system_temp_color),
             LoadSystemConstant(
                 SystemConstants::Index::kColorExpBias,
                 offsetof(SystemConstants, color_exp_bias) + sizeof(float) * i,
                 dxbc::Src::kXXXX));
    if (!gamma_render_target_as_srgb_) {
      // Convert to gamma space - this is incorrect, since it must be done after
      // blending on the Xbox 360, but this is just one of many blending issues
      // in the RTV path.
      a_.OpAnd(dxbc::Dest::R(gamma_temp, 0b0001), LoadFlagsSystemConstant(),
               dxbc::Src::LU(kSysFlag_ConvertColor0ToGamma << i));
      a_.OpIf(true, dxbc::Src::R(gamma_temp, dxbc::Src::kXXXX));
      // Saturate before the gamma conversion.
      a_.OpMov(dxbc::Dest::R(system_temp_color, 0b0111),
               dxbc::Src::R(system_temp_color), true);
      for (uint32_t j = 0; j < 3; ++j) {
        PreSaturatedLinearToPWLGamma(system_temp_color, j, system_temp_color, j,
                                     gamma_temp, 0, gamma_temp, 1);
      }
      a_.OpEndIf();
    }
    // Copy the color from a readable temp register to an output register.
    a_.OpMov(dxbc::Dest::O(i), dxbc::Src::R(system_temp_color));
  }
  // Release gamma_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader_DSV_DepthTo24Bit() {
  bool shader_writes_depth = current_shader().writes_depth();

  if (!DSV_IsWritingFloat24Depth()) {
    if (shader_writes_depth) {
      // If not converting, but the shader writes depth explicitly, for float24,
      // need to scale it from guest 0...1 to host 0...0.5 to support
      // reinterpretation round trips as viewport scaling doesn't apply to
      // oDepth.
      a_.OpAnd(dxbc::Dest::R(system_temp_depth_stencil_, 0b0010),
               LoadFlagsSystemConstant(), dxbc::Src::LU(kSysFlag_DepthFloat24));
      a_.OpIf(true, dxbc::Src::R(system_temp_depth_stencil_, dxbc::Src::kYYYY));
      a_.OpMul(dxbc::Dest::R(system_temp_depth_stencil_, 0b0001),
               dxbc::Src::R(system_temp_depth_stencil_, dxbc::Src::kXXXX),
               dxbc::Src::LF(0.5f));
      a_.OpEndIf();
      // Write the depth from the temporary to the system depth output.
      a_.OpMov(dxbc::Dest::ODepth(),
               dxbc::Src::R(system_temp_depth_stencil_, dxbc::Src::kXXXX));
    }
    return;
  }

  uint32_t temp;
  if (shader_writes_depth) {
    // The depth is already written to system_temp_depth_stencil_.x and clamped
    // to 0...1 with NaNs dropped (saturating in StoreResult); yzw are free.
    temp = system_temp_depth_stencil_;
  } else {
    // Need a temporary variable; remap the sample's depth input from host
    // 0...0.5 back to guest 0...1 for conversion purposes to it and saturate it
    // (in Direct3D 11, depth is clamped to the viewport bounds after the pixel
    // shader, and SV_Position.z contains the unclamped depth, which may be
    // outside the viewport's depth range if it's biased); though it will be
    // clamped to the viewport bounds anyway, but to be able to make the
    // assumption of it being clamped while working with the bit representation.
    temp = PushSystemTemp();
    in_position_used_ |= 0b0100;
    a_.OpMul(dxbc::Dest::R(temp, 0b0001),
             dxbc::Src::V1D(in_reg_ps_position_, dxbc::Src::kZZZZ),
             dxbc::Src::LF(2.0f), true);
  }

  dxbc::Dest temp_x_dest(dxbc::Dest::R(temp, 0b0001));
  dxbc::Src temp_x_src(dxbc::Src::R(temp, dxbc::Src::kXXXX));
  dxbc::Dest temp_y_dest(dxbc::Dest::R(temp, 0b0010));
  dxbc::Src temp_y_src(dxbc::Src::R(temp, dxbc::Src::kYYYY));

  if (GetDxbcShaderModification().pixel.depth_stencil_mode ==
      Modification::DepthStencilMode::kFloat24Truncating) {
    // Simplified conversion, always less than or equal to the original value -
    // just drop the lower bits.
    // The float32 exponent bias is 127.
    // After saturating, the exponent range is -127...0.
    // The smallest normalized 20e4 exponent is -14 - should drop 3 mantissa
    // bits at -14 or above.
    // The smallest denormalized 20e4 number is -34 - should drop 23 mantissa
    // bits at -34.
    // Anything smaller than 2^-34 becomes 0.
    dxbc::Dest truncate_dest(shader_writes_depth ? dxbc::Dest::ODepth()
                                                 : dxbc::Dest::ODepthLE());
    // Check if the number is representable as a float24 after truncation - the
    // exponent is at least -34.
    a_.OpUGE(temp_y_dest, temp_x_src, dxbc::Src::LU(0x2E800000));
    a_.OpIf(true, temp_y_src);
    {
      // Extract the biased float32 exponent to temp.y.
      // temp.y = 113+ at exponent -14+.
      // temp.y = 93 at exponent -34.
      a_.OpUBFE(temp_y_dest, dxbc::Src::LU(8), dxbc::Src::LU(23), temp_x_src);
      // Convert exponent to the unclamped number of bits to truncate.
      // 116 - 113 = 3.
      // 116 - 93 = 23.
      // temp.y = 3+ at exponent -14+.
      // temp.y = 23 at exponent -34.
      a_.OpIAdd(temp_y_dest, dxbc::Src::LI(116), -temp_y_src);
      // Clamp the truncated bit count to drop 3 bits of any normal number.
      // Exponents below -34 are handled separately.
      // temp.y = 3 at exponent -14.
      // temp.y = 23 at exponent -34.
      a_.OpIMax(temp_y_dest, temp_y_src, dxbc::Src::LI(3));
      // Truncate the mantissa - fill the low bits with zeros.
      // temp.x = result in 0...1 range
      a_.OpBFI(temp_x_dest, temp_y_src, dxbc::Src::LU(0), dxbc::Src::LU(0),
               temp_x_src);
      // Remap from guest 0...1 to host 0...0.5.
      a_.OpMul(truncate_dest, temp_x_src, dxbc::Src::LF(0.5f));
    }
    // The number is not representable as float24 after truncation - zero.
    a_.OpElse();
    a_.OpMov(truncate_dest, dxbc::Src::LF(0.0f));
    // Close the non-zero result check.
    a_.OpEndIf();
  } else {
    // Properly convert to 20e4, with rounding to the nearest even (the bias was
    // pre-applied by multiplying by 2), then convert back restoring the bias.
    PreClampedDepthTo20e4(a_, temp, 0, temp, 0, temp, 1, true, false);
    Depth20e4To32(a_, dxbc::Dest::ODepth(), temp, 0, 0, temp, 0, temp, 1, true);
  }

  if (!shader_writes_depth) {
    // Release temp.
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::CompletePixelShader_AlphaToMaskSample(
    bool initialize, uint32_t sample_index, float threshold_base,
    dxbc::Src threshold_offset, float threshold_offset_scale,
    uint32_t coverage_temp, uint32_t coverage_temp_component, uint32_t temp,
    uint32_t temp_component) {
  dxbc::Dest temp_dest(dxbc::Dest::R(temp, 1 << temp_component));
  dxbc::Src temp_src(dxbc::Src::R(temp).Select(temp_component));
  // Calculate the threshold.
  a_.OpMAd(temp_dest, threshold_offset, dxbc::Src::LF(-threshold_offset_scale),
           dxbc::Src::LF(threshold_base));
  // Check if alpha of oC0 is at or greater than the threshold (handling NaN
  // according to the Direct3D 11.3 functional specification, as not covered).
  a_.OpGE(temp_dest, dxbc::Src::R(system_temps_color_[0], dxbc::Src::kWWWW),
          temp_src);
  dxbc::Dest coverage_dest(
      dxbc::Dest::R(coverage_temp, 1 << coverage_temp_component));
  dxbc::Src coverage_src(
      dxbc::Src::R(coverage_temp).Select(coverage_temp_component));
  if (edram_rov_used_) {
    assert_true(coverage_temp != temp ||
                coverage_temp_component != temp_component);
    // Keep all bits in but the ones that need to be removed in case of failure.
    // For ROV, the test must effect not only the coverage bits, but also the
    // deferred depth/stencil write bits since the coverage is zeroed for
    // samples that have failed the depth/stencil test, but stencil may still
    // require writing - but if the sample is discarded by alpha to coverage, it
    // must not be written at all.
    a_.OpOr(temp_dest, temp_src,
            dxbc::Src::LU(~(uint32_t(0b00010001) << sample_index)));
    // Clear the coverage for samples that have failed the test.
    a_.OpAnd(coverage_dest, coverage_src, temp_src);
  } else {
    if (initialize) {
      // First sample tested - initialize.
      assert_true(coverage_temp != temp ||
                  coverage_temp_component != temp_component);
      a_.OpAnd(coverage_dest, temp_src,
               dxbc::Src::LU(uint32_t(1) << sample_index));
    } else {
      // Not first sample tested - add.
      a_.OpAnd(temp_dest, temp_src, dxbc::Src::LU(uint32_t(1) << sample_index));
      a_.OpOr(coverage_dest, coverage_src, temp_src);
    }
  }
}

void DxbcShaderTranslator::CompletePixelShader_AlphaToMask() {
  // Check if alpha to coverage can be done at all in this shader.
  if (!current_shader().writes_color_target(0) ||
      IsForceEarlyDepthStencilGlobalFlagEnabled()) {
    return;
  }

  if (!edram_rov_used_) {
    // Initialize the output coverage for the case if alpha to mask is not
    // enabled - it needs to be written on every execution path.
    a_.OpMov(dxbc::Dest::OMask(), dxbc::Src::LU(UINT32_MAX));
  }

  // Check if alpha to coverage is enabled.
  dxbc::Src alpha_to_mask_constant_src(LoadSystemConstant(
      SystemConstants::Index::kAlphaToMask,
      offsetof(SystemConstants, alpha_to_mask), dxbc::Src::kXXXX));
  a_.OpIf(true, alpha_to_mask_constant_src);

  uint32_t temp = PushSystemTemp();
  dxbc::Dest temp_x_dest(dxbc::Dest::R(temp, 0b0001));
  dxbc::Src temp_x_src(dxbc::Src::R(temp, dxbc::Src::kXXXX));

  // Get the dithering threshold offset index for the pixel, Y - low bit of
  // offset index, X - high bit, and extract the offset and convert it to
  // floating-point. With resolution scaling, still using host pixels, to
  // preserve the idea of dithering.
  // temp.x = alpha to coverage offset as float 0.0...3.0.
  in_position_used_ |= 0b0011;
  a_.OpFToU(dxbc::Dest::R(temp, 0b0011), dxbc::Src::V1D(in_reg_ps_position_));
  a_.OpAnd(dxbc::Dest::R(temp, 0b0010), dxbc::Src::R(temp, dxbc::Src::kYYYY),
           dxbc::Src::LU(1));
  a_.OpBFI(temp_x_dest, dxbc::Src::LU(1), dxbc::Src::LU(1), temp_x_src,
           dxbc::Src::R(temp, dxbc::Src::kYYYY));
  a_.OpIShL(temp_x_dest, temp_x_src, dxbc::Src::LU(1));
  a_.OpUBFE(temp_x_dest, dxbc::Src::LU(2), temp_x_src,
            alpha_to_mask_constant_src);
  a_.OpUToF(temp_x_dest, temp_x_src);

  // Write the result to temp.z for RTV or to system_temp_rov_params_.x for ROV.
  // temp.x = alpha to coverage offset as float 0.0...3.0.
  // temp.z = without ROV, accumulated coverage.
  uint32_t coverage_temp = edram_rov_used_ ? system_temp_rov_params_ : temp;
  uint32_t coverage_temp_component = edram_rov_used_ ? 0 : 2;

  // Check if MSAA is enabled.
  a_.OpIf(true, LoadSystemConstant(SystemConstants::Index::kSampleCountLog2,
                                   offsetof(SystemConstants, sample_count_log2),
                                   dxbc::Src::kYYYY));
  {
    // Check if MSAA is 4x or 2x.
    a_.OpIf(true,
            LoadSystemConstant(SystemConstants::Index::kSampleCountLog2,
                               offsetof(SystemConstants, sample_count_log2),
                               dxbc::Src::kXXXX));
    // 4x MSAA.
    // Sample 0 must be checked first - CompletePixelShader_AlphaToMaskSample
    // initializes the result for sample index 0.
    CompletePixelShader_AlphaToMaskSample(true, 0, 0.75f, temp_x_src,
                                          1.0f / 16.0f, coverage_temp,
                                          coverage_temp_component, temp, 1);
    CompletePixelShader_AlphaToMaskSample(false, 1, 0.25f, temp_x_src,
                                          1.0f / 16.0f, coverage_temp,
                                          coverage_temp_component, temp, 1);
    CompletePixelShader_AlphaToMaskSample(false, 2, 0.5f, temp_x_src,
                                          1.0f / 16.0f, coverage_temp,
                                          coverage_temp_component, temp, 1);
    CompletePixelShader_AlphaToMaskSample(false, 3, 1.0f, temp_x_src,
                                          1.0f / 16.0f, coverage_temp,
                                          coverage_temp_component, temp, 1);
    // 2x MSAA.
    // With ROV, using guest sample indices.
    // Without ROV:
    // - Native 2x: top (0 in Xenia) is 1 in D3D10.1+, bottom (1 in Xenia) is 0.
    // - 2x as 4x: top is 0, bottom is 3.
    a_.OpElse();
    CompletePixelShader_AlphaToMaskSample(
        true, (!edram_rov_used_ && msaa_2x_supported_) ? 1 : 0, 0.5f,
        temp_x_src, 1.0f / 8.0f, coverage_temp, coverage_temp_component, temp,
        1);
    CompletePixelShader_AlphaToMaskSample(
        false, edram_rov_used_ ? 1 : (msaa_2x_supported_ ? 0 : 3), 1.0f,
        temp_x_src, 1.0f / 8.0f, coverage_temp, coverage_temp_component, temp,
        1);
    // Close the 4x check.
    a_.OpEndIf();
  }
  // MSAA is disabled.
  a_.OpElse();
  CompletePixelShader_AlphaToMaskSample(true, 0, 1.0f, temp_x_src, 1.0f / 4.0f,
                                        coverage_temp, coverage_temp_component,
                                        temp, 1);
  // Close the 2x/4x check.
  a_.OpEndIf();

  // Check if any sample is still covered and return to avoid unneeded work (the
  // driver's shader compiler may place return after a discard, but it will
  // likely not place one during SV_Coverage assignment - that's what the AMD
  // compiler does, at least). Then, if needed, write the coverage value.
  if (edram_rov_used_) {
    // The mask includes both 0:3 and 4:7 parts because there may be samples
    // which passed alpha to coverage, but not stencil test, and the stencil
    // buffer needs to be modified - in this case, samples would be dropped in
    // 0:3, but not in 4:7).
    a_.OpAnd(temp_x_dest,
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::LU(0b11111111));
    a_.OpRetC(false, temp_x_src);
  } else {
    dxbc::Src coverage_src(
        dxbc::Src::R(coverage_temp, coverage_temp_component));
    a_.OpDiscard(false, coverage_src);
    a_.OpMov(dxbc::Dest::OMask(), coverage_src);
  }

  // Release temp.
  PopSystemTemp();

  // Close the alpha to coverage check.
  a_.OpEndIf();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV() {
  uint32_t temp = PushSystemTemp();
  dxbc::Dest temp_x_dest(dxbc::Dest::R(temp, 0b0001));
  dxbc::Src temp_x_src(dxbc::Src::R(temp, dxbc::Src::kXXXX));
  dxbc::Dest temp_y_dest(dxbc::Dest::R(temp, 0b0010));
  dxbc::Src temp_y_src(dxbc::Src::R(temp, dxbc::Src::kYYYY));
  dxbc::Dest temp_z_dest(dxbc::Dest::R(temp, 0b0100));
  dxbc::Src temp_z_src(dxbc::Src::R(temp, dxbc::Src::kZZZZ));
  dxbc::Dest temp_w_dest(dxbc::Dest::R(temp, 0b1000));
  dxbc::Src temp_w_src(dxbc::Src::R(temp, dxbc::Src::kWWWW));

  uint32_t tile_width =
      xenos::kEdramTileWidthSamples * draw_resolution_scale_x_;

  // Do late depth/stencil test (which includes writing) if needed or deferred
  // depth writing.
  if (ROV_IsDepthStencilEarly()) {
    // Write modified depth/stencil.
    for (uint32_t i = 0; i < 4; ++i) {
      // Get if need to write to temp.x.
      // temp.x = whether the depth sample needs to be written.
      a_.OpAnd(temp_x_dest,
               dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
               dxbc::Src::LU(1 << (4 + i)));
      // Check if need to write.
      // temp.x = free.
      a_.OpIf(true, temp_x_src);
      {
        // Write the new depth/stencil.
        if (uav_index_edram_ == kBindingIndexUnallocated) {
          uav_index_edram_ = uav_count_++;
        }
        a_.OpStoreUAVTyped(
            dxbc::Dest::U(uav_index_edram_, uint32_t(UAVRegister::kEdram)),
            dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY), 1,
            dxbc::Src::R(system_temp_depth_stencil_).Select(i));
      }
      // Close the write check.
      a_.OpEndIf();
      // Go to the next sample (samples are at +0, +(80*scale_x), +1,
      // +(80*scale_x+1), so need to do +(80*scale_x), -(80*scale_x-1),
      // +(80*scale_x) and -(80*scale_x+1) after each sample).
      if (i < 3) {
        a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
                  dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
                  dxbc::Src::LI((i & 1) ? -int32_t(tile_width) + 2 - i
                                        : int32_t(tile_width)));
      }
    }
  } else {
    ROV_DepthStencilTest();
  }

  // system_temp_rov_params_.y (the depth / stencil sample address) is not
  // needed anymore, can be used for color writing.

  if (!is_depth_only_pixel_shader_) {
    // Check if any sample is still covered after depth testing and writing,
    // skip color writing completely in this case.
    // temp.x = whether any sample is still covered.
    a_.OpAnd(temp_x_dest,
             dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
             dxbc::Src::LU(0b1111));
    // temp.x = free.
    a_.OpRetC(false, temp_x_src);
  }

  // Write color values.
  uint32_t shader_writes_color_targets =
      current_shader().writes_color_targets();
  uint32_t edram_size_32bpp_samples =
      (xenos::kEdramTileHeightSamples * draw_resolution_scale_y_) * tile_width *
      xenos::kEdramTileCount;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(shader_writes_color_targets & (1 << i))) {
      continue;
    }

    // This includes a swizzle to choose XY for even render targets or ZW for
    // odd ones - use SelectFromSwizzled and SwizzleSwizzled.
    dxbc::Src keep_mask_src(
        LoadSystemConstant(SystemConstants::Index::kEdramRTKeepMask,
                           offsetof(SystemConstants, edram_rt_keep_mask) +
                               sizeof(uint32_t) * 2 * i,
                           0b0100));

    // Check if color writing is disabled - special keep mask constant case,
    // both 32bpp parts are forced UINT32_MAX, but also check whether the shader
    // has written anything to this target at all.

    // Combine both parts of the keep mask to check if both are 0xFFFFFFFF.
    // temp.x = whether all bits need to be kept.
    a_.OpAnd(temp_x_dest, keep_mask_src.SelectFromSwizzled(0),
             keep_mask_src.SelectFromSwizzled(1));
    // Flip the bits so both UINT32_MAX would result in 0 - not writing.
    // temp.x = whether any bits need to be written.
    a_.OpNot(temp_x_dest, temp_x_src);
    // Get the bits that will be used for checking wherther the render target
    // has been written to on the taken execution path - if the write mask is
    // empty, AND zero with the test bit to always get zero.
    // temp.x = bits for checking whether the render target has been written to.
    a_.OpMovC(temp_x_dest, temp_x_src,
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
              dxbc::Src::LU(0));
    // Check if the render target was written to on the execution path.
    // temp.x = whether anything was written and needs to be stored.
    a_.OpAnd(temp_x_dest, temp_x_src, dxbc::Src::LU(1 << (8 + i)));
    // Check if need to write anything to the render target.
    // temp.x = free.
    a_.OpIf(true, temp_x_src);

    // Apply the exponent bias after alpha to coverage because it needs the
    // unbiased alpha from the shader.
    a_.OpMul(dxbc::Dest::R(system_temps_color_[i]),
             dxbc::Src::R(system_temps_color_[i]),
             LoadSystemConstant(
                 SystemConstants::Index::kColorExpBias,
                 offsetof(SystemConstants, color_exp_bias) + sizeof(float) * i,
                 dxbc::Src::kXXXX));

    dxbc::Src rt_format_flags_src(LoadSystemConstant(
        SystemConstants::Index::kEdramRTFormatFlags,
        offsetof(SystemConstants, edram_rt_format_flags) + sizeof(uint32_t) * i,
        dxbc::Src::kXXXX));

    // Load whether the render target is 64bpp to system_temp_rov_params_.y to
    // get the needed relative sample address.
    a_.OpAnd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
             rt_format_flags_src,
             dxbc::Src::LU(RenderTargetCache::kPSIColorFormatFlag_64bpp));
    // Choose the relative sample address for the render target to
    // system_temp_rov_params_.y.
    a_.OpMovC(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kWWWW),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kZZZZ));
    // Add the EDRAM base of the render target to system_temp_rov_params_.y.
    a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              LoadSystemConstant(
                  SystemConstants::Index::kEdramRTBaseDwordsScaled,
                  offsetof(SystemConstants, edram_rt_base_dwords_scaled) +
                      sizeof(uint32_t) * i,
                  dxbc::Src::kXXXX));
    // Wrap EDRAM addressing for the color render target to get the final sample
    // address in the EDRAM to system_temp_rov_params_.y.
    a_.OpUDiv(dxbc::Dest::Null(),
              dxbc::Dest::R(system_temp_rov_params_, 0b0010),
              dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
              dxbc::Src::LU(edram_size_32bpp_samples));

    dxbc::Src rt_blend_factors_ops_src(LoadSystemConstant(
        SystemConstants::Index::kEdramRTBlendFactorsOps,
        offsetof(SystemConstants, edram_rt_blend_factors_ops) +
            sizeof(uint32_t) * i,
        dxbc::Src::kXXXX));
    dxbc::Src rt_clamp_vec_src(LoadSystemConstant(
        SystemConstants::Index::kEdramRTClamp,
        offsetof(SystemConstants, edram_rt_clamp) + sizeof(float) * 4 * i,
        dxbc::Src::kXYZW));
    // Get if not blending to pack the color once for all 4 samples.
    // temp.x = whether blending is disabled.
    a_.OpIEq(temp_x_dest, rt_blend_factors_ops_src, dxbc::Src::LU(0x00010001));
    // Check if not blending.
    // temp.x = free.
    a_.OpIf(true, temp_x_src);
    {
      // Clamp the color to the render target's representable range - will be
      // packed.
      a_.OpMax(dxbc::Dest::R(system_temps_color_[i]),
               dxbc::Src::R(system_temps_color_[i]),
               rt_clamp_vec_src.Swizzle(0b01000000));
      a_.OpMin(dxbc::Dest::R(system_temps_color_[i]),
               dxbc::Src::R(system_temps_color_[i]),
               rt_clamp_vec_src.Swizzle(0b11101010));
      // Pack the color once if blending.
      // temp.xy = packed color.
      ROV_PackPreClampedColor(i, system_temps_color_[i], temp, 0, temp, 2, temp,
                              3);
    }
    // Blending is enabled.
    a_.OpElse();
    {
      // Get if the blending source color is fixed-point for clamping if it is.
      // temp.x = whether color is fixed-point.
      a_.OpAnd(temp_x_dest, rt_format_flags_src,
               dxbc::Src::LU(
                   RenderTargetCache::kPSIColorFormatFlag_FixedPointColor));
      // Check if the blending source color is fixed-point and needs clamping.
      // temp.x = free.
      a_.OpIf(true, temp_x_src);
      {
        // Clamp the blending source color if needed.
        a_.OpMax(dxbc::Dest::R(system_temps_color_[i], 0b0111),
                 dxbc::Src::R(system_temps_color_[i]),
                 rt_clamp_vec_src.Select(0));
        a_.OpMin(dxbc::Dest::R(system_temps_color_[i], 0b0111),
                 dxbc::Src::R(system_temps_color_[i]),
                 rt_clamp_vec_src.Select(2));
      }
      // Close the fixed-point color check.
      a_.OpEndIf();

      // Get if the blending source alpha is fixed-point for clamping if it is.
      // temp.x = whether alpha is fixed-point.
      a_.OpAnd(temp_x_dest, rt_format_flags_src,
               dxbc::Src::LU(
                   RenderTargetCache::kPSIColorFormatFlag_FixedPointAlpha));
      // Check if the blending source alpha is fixed-point and needs clamping.
      // temp.x = free.
      a_.OpIf(true, temp_x_src);
      {
        // Clamp the blending source alpha if needed.
        a_.OpMax(dxbc::Dest::R(system_temps_color_[i], 0b1000),
                 dxbc::Src::R(system_temps_color_[i], dxbc::Src::kWWWW),
                 rt_clamp_vec_src.Select(1));
        a_.OpMin(dxbc::Dest::R(system_temps_color_[i], 0b1000),
                 dxbc::Src::R(system_temps_color_[i], dxbc::Src::kWWWW),
                 rt_clamp_vec_src.Select(3));
      }
      // Close the fixed-point alpha check.
      a_.OpEndIf();
      // Break register dependency in the color sample raster operation.
      // temp.xy = 0 instead of packed color.
      a_.OpMov(dxbc::Dest::R(temp, 0b0011), dxbc::Src::LU(0));
    }
    a_.OpEndIf();

    // Blend, mask and write all samples.
    for (uint32_t j = 0; j < 4; ++j) {
      // Get if the sample is covered.
      // temp.z = whether the sample is covered.
      a_.OpAnd(temp_z_dest,
               dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
               dxbc::Src::LU(1 << j));

      // Check if the sample is covered.
      // temp.z = free.
      a_.OpIf(true, temp_z_src);

      // Only temp.xy are used at this point (containing the packed color from
      // the shader if not blending).

      // ***********************************************************************
      // Color sample raster operation.
      // ***********************************************************************

      // ***********************************************************************
      // Checking if color loading must be done - if any component needs to be
      // kept or if blending is enabled.
      // ***********************************************************************

      // Get if need to keep any components to temp.z.
      // temp.z = whether any components must be kept (OR of keep masks).
      a_.OpOr(temp_z_dest, keep_mask_src.SelectFromSwizzled(0),
              keep_mask_src.SelectFromSwizzled(1));
      // Blending isn't done if it's 1 * source + 0 * destination. But since the
      // previous color also needs to be loaded if any original components need
      // to be kept, force the blend control to something with blending in this
      // case in temp.z.
      // temp.z = blending mode used to check if need to load.
      a_.OpMovC(temp_z_dest, temp_z_src, dxbc::Src::LU(0),
                rt_blend_factors_ops_src);
      // Get if the blend control register requires loading the color to temp.z.
      // temp.z = whether need to load the color.
      a_.OpINE(temp_z_dest, temp_z_src, dxbc::Src::LU(0x00010001));
      // Check if need to do something with the previous color.
      // temp.z = free.
      a_.OpIf(true, temp_z_src);
      {
        // *********************************************************************
        // Loading the previous color to temp.zw.
        // *********************************************************************

        // Load the 32bpp color, or the lower 32 bits of the 64bpp color, to
        // temp.z.
        // temp.z = 32-bit packed color or lower 32 bits of the packed color.
        if (uav_index_edram_ == kBindingIndexUnallocated) {
          uav_index_edram_ = uav_count_++;
        }
        a_.OpLdUAVTyped(
            temp_z_dest,
            dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY), 1,
            dxbc::Src::U(uav_index_edram_, uint32_t(UAVRegister::kEdram),
                         dxbc::Src::kXXXX));
        // Get if the format is 64bpp to temp.w.
        // temp.w = whether the render target is 64bpp.
        a_.OpAnd(temp_w_dest, rt_format_flags_src,
                 dxbc::Src::LU(RenderTargetCache::kPSIColorFormatFlag_64bpp));
        // Check if the format is 64bpp.
        // temp.w = free.
        a_.OpIf(true, temp_w_src);
        {
          // Get the address of the upper 32 bits of the color to temp.w.
          // temp.w = address of the upper 32 bits of the packed color.
          a_.OpIAdd(temp_w_dest,
                    dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
                    dxbc::Src::LU(1));
          // Load the upper 32 bits of the 64bpp color to temp.w.
          // temp.zw = packed destination color/alpha.
          if (uav_index_edram_ == kBindingIndexUnallocated) {
            uav_index_edram_ = uav_count_++;
          }
          a_.OpLdUAVTyped(
              temp_w_dest, temp_w_src, 1,
              dxbc::Src::U(uav_index_edram_, uint32_t(UAVRegister::kEdram),
                           dxbc::Src::kXXXX));
        }
        // The color is 32bpp.
        a_.OpElse();
        {
          // Break register dependency in temp.w if the color is 32bpp.
          // temp.zw = packed destination color/alpha.
          a_.OpMov(temp_w_dest, dxbc::Src::LU(0));
        }
        // Close the color format check.
        a_.OpEndIf();

        uint32_t color_temp = PushSystemTemp();
        dxbc::Dest color_temp_rgb_dest(dxbc::Dest::R(color_temp, 0b0111));
        dxbc::Dest color_temp_a_dest(dxbc::Dest::R(color_temp, 0b1000));
        dxbc::Src color_temp_src(dxbc::Src::R(color_temp));
        dxbc::Src color_temp_a_src(dxbc::Src::R(color_temp, dxbc::Src::kWWWW));

        // Get if blending is enabled to color_temp.x.
        // color_temp.x = whether blending is enabled.
        a_.OpINE(dxbc::Dest::R(color_temp, 0b0001), rt_blend_factors_ops_src,
                 dxbc::Src::LU(0x00010001));
        // Check if need to blend.
        // color_temp.x = free.
        a_.OpIf(true, dxbc::Src::R(color_temp, dxbc::Src::kXXXX));
        {
          // Now, when blending is enabled, temp.xy are used as scratch since
          // the color is packed after blending.

          // Unpack the destination color to color_temp, using temp.xy as temps.
          // The destination color never needs clamping because out-of-range
          // values can't be loaded.
          // color_temp.xyzw = destination color/alpha.
          ROV_UnpackColor(i, temp, 2, color_temp, temp, 0, temp, 1);

          // *******************************************************************
          // Color blending.
          // *******************************************************************

          // Extract the color min/max bit to temp.x.
          // temp.x = whether min/max should be used for color.
          a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                   dxbc::Src::LU(1 << (5 + 1)));
          // Check if need to do blend the color with factors.
          // temp.x = free.
          a_.OpIf(false, temp_x_src);
          {
            uint32_t blend_src_temp = PushSystemTemp();
            dxbc::Dest blend_src_temp_rgb_dest(
                dxbc::Dest::R(blend_src_temp, 0b0111));
            dxbc::Src blend_src_temp_src(dxbc::Src::R(blend_src_temp));

            // Extract the source color factor to temp.x.
            // temp.x = source color factor index.
            a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                     dxbc::Src::LU((1 << 5) - 1));
            // Check if the source color factor is not zero - if it is, the
            // source must be ignored completely, and Infinity and NaN in it
            // shouldn't affect blending.
            a_.OpIf(true, temp_x_src);
            {
              // Open the switch for choosing the source color blend factor.
              // temp.x = free.
              a_.OpSwitch(temp_x_src);
              // Write the source color factor to blend_src_temp.xyz.
              // blend_src_temp.xyz = unclamped source color factor.
              ROV_HandleColorBlendFactorCases(system_temps_color_[i],
                                              color_temp, blend_src_temp);
              // Close the source color factor switch.
              a_.OpEndSwitch();
              // Get if the render target color is fixed-point and the source
              // color factor needs clamping to temp.x.
              // temp.x = whether color is fixed-point.
              a_.OpAnd(
                  temp_x_dest, rt_format_flags_src,
                  dxbc::Src::LU(
                      RenderTargetCache::kPSIColorFormatFlag_FixedPointColor));
              // Check if the source color factor needs clamping.
              a_.OpIf(true, temp_x_src);
              {
                // Clamp the source color factor in blend_src_temp.xyz.
                // blend_src_temp.xyz = source color factor.
                a_.OpMax(blend_src_temp_rgb_dest, blend_src_temp_src,
                         rt_clamp_vec_src.Select(0));
                a_.OpMin(blend_src_temp_rgb_dest, blend_src_temp_src,
                         rt_clamp_vec_src.Select(2));
              }
              // Close the source color factor clamping check.
              a_.OpEndIf();
              // Apply the factor to the source color.
              // blend_src_temp.xyz = unclamped source color part without
              //                      addition sign.
              a_.OpMul(blend_src_temp_rgb_dest,
                       dxbc::Src::R(system_temps_color_[i]),
                       blend_src_temp_src);
              // Check if the source color part needs clamping after the
              // multiplication.
              // temp.x = free.
              a_.OpIf(true, temp_x_src);
              {
                // Clamp the source color part.
                // blend_src_temp.xyz = source color part without addition sign.
                a_.OpMax(blend_src_temp_rgb_dest, blend_src_temp_src,
                         rt_clamp_vec_src.Select(0));
                a_.OpMin(blend_src_temp_rgb_dest, blend_src_temp_src,
                         rt_clamp_vec_src.Select(2));
              }
              // Close the source color part clamping check.
              a_.OpEndIf();
              // Extract the source color sign to temp.x.
              // temp.x = source color sign as zero for 1 and non-zero for -1.
              a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                       dxbc::Src::LU(1 << (5 + 2)));
              // Apply the source color sign.
              // blend_src_temp.xyz = source color part.
              // temp.x = free.
              a_.OpMovC(blend_src_temp_rgb_dest, temp_x_src,
                        -blend_src_temp_src, blend_src_temp_src);
            }
            // The source color factor is zero.
            a_.OpElse();
            {
              // Write zero to the source color part.
              // blend_src_temp.xyz = source color part.
              // temp.x = free.
              a_.OpMov(blend_src_temp_rgb_dest, dxbc::Src::LF(0.0f));
            }
            // Close the source color factor zero check.
            a_.OpEndIf();

            // Extract the destination color factor to temp.x.
            // temp.x = destination color factor index.
            a_.OpUBFE(temp_x_dest, dxbc::Src::LU(5), dxbc::Src::LU(8),
                      rt_blend_factors_ops_src);
            // Check if the destination color factor is not zero.
            a_.OpIf(true, temp_x_src);
            {
              uint32_t blend_dest_factor_temp = PushSystemTemp();
              dxbc::Src blend_dest_factor_temp_src(
                  dxbc::Src::R(blend_dest_factor_temp));
              // Open the switch for choosing the destination color blend
              // factor.
              // temp.x = free.
              a_.OpSwitch(temp_x_src);
              // Write the destination color factor to
              // blend_dest_factor_temp.xyz.
              // blend_dest_factor_temp.xyz = unclamped destination color
              //                              factor.
              ROV_HandleColorBlendFactorCases(
                  system_temps_color_[i], color_temp, blend_dest_factor_temp);
              // Close the destination color factor switch.
              a_.OpEndSwitch();
              // Get if the render target color is fixed-point and the
              // destination color factor needs clamping to temp.x.
              // temp.x = whether color is fixed-point.
              a_.OpAnd(
                  temp_x_dest, rt_format_flags_src,
                  dxbc::Src::LU(
                      RenderTargetCache::kPSIColorFormatFlag_FixedPointColor));
              // Check if the destination color factor needs clamping.
              a_.OpIf(true, temp_x_src);
              {
                // Clamp the destination color factor in
                // blend_dest_factor_temp.xyz.
                // blend_dest_factor_temp.xyz = destination color factor.
                a_.OpMax(dxbc::Dest::R(blend_dest_factor_temp, 0b0111),
                         blend_dest_factor_temp_src,
                         rt_clamp_vec_src.Select(0));
                a_.OpMin(dxbc::Dest::R(blend_dest_factor_temp, 0b0111),
                         blend_dest_factor_temp_src,
                         rt_clamp_vec_src.Select(2));
              }
              // Close the destination color factor clamping check.
              a_.OpEndIf();
              // Apply the factor to the destination color in color_temp.xyz.
              // color_temp.xyz = unclamped destination color part without
              //                  addition sign.
              // blend_dest_temp.xyz = free.
              a_.OpMul(color_temp_rgb_dest, color_temp_src,
                       blend_dest_factor_temp_src);
              // Release blend_dest_factor_temp.
              PopSystemTemp();
              // Check if the destination color part needs clamping after the
              // multiplication.
              // temp.x = free.
              a_.OpIf(true, temp_x_src);
              {
                // Clamp the destination color part.
                // color_temp.xyz = destination color part without addition
                // sign.
                a_.OpMax(color_temp_rgb_dest, color_temp_src,
                         rt_clamp_vec_src.Select(0));
                a_.OpMin(color_temp_rgb_dest, color_temp_src,
                         rt_clamp_vec_src.Select(2));
              }
              // Close the destination color part clamping check.
              a_.OpEndIf();
              // Extract the destination color sign to temp.x.
              // temp.x = destination color sign as zero for 1 and non-zero for
              //          -1.
              a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                       dxbc::Src::LU(1 << 5));
              // Select the sign for destination multiply-add as 1.0 or -1.0 to
              // temp.x.
              // temp.x = destination color sign as float.
              a_.OpMovC(temp_x_dest, temp_x_src, dxbc::Src::LF(-1.0f),
                        dxbc::Src::LF(1.0f));
              // Perform color blending to color_temp.xyz.
              // color_temp.xyz = unclamped blended color.
              // blend_src_temp.xyz = free.
              // temp.x = free.
              a_.OpMAd(color_temp_rgb_dest, color_temp_src, temp_x_src,
                       blend_src_temp_src);
            }
            // The destination color factor is zero.
            a_.OpElse();
            {
              // Write the source color part without applying the destination
              // color.
              // color_temp.xyz = unclamped blended color.
              // blend_src_temp.xyz = free.
              // temp.x = free.
              a_.OpMov(color_temp_rgb_dest, blend_src_temp_src);
            }
            // Close the destination color factor zero check.
            a_.OpEndIf();

            // Release blend_src_temp.
            PopSystemTemp();

            // Clamp the color in color_temp.xyz before packing.
            // color_temp.xyz = blended color.
            a_.OpMax(color_temp_rgb_dest, color_temp_src,
                     rt_clamp_vec_src.Select(0));
            a_.OpMin(color_temp_rgb_dest, color_temp_src,
                     rt_clamp_vec_src.Select(2));
          }
          // Need to do min/max for color.
          a_.OpElse();
          {
            // Extract the color min (0) or max (1) bit to temp.x
            // temp.x = whether min or max should be used for color.
            a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                     dxbc::Src::LU(1 << 5));
            // Check if need to do min or max for color.
            // temp.x = free.
            a_.OpIf(true, temp_x_src);
            {
              // Choose max of the colors without applying the factors to
              // color_temp.xyz.
              // color_temp.xyz = blended color.
              a_.OpMax(color_temp_rgb_dest,
                       dxbc::Src::R(system_temps_color_[i]), color_temp_src);
            }
            // Need to do min.
            a_.OpElse();
            {
              // Choose min of the colors without applying the factors to
              // color_temp.xyz.
              // color_temp.xyz = blended color.
              a_.OpMin(color_temp_rgb_dest,
                       dxbc::Src::R(system_temps_color_[i]), color_temp_src);
            }
            // Close the min or max check.
            a_.OpEndIf();
          }
          // Close the color factor blending or min/max check.
          a_.OpEndIf();

          // *******************************************************************
          // Alpha blending.
          // *******************************************************************

          // Extract the alpha min/max bit to temp.x.
          // temp.x = whether min/max should be used for alpha.
          a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                   dxbc::Src::LU(1 << (21 + 1)));
          // Check if need to do blend the color with factors.
          // temp.x = free.
          a_.OpIf(false, temp_x_src);
          {
            // Extract the source alpha factor to temp.x.
            // temp.x = source alpha factor index.
            a_.OpUBFE(temp_x_dest, dxbc::Src::LU(5), dxbc::Src::LU(16),
                      rt_blend_factors_ops_src);
            // Check if the source alpha factor is not zero.
            a_.OpIf(true, temp_x_src);
            {
              // Open the switch for choosing the source alpha blend factor.
              // temp.x = free.
              a_.OpSwitch(temp_x_src);
              // Write the source alpha factor to temp.x.
              // temp.x = unclamped source alpha factor.
              ROV_HandleAlphaBlendFactorCases(system_temps_color_[i],
                                              color_temp, temp, 0);
              // Close the source alpha factor switch.
              a_.OpEndSwitch();
              // Get if the render target alpha is fixed-point and the source
              // alpha factor needs clamping to temp.y.
              // temp.y = whether alpha is fixed-point.
              a_.OpAnd(
                  temp_y_dest, rt_format_flags_src,
                  dxbc::Src::LU(
                      RenderTargetCache::kPSIColorFormatFlag_FixedPointAlpha));
              // Check if the source alpha factor needs clamping.
              a_.OpIf(true, temp_y_src);
              {
                // Clamp the source alpha factor in temp.x.
                // temp.x = source alpha factor.
                a_.OpMax(temp_x_dest, temp_x_src, rt_clamp_vec_src.Select(1));
                a_.OpMin(temp_x_dest, temp_x_src, rt_clamp_vec_src.Select(3));
              }
              // Close the source alpha factor clamping check.
              a_.OpEndIf();
              // Apply the factor to the source alpha.
              // temp.x = unclamped source alpha part without addition sign.
              a_.OpMul(temp_x_dest,
                       dxbc::Src::R(system_temps_color_[i], dxbc::Src::kWWWW),
                       temp_x_src);
              // Check if the source alpha part needs clamping after the
              // multiplication.
              // temp.y = free.
              a_.OpIf(true, temp_y_src);
              {
                // Clamp the source alpha part.
                // temp.x = source alpha part without addition sign.
                a_.OpMax(temp_x_dest, temp_x_src, rt_clamp_vec_src.Select(1));
                a_.OpMin(temp_x_dest, temp_x_src, rt_clamp_vec_src.Select(3));
              }
              // Close the source alpha part clamping check.
              a_.OpEndIf();
              // Extract the source alpha sign to temp.y.
              // temp.y = source alpha sign as zero for 1 and non-zero for -1.
              a_.OpAnd(temp_y_dest, rt_blend_factors_ops_src,
                       dxbc::Src::LU(1 << (21 + 2)));
              // Apply the source alpha sign.
              // temp.x = source alpha part.
              a_.OpMovC(temp_x_dest, temp_y_src, -temp_x_src, temp_x_src);
            }
            // The source alpha factor is zero.
            a_.OpElse();
            {
              // Write zero to the source alpha part.
              // temp.x = source alpha part.
              a_.OpMov(temp_x_dest, dxbc::Src::LF(0.0f));
            }
            // Close the source alpha factor zero check.
            a_.OpEndIf();

            // Extract the destination alpha factor to temp.y.
            // temp.y = destination alpha factor index.
            a_.OpUBFE(temp_y_dest, dxbc::Src::LU(5), dxbc::Src::LU(24),
                      rt_blend_factors_ops_src);
            // Check if the destination alpha factor is not zero.
            a_.OpIf(true, temp_y_src);
            {
              // Open the switch for choosing the destination alpha blend
              // factor.
              // temp.y = free.
              a_.OpSwitch(temp_y_src);
              // Write the destination alpha factor to temp.y.
              // temp.y = unclamped destination alpha factor.
              ROV_HandleAlphaBlendFactorCases(system_temps_color_[i],
                                              color_temp, temp, 1);
              // Close the destination alpha factor switch.
              a_.OpEndSwitch();
              // Get if the render target alpha is fixed-point and the
              // destination alpha factor needs clamping.
              // alpha_is_fixed_temp.x = whether alpha is fixed-point.
              uint32_t alpha_is_fixed_temp = PushSystemTemp();
              a_.OpAnd(
                  dxbc::Dest::R(alpha_is_fixed_temp, 0b0001),
                  rt_format_flags_src,
                  dxbc::Src::LU(
                      RenderTargetCache::kPSIColorFormatFlag_FixedPointAlpha));
              // Check if the destination alpha factor needs clamping.
              a_.OpIf(true,
                      dxbc::Src::R(alpha_is_fixed_temp, dxbc::Src::kXXXX));
              {
                // Clamp the destination alpha factor in temp.y.
                // temp.y = destination alpha factor.
                a_.OpMax(temp_y_dest, temp_y_src, rt_clamp_vec_src.Select(1));
                a_.OpMin(temp_y_dest, temp_y_src, rt_clamp_vec_src.Select(3));
              }
              // Close the destination alpha factor clamping check.
              a_.OpEndIf();
              // Apply the factor to the destination alpha in color_temp.w.
              // color_temp.w = unclamped destination alpha part without
              //                addition sign.
              a_.OpMul(color_temp_a_dest, color_temp_a_src, temp_y_src);
              // Check if the destination alpha part needs clamping after the
              // multiplication.
              // alpha_is_fixed_temp.x = free.
              a_.OpIf(true,
                      dxbc::Src::R(alpha_is_fixed_temp, dxbc::Src::kXXXX));
              // Release alpha_is_fixed_temp.
              PopSystemTemp();
              {
                // Clamp the destination alpha part.
                // color_temp.w = destination alpha part without addition sign.
                a_.OpMax(color_temp_a_dest, color_temp_a_src,
                         rt_clamp_vec_src.Select(1));
                a_.OpMin(color_temp_a_dest, color_temp_a_src,
                         rt_clamp_vec_src.Select(3));
              }
              // Close the destination alpha factor clamping check.
              a_.OpEndIf();
              // Extract the destination alpha sign to temp.y.
              // temp.y = destination alpha sign as zero for 1 and non-zero for
              //          -1.
              a_.OpAnd(temp_y_dest, rt_blend_factors_ops_src,
                       dxbc::Src::LU(1 << 21));
              // Select the sign for destination multiply-add as 1.0 or -1.0 to
              // temp.y.
              // temp.y = destination alpha sign as float.
              a_.OpMovC(temp_y_dest, temp_y_src, dxbc::Src::LF(-1.0f),
                        dxbc::Src::LF(1.0f));
              // Perform alpha blending to color_temp.w.
              // color_temp.w = unclamped blended alpha.
              // temp.xy = free.
              a_.OpMAd(color_temp_a_dest, color_temp_a_src, temp_y_src,
                       temp_x_src);
            }
            // The destination alpha factor is zero.
            a_.OpElse();
            {
              // Write the source alpha part without applying the destination
              // alpha.
              // color_temp.w = unclamped blended alpha.
              // temp.xy = free.
              a_.OpMov(color_temp_a_dest, temp_x_src);
            }
            // Close the destination alpha factor zero check.
            a_.OpEndIf();

            // Clamp the alpha in color_temp.w before packing.
            // color_temp.w = blended alpha.
            a_.OpMax(color_temp_a_dest, color_temp_a_src,
                     rt_clamp_vec_src.Select(1));
            a_.OpMin(color_temp_a_dest, color_temp_a_src,
                     rt_clamp_vec_src.Select(3));
          }
          // Need to do min/max for alpha.
          a_.OpElse();
          {
            // Extract the alpha min (0) or max (1) bit to temp.x.
            // temp.x = whether min or max should be used for alpha.
            a_.OpAnd(temp_x_dest, rt_blend_factors_ops_src,
                     dxbc::Src::LU(1 << 21));
            // Check if need to do min or max for alpha.
            // temp.x = free.
            a_.OpIf(true, temp_x_src);
            {
              // Choose max of the alphas without applying the factors to
              // color_temp.w.
              // color_temp.w = blended alpha.
              a_.OpMax(color_temp_a_dest,
                       dxbc::Src::R(system_temps_color_[i], dxbc::Src::kWWWW),
                       color_temp_a_src);
            }
            // Need to do min.
            a_.OpElse();
            {
              // Choose min of the alphas without applying the factors to
              // color_temp.w.
              // color_temp.w = blended alpha.
              a_.OpMin(color_temp_a_dest,
                       dxbc::Src::R(system_temps_color_[i], dxbc::Src::kWWWW),
                       color_temp_a_src);
            }
            // Close the min or max check.
            a_.OpEndIf();
          }
          // Close the alpha factor blending or min/max check.
          a_.OpEndIf();

          // Pack the new color/alpha to temp.xy.
          // temp.xy = packed new color/alpha.
          uint32_t color_pack_temp = PushSystemTemp();
          ROV_PackPreClampedColor(i, color_temp, temp, 0, color_pack_temp, 0,
                                  color_pack_temp, 1);
          // Release color_pack_temp.
          PopSystemTemp();
        }
        // Close the blending check.
        a_.OpEndIf();

        // *********************************************************************
        // Write mask application
        // *********************************************************************

        // Apply the keep mask to the previous packed color/alpha in temp.zw.
        // temp.zw = masked packed old color/alpha.
        a_.OpAnd(dxbc::Dest::R(temp, 0b1100), dxbc::Src::R(temp),
                 keep_mask_src.SwizzleSwizzled(0b0100 << 4));
        // Invert the keep mask into color_temp.xy.
        // color_temp.xy = inverted keep mask (write mask).
        a_.OpNot(dxbc::Dest::R(color_temp, 0b0011), keep_mask_src);
        // Release color_temp.
        PopSystemTemp();
        // Apply the write mask to the new color/alpha in temp.xy.
        // temp.xy = masked packed new color/alpha.
        a_.OpAnd(dxbc::Dest::R(temp, 0b0011), dxbc::Src::R(temp),
                 dxbc::Src::R(color_temp));
        // Combine the masked colors into temp.xy.
        // temp.xy = packed resulting color/alpha.
        // temp.zw = free.
        a_.OpOr(dxbc::Dest::R(temp, 0b0011), dxbc::Src::R(temp),
                dxbc::Src::R(temp, 0b1110));
      }
      // Close the previous color load check.
      a_.OpEndIf();

      // ***********************************************************************
      // Writing the color
      // ***********************************************************************

      // Store the 32bpp color or the lower 32 bits of the 64bpp color.
      if (uav_index_edram_ == kBindingIndexUnallocated) {
        uav_index_edram_ = uav_count_++;
      }
      a_.OpStoreUAVTyped(
          dxbc::Dest::U(uav_index_edram_, uint32_t(UAVRegister::kEdram)),
          dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY), 1,
          temp_x_src);
      // Get if the format is 64bpp to temp.z.
      // temp.z = whether the render target is 64bpp.
      a_.OpAnd(temp_z_dest, rt_format_flags_src,
               dxbc::Src::LU(RenderTargetCache::kPSIColorFormatFlag_64bpp));
      // Check if the format is 64bpp.
      // temp.z = free.
      a_.OpIf(true, temp_z_src);
      {
        // Get the address of the upper 32 bits of the color to temp.z (can't
        // use temp.x because components when not blending, packing is done once
        // for all samples, so it has to be preserved).
        a_.OpIAdd(temp_z_dest,
                  dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
                  dxbc::Src::LU(1));
        // Store the upper 32 bits of the 64bpp color.
        if (uav_index_edram_ == kBindingIndexUnallocated) {
          uav_index_edram_ = uav_count_++;
        }
        a_.OpStoreUAVTyped(
            dxbc::Dest::U(uav_index_edram_, uint32_t(UAVRegister::kEdram)),
            temp_z_src, 1, temp_y_src);
      }
      // Close the 64bpp conditional.
      a_.OpEndIf();

      // ***********************************************************************
      // End of color sample raster operation.
      // ***********************************************************************

      // Close the sample covered check.
      a_.OpEndIf();

      // Go to the next sample (samples are at +0, +(80*scale_x), +dwpp,
      // +(80*scale_x+dwpp), so need to do +(80*scale_x), -(80*scale_x-dwpp),
      // +(80*scale_x) and -(80*scale_x+dwpp) after each sample).
      // Though no need to do this for the last sample as for the next render
      // target, the address will be recalculated.
      if (j < 3) {
        if (j & 1) {
          // temp.z = whether the render target is 64bpp.
          a_.OpAnd(temp_z_dest, rt_format_flags_src,
                   dxbc::Src::LU(RenderTargetCache::kPSIColorFormatFlag_64bpp));
          // temp.z = offset from the current sample to the next.
          a_.OpMovC(temp_z_dest, temp_z_src,
                    dxbc::Src::LI(-int32_t(tile_width) + 2 * (2 - int32_t(j))),
                    dxbc::Src::LI(-int32_t(tile_width) + (2 - int32_t(j))));
          // temp.z = free.
          a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
                    dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
                    temp_z_src);
        } else {
          a_.OpIAdd(dxbc::Dest::R(system_temp_rov_params_, 0b0010),
                    dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kYYYY),
                    dxbc::Src::LU(tile_width));
        }
      }
    }

    // Close the render target write check.
    a_.OpEndIf();
  }

  // Release temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader() {
  if (is_depth_only_pixel_shader_) {
    // The depth-only shader only needs to do the depth test and to write the
    // depth to the ROV.
    if (edram_rov_used_) {
      CompletePixelShader_WriteToROV();
    }
    return;
  }

  if (current_shader().writes_color_target(0) &&
      !IsForceEarlyDepthStencilGlobalFlagEnabled()) {
    if (edram_rov_used_) {
      // Check if the render target 0 was written to on the execution path.
      uint32_t rt_0_written_temp = PushSystemTemp();
      a_.OpAnd(dxbc::Dest::R(rt_0_written_temp, 0b0001),
               dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
               dxbc::Src::LU(1 << 8));
      a_.OpIf(true, dxbc::Src::R(rt_0_written_temp, dxbc::Src::kXXXX));
      // Release rt_0_written_temp.
      PopSystemTemp();
    }

    // Alpha test.
    // X - mask, then masked result (SGPR for loading, VGPR for masking).
    // Y - operation result (SGPR for mask operations, VGPR for alpha
    //     operations).
    uint32_t alpha_test_temp = PushSystemTemp();
    dxbc::Dest alpha_test_mask_dest(dxbc::Dest::R(alpha_test_temp, 0b0001));
    dxbc::Src alpha_test_mask_src(
        dxbc::Src::R(alpha_test_temp, dxbc::Src::kXXXX));
    dxbc::Dest alpha_test_op_dest(dxbc::Dest::R(alpha_test_temp, 0b0010));
    dxbc::Src alpha_test_op_src(
        dxbc::Src::R(alpha_test_temp, dxbc::Src::kYYYY));
    dxbc::Dest alpha_test_fuzzy_diff_dest(
        dxbc::Dest::R(alpha_test_temp, 0b0100));
    dxbc::Src alpha_test_fuzzy_diff_src(
        dxbc::Src::R(alpha_test_temp, dxbc::Src::kZZZZ));
    // Extract the comparison mask to check if the test needs to be done at all.
    // Don't care about flow control being somewhat dynamic - early Z is forced
    // using a special version of the shader anyway.
    a_.OpUBFE(alpha_test_mask_dest, dxbc::Src::LU(3),
              dxbc::Src::LU(kSysFlag_AlphaPassIfLess_Shift),
              LoadFlagsSystemConstant());
    // Compare the mask to ALWAYS to check if the test shouldn't be done (will
    // pass even for NaNs, though the expected behavior in this case hasn't been
    // checked, but let's assume this means "always", not "less, equal or
    // greater".
    // TODO(Triang3l): Check how alpha test works with NaN on Direct3D 9.
    a_.OpINE(alpha_test_op_dest, alpha_test_mask_src,
             dxbc::Src::LU(uint32_t(xenos::CompareFunction::kAlways)));
    // Don't do the test if the mode is "always".
    a_.OpIf(true, alpha_test_op_src);
    {
      // Do the test.
      dxbc::Src alpha_src(
          dxbc::Src::R(system_temps_color_[0], dxbc::Src::kWWWW));
      dxbc::Src alpha_test_reference_src(LoadSystemConstant(
          SystemConstants::Index::kAlphaTestReference,
          offsetof(SystemConstants, alpha_test_reference), dxbc::Src::kXXXX));
      // Epsilon for alpha checks
      dxbc::Src fuzzy_epsilon = dxbc::Src::LF(1e-3f);
      // Handle "not equal" specially (specifically as "not equal" so it's true
      // for NaN, not "less or greater" which is false for NaN).
      a_.OpIEq(alpha_test_op_dest, alpha_test_mask_src,
               dxbc::Src::LU(uint32_t(xenos::CompareFunction::kNotEqual)));
      a_.OpIf(true, alpha_test_op_src);
      {
        if (cvars::use_fuzzy_alpha_epsilon) {
          a_.OpAdd(alpha_test_fuzzy_diff_dest, alpha_src,
                   -alpha_test_reference_src);
          // Check if distance to desired value is less then eps (false for NaN)
          // and write the negated result
          a_.OpLT(alpha_test_mask_dest, alpha_test_fuzzy_diff_src.Abs(),
                  fuzzy_epsilon);
          a_.OpNot(alpha_test_mask_dest, alpha_test_mask_src);
        } else {
          a_.OpNE(alpha_test_mask_dest, alpha_src, alpha_test_reference_src);
        }
      }
      a_.OpElse();
      {
        if (cvars::use_fuzzy_alpha_epsilon) {
          // Less than.
          a_.OpAdd(alpha_test_fuzzy_diff_dest, alpha_src, -fuzzy_epsilon);
          a_.OpLT(alpha_test_op_dest, alpha_test_fuzzy_diff_src,
                  alpha_test_reference_src);
          a_.OpOr(alpha_test_op_dest, alpha_test_op_src,
                  dxbc::Src::LU(~uint32_t(1 << 0)));
          a_.OpAnd(alpha_test_mask_dest, alpha_test_mask_src,
                   alpha_test_op_src);
          // "Equals" to.
          a_.OpAdd(alpha_test_fuzzy_diff_dest, alpha_src,
                   -alpha_test_reference_src);
          a_.OpLT(alpha_test_op_dest, alpha_test_fuzzy_diff_src.Abs(),
                  fuzzy_epsilon);
          a_.OpOr(alpha_test_op_dest, alpha_test_op_src,
                  dxbc::Src::LU(~uint32_t(1 << 1)));
          a_.OpAnd(alpha_test_mask_dest, alpha_test_mask_src,
                   alpha_test_op_src);
          // Greater than.
          a_.OpAdd(alpha_test_fuzzy_diff_dest, alpha_src, fuzzy_epsilon);
          a_.OpLT(alpha_test_op_dest, alpha_test_reference_src,
                  alpha_test_fuzzy_diff_src);
          a_.OpOr(alpha_test_op_dest, alpha_test_op_src,
                  dxbc::Src::LU(~uint32_t(1 << 2)));
          a_.OpAnd(alpha_test_mask_dest, alpha_test_mask_src,
                   alpha_test_op_src);
        } else {
          // Less than.
          a_.OpLT(alpha_test_op_dest, alpha_src, alpha_test_reference_src);
          a_.OpOr(alpha_test_op_dest, alpha_test_op_src,
                  dxbc::Src::LU(~uint32_t(1 << 0)));
          a_.OpAnd(alpha_test_mask_dest, alpha_test_mask_src,
                   alpha_test_op_src);
          a_.OpEq(alpha_test_op_dest, alpha_src, alpha_test_reference_src);
          a_.OpOr(alpha_test_op_dest, alpha_test_op_src,
                  dxbc::Src::LU(~uint32_t(1 << 1)));
          a_.OpAnd(alpha_test_mask_dest, alpha_test_mask_src,
                   alpha_test_op_src);
          // Greater than.
          a_.OpLT(alpha_test_op_dest, alpha_test_reference_src, alpha_src);
          a_.OpOr(alpha_test_op_dest, alpha_test_op_src,
                  dxbc::Src::LU(~uint32_t(1 << 2)));
          a_.OpAnd(alpha_test_mask_dest, alpha_test_mask_src,
                   alpha_test_op_src);
        }
      }
      // Close the "not equal" check.
      a_.OpEndIf();
      // Discard the pixel if it has failed the test.
      if (edram_rov_used_) {
        a_.OpRetC(false, alpha_test_mask_src);
      } else {
        a_.OpDiscard(false, alpha_test_mask_src);
      }
    }
    // Close the "not always" check.
    a_.OpEndIf();
    // Release alpha_test_temp.
    PopSystemTemp();

    // Discard samples with alpha to coverage.
    CompletePixelShader_AlphaToMask();

    if (edram_rov_used_) {
      // Close the render target 0 written check.
      a_.OpEndIf();
    }
  }

  // Write the values to the render targets. Not applying the exponent bias yet
  // because the original 0 to 1 alpha value is needed for alpha to coverage,
  // which is done differently for ROV and RTV/DSV.
  if (edram_rov_used_) {
    CompletePixelShader_WriteToROV();
  } else {
    CompletePixelShader_WriteToRTVs();
    CompletePixelShader_DSV_DepthTo24Bit();
  }
}

void DxbcShaderTranslator::PreClampedFloat32To7e3(
    dxbc::Assembler& a, uint32_t f10_temp, uint32_t f10_temp_component,
    uint32_t f32_temp, uint32_t f32_temp_component, uint32_t temp_temp,
    uint32_t temp_temp_component) {
  assert_true(temp_temp != f10_temp ||
              temp_temp_component != f10_temp_component);
  assert_true(temp_temp != f32_temp ||
              temp_temp_component != f32_temp_component);
  // Source and destination may be the same.
  dxbc::Dest f10_dest(dxbc::Dest::R(f10_temp, 1 << f10_temp_component));
  dxbc::Src f10_src(dxbc::Src::R(f10_temp).Select(f10_temp_component));
  dxbc::Src f32_src(dxbc::Src::R(f32_temp).Select(f32_temp_component));
  dxbc::Dest temp_dest(dxbc::Dest::R(temp_temp, 1 << temp_temp_component));
  dxbc::Src temp_src(dxbc::Src::R(temp_temp).Select(temp_temp_component));

  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // Assuming the color is already clamped to [0, 31.875].

  // Check if the number is too small to be represented as normalized 7e3.
  // temp = f32 < 2^-2
  a.OpULT(temp_dest, f32_src, dxbc::Src::LU(0x3E800000));
  // Handle denormalized numbers separately.
  a.OpIf(true, temp_src);
  {
    // temp = f32 >> 23
    a.OpUShR(temp_dest, f32_src, dxbc::Src::LU(23));
    // temp = 125 - (f32 >> 23)
    a.OpIAdd(temp_dest, dxbc::Src::LI(125), -temp_src);
    // Don't allow the shift to overflow, since in DXBC the lower 5 bits of the
    // shift amount are used.
    // temp = min(125 - (f32 >> 23), 24)
    a.OpUMin(temp_dest, temp_src, dxbc::Src::LU(24));
    // biased_f32 = (f32 & 0x7FFFFF) | 0x800000
    a.OpBFI(f10_dest, dxbc::Src::LU(9), dxbc::Src::LU(23), dxbc::Src::LU(1),
            f32_src);
    // biased_f32 = ((f32 & 0x7FFFFF) | 0x800000) >> min(125 - (f32 >> 23), 24)
    a.OpUShR(f10_dest, f10_src, temp_src);
  }
  // Not denormalized?
  a.OpElse();
  {
    // Bias the exponent.
    // biased_f32 = f32 + (-124 << 23)
    // (left shift of a negative value is undefined behavior)
    a.OpIAdd(f10_dest, f32_src, dxbc::Src::LU(0xC2000000u));
  }
  // Close the denormal check.
  a.OpEndIf();
  // Build the 7e3 number.
  // temp = (biased_f32 >> 16) & 1
  a.OpUBFE(temp_dest, dxbc::Src::LU(1), dxbc::Src::LU(16), f10_src);
  // f10 = biased_f32 + 0x7FFF
  a.OpIAdd(f10_dest, f10_src, dxbc::Src::LU(0x7FFF));
  // f10 = biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)
  a.OpIAdd(f10_dest, f10_src, temp_src);
  // f24 = ((biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)) >> 16) & 0x3FF
  a.OpUBFE(f10_dest, dxbc::Src::LU(10), dxbc::Src::LU(16), f10_src);
}

void DxbcShaderTranslator::UnclampedFloat32To7e3(
    dxbc::Assembler& a, uint32_t f10_temp, uint32_t f10_temp_component,
    uint32_t f32_temp, uint32_t f32_temp_component, uint32_t temp_temp,
    uint32_t temp_temp_component) {
  // Source and destination might be the same or different, just like in
  // PreClampedFloat32To7e3 - clamp to the destination and use it as source.
  a.OpMax(dxbc::Dest::R(f10_temp, 1 << f10_temp_component),
          dxbc::Src::R(f32_temp).Select(f32_temp_component),
          dxbc::Src::LF(0.0f));
  a.OpMin(dxbc::Dest::R(f10_temp, 1 << f10_temp_component),
          dxbc::Src::R(f10_temp).Select(f10_temp_component),
          dxbc::Src::LF(31.875f));
  PreClampedFloat32To7e3(a, f10_temp, f10_temp_component, f10_temp,
                         f10_temp_component, temp_temp, temp_temp_component);
}

void DxbcShaderTranslator::Float7e3To32(
    dxbc::Assembler& a, const dxbc::Dest& f32, uint32_t f10_temp,
    uint32_t f10_temp_component, uint32_t f10_shift, uint32_t temp1_temp,
    uint32_t temp1_temp_component, uint32_t temp2_temp,
    uint32_t temp2_temp_component) {
  assert_true(f10_shift <= (32 - 10));
  assert_true(temp1_temp != temp2_temp ||
              temp1_temp_component != temp2_temp_component);
  // Source may be the same as temp1 or temp2.
  dxbc::Dest exponent_dest(
      dxbc::Dest::R(temp1_temp, 1 << temp1_temp_component));
  dxbc::Src exponent_src(dxbc::Src::R(temp1_temp).Select(temp1_temp_component));
  dxbc::Dest mantissa_dest(
      dxbc::Dest::R(temp2_temp, 1 << temp2_temp_component));
  dxbc::Src mantissa_src(dxbc::Src::R(temp2_temp).Select(temp2_temp_component));

  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

  if (!(f10_temp == temp1_temp && f10_temp_component == temp1_temp_component)) {
    // Unpack the exponent before the mantissa if that doesn't overwrite the
    // source.
    a.OpUBFE(exponent_dest, dxbc::Src::LU(3), dxbc::Src::LU(f10_shift + 7),
             dxbc::Src::R(f10_temp).Select(f10_temp_component));
  }
  // Unpack the mantissa.
  a.OpUBFE(mantissa_dest, dxbc::Src::LU(7), dxbc::Src::LU(f10_shift),
           dxbc::Src::R(f10_temp).Select(f10_temp_component));
  if (f10_temp == temp1_temp && f10_temp_component == temp1_temp_component) {
    // Unpack the exponent after the mantissa if doing that before the mantissa
    // would overwrite the source.
    a.OpUBFE(exponent_dest, dxbc::Src::LU(3), dxbc::Src::LU(f10_shift + 7),
             dxbc::Src::R(f10_temp).Select(f10_temp_component));
  }
  // Check if the number is denormalized.
  a.OpIf(false, exponent_src);
  {
    // Check if the number is non-zero (if the mantissa isn't zero - the
    // exponent is known to be zero at this point).
    a.OpIf(true, mantissa_src);
    {
      // Normalize the mantissa.
      // Note that HLSL firstbithigh(x) is compiled to DXBC like:
      // `x ? 31 - firstbit_hi(x) : -1`
      // (returns the index from the LSB, not the MSB, but -1 for zero too).
      // exponent = firstbit_hi(mantissa)
      a.OpFirstBitHi(exponent_dest, mantissa_src);
      // exponent = 7 - firstbithigh(mantissa)
      // Or:
      // exponent = 7 - (31 - firstbit_hi(mantissa))
      a.OpIAdd(exponent_dest, exponent_src, dxbc::Src::LI(7 - 31));
      // mantissa = mantissa << (7 - firstbithigh(mantissa))
      // AND 0x7F not needed after this - BFI will do it.
      a.OpIShL(mantissa_dest, mantissa_src, exponent_src);
      // Get the normalized exponent.
      // exponent = 1 - (7 - firstbithigh(mantissa))
      a.OpIAdd(exponent_dest, dxbc::Src::LI(1), -exponent_src);
    }
    // The number is zero.
    a.OpElse();
    {
      // Set the unbiased exponent to -124 for zero - 124 will be added later,
      // resulting in zero float32.
      a.OpMov(exponent_dest, dxbc::Src::LI(-124));
    }
    // Close the non-zero check.
    a.OpEndIf();
  }
  // Close the denormal check.
  a.OpEndIf();
  // Bias the exponent and move it to the correct location in f32.
  a.OpIMAd(exponent_dest, exponent_src, dxbc::Src::LI(1 << 23),
           dxbc::Src::LI(124 << 23));
  // Combine the mantissa and the exponent.
  a.OpBFI(f32, dxbc::Src::LU(7), dxbc::Src::LU(23 - 7), mantissa_src,
          exponent_src);
}

void DxbcShaderTranslator::PreClampedDepthTo20e4(
    dxbc::Assembler& a, uint32_t f24_temp, uint32_t f24_temp_component,
    uint32_t f32_temp, uint32_t f32_temp_component, uint32_t temp_temp,
    uint32_t temp_temp_component, bool round_to_nearest_even,
    bool remap_from_0_to_0_5) {
  assert_true(temp_temp != f24_temp ||
              temp_temp_component != f24_temp_component);
  assert_true(temp_temp != f32_temp ||
              temp_temp_component != f32_temp_component);
  // Source and destination may be the same.
  dxbc::Dest f24_dest(dxbc::Dest::R(f24_temp, 1 << f24_temp_component));
  dxbc::Src f24_src(dxbc::Src::R(f24_temp).Select(f24_temp_component));
  dxbc::Src f32_src(dxbc::Src::R(f32_temp).Select(f32_temp_component));
  dxbc::Dest temp_dest(dxbc::Dest::R(temp_temp, 1 << temp_temp_component));
  dxbc::Src temp_src(dxbc::Src::R(temp_temp).Select(temp_temp_component));

  // CFloat24 from d3dref9.dll +
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // Assuming the depth is already clamped to [0, 2) (in all places, the depth
  // is written with the saturate flag set).

  uint32_t remap_bias = uint32_t(remap_from_0_to_0_5);

  // Check if the number is too small to be represented as normalized 20e4.
  // temp = f32 < 2^-14
  a.OpULT(temp_dest, f32_src, dxbc::Src::LU(0x38800000 - (remap_bias << 23)));
  // Handle denormalized numbers separately.
  a.OpIf(true, temp_src);
  {
    // temp = f32 >> 23
    a.OpUShR(temp_dest, f32_src, dxbc::Src::LU(23));
    // temp = 113 - (f32 >> 23)
    a.OpIAdd(temp_dest, dxbc::Src::LI(113 - remap_bias), -temp_src);
    // Don't allow the shift to overflow, since in DXBC the lower 5 bits of the
    // shift amount are used (otherwise 0 becomes 8).
    // temp = min(113 - (f32 >> 23), 24)
    a.OpUMin(temp_dest, temp_src, dxbc::Src::LU(24));
    // biased_f32 = (f32 & 0x7FFFFF) | 0x800000
    a.OpBFI(f24_dest, dxbc::Src::LU(9), dxbc::Src::LU(23), dxbc::Src::LU(1),
            f32_src);
    // biased_f32 = ((f32 & 0x7FFFFF) | 0x800000) >> min(113 - (f32 >> 23), 24)
    a.OpUShR(f24_dest, f24_src, temp_src);
  }
  // Not denormalized?
  a.OpElse();
  {
    // Bias the exponent.
    // biased_f32 = f32 + (-112 << 23)
    // (left shift of a negative value is undefined behavior)
    a.OpIAdd(f24_dest, f32_src,
             dxbc::Src::LU(0xC8000000u + (remap_bias << 23)));
  }
  // Close the denormal check.
  a.OpEndIf();
  // Build the 20e4 number.
  if (round_to_nearest_even) {
    // temp = (biased_f32 >> 3) & 1
    a.OpUBFE(temp_dest, dxbc::Src::LU(1), dxbc::Src::LU(3), f24_src);
    // f24 = biased_f32 + 3
    a.OpIAdd(f24_dest, f24_src, dxbc::Src::LU(3));
    // f24 = biased_f32 + 3 + ((biased_f32 >> 3) & 1)
    a.OpIAdd(f24_dest, f24_src, temp_src);
  }
  // For rounding to the nearest even:
  // f24 = ((biased_f32 + 3 + ((biased_f32 >> 3) & 1)) >> 3) & 0xFFFFFF
  // For rounding towards zero:
  // f24 = (biased_f32 >> 3) & 0xFFFFFF
  a.OpUBFE(f24_dest, dxbc::Src::LU(24), dxbc::Src::LU(3), f24_src);
}

void DxbcShaderTranslator::Depth20e4To32(
    dxbc::Assembler& a, const dxbc::Dest& f32, uint32_t f24_temp,
    uint32_t f24_temp_component, uint32_t f24_shift, uint32_t temp1_temp,
    uint32_t temp1_temp_component, uint32_t temp2_temp,
    uint32_t temp2_temp_component, bool remap_to_0_to_0_5) {
  assert_true(f24_shift <= (32 - 24));
  assert_true(temp1_temp != temp2_temp ||
              temp1_temp_component != temp2_temp_component);
  // Source may be the same as temp1 or temp2.
  dxbc::Dest exponent_dest(
      dxbc::Dest::R(temp1_temp, 1 << temp1_temp_component));
  dxbc::Src exponent_src(dxbc::Src::R(temp1_temp).Select(temp1_temp_component));
  dxbc::Dest mantissa_dest(
      dxbc::Dest::R(temp2_temp, 1 << temp2_temp_component));
  dxbc::Src mantissa_src(dxbc::Src::R(temp2_temp).Select(temp2_temp_component));

  // CFloat24 from d3dref9.dll +
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

  uint32_t remap_bias = uint32_t(remap_to_0_to_0_5);

  if (!(f24_temp == temp1_temp && f24_temp_component == temp1_temp_component)) {
    // Unpack the exponent before the mantissa if that doesn't overwrite the
    // source.
    a.OpUBFE(exponent_dest, dxbc::Src::LU(4), dxbc::Src::LU(f24_shift + 20),
             dxbc::Src::R(f24_temp).Select(f24_temp_component));
  }
  // Unpack the mantissa.
  a.OpUBFE(mantissa_dest, dxbc::Src::LU(20), dxbc::Src::LU(f24_shift),
           dxbc::Src::R(f24_temp).Select(f24_temp_component));
  if (f24_temp == temp1_temp && f24_temp_component == temp1_temp_component) {
    // Unpack the exponent after the mantissa if doing that before the mantissa
    // would overwrite the source.
    a.OpUBFE(exponent_dest, dxbc::Src::LU(4), dxbc::Src::LU(f24_shift + 20),
             dxbc::Src::R(f24_temp).Select(f24_temp_component));
  }
  // Check if the number is denormalized.
  a.OpIf(false, exponent_src);
  {
    // Check if the number is non-zero (if the mantissa isn't zero - the
    // exponent is known to be zero at this point).
    a.OpIf(true, mantissa_src);
    {
      // Normalize the mantissa.
      // Note that HLSL firstbithigh(x) is compiled to DXBC like:
      // `x ? 31 - firstbit_hi(x) : -1`
      // (returns the index from the LSB, not the MSB, but -1 for zero too).
      // exponent = firstbit_hi(mantissa)
      a.OpFirstBitHi(exponent_dest, mantissa_src);
      // exponent = 20 - firstbithigh(mantissa)
      // Or:
      // exponent = 20 - (31 - firstbit_hi(mantissa))
      a.OpIAdd(exponent_dest, exponent_src, dxbc::Src::LI(20 - 31));
      // mantissa = mantissa << (20 - firstbithigh(mantissa))
      // AND 0xFFFFF not needed after this - BFI will do it.
      a.OpIShL(mantissa_dest, mantissa_src, exponent_src);
      // Get the normalized exponent.
      // exponent = 1 - (20 - firstbithigh(mantissa))
      a.OpIAdd(exponent_dest, dxbc::Src::LI(1), -exponent_src);
    }
    // The number is zero.
    a.OpElse();
    {
      // Set the unbiased exponent to -112 for zero - 112 will be added later
      // (taking the range remap bias into account), resulting in zero float32.
      a.OpMov(exponent_dest, dxbc::Src::LI(-int32_t(112 - remap_bias)));
    }
    // Close the non-zero check.
    a.OpEndIf();
  }
  // Close the denormal check.
  a.OpEndIf();
  // Bias the exponent and move it to the correct location in f32, and also
  // remap from guest 0...1 to host 0...0.5 if needed.
  a.OpIMAd(exponent_dest, exponent_src, dxbc::Src::LI(1 << 23),
           dxbc::Src::LI((112 - remap_bias) << 23));
  // Combine the mantissa and the exponent.
  a.OpBFI(f32, dxbc::Src::LU(20), dxbc::Src::LU(23 - 20), mantissa_src,
          exponent_src);
}

void DxbcShaderTranslator::ROV_DepthTo24Bit(uint32_t d24_temp,
                                            uint32_t d24_temp_component,
                                            uint32_t d32_temp,
                                            uint32_t d32_temp_component,
                                            uint32_t temp_temp,
                                            uint32_t temp_temp_component) {
  assert_true(temp_temp != d32_temp ||
              temp_temp_component != d32_temp_component);
  // Source and destination may be the same.

  a_.OpAnd(dxbc::Dest::R(temp_temp, 1 << temp_temp_component),
           LoadFlagsSystemConstant(), dxbc::Src::LU(kSysFlag_DepthFloat24));
  // Convert according to the format.
  a_.OpIf(true, dxbc::Src::R(temp_temp).Select(temp_temp_component));
  {
    // 20e4 conversion.
    PreClampedDepthTo20e4(a_, d24_temp, d24_temp_component, d32_temp,
                          d32_temp_component, temp_temp, temp_temp_component,
                          true, false);
  }
  a_.OpElse();
  {
    // Unorm24 conversion.
    dxbc::Dest d24_dest(dxbc::Dest::R(d24_temp, 1 << d24_temp_component));
    dxbc::Src d24_src(dxbc::Src::R(d24_temp).Select(d24_temp_component));
    a_.OpMul(d24_dest, dxbc::Src::R(d32_temp).Select(d32_temp_component),
             dxbc::Src::LF(float(0xFFFFFF)));
    // Round to the nearest even integer. This seems to be the correct way:
    // rounding towards zero gives 0xFF instead of 0x100 in clear shaders in,
    // for instance, 4D5307E6, but other clear shaders in it are also broken if
    // 0.5 is added before ftou instead of round_ne.
    a_.OpRoundNE(d24_dest, d24_src);
    // Convert to fixed-point.
    a_.OpFToU(d24_dest, d24_src);
  }
  a_.OpEndIf();
}

}  // namespace gpu
}  // namespace xe
