/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include "xenia/base/math.h"

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::ROV_GetColorFormatSystemConstants(
    ColorRenderTargetFormat format, uint32_t write_mask, float& clamp_rgb_low,
    float& clamp_alpha_low, float& clamp_rgb_high, float& clamp_alpha_high,
    uint32_t& keep_mask_low, uint32_t& keep_mask_high) {
  keep_mask_low = keep_mask_high = 0;
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0xFF) << (i * 8);
        }
      }
    } break;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0x3FF) << (i * 10);
        }
      }
      if (!(write_mask & 0b1000)) {
        keep_mask_low |= uint32_t(3) << 30;
      }
    } break;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = 31.875f;
      clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0x3FF) << (i * 10);
        }
      }
      if (!(write_mask & 0b1000)) {
        keep_mask_low |= uint32_t(3) << 30;
      }
    } break;
    case ColorRenderTargetFormat::k_16_16:
    case ColorRenderTargetFormat::k_16_16_16_16:
      // Alpha clamping affects blending source, so it's non-zero for alpha for
      // k_16_16 (the render target is fixed-point).
      clamp_rgb_low = clamp_alpha_low = -32.0f;
      clamp_rgb_high = clamp_alpha_high = 32.0f;
      if (!(write_mask & 0b0001)) {
        keep_mask_low |= 0xFFFFu;
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_low |= 0xFFFF0000u;
      }
      if (format == ColorRenderTargetFormat::k_16_16_16_16) {
        if (!(write_mask & 0b0100)) {
          keep_mask_high |= 0xFFFFu;
        }
        if (!(write_mask & 0b1000)) {
          keep_mask_high |= 0xFFFF0000u;
        }
      } else {
        write_mask &= 0b0011;
      }
      break;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      // No NaNs on the Xbox 360 GPU, though can't use the extended range with
      // f32tof16.
      clamp_rgb_low = clamp_alpha_low = -65504.0f;
      clamp_rgb_high = clamp_alpha_high = 65504.0f;
      if (!(write_mask & 0b0001)) {
        keep_mask_low |= 0xFFFFu;
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_low |= 0xFFFF0000u;
      }
      if (format == ColorRenderTargetFormat::k_16_16_16_16_FLOAT) {
        if (!(write_mask & 0b0100)) {
          keep_mask_high |= 0xFFFFu;
        }
        if (!(write_mask & 0b1000)) {
          keep_mask_high |= 0xFFFF0000u;
        }
      } else {
        write_mask &= 0b0011;
      }
      break;
    case ColorRenderTargetFormat::k_32_FLOAT:
      // No clamping - let min/max always pick the original value.
      clamp_rgb_low = clamp_alpha_low = clamp_rgb_high = clamp_alpha_high =
          std::nanf("");
      write_mask &= 0b0001;
      if (!(write_mask & 0b0001)) {
        keep_mask_low = ~uint32_t(0);
      }
      break;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      // No clamping - let min/max always pick the original value.
      clamp_rgb_low = clamp_alpha_low = clamp_rgb_high = clamp_alpha_high =
          std::nanf("");
      write_mask &= 0b0011;
      if (!(write_mask & 0b0001)) {
        keep_mask_low = ~uint32_t(0);
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_high = ~uint32_t(0);
      }
      break;
    default:
      assert_unhandled_case(format);
      // Disable invalid render targets.
      write_mask = 0;
      break;
  }
  // Special case handled in the shaders for empty write mask to completely skip
  // a disabled render target: all keep bits are set.
  if (!write_mask) {
    keep_mask_low = keep_mask_high = ~uint32_t(0);
  }
}

void DxbcShaderTranslator::StartPixelShader_LoadROVParameters() {
  bool color_targets_written = writes_any_color_target();

  // ***************************************************************************
  // Get EDRAM offsets for the pixel:
  // system_temp_rov_params_.y - for depth (absolute).
  // system_temp_rov_params_.z - for 32bpp color (base-relative).
  // system_temp_rov_params_.w - for 64bpp color (base-relative).
  // ***************************************************************************

  // Extract the resolution scale as log2(scale)/2 specific for 1 (-> 0) and
  // 4 (-> 1) to a temp SGPR.
  uint32_t resolution_scale_log2_temp = PushSystemTemp();
  system_constants_used_ |= 1ull << kSysConst_EDRAMResolutionSquareScale_Index;
  DxbcOpUShR(DxbcDest::R(resolution_scale_log2_temp, 0b0001),
             DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_EDRAMResolutionSquareScale_Vec)
                 .Select(kSysConst_EDRAMResolutionSquareScale_Comp),
             DxbcSrc::LU(2));
  // Convert the pixel position (if resolution scale is 4, this will be 2x2
  // bigger) to integer to system_temp_rov_params_.zw.
  // system_temp_rov_params_.z = X host pixel position as uint
  // system_temp_rov_params_.w = Y host pixel position as uint
  DxbcOpFToU(DxbcDest::R(system_temp_rov_params_, 0b1100),
             DxbcSrc::V(uint32_t(InOutRegister::kPSInPosition), 0b01000000));
  // Revert the resolution scale to convert the position to guest pixels.
  // system_temp_rov_params_.z = X guest pixel position / sample width
  // system_temp_rov_params_.w = Y guest pixel position / sample height
  DxbcOpUShR(DxbcDest::R(system_temp_rov_params_, 0b1100),
             DxbcSrc::R(system_temp_rov_params_),
             DxbcSrc::R(resolution_scale_log2_temp, DxbcSrc::kXXXX));

  // Convert the position from pixels to samples.
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  DxbcOpIShL(DxbcDest::R(system_temp_rov_params_, 0b1100),
             DxbcSrc::R(system_temp_rov_params_),
             DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_SampleCountLog2_Vec,
                         (kSysConst_SampleCountLog2_Comp << 4) |
                             ((kSysConst_SampleCountLog2_Comp + 1) << 6)));
  // Get 80x16 samples tile index - start dividing X by 80 by getting the high
  // part of the result of multiplication of X by 0xCCCCCCCD into X.
  // system_temp_rov_params_.x = (X * 0xCCCCCCCD) >> 32, or X / 80 * 64
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  DxbcOpUMul(DxbcDest::R(system_temp_rov_params_, 0b0001), DxbcDest::Null(),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ),
             DxbcSrc::LU(0xCCCCCCCDu));
  // Get 80x16 samples tile index - finish dividing X by 80 and divide Y by 16
  // into system_temp_rov_params_.xy.
  // system_temp_rov_params_.x = X tile position
  // system_temp_rov_params_.y = Y tile position
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  DxbcOpUShR(DxbcDest::R(system_temp_rov_params_, 0b0011),
             DxbcSrc::R(system_temp_rov_params_, 0b00001100),
             DxbcSrc::LU(6, 4, 0, 0));
  // Get the tile index to system_temp_rov_params_.y.
  // system_temp_rov_params_.x = X tile position
  // system_temp_rov_params_.y = tile index
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  system_constants_used_ |= 1ull << kSysConst_EDRAMPitchTiles_Index;
  DxbcOpUMAd(DxbcDest::R(system_temp_rov_params_, 0b0010),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY),
             DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_EDRAMPitchTiles_Vec)
                 .Select(kSysConst_EDRAMPitchTiles_Comp),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX));
  // Convert the tile index into a tile offset.
  // system_temp_rov_params_.x = X tile position
  // system_temp_rov_params_.y = tile offset
  // system_temp_rov_params_.z = X guest sample 0 position
  // system_temp_rov_params_.w = Y guest sample 0 position
  DxbcOpUMul(DxbcDest::Null(), DxbcDest::R(system_temp_rov_params_, 0b0010),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY),
             DxbcSrc::LU(1280));
  // Get tile-local X sample index into system_temp_rov_params_.z.
  // system_temp_rov_params_.y = tile offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  // system_temp_rov_params_.w = Y guest sample 0 position
  DxbcOpIMAd(DxbcDest::R(system_temp_rov_params_, 0b0100),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
             DxbcSrc::LI(-80),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ));
  // Get tile-local Y sample index into system_temp_rov_params_.w.
  // system_temp_rov_params_.y = tile offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  // system_temp_rov_params_.w = Y sample 0 position within the tile
  DxbcOpAnd(DxbcDest::R(system_temp_rov_params_, 0b1000),
            DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kWWWW),
            DxbcSrc::LU(15));
  // Go to the target row within the tile in system_temp_rov_params_.y.
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  DxbcOpIMAd(DxbcDest::R(system_temp_rov_params_, 0b0010),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kWWWW),
             DxbcSrc::LI(80),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY));
  // Choose in which 40-sample half of the tile the pixel is, for swapping
  // 40-sample columns when accessing the depth buffer - games expect this
  // behavior when writing depth back to the EDRAM via color writing (GTA IV,
  // Halo 3).
  // system_temp_rov_params_.x = tile-local sample 0 X >= 40
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  DxbcOpUGE(DxbcDest::R(system_temp_rov_params_, 0b0001),
            DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ),
            DxbcSrc::LU(40));
  // Choose what to add to the depth/stencil X position.
  // system_temp_rov_params_.x = 40 or -40 offset for the depth buffer
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  DxbcOpMovC(DxbcDest::R(system_temp_rov_params_, 0b0001),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
             DxbcSrc::LI(-40), DxbcSrc::LI(40));
  // Flip tile halves for the depth/stencil buffer.
  // system_temp_rov_params_.x = X sample 0 position within the depth tile
  // system_temp_rov_params_.y = row offset
  // system_temp_rov_params_.z = X sample 0 position within the tile
  DxbcOpIAdd(DxbcDest::R(system_temp_rov_params_, 0b0001),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX));
  if (color_targets_written) {
    // Write 32bpp color offset to system_temp_rov_params_.z.
    // system_temp_rov_params_.x = X sample 0 position within the depth tile
    // system_temp_rov_params_.y = row offset
    // system_temp_rov_params_.z = unscaled 32bpp color offset
    DxbcOpIAdd(DxbcDest::R(system_temp_rov_params_, 0b0100),
               DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY),
               DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ));
  }
  // Write depth/stencil offset to system_temp_rov_params_.y.
  // system_temp_rov_params_.y = unscaled 32bpp depth/stencil offset
  // system_temp_rov_params_.z = unscaled 32bpp color offset if needed
  DxbcOpIAdd(DxbcDest::R(system_temp_rov_params_, 0b0010),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX));
  // Add the EDRAM base for depth/stencil.
  // system_temp_rov_params_.y = unscaled 32bpp depth/stencil address
  // system_temp_rov_params_.z = unscaled 32bpp color offset if needed
  system_constants_used_ |= 1ull << kSysConst_EDRAMDepthBaseDwords_Index;
  DxbcOpIAdd(DxbcDest::R(system_temp_rov_params_, 0b0010),
             DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY),
             DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_EDRAMDepthBaseDwords_Vec)
                 .Select(kSysConst_EDRAMDepthBaseDwords_Comp));

  // Apply the resolution scale.
  DxbcOpIf(true, DxbcSrc::R(resolution_scale_log2_temp, DxbcSrc::kXXXX));
  // Release resolution_scale_log2_temp.
  PopSystemTemp();
  {
    DxbcDest offsets_dest(DxbcDest::R(system_temp_rov_params_,
                                      color_targets_written ? 0b0110 : 0b0010));
    // Scale the offsets by the resolution scale.
    // system_temp_rov_params_.y = scaled 32bpp depth/stencil first host pixel
    //                             address
    // system_temp_rov_params_.z = scaled 32bpp color first host pixel offset if
    //                             needed
    DxbcOpIShL(offsets_dest, DxbcSrc::R(system_temp_rov_params_),
               DxbcSrc::LU(2));
    // Add host pixel offsets.
    // system_temp_rov_params_.y = scaled 32bpp depth/stencil address
    // system_temp_rov_params_.z = scaled 32bpp color offset if needed
    for (uint32_t i = 0; i < 2; ++i) {
      // Convert a position component to integer.
      DxbcOpFToU(DxbcDest::R(system_temp_rov_params_, 0b0001),
                 DxbcSrc::V(uint32_t(InOutRegister::kPSInPosition)).Select(i));
      // Insert the host pixel offset on each axis.
      DxbcOpBFI(offsets_dest, DxbcSrc::LU(1), DxbcSrc::LU(i),
                DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
                DxbcSrc::R(system_temp_rov_params_));
    }
  }
  // Close the resolution scale conditional.
  DxbcOpEndIf();

  if (color_targets_written) {
    // Get the 64bpp color offset to system_temp_rov_params_.w.
    // TODO(Triang3l): Find some game that aliases 64bpp with 32bpp to emulate
    // the real layout.
    // system_temp_rov_params_.y = scaled 32bpp depth/stencil address
    // system_temp_rov_params_.z = scaled 32bpp color offset
    // system_temp_rov_params_.w = scaled 64bpp color offset
    DxbcOpIShL(DxbcDest::R(system_temp_rov_params_, 0b1000),
               DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ),
               DxbcSrc::LU(1));
  }

  // ***************************************************************************
  // Sample coverage to system_temp_rov_params_.x.
  // ***************************************************************************

  // Using ForcedSampleCount of 4 (2 is not supported on Nvidia), so for 2x
  // MSAA, handling samples 0 and 3 (upper-left and lower-right) as 0 and 1.

  // Check if 4x MSAA is enabled.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  DxbcOpIf(true, DxbcSrc::CB(cbuffer_index_system_constants_,
                             uint32_t(CbufferRegister::kSystemConstants),
                             kSysConst_SampleCountLog2_Vec)
                     .Select(kSysConst_SampleCountLog2_Comp));
  {
    // Copy the 4x AA coverage to system_temp_rov_params_.x.
    DxbcOpAnd(DxbcDest::R(system_temp_rov_params_, 0b0001),
              DxbcSrc::VCoverage(), DxbcSrc::LU((1 << 4) - 1));
  }
  // Handle 1 or 2 samples.
  DxbcOpElse();
  {
    // Extract sample 3 coverage, which will be used as sample 1.
    DxbcOpUBFE(DxbcDest::R(system_temp_rov_params_, 0b0001), DxbcSrc::LU(1),
               DxbcSrc::LU(3), DxbcSrc::VCoverage());
    // Combine coverage of samples 0 (in bit 0 of vCoverage) and 3 (in bit 0 of
    // system_temp_rov_params_.x).
    DxbcOpBFI(DxbcDest::R(system_temp_rov_params_, 0b0001), DxbcSrc::LU(31),
              DxbcSrc::LU(1),
              DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
              DxbcSrc::VCoverage());
  }
  // Close the 4x MSAA conditional.
  DxbcOpEndIf();
}

void DxbcShaderTranslator::ROV_DepthStencilTest() {
  uint32_t temp1 = PushSystemTemp();

  // Check whether depth/stencil is enabled. 1 SGPR taken.
  // temp1.x = kSysFlag_ROVDepthStencil
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(temp1, 0b0001),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_ROVDepthStencil));
  // Open the depth/stencil enabled conditional. 1 SGPR released.
  // temp1.x = free
  DxbcOpIf(true, DxbcSrc::R(temp1, DxbcSrc::kXXXX));

  if (writes_depth()) {
    // Convert the shader-generated depth to 24-bit - move the 32-bit depth to
    // the conversion subroutine's argument.
    DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b0001),
              DxbcSrc::R(system_temp_rov_depth_stencil_, DxbcSrc::kXXXX));
    // Convert the shader-generated depth to 24-bit.
    DxbcOpCall(DxbcSrc::Label(label_rov_depth_to_24bit_));
    // Store a copy of the depth in temp1.x to reload later.
    // temp1.x = 24-bit oDepth
    DxbcOpMov(DxbcDest::R(temp1, 0b0001),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
  } else {
    // Load the first sample's Z and W to system_temps_subroutine_[0] - need
    // this regardless of coverage for polygon offset.
    DxbcOpEvalSampleIndex(DxbcDest::R(system_temps_subroutine_, 0b0011),
                          DxbcSrc::V(uint32_t(InOutRegister::kPSInClipSpaceZW)),
                          DxbcSrc::LU(0));
    // Calculate the first sample's Z/W to system_temps_subroutine_[0].x for
    // conversion to 24-bit and depth test.
    DxbcOpDiv(DxbcDest::R(system_temps_subroutine_, 0b0001),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY), true);
    // Apply viewport Z range to the first sample because this would affect the
    // slope-scaled depth bias (tested on PC on Direct3D 12, by comparing the
    // fraction of the polygon's area with depth clamped - affected by the
    // constant bias, but not affected by the slope-scaled bias, also depth
    // range clamping should be done after applying the offset as well).
    system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
    DxbcOpMAd(DxbcDest::R(system_temps_subroutine_, 0b0001),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_EDRAMDepthRange_Vec)
                  .Select(kSysConst_EDRAMDepthRangeScale_Comp),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_EDRAMDepthRange_Vec)
                  .Select(kSysConst_EDRAMDepthRangeOffset_Comp),
              true);
    // Get the derivatives of a sample's depth, for the slope-scaled polygon
    // offset. Probably not very significant that it's for the sample 0 rather
    // than for the center, likely neither is accurate because Xenos probably
    // calculates the slope between 16ths of a pixel according to the meaning of
    // the slope-scaled polygon offset in R5xx Acceleration. Take 2 VGPRs.
    // temp1.x = ddx(z)
    // temp1.y = ddy(z)
    DxbcOpDerivRTXCoarse(DxbcDest::R(temp1, 0b0001),
                         DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
    DxbcOpDerivRTYCoarse(DxbcDest::R(temp1, 0b0010),
                         DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
    // Get the maximum depth slope for polygon offset to temp1.y.
    // Release 1 VGPR (Y derivative).
    // temp1.x = max(|ddx(z)|, |ddy(z)|)
    // temp1.y = free
    // https://docs.microsoft.com/en-us/windows/desktop/direct3d9/depth-bias
    DxbcOpMax(DxbcDest::R(temp1, 0b0001),
              DxbcSrc::R(temp1, DxbcSrc::kXXXX).Abs(),
              DxbcSrc::R(temp1, DxbcSrc::kYYYY).Abs());
    // Copy the needed polygon offset values to temp1.yz. Take 2 VGPRs.
    // temp1.x = max(|ddx(z)|, |ddy(z)|)
    // temp1.y = polygon offset scale
    // temp1.z = polygon offset bias
    system_constants_used_ |= (1ull << kSysConst_EDRAMPolyOffsetFront_Index) |
                              (1ull << kSysConst_EDRAMPolyOffsetBack_Index);
    DxbcOpMovC(
        DxbcDest::R(temp1, 0b0110),
        DxbcSrc::V(uint32_t(InOutRegister::kPSInFrontFace), DxbcSrc::kXXXX),
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_EDRAMPolyOffsetFront_Vec,
                    (kSysConst_EDRAMPolyOffsetFrontScale_Comp << 2) |
                        (kSysConst_EDRAMPolyOffsetFrontOffset_Comp << 4)),
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_EDRAMPolyOffsetBack_Vec,
                    (kSysConst_EDRAMPolyOffsetBackScale_Comp << 2) |
                        (kSysConst_EDRAMPolyOffsetBackOffset_Comp << 4)));
    // Apply the slope scale and the constant bias to the offset, and release 2
    // VGPRs.
    // temp1.x = polygon offset
    // temp1.y = free
    // temp1.z = free
    DxbcOpMAd(DxbcDest::R(temp1, 0b0001), DxbcSrc::R(temp1, DxbcSrc::kYYYY),
              DxbcSrc::R(temp1, DxbcSrc::kXXXX),
              DxbcSrc::R(temp1, DxbcSrc::kZZZZ));
    // Calculate the upper Z range bound to temp1.y for clamping after biasing,
    // taking 1 SGPR.
    // temp1.x = polygon offset
    // temp1.y = viewport maximum depth
    system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
    DxbcOpAdd(DxbcDest::R(temp1, 0b0010),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_EDRAMDepthRange_Vec)
                  .Select(kSysConst_EDRAMDepthRangeOffset_Comp),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_EDRAMDepthRange_Vec)
                  .Select(kSysConst_EDRAMDepthRangeScale_Comp));
  }

  for (uint32_t i = 0; i < 4; ++i) {
    // Get if the current sample is covered to temp1.y. Take 1 VGPR.
    // temp1.x = polygon offset or 24-bit oDepth
    // temp1.y = viewport maximum depth if not writing to oDepth
    // temp1.z = coverage of the current sample
    DxbcOpAnd(DxbcDest::R(temp1, 0b0100),
              DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
              DxbcSrc::LU(1 << i));
    // Check if the current sample is covered. Release 1 VGPR.
    // temp1.x = polygon offset or 24-bit oDepth
    // temp1.y = viewport maximum depth if not writing to oDepth
    // temp1.z = free
    DxbcOpIf(true, DxbcSrc::R(temp1, DxbcSrc::kZZZZ));

    if (writes_depth()) {
      // Same depth for all samples, already converted to 24-bit - only move it
      // to the depth/stencil sample subroutine argument from temp1.x if it's
      // not already there (it's there for the first sample - returned from the
      // conversion to 24-bit).
      if (i) {
        DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b0001),
                  DxbcSrc::R(temp1, DxbcSrc::kXXXX));
      }
    } else {
      if (i) {
        // Sample's depth precalculated for sample 0 (for slope-scaled depth
        // bias calculation), but need to calculate it for other samples.

        // Using system_temps_subroutine_[0].xy as temps for Z/W since Y will
        // contain the result anyway after the call, and temp1.x contains the
        // polygon offset.

        if (i == 1) {
          // Using ForcedSampleCount of 4 (2 is not supported on Nvidia), so for
          // 2x MSAA, handling samples 0 and 3 (upper-left and lower-right) as 0
          // and 1. Thus, evaluate Z/W at sample 3 when 4x is not enabled.
          system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
          DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0001),
                     DxbcSrc::CB(cbuffer_index_system_constants_,
                                 uint32_t(CbufferRegister::kSystemConstants),
                                 kSysConst_SampleCountLog2_Vec)
                         .Select(kSysConst_SampleCountLog2_Comp),
                     DxbcSrc::LU(3), DxbcSrc::LU(1));
          DxbcOpEvalSampleIndex(
              DxbcDest::R(system_temps_subroutine_, 0b0011),
              DxbcSrc::V(uint32_t(InOutRegister::kPSInClipSpaceZW)),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        } else {
          DxbcOpEvalSampleIndex(
              DxbcDest::R(system_temps_subroutine_, 0b0011),
              DxbcSrc::V(uint32_t(InOutRegister::kPSInClipSpaceZW)),
              DxbcSrc::LU(i));
        }
        // Calculate Z/W for the current sample from the evaluated Z and W.
        DxbcOpDiv(DxbcDest::R(system_temps_subroutine_, 0b0001),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY), true);
        // Apply viewport Z range the same way as it was applied to sample 0.
        system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
        DxbcOpMAd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                  DxbcSrc::CB(cbuffer_index_system_constants_,
                              uint32_t(CbufferRegister::kSystemConstants),
                              kSysConst_EDRAMDepthRange_Vec)
                      .Select(kSysConst_EDRAMDepthRangeScale_Comp),
                  DxbcSrc::CB(cbuffer_index_system_constants_,
                              uint32_t(CbufferRegister::kSystemConstants),
                              kSysConst_EDRAMDepthRange_Vec)
                      .Select(kSysConst_EDRAMDepthRangeOffset_Comp),
                  true);
      }
      // Add the bias to the depth of the sample.
      DxbcOpAdd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                DxbcSrc::R(temp1, DxbcSrc::kXXXX));
      // Clamp the biased depth to the lower viewport depth bound.
      system_constants_used_ |= 1ull << kSysConst_EDRAMDepthRange_Index;
      DxbcOpMax(DxbcDest::R(system_temps_subroutine_, 0b0001),
                DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                DxbcSrc::CB(cbuffer_index_system_constants_,
                            uint32_t(CbufferRegister::kSystemConstants),
                            kSysConst_EDRAMDepthRange_Vec)
                    .Select(kSysConst_EDRAMDepthRangeOffset_Comp));
      // Clamp the biased depth to the upper viewport depth bound.
      DxbcOpMin(DxbcDest::R(system_temps_subroutine_, 0b0001),
                DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                DxbcSrc::R(temp1, DxbcSrc::kYYYY), true);
      // Convert the depth to 24-bit - takes system_temps_subroutine_[0].x,
      // returns also in system_temps_subroutine_[0].x.
      DxbcOpCall(DxbcSrc::Label(label_rov_depth_to_24bit_));
    }

    // Perform depth/stencil test for the sample, get the result in bits 4
    // (passed) and 8 (new depth/stencil buffer value is different).
    DxbcOpCall(DxbcSrc::Label(label_rov_depth_stencil_sample_));
    // Write the resulting depth/stencil value in system_temps_subroutine_[0].x
    // to the sample's depth in system_temp_rov_depth_stencil_.
    DxbcOpMov(DxbcDest::R(system_temp_rov_depth_stencil_, 1 << i),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
    if (i) {
      // Shift the result bits to the correct position.
      DxbcOpIShL(DxbcDest::R(system_temps_subroutine_, 0b0010),
                 DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
                 DxbcSrc::LU(i));
    }
    // Add the result in system_temps_subroutine_[0].y to
    // system_temp_rov_params_.x. Bits 0:3 will be cleared in case of test
    // failure (only doing this for covered samples), bits 4:7 will be added if
    // need to defer writing.
    DxbcOpXOr(DxbcDest::R(system_temp_rov_params_, 0b0001),
              DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));

    // Close the sample conditional.
    DxbcOpEndIf();

    // Go to the next sample (samples are at +0, +80, +1, +81, so need to do
    // +80, -79, +80 and -81 after each sample).
    system_constants_used_ |= 1ull
                              << kSysConst_EDRAMResolutionSquareScale_Index;
    DxbcOpIMAd(DxbcDest::R(system_temp_rov_params_, 0b0010),
               DxbcSrc::LI((i & 1) ? -78 - i : 80),
               DxbcSrc::CB(cbuffer_index_system_constants_,
                           uint32_t(CbufferRegister::kSystemConstants),
                           kSysConst_EDRAMResolutionSquareScale_Vec)
                   .Select(kSysConst_EDRAMResolutionSquareScale_Comp),
               DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY));
  }

  if (ROV_IsDepthStencilEarly()) {
    // Check if safe to discard the whole 2x2 quad early, without running the
    // translated pixel shader, by checking if coverage is 0 in all pixels in
    // the quad and if there are no samples which failed the depth test, but
    // where stencil was modified and needs to be written in the end. Must
    // reject at 2x2 quad granularity because texture fetches need derivatives.

    // temp1.x = coverage | deferred depth/stencil write
    DxbcOpAnd(DxbcDest::R(temp1, 0b0001),
              DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
              DxbcSrc::LU(0b11111111));
    // temp1.x = 1.0 if any sample is covered or potentially needs stencil write
    // in the end of the shader in the current pixel
    DxbcOpMovC(DxbcDest::R(temp1, 0b0001), DxbcSrc::R(temp1, DxbcSrc::kXXXX),
               DxbcSrc::LF(1.0f), DxbcSrc::LF(0.0f));
    // temp1.x = 1.0 if any sample is covered or potentially needs stencil write
    // in the end of the shader in the current pixel
    // temp1.y = non-zero if anything is covered in the pixel across X
    DxbcOpDerivRTXFine(DxbcDest::R(temp1, 0b0010),
                       DxbcSrc::R(temp1, DxbcSrc::kXXXX));
    // temp1.x = 1.0 if anything is covered in the current half of the quad
    // temp1.y = free
    DxbcOpMovC(DxbcDest::R(temp1, 0b0001), DxbcSrc::R(temp1, DxbcSrc::kYYYY),
               DxbcSrc::LF(1.0f), DxbcSrc::R(temp1, DxbcSrc::kXXXX));
    // temp1.x = 1.0 if anything is covered in the current half of the quad
    // temp1.y = non-zero if anything is covered in the two pixels across Y
    DxbcOpDerivRTYCoarse(DxbcDest::R(temp1, 0b0010),
                         DxbcSrc::R(temp1, DxbcSrc::kXXXX));
    // temp1.x = 1.0 if anything is covered in the current whole quad
    // temp1.y = free
    DxbcOpMovC(DxbcDest::R(temp1, 0b0001), DxbcSrc::R(temp1, DxbcSrc::kYYYY),
               DxbcSrc::LF(1.0f), DxbcSrc::R(temp1, DxbcSrc::kXXXX));
    // End the shader if nothing is covered in the 2x2 quad after early
    // depth/stencil.
    // temp1.x = free
    DxbcOpRetC(false, DxbcSrc::R(temp1, DxbcSrc::kXXXX));
  }

  // Close the large depth/stencil conditional.
  DxbcOpEndIf();

  // Release temp1.
  PopSystemTemp();
}

void DxbcShaderTranslator::ROV_UnpackColor(
    uint32_t rt_index, uint32_t packed_temp, uint32_t packed_temp_components,
    uint32_t color_temp, uint32_t temp1, uint32_t temp1_component,
    uint32_t temp2, uint32_t temp2_component) {
  assert_true(color_temp != packed_temp || packed_temp_components == 0);

  DxbcSrc packed_temp_low(
      DxbcSrc::R(packed_temp).Select(packed_temp_components));
  DxbcDest temp1_dest(DxbcDest::R(temp1, 1 << temp1_component));
  DxbcSrc temp1_src(DxbcSrc::R(temp1).Select(temp1_component));
  DxbcDest temp2_dest(DxbcDest::R(temp2, 1 << temp2_component));
  DxbcSrc temp2_src(DxbcSrc::R(temp2).Select(temp2_component));

  // Break register dependencies and initialize if there are not enough
  // components. The rest of the function will write at least RG (k_32_FLOAT and
  // k_32_32_FLOAT handled with the same default label), and if packed_temp is
  // the same as color_temp, the packed color won't be touched.
  DxbcOpMov(DxbcDest::R(color_temp, 0b1100),
            DxbcSrc::LF(0.0f, 0.0f, 0.0f, 1.0f));

  // Choose the packing based on the render target's format.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  DxbcOpSwitch(DxbcSrc::CB(cbuffer_index_system_constants_,
                           uint32_t(CbufferRegister::kSystemConstants),
                           kSysConst_EDRAMRTFormatFlags_Vec)
                   .Select(rt_index));

  // ***************************************************************************
  // k_8_8_8_8
  // k_8_8_8_8_GAMMA
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    DxbcOpCase(DxbcSrc::LU(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_8_8_8_8_GAMMA
                                  : ColorRenderTargetFormat::k_8_8_8_8)));
    // Unpack the components.
    DxbcOpUBFE(DxbcDest::R(color_temp), DxbcSrc::LU(8),
               DxbcSrc::LU(0, 8, 16, 24), packed_temp_low);
    // Convert from fixed-point.
    DxbcOpUToF(DxbcDest::R(color_temp), DxbcSrc::R(color_temp));
    // Normalize.
    DxbcOpMul(DxbcDest::R(color_temp), DxbcSrc::R(color_temp),
              DxbcSrc::LF(1.0f / 255.0f));
    if (i) {
      for (uint32_t j = 0; j < 3; ++j) {
        ConvertPWLGamma(false, color_temp, j, color_temp, j, temp1,
                        temp1_component, temp2, temp2_component);
      }
    }
    DxbcOpBreak();
  }

  // ***************************************************************************
  // k_2_10_10_10
  // k_2_10_10_10_AS_10_10_10_10
  // ***************************************************************************
  DxbcOpCase(DxbcSrc::LU(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10)));
  DxbcOpCase(DxbcSrc::LU(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10)));
  {
    // Unpack the components.
    DxbcOpUBFE(DxbcDest::R(color_temp), DxbcSrc::LU(10, 10, 10, 2),
               DxbcSrc::LU(0, 10, 20, 30), packed_temp_low);
    // Convert from fixed-point.
    DxbcOpUToF(DxbcDest::R(color_temp), DxbcSrc::R(color_temp));
    // Normalize.
    DxbcOpMul(DxbcDest::R(color_temp), DxbcSrc::R(color_temp),
              DxbcSrc::LF(1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f,
                          1.0f / 3.0f));
  }
  DxbcOpBreak();

  // ***************************************************************************
  // k_2_10_10_10_FLOAT
  // k_2_10_10_10_FLOAT_AS_16_16_16_16
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************
  DxbcOpCase(DxbcSrc::LU(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10_FLOAT)));
  DxbcOpCase(DxbcSrc::LU(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16)));
  {
    // Unpack the alpha.
    DxbcOpUBFE(DxbcDest::R(color_temp, 0b1000), DxbcSrc::LU(2), DxbcSrc::LU(30),
               packed_temp_low);
    // Convert the alpha from fixed-point.
    DxbcOpUToF(DxbcDest::R(color_temp, 0b1000),
               DxbcSrc::R(color_temp, DxbcSrc::kWWWW));
    // Normalize the alpha.
    DxbcOpMul(DxbcDest::R(color_temp, 0b1000),
              DxbcSrc::R(color_temp, DxbcSrc::kWWWW), DxbcSrc::LF(1.0f / 3.0f));
    // Process the components in reverse order because color_temp.r stores the
    // packed color which shouldn't be touched until G and B are converted if
    // packed_temp and color_temp are the same.
    for (int32_t i = 2; i >= 0; --i) {
      DxbcDest color_component_dest(DxbcDest::R(color_temp, 1 << i));
      DxbcSrc color_component_src(DxbcSrc::R(color_temp).Select(i));
      // Unpack the exponent to the temp.
      DxbcOpUBFE(temp1_dest, DxbcSrc::LU(3), DxbcSrc::LU(i * 10 + 7),
                 packed_temp_low);
      // Unpack the mantissa to the result.
      DxbcOpUBFE(color_component_dest, DxbcSrc::LU(7), DxbcSrc::LU(i * 10),
                 packed_temp_low);
      // Check if the number is denormalized.
      DxbcOpIf(false, temp1_src);
      {
        // Check if the number is non-zero (if the mantissa isn't zero - the
        // exponent is known to be zero at this point).
        DxbcOpIf(true, color_component_src);
        {
          // Normalize the mantissa.
          // Note that HLSL firstbithigh(x) is compiled to DXBC like:
          // `x ? 31 - firstbit_hi(x) : -1`
          // (returns the index from the LSB, not the MSB, but -1 for zero too).
          // temp = firstbit_hi(mantissa)
          DxbcOpFirstBitHi(temp1_dest, color_component_src);
          // temp  = 7 - (31 - firstbit_hi(mantissa))
          // Or, if expanded:
          // temp = firstbit_hi(mantissa) - 24
          DxbcOpIAdd(temp1_dest, temp1_src, DxbcSrc::LI(-24));
          // mantissa = mantissa << (7 - firstbithigh(mantissa))
          // AND 0x7F not needed after this - BFI will do it.
          DxbcOpIShL(color_component_dest, color_component_src, temp1_src);
          // Get the normalized exponent.
          // exponent = 1 - (7 - firstbithigh(mantissa))
          DxbcOpIAdd(temp1_dest, DxbcSrc::LI(1), -temp1_src);
        }
        // The number is zero.
        DxbcOpElse();
        {
          // Set the unbiased exponent to -124 for zero - 124 will be added
          // later, resulting in zero float32.
          DxbcOpMov(temp1_dest, DxbcSrc::LI(-124));
        }
        // Close the non-zero check.
        DxbcOpEndIf();
      }
      // Close the denormal check.
      DxbcOpEndIf();
      // Bias the exponent and move it to the correct location in f32.
      DxbcOpIMAd(temp1_dest, temp1_src, DxbcSrc::LI(1 << 23),
                 DxbcSrc::LI(124 << 23));
      // Combine the mantissa and the exponent.
      DxbcOpBFI(color_component_dest, DxbcSrc::LU(7), DxbcSrc::LU(16),
                color_component_src, temp1_src);
    }
  }
  DxbcOpBreak();

  // ***************************************************************************
  // k_16_16
  // k_16_16_16_16 (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    DxbcOpCase(DxbcSrc::LU(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16
                                  : ColorRenderTargetFormat::k_16_16)));
    DxbcDest color_components_dest(
        DxbcDest::R(color_temp, i ? 0b1111 : 0b0011));
    // Unpack the components.
    DxbcOpIBFE(color_components_dest, DxbcSrc::LU(16),
               DxbcSrc::LU(0, 16, 0, 16),
               DxbcSrc::R(packed_temp,
                          0b01010000 + packed_temp_components * 0b01010101));
    // Convert from fixed-point.
    DxbcOpIToF(color_components_dest, DxbcSrc::R(color_temp));
    // Normalize.
    DxbcOpMul(color_components_dest, DxbcSrc::R(color_temp),
              DxbcSrc::LF(32.0f / 32767.0f));
    DxbcOpBreak();
  }

  // ***************************************************************************
  // k_16_16_FLOAT
  // k_16_16_16_16_FLOAT (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    DxbcOpCase(DxbcSrc::LU(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16_FLOAT
                                  : ColorRenderTargetFormat::k_16_16_FLOAT)));
    DxbcDest color_components_dest(
        DxbcDest::R(color_temp, i ? 0b1111 : 0b0011));
    // Unpack the components.
    DxbcOpUBFE(color_components_dest, DxbcSrc::LU(16),
               DxbcSrc::LU(0, 16, 0, 16),
               DxbcSrc::R(packed_temp,
                          0b01010000 + packed_temp_components * 0b01010101));
    // Convert from 16-bit float.
    DxbcOpF16ToF32(color_components_dest, DxbcSrc::R(color_temp));
    DxbcOpBreak();
  }

  if (packed_temp != color_temp) {
    // Assume k_32_FLOAT or k_32_32_FLOAT for the rest.
    DxbcOpDefault();
    DxbcOpMov(
        DxbcDest::R(color_temp, 0b0011),
        DxbcSrc::R(packed_temp, 0b0100 + packed_temp_components * 0b0101));
    DxbcOpBreak();
  }

  DxbcOpEndSwitch();
}

void DxbcShaderTranslator::ROV_PackPreClampedColor(
    uint32_t rt_index, uint32_t color_temp, uint32_t packed_temp,
    uint32_t packed_temp_components, uint32_t temp1, uint32_t temp1_component,
    uint32_t temp2, uint32_t temp2_component) {
  assert_true(color_temp != packed_temp || packed_temp_components == 0);

  DxbcDest packed_dest_low(
      DxbcDest::R(packed_temp, 1 << packed_temp_components));
  DxbcSrc packed_src_low(
      DxbcSrc::R(packed_temp).Select(packed_temp_components));
  DxbcDest temp1_dest(DxbcDest::R(temp1, 1 << temp1_component));
  DxbcSrc temp1_src(DxbcSrc::R(temp1).Select(temp1_component));
  DxbcDest temp2_dest(DxbcDest::R(temp2, 1 << temp2_component));
  DxbcSrc temp2_src(DxbcSrc::R(temp2).Select(temp2_component));

  // Break register dependency after 32bpp cases.
  DxbcOpMov(DxbcDest::R(packed_temp, 1 << (packed_temp_components + 1)),
            DxbcSrc::LU(0));

  // Choose the packing based on the render target's format.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  DxbcOpSwitch(DxbcSrc::CB(cbuffer_index_system_constants_,
                           uint32_t(CbufferRegister::kSystemConstants),
                           kSysConst_EDRAMRTFormatFlags_Vec)
                   .Select(rt_index));

  // ***************************************************************************
  // k_8_8_8_8
  // k_8_8_8_8_GAMMA
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    DxbcOpCase(DxbcSrc::LU(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_8_8_8_8_GAMMA
                                  : ColorRenderTargetFormat::k_8_8_8_8)));
    for (uint32_t j = 0; j < 4; ++j) {
      if (i && j < 3) {
        ConvertPWLGamma(true, color_temp, j, temp1, temp1_component, temp1,
                        temp1_component, temp2, temp2_component);
        // Denormalize.
        DxbcOpMul(temp1_dest, temp1_src, DxbcSrc::LF(255.0f));
      } else {
        // Denormalize.
        DxbcOpMul(temp1_dest, DxbcSrc::R(color_temp).Select(j),
                  DxbcSrc::LF(255.0f));
      }
      // Round towards the nearest even integer. Rounding towards the nearest
      // (adding +-0.5 before truncating) is giving incorrect results for
      // depth, so better to use round_ne here too.
      DxbcOpRoundNE(temp1_dest, temp1_src);
      // Convert to fixed-point.
      DxbcOpFToU(j ? temp1_dest : packed_dest_low, temp1_src);
      // Pack the upper components.
      if (j) {
        DxbcOpBFI(packed_dest_low, DxbcSrc::LU(8), DxbcSrc::LU(j * 8),
                  temp1_src, packed_src_low);
      }
    }
    DxbcOpBreak();
  }

  // ***************************************************************************
  // k_2_10_10_10
  // k_2_10_10_10_AS_10_10_10_10
  // ***************************************************************************
  DxbcOpCase(DxbcSrc::LU(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10)));
  DxbcOpCase(DxbcSrc::LU(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10)));
  for (uint32_t i = 0; i < 4; ++i) {
    // Denormalize.
    DxbcOpMul(temp1_dest, DxbcSrc::R(color_temp).Select(i),
              DxbcSrc::LF(i < 3 ? 1023.0f : 3.0f));
    // Round towards the nearest even integer. Rounding towards the nearest
    // (adding +-0.5 before truncating) is giving incorrect results for depth,
    // so better to use round_ne here too.
    DxbcOpRoundNE(temp1_dest, temp1_src);
    // Convert to fixed-point.
    DxbcOpFToU(i ? temp1_dest : packed_dest_low, temp1_src);
    // Pack the upper components.
    if (i) {
      DxbcOpBFI(packed_dest_low, DxbcSrc::LU(i < 3 ? 10 : 2),
                DxbcSrc::LU(i * 10), temp1_src, packed_src_low);
    }
  }
  DxbcOpBreak();

  // ***************************************************************************
  // k_2_10_10_10_FLOAT
  // k_2_10_10_10_FLOAT_AS_16_16_16_16
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // ***************************************************************************
  DxbcOpCase(DxbcSrc::LU(
      ROV_AddColorFormatFlags(ColorRenderTargetFormat::k_2_10_10_10_FLOAT)));
  DxbcOpCase(DxbcSrc::LU(ROV_AddColorFormatFlags(
      ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16)));
  {
    for (uint32_t i = 0; i < 3; ++i) {
      DxbcSrc color_component_src(DxbcSrc::R(color_temp).Select(i));
      // Check if the number is too small to be represented as normalized 7e3.
      // temp2 = f32 < 2^-2
      DxbcOpULT(temp2_dest, color_component_src, DxbcSrc::LU(0x3E800000));
      // Handle denormalized numbers separately.
      DxbcOpIf(true, temp2_src);
      {
        // temp2 = f32 >> 23
        DxbcOpUShR(temp2_dest, color_component_src, DxbcSrc::LU(23));
        // temp2 = 125 - (f32 >> 23)
        DxbcOpIAdd(temp2_dest, DxbcSrc::LI(125), -temp2_src);
        // Don't allow the shift to overflow, since in DXBC the lower 5 bits of
        // the shift amount are used.
        // temp2 = min(125 - (f32 >> 23), 24)
        DxbcOpUMin(temp2_dest, temp2_src, DxbcSrc::LU(24));
        // biased_f32 = (f32 & 0x7FFFFF) | 0x800000
        DxbcOpBFI(temp1_dest, DxbcSrc::LU(9), DxbcSrc::LU(23), DxbcSrc::LU(1),
                  color_component_src);
        // biased_f32 =
        //     ((f32 & 0x7FFFFF) | 0x800000) >> min(125 - (f32 >> 23), 24)
        DxbcOpUShR(temp1_dest, temp1_src, temp2_src);
      }
      // Not denormalized?
      DxbcOpElse();
      {
        // Bias the exponent.
        // biased_f32 = f32 + (-124 << 23)
        // (left shift of a negative value is undefined behavior)
        DxbcOpIAdd(temp1_dest, color_component_src, DxbcSrc::LU(0xC2000000u));
      }
      // Close the denormal check.
      DxbcOpEndIf();
      // Build the 7e3 number.
      // temp2 = (biased_f32 >> 16) & 1
      DxbcOpUBFE(temp2_dest, DxbcSrc::LU(1), DxbcSrc::LU(16), temp1_src);
      // f10 = biased_f32 + 0x7FFF
      DxbcOpIAdd(temp1_dest, temp1_src, DxbcSrc::LU(0x7FFF));
      // f10 = biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)
      DxbcOpIAdd(temp1_dest, temp1_src, temp2_src);
      // f10 = ((biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)) >> 16) & 0x3FF
      DxbcOpUBFE(i ? temp1_dest : packed_dest_low, DxbcSrc::LU(10),
                 DxbcSrc::LU(16), temp1_src);
      // Pack the upper components.
      if (i) {
        DxbcOpBFI(packed_dest_low, DxbcSrc::LU(10), DxbcSrc::LU(i * 10),
                  temp1_src, packed_src_low);
      }
    }
    // Denormalize the alpha.
    DxbcOpMul(temp1_dest, DxbcSrc::R(color_temp, DxbcSrc::kWWWW),
              DxbcSrc::LF(3.0f));
    // Round the alpha towards the nearest even integer. Rounding towards the
    // nearest (adding +-0.5 before truncating) is giving incorrect results for
    // depth, so better to use round_ne here too.
    DxbcOpRoundNE(temp1_dest, temp1_src);
    // Convert the alpha to fixed-point.
    DxbcOpFToU(temp1_dest, temp1_src);
    // Pack the alpha.
    DxbcOpBFI(packed_dest_low, DxbcSrc::LU(2), DxbcSrc::LU(30), temp1_src,
              packed_src_low);
  }
  DxbcOpBreak();

  // ***************************************************************************
  // k_16_16
  // k_16_16_16_16 (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    DxbcOpCase(DxbcSrc::LU(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16
                                  : ColorRenderTargetFormat::k_16_16)));
    for (uint32_t j = 0; j < (uint32_t(2) << i); ++j) {
      // Denormalize.
      DxbcOpMul(temp1_dest, DxbcSrc::R(color_temp).Select(j),
                DxbcSrc::LF(32767.0f / 32.0f));
      // Round towards the nearest even integer. Rounding towards the nearest
      // (adding +-0.5 before truncating) is giving incorrect results for depth,
      // so better to use round_ne here too.
      DxbcOpRoundNE(temp1_dest, temp1_src);
      DxbcDest packed_dest_half(
          DxbcDest::R(packed_temp, 1 << (packed_temp_components + (j >> 1))));
      // Convert to fixed-point.
      DxbcOpFToI((j & 1) ? temp1_dest : packed_dest_half, temp1_src);
      // Pack green or alpha.
      if (j & 1) {
        DxbcOpBFI(
            packed_dest_half, DxbcSrc::LU(16), DxbcSrc::LU(16), temp1_src,
            DxbcSrc::R(packed_temp).Select(packed_temp_components + (j >> 1)));
      }
    }
    DxbcOpBreak();
  }

  // ***************************************************************************
  // k_16_16_FLOAT
  // k_16_16_16_16_FLOAT (64bpp)
  // ***************************************************************************
  for (uint32_t i = 0; i < 2; ++i) {
    DxbcOpCase(DxbcSrc::LU(
        ROV_AddColorFormatFlags(i ? ColorRenderTargetFormat::k_16_16_16_16_FLOAT
                                  : ColorRenderTargetFormat::k_16_16_FLOAT)));
    for (uint32_t j = 0; j < (uint32_t(2) << i); ++j) {
      DxbcDest packed_dest_half(
          DxbcDest::R(packed_temp, 1 << (packed_temp_components + (j >> 1))));
      // Convert to 16-bit float.
      DxbcOpF32ToF16((j & 1) ? temp1_dest : packed_dest_half,
                     DxbcSrc::R(color_temp).Select(j));
      // Pack green or alpha.
      if (j & 1) {
        DxbcOpBFI(
            packed_dest_half, DxbcSrc::LU(16), DxbcSrc::LU(16), temp1_src,
            DxbcSrc::R(packed_temp).Select(packed_temp_components + (j >> 1)));
      }
    }
    DxbcOpBreak();
  }

  if (packed_temp != color_temp) {
    // Assume k_32_FLOAT or k_32_32_FLOAT for the rest.
    DxbcOpDefault();
    DxbcOpMov(DxbcDest::R(packed_temp, 0b11 << packed_temp_components),
              DxbcSrc::R(color_temp, 0b0100 << (packed_temp_components * 2)));
    DxbcOpBreak();
  }

  DxbcOpEndSwitch();
}

void DxbcShaderTranslator::ROV_HandleColorBlendFactorCases(
    uint32_t src_temp, uint32_t dst_temp, uint32_t factor_temp) {
  DxbcDest factor_dest(DxbcDest::R(factor_temp, 0b0111));
  DxbcSrc one_src(DxbcSrc::LF(1.0f));

  // kOne.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOne)));
  DxbcOpMov(factor_dest, one_src);
  DxbcOpBreak();

  // kSrcColor
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kSrcColor)));
  if (factor_temp != src_temp) {
    DxbcOpMov(factor_dest, DxbcSrc::R(src_temp));
  }
  DxbcOpBreak();

  // kOneMinusSrcColor
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusSrcColor)));
  DxbcOpAdd(factor_dest, one_src, -DxbcSrc::R(src_temp));
  DxbcOpBreak();

  // kSrcAlpha
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kSrcAlpha)));
  DxbcOpMov(factor_dest, DxbcSrc::R(src_temp, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kOneMinusSrcAlpha
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusSrcAlpha)));
  DxbcOpAdd(factor_dest, one_src, -DxbcSrc::R(src_temp, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kDstColor
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kDstColor)));
  if (factor_temp != dst_temp) {
    DxbcOpMov(factor_dest, DxbcSrc::R(dst_temp));
  }
  DxbcOpBreak();

  // kOneMinusDstColor
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusDstColor)));
  DxbcOpAdd(factor_dest, one_src, -DxbcSrc::R(dst_temp));
  DxbcOpBreak();

  // kDstAlpha
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kDstAlpha)));
  DxbcOpMov(factor_dest, DxbcSrc::R(dst_temp, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kOneMinusDstAlpha
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusDstAlpha)));
  DxbcOpAdd(factor_dest, one_src, -DxbcSrc::R(dst_temp, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // Factors involving the constant.
  system_constants_used_ |= 1ull << kSysConst_EDRAMBlendConstant_Index;

  // kConstantColor
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kConstantColor)));
  DxbcOpMov(factor_dest,
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_EDRAMBlendConstant_Vec));
  DxbcOpBreak();

  // kOneMinusConstantColor
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusConstantColor)));
  DxbcOpAdd(factor_dest, one_src,
            -DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_EDRAMBlendConstant_Vec));
  DxbcOpBreak();

  // kConstantAlpha
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kConstantAlpha)));
  DxbcOpMov(factor_dest,
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_EDRAMBlendConstant_Vec, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kOneMinusConstantAlpha
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusConstantAlpha)));
  DxbcOpAdd(factor_dest, one_src,
            -DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_EDRAMBlendConstant_Vec, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kSrcAlphaSaturate
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kSrcAlphaSaturate)));
  DxbcOpAdd(DxbcDest::R(factor_temp, 0b0001), one_src,
            -DxbcSrc::R(dst_temp, DxbcSrc::kWWWW));
  DxbcOpMin(factor_dest, DxbcSrc::R(src_temp, DxbcSrc::kWWWW),
            DxbcSrc::R(factor_temp, DxbcSrc::kXXXX));
  DxbcOpBreak();

  // kZero default.
  DxbcOpDefault();
  DxbcOpMov(factor_dest, DxbcSrc::LF(0.0f));
  DxbcOpBreak();
}

void DxbcShaderTranslator::ROV_HandleAlphaBlendFactorCases(
    uint32_t src_temp, uint32_t dst_temp, uint32_t factor_temp,
    uint32_t factor_component) {
  DxbcDest factor_dest(DxbcDest::R(factor_temp, 1 << factor_component));
  DxbcSrc one_src(DxbcSrc::LF(1.0f));

  // kOne, kSrcAlphaSaturate.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOne)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kSrcAlphaSaturate)));
  DxbcOpMov(factor_dest, one_src);
  DxbcOpBreak();

  // kSrcColor, kSrcAlpha.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kSrcColor)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kSrcAlpha)));
  if (factor_temp != src_temp || factor_component != 3) {
    DxbcOpMov(factor_dest, DxbcSrc::R(src_temp, DxbcSrc::kWWWW));
  }
  DxbcOpBreak();

  // kOneMinusSrcColor, kOneMinusSrcAlpha.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusSrcColor)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusSrcAlpha)));
  DxbcOpAdd(factor_dest, one_src, -DxbcSrc::R(src_temp, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kDstColor, kDstAlpha.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kDstColor)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kDstAlpha)));
  if (factor_temp != dst_temp || factor_component != 3) {
    DxbcOpMov(factor_dest, DxbcSrc::R(dst_temp, DxbcSrc::kWWWW));
  }
  DxbcOpBreak();

  // kOneMinusDstColor, kOneMinusDstAlpha.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusDstColor)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusDstAlpha)));
  DxbcOpAdd(factor_dest, one_src, -DxbcSrc::R(dst_temp, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // Factors involving the constant.
  system_constants_used_ |= 1ull << kSysConst_EDRAMBlendConstant_Index;

  // kConstantColor, kConstantAlpha.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kConstantColor)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kConstantAlpha)));
  DxbcOpMov(factor_dest,
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_EDRAMBlendConstant_Vec, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kOneMinusConstantColor, kOneMinusConstantAlpha.
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusConstantColor)));
  DxbcOpCase(DxbcSrc::LU(uint32_t(BlendFactor::kOneMinusConstantAlpha)));
  DxbcOpAdd(factor_dest, one_src,
            -DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_EDRAMBlendConstant_Vec, DxbcSrc::kWWWW));
  DxbcOpBreak();

  // kZero default.
  DxbcOpDefault();
  DxbcOpMov(factor_dest, DxbcSrc::LF(0.0f));
  DxbcOpBreak();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs_AlphaToCoverage() {
  // Refer to CompletePixelShader_ROV_AlphaToCoverage for the description of the
  // alpha to coverage pattern used.
  if (!writes_color_target(0)) {
    return;
  }
  uint32_t atoc_temp = PushSystemTemp();
  // Extract the flag to check if alpha to coverage is enabled.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(atoc_temp, 0b0001),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_AlphaToCoverage));
  // Check if alpha to coverage is enabled.
  DxbcOpIf(true, DxbcSrc::R(atoc_temp, DxbcSrc::kXXXX));
  // Convert SSAA sample position to integer (not caring about the resolution
  // scale because it's not supported anywhere on the RTV output path).
  DxbcOpFToU(DxbcDest::R(atoc_temp, 0b0011),
             DxbcSrc::V(uint32_t(InOutRegister::kPSInPosition)));
  // Get SSAA sample coordinates in the pixel.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  DxbcOpAnd(DxbcDest::R(atoc_temp, 0b0011), DxbcSrc::R(atoc_temp),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_SampleCountLog2_Vec,
                        kSysConst_SampleCountLog2_Comp |
                            ((kSysConst_SampleCountLog2_Comp + 1) << 2)));
  // Get the sample index - 0 and 2 being the top ones, 1 and 3 being the bottom
  // ones (because at 2x SSAA, 1 is the bottom).
  DxbcOpUMAd(DxbcDest::R(atoc_temp, 0b0001),
             DxbcSrc::R(atoc_temp, DxbcSrc::kXXXX), DxbcSrc::LU(2),
             DxbcSrc::R(atoc_temp, DxbcSrc::kYYYY));
  // Create a mask to choose the specific threshold to compare to.
  DxbcOpIEq(DxbcDest::R(atoc_temp), DxbcSrc::R(atoc_temp, DxbcSrc::kXXXX),
            DxbcSrc::LU(0, 1, 2, 3));
  uint32_t atoc_thresholds_temp = PushSystemTemp();
  // Choose the thresholds based on the sample count - first between 2 and 1
  // samples. 0.25 and 0.75 for 2 samples, 0.5 for 1 sample. NaN where
  // comparison must always fail.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  DxbcOpMovC(DxbcDest::R(atoc_thresholds_temp),
             DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_SampleCountLog2_Vec)
                 .Select(kSysConst_SampleCountLog2_Comp + 1),
             DxbcSrc::LU(0x3E800000, 0x3F400000, 0x7FC00000, 0x7FC00000),
             DxbcSrc::LU(0x3F000000, 0x7FC00000, 0x7FC00000, 0x7FC00000));
  // Choose the thresholds based on the sample count - between 4 or 1/2 samples.
  // 0.625, 0.125, 0.375, 0.875 for 4 samples.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  DxbcOpMovC(DxbcDest::R(atoc_thresholds_temp),
             DxbcSrc::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_SampleCountLog2_Vec)
                 .Select(kSysConst_SampleCountLog2_Comp),
             DxbcSrc::LU(0x3F200000, 0x3E000000, 0x3EC00000, 0x3F600000),
             DxbcSrc::R(atoc_thresholds_temp));
  // Choose the threshold to compare the alpha to according to the current
  // sample index - mask.
  DxbcOpAnd(DxbcDest::R(atoc_temp), DxbcSrc::R(atoc_thresholds_temp),
            DxbcSrc::R(atoc_temp));
  // Release atoc_thresholds_temp.
  PopSystemTemp();
  // Choose the threshold to compare the alpha to according to the current
  // sample index - select within pairs.
  DxbcOpOr(DxbcDest::R(atoc_temp, 0b0011), DxbcSrc::R(atoc_temp),
           DxbcSrc::R(atoc_temp, 0b1110));
  // Choose the threshold to compare the alpha to according to the current
  // sample index - combine pairs.
  DxbcOpOr(DxbcDest::R(atoc_temp, 0b0001),
           DxbcSrc::R(atoc_temp, DxbcSrc::kXXXX),
           DxbcSrc::R(atoc_temp, DxbcSrc::kYYYY));
  // Compare the alpha to the threshold.
  DxbcOpGE(DxbcDest::R(atoc_temp, 0b0001),
           DxbcSrc::R(system_temps_color_[0], DxbcSrc::kWWWW),
           DxbcSrc::R(atoc_temp, DxbcSrc::kXXXX));
  // Discard the SSAA sample if it's not covered.
  DxbcOpDiscard(false, DxbcSrc::R(atoc_temp, DxbcSrc::kXXXX));
  // Close the alpha to coverage check.
  DxbcOpEndIf();
  // Release atoc_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToRTVs() {
  if (!writes_any_color_target()) {
    return;
  }

  // Check if this sample needs to be discarded by alpha to coverage.
  CompletePixelShader_WriteToRTVs_AlphaToCoverage();

  // Get the write mask as components, and also apply the exponent bias after
  // alpha to coverage because it needs the unbiased alpha from the shader.
  uint32_t guest_rt_mask = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!writes_color_target(i)) {
      continue;
    }
    guest_rt_mask |= 1 << i;
    system_constants_used_ |= 1ull << kSysConst_ColorExpBias_Index;
    DxbcOpMul(DxbcDest::R(system_temps_color_[i]),
              DxbcSrc::R(system_temps_color_[i]),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_ColorExpBias_Vec)
                  .Select(i));
  }

  // Convert to gamma space - this is incorrect, since it must be done after
  // blending on the Xbox 360, but this is just one of many blending issues in
  // the RTV path.
  uint32_t gamma_temp = PushSystemTemp();
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(guest_rt_mask & (1 << i))) {
      continue;
    }
    system_constants_used_ |= 1ull << kSysConst_Flags_Index;
    DxbcOpAnd(DxbcDest::R(gamma_temp, 0b0001),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_Flags_Vec)
                  .Select(kSysConst_Flags_Comp),
              DxbcSrc::LU(kSysFlag_Color0Gamma << i));
    DxbcOpIf(true, DxbcSrc::R(gamma_temp, DxbcSrc::kXXXX));
    for (uint32_t j = 0; j < 3; ++j) {
      ConvertPWLGamma(true, system_temps_color_[i], j, system_temps_color_[i],
                      j, gamma_temp, 0, gamma_temp, 1);
    }
    DxbcOpEndIf();
  }
  // Release gamma_temp.
  PopSystemTemp();

  // Remap guest render target indices to host since because on the host, the
  // indices of the bound render targets are consecutive. This is done using 16
  // movc instructions because indexable temps are known to be causing
  // performance issues on some Nvidia GPUs. In the map, the components are host
  // render target indices, and the values are the guest ones.
  uint32_t remap_movc_mask_temp = PushSystemTemp();
  uint32_t remap_movc_target_temp = PushSystemTemp();
  system_constants_used_ |= 1ull << kSysConst_ColorOutputMap_Index;
  // Host RT i, guest RT j.
  for (uint32_t i = 0; i < 4; ++i) {
    // mask = map.iiii == (0, 1, 2, 3)
    DxbcOpIEq(DxbcDest::R(remap_movc_mask_temp, guest_rt_mask),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_ColorOutputMap_Vec)
                  .Select(i),
              DxbcSrc::LU(0, 1, 2, 3));
    bool guest_rt_first = true;
    for (uint32_t j = 0; j < 4; ++j) {
      // If map.i == j, move guest color j to the temporary host color.
      if (!(guest_rt_mask & (1 << j))) {
        continue;
      }
      DxbcOpMovC(DxbcDest::R(remap_movc_target_temp),
                 DxbcSrc::R(remap_movc_mask_temp).Select(j),
                 DxbcSrc::R(system_temps_color_[j]),
                 guest_rt_first ? DxbcSrc::LF(0.0f)
                                : DxbcSrc::R(remap_movc_target_temp));
    }
    // Write the remapped color to host render target i.
    DxbcOpMov(DxbcDest::O(i), DxbcSrc::R(remap_movc_target_temp));
  }
  // Release remap_movc_mask_temp and remap_movc_target_temp.
  PopSystemTemp(2);
}

void DxbcShaderTranslator::CompletePixelShader_ROV_AlphaToCoverageSample(
    uint32_t sample_index, float threshold, uint32_t temp,
    uint32_t temp_component) {
  DxbcDest temp_dest(DxbcDest::R(temp, 1 << temp_component));
  DxbcSrc temp_src(DxbcSrc::R(temp).Select(temp_component));
  // Check if alpha of oC0 is at or greater than the threshold.
  DxbcOpGE(temp_dest, DxbcSrc::R(system_temps_color_[0], DxbcSrc::kWWWW),
           DxbcSrc::LF(threshold));
  // Keep all bits in system_temp_rov_params_.x but the ones that need to be
  // removed in case of failure (coverage and deferred depth/stencil write are
  // removed).
  DxbcOpOr(temp_dest, temp_src,
           DxbcSrc::LU(~(uint32_t(0b00010001) << sample_index)));
  // Clear the coverage for samples that have failed the test.
  DxbcOpAnd(DxbcDest::R(system_temp_rov_params_, 0b0001),
            DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX), temp_src);
}

void DxbcShaderTranslator::CompletePixelShader_ROV_AlphaToCoverage() {
  // Check if alpha to coverage can be done at all in this shader.
  if (!writes_color_target(0)) {
    return;
  }

  // 1 VGPR or 1 SGPR.
  uint32_t temp = PushSystemTemp();
  DxbcDest temp_dest(DxbcDest::R(temp, 0b0001));
  DxbcSrc temp_src(DxbcSrc::R(temp, DxbcSrc::kXXXX));

  // Extract the flag to check if alpha to coverage is enabled (1 SGPR).
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(temp_dest,
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_AlphaToCoverage));
  // Check if alpha to coverage is enabled.
  DxbcOpIf(true, temp_src);

  // According to tests on an Adreno 200 device (LG Optimus L7), without
  // dithering, done by drawing 0.5x0.5 rectangles in different corners of four
  // pixels in a quad to a multisampled GLSurfaceView, the coverage is the
  // following for 4 samples:
  // 0.25)  [0.25, 0.5)  [0.5, 0.75)  [0.75, 1)   [1
  //  --        --           --          --       --
  // |  |      |  |         | #|        |##|     |##|
  // |  |      |# |         |# |        |# |     |##|
  //  --        --           --          --       --
  // (VPOS near 0 on the top, near 1 on the bottom here.)
  // For 2 samples, the top sample (closer to VPOS 0) is covered when alpha is
  // in [0.5, 1).
  // With these values, however, in Red Dead Redemption, almost all distant
  // trees are transparent, and it's also weird that the values are so
  // unbalanced (0.25-wide range with zero coverage, but only one point with
  // full coverage), so ranges are halfway offset here.
  // TODO(Triang3l): Find an Adreno device with dithering enabled, and where the
  // numbers 3, 1, 0, 2 look meaningful for pixels in quads, and implement
  // offsets.

  // The test must effect not only the coverage bits, but also the deferred
  // depth/stencil write bits since the coverage is zeroed for samples that have
  // failed the depth/stencil test, but stencil may still require writing - but
  // if the sample is discarded by alpha to coverage, it must not be written at
  // all.

  // Check if any MSAA is enabled.
  system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
  DxbcOpIf(true, DxbcSrc::CB(cbuffer_index_system_constants_,
                             uint32_t(CbufferRegister::kSystemConstants),
                             kSysConst_SampleCountLog2_Vec)
                     .Select(kSysConst_SampleCountLog2_Comp + 1));
  {
    // Check if MSAA is 4x or 2x.
    system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
    DxbcOpIf(true, DxbcSrc::CB(cbuffer_index_system_constants_,
                               uint32_t(CbufferRegister::kSystemConstants),
                               kSysConst_SampleCountLog2_Vec)
                       .Select(kSysConst_SampleCountLog2_Comp));
    {
      CompletePixelShader_ROV_AlphaToCoverageSample(0, 0.625f, temp, 0);
      CompletePixelShader_ROV_AlphaToCoverageSample(1, 0.375f, temp, 0);
      CompletePixelShader_ROV_AlphaToCoverageSample(2, 0.125f, temp, 0);
      CompletePixelShader_ROV_AlphaToCoverageSample(3, 0.875f, temp, 0);
    }
    // 2x MSAA is used.
    DxbcOpElse();
    {
      CompletePixelShader_ROV_AlphaToCoverageSample(0, 0.25f, temp, 0);
      CompletePixelShader_ROV_AlphaToCoverageSample(1, 0.75f, temp, 0);
    }
    // Close the 4x check.
    DxbcOpEndIf();
  }
  // MSAA is disabled.
  DxbcOpElse();
  { CompletePixelShader_ROV_AlphaToCoverageSample(0, 0.5f, temp, 0); }
  // Close the 2x/4x check.
  DxbcOpEndIf();

  // Check if any sample is still covered (the mask includes both 0:3 and 4:7
  // parts because there may be samples which passed alpha to coverage, but not
  // stencil test, and the stencil buffer needs to be modified - in this case,
  // samples would be dropped in 0:3, but not in 4:7).
  DxbcOpAnd(temp_dest, DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
            DxbcSrc::LU(0b11111111));
  DxbcOpRetC(false, temp_src);

  // Release temp.
  PopSystemTemp();

  // Close the alpha to coverage check.
  DxbcOpEndIf();
}

void DxbcShaderTranslator::CompletePixelShader_WriteToROV() {
  // Discard samples with alpha to coverage.
  CompletePixelShader_ROV_AlphaToCoverage();

  // 2 VGPR (at most, as temp when packing during blending) or 1 SGPR.
  uint32_t temp = PushSystemTemp();
  DxbcDest temp_x_dest(DxbcDest::R(temp, 0b0001));
  DxbcSrc temp_x_src(DxbcSrc::R(temp, DxbcSrc::kXXXX));

  // Do late depth/stencil test (which includes writing) if needed or deferred
  // depth writing.
  if (ROV_IsDepthStencilEarly()) {
    // Write modified depth/stencil.
    for (uint32_t i = 0; i < 4; ++i) {
      // Get if need to write to temp1.x.
      DxbcOpAnd(temp_x_dest,
                DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
                DxbcSrc::LU(1 << (4 + i)));
      // Check if need to write.
      DxbcOpIf(true, temp_x_src);
      {
        // Write the new depth/stencil.
        DxbcOpStoreUAVTyped(
            DxbcDest::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM)),
            DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY), 1,
            DxbcSrc::R(system_temp_rov_depth_stencil_).Select(i));
      }
      // Close the write check.
      DxbcOpEndIf();
      // Go to the next sample (samples are at +0, +80, +1, +81, so need to do
      // +80, -79, +80 and -81 after each sample).
      if (i < 3) {
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMResolutionSquareScale_Index;
        DxbcOpIMAd(DxbcDest::R(system_temp_rov_params_, 0b0010),
                   DxbcSrc::LI((i & 1) ? -78 - i : 80),
                   DxbcSrc::CB(cbuffer_index_system_constants_,
                               uint32_t(CbufferRegister::kSystemConstants),
                               kSysConst_EDRAMResolutionSquareScale_Vec)
                       .Select(kSysConst_EDRAMResolutionSquareScale_Comp),
                   DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY));
      }
    }
  } else {
    ROV_DepthStencilTest();
  }

  // Check if any sample is still covered after depth testing and writing, skip
  // color writing completely in this case.
  DxbcOpAnd(temp_x_dest, DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
            DxbcSrc::LU(0b1111));
  DxbcOpRetC(false, temp_x_src);

  // Write color values.
  for (uint32_t i = 0; i < 4; ++i) {
    if (!writes_color_target(i)) {
      continue;
    }

    DxbcSrc keep_mask_vec_src(
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_EDRAMRTKeepMask_Vec + (i >> 1)));
    uint32_t keep_mask_component = (i & 1) * 2;

    // Check if color writing is disabled - special keep mask constant case,
    // both 32bpp parts are forced UINT32_MAX, but also check whether the shader
    // has written anything to this target at all.

    // Combine both parts of the keep mask to check if both are 0xFFFFFFFF.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
    DxbcOpAnd(temp_x_dest, keep_mask_vec_src.Select(keep_mask_component),
              keep_mask_vec_src.Select(keep_mask_component + 1));
    // Flip the bits so both UINT32_MAX would result in 0 - not writing.
    DxbcOpNot(temp_x_dest, temp_x_src);
    // Get the bits that will be used for checking wherther the render target
    // has been written to on the taken execution path - if the write mask is
    // empty, AND zero with the test bit to always get zero.
    DxbcOpMovC(temp_x_dest, temp_x_src,
               DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
               DxbcSrc::LU(0));
    // Check if the render target was written to on the execution path.
    DxbcOpAnd(temp_x_dest, temp_x_src, DxbcSrc::LU(1 << (8 + i)));
    // Check if need to write anything to the render target.
    DxbcOpIf(true, temp_x_src);

    // Apply the exponent bias after alpha to coverage because it needs the
    // unbiased alpha from the shader.
    system_constants_used_ |= 1ull << kSysConst_ColorExpBias_Index;
    DxbcOpMul(DxbcDest::R(system_temps_color_[i]),
              DxbcSrc::R(system_temps_color_[i]),
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_ColorExpBias_Vec)
                  .Select(i));

    // Add the EDRAM bases of the render target to system_temp_rov_params_.zw.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBaseDwordsScaled_Index;
    DxbcOpIAdd(DxbcDest::R(system_temp_rov_params_, 0b1100),
               DxbcSrc::R(system_temp_rov_params_),
               DxbcSrc::CB(cbuffer_index_system_constants_,
                           uint32_t(CbufferRegister::kSystemConstants),
                           kSysConst_EDRAMRTBaseDwordsScaled_Vec)
                   .Select(i));

    DxbcSrc rt_clamp_vec_src(
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_EDRAMRTClamp_Vec + i));
    // Get if not blending to pack the color once for all 4 samples.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
    DxbcOpIEq(temp_x_dest,
              DxbcSrc::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_EDRAMRTBlendFactorsOps_Vec)
                  .Select(i),
              DxbcSrc::LU(0x00010001));
    // Check if not blending.
    DxbcOpIf(true, temp_x_src);
    {
      // Clamp the color to the render target's representable range - will be
      // packed.
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
      DxbcOpMax(DxbcDest::R(system_temps_color_[i]),
                DxbcSrc::R(system_temps_color_[i]),
                rt_clamp_vec_src.Swizzle(0b01000000));
      DxbcOpMin(DxbcDest::R(system_temps_color_[i]),
                DxbcSrc::R(system_temps_color_[i]),
                rt_clamp_vec_src.Swizzle(0b11101010));
      // Pack the color once if blending.
      ROV_PackPreClampedColor(i, system_temps_color_[i],
                              system_temps_subroutine_, 0, temp, 0, temp, 1);
    }
    // Blending is enabled.
    DxbcOpElse();
    {
      // Get if the blending source color is fixed-point for clamping if it is.
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
      DxbcOpAnd(temp_x_dest,
                DxbcSrc::CB(cbuffer_index_system_constants_,
                            uint32_t(CbufferRegister::kSystemConstants),
                            kSysConst_EDRAMRTFormatFlags_Vec)
                    .Select(i),
                DxbcSrc::LU(kRTFormatFlag_FixedPointColor));
      // Check if the blending source color is fixed-point and needs clamping.
      DxbcOpIf(true, temp_x_src);
      {
        // Clamp the blending source color if needed.
        system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
        DxbcOpMax(DxbcDest::R(system_temps_color_[i], 0b0111),
                  DxbcSrc::R(system_temps_color_[i]),
                  rt_clamp_vec_src.Select(0));
        DxbcOpMin(DxbcDest::R(system_temps_color_[i], 0b0111),
                  DxbcSrc::R(system_temps_color_[i]),
                  rt_clamp_vec_src.Select(2));
      }
      // Close the fixed-point color check.
      DxbcOpEndIf();

      // Get if the blending source alpha is fixed-point for clamping if it is.
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
      DxbcOpAnd(temp_x_dest,
                DxbcSrc::CB(cbuffer_index_system_constants_,
                            uint32_t(CbufferRegister::kSystemConstants),
                            kSysConst_EDRAMRTFormatFlags_Vec)
                    .Select(i),
                DxbcSrc::LU(kRTFormatFlag_FixedPointAlpha));
      // Check if the blending source alpha is fixed-point and needs clamping.
      DxbcOpIf(true, temp_x_src);
      {
        // Clamp the blending source alpha if needed.
        system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
        DxbcOpMax(DxbcDest::R(system_temps_color_[i], 0b1000),
                  DxbcSrc::R(system_temps_color_[i], DxbcSrc::kWWWW),
                  rt_clamp_vec_src.Select(1));
        DxbcOpMin(DxbcDest::R(system_temps_color_[i], 0b1000),
                  DxbcSrc::R(system_temps_color_[i], DxbcSrc::kWWWW),
                  rt_clamp_vec_src.Select(3));
      }
      // Close the fixed-point alpha check.
      DxbcOpEndIf();
      // Break register dependency in the color sample subroutine.
      DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b0011), DxbcSrc::LU(0));
    }
    DxbcOpEndIf();

    // Blend, mask and write all samples.
    for (uint32_t j = 0; j < 4; ++j) {
      // Get if the sample is covered.
      DxbcOpAnd(temp_x_dest,
                DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kXXXX),
                DxbcSrc::LU(1 << j));
      // Do ROP for the sample if it's covered.
      DxbcOpCallC(true, temp_x_src, DxbcSrc::Label(label_rov_color_sample_[i]));
      // Go to the next sample (samples are at +0, +80, +1, +81, so need to do
      // +80, -79, +80 and -81 after each sample).
      system_constants_used_ |= 1ull
                                << kSysConst_EDRAMResolutionSquareScale_Index;
      DxbcOpIMAd(DxbcDest::R(system_temp_rov_params_, 0b1100),
                 DxbcSrc::LI(0, 0, (j & 1) ? -78 - j : 80,
                             ((j & 1) ? -78 - j : 80) * 2),
                 DxbcSrc::CB(cbuffer_index_system_constants_,
                             uint32_t(CbufferRegister::kSystemConstants),
                             kSysConst_EDRAMResolutionSquareScale_Vec)
                     .Select(kSysConst_EDRAMResolutionSquareScale_Comp),
                 DxbcSrc::R(system_temp_rov_params_));
    }

    // Revert adding the EDRAM bases of the render target to
    // system_temp_rov_params_.zw.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBaseDwordsScaled_Index;
    DxbcOpIAdd(DxbcDest::R(system_temp_rov_params_, 0b1100),
               DxbcSrc::R(system_temp_rov_params_),
               -DxbcSrc::CB(cbuffer_index_system_constants_,
                            uint32_t(CbufferRegister::kSystemConstants),
                            kSysConst_EDRAMRTBaseDwordsScaled_Vec)
                    .Select(i));
    // Close the render target write check.
    DxbcOpEndIf();
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

  if (writes_color_target(0)) {
    // Alpha test.
    // X - mask, then masked result (SGPR for loading, VGPR for masking).
    // Y - operation result (SGPR for mask operations, VGPR for alpha
    //     operations).
    uint32_t alpha_test_temp = PushSystemTemp();
    DxbcDest alpha_test_mask_dest(DxbcDest::R(alpha_test_temp, 0b0001));
    DxbcSrc alpha_test_mask_src(DxbcSrc::R(alpha_test_temp, DxbcSrc::kXXXX));
    DxbcDest alpha_test_op_dest(DxbcDest::R(alpha_test_temp, 0b0010));
    DxbcSrc alpha_test_op_src(DxbcSrc::R(alpha_test_temp, DxbcSrc::kYYYY));
    // Extract the comparison mask to check if the test needs to be done at all.
    // Don't care about flow control being somewhat dynamic - early Z is forced
    // using a special version of the shader anyway.
    system_constants_used_ |= 1ull << kSysConst_Flags_Index;
    DxbcOpUBFE(alpha_test_mask_dest, DxbcSrc::LU(3),
               DxbcSrc::LU(kSysFlag_AlphaPassIfLess_Shift),
               DxbcSrc::CB(cbuffer_index_system_constants_,
                           uint32_t(CbufferRegister::kSystemConstants),
                           kSysConst_Flags_Vec)
                   .Select(kSysConst_Flags_Comp));
    // Compare the mask to ALWAYS to check if the test shouldn't be done (will
    // pass even for NaNs, though the expected behavior in this case hasn't been
    // checked, but let's assume this means "always", not "less, equal or
    // greater".
    // TODO(Triang3l): Check how alpha test works with NaN on Direct3D 9.
    DxbcOpINE(alpha_test_op_dest, alpha_test_mask_src, DxbcSrc::LU(0b111));
    // Don't do the test if the mode is "always".
    DxbcOpIf(true, alpha_test_op_src);
    {
      // Do the test. Can't use subtraction and sign because of float specials.
      DxbcSrc alpha_src(DxbcSrc::R(system_temps_color_[0], DxbcSrc::kWWWW));
      system_constants_used_ |= 1ull << kSysConst_AlphaTestReference_Index;
      DxbcSrc alpha_test_reference_src(
          DxbcSrc::CB(cbuffer_index_system_constants_,
                      uint32_t(CbufferRegister::kSystemConstants),
                      kSysConst_AlphaTestReference_Vec)
              .Select(kSysConst_AlphaTestReference_Comp));
      // Less than.
      DxbcOpLT(alpha_test_op_dest, alpha_src, alpha_test_reference_src);
      DxbcOpOr(alpha_test_op_dest, alpha_test_op_src,
               DxbcSrc::LU(~uint32_t(1 << 0)));
      DxbcOpAnd(alpha_test_mask_dest, alpha_test_mask_src, alpha_test_op_src);
      // Equals to.
      DxbcOpEq(alpha_test_op_dest, alpha_src, alpha_test_reference_src);
      DxbcOpOr(alpha_test_op_dest, alpha_test_op_src,
               DxbcSrc::LU(~uint32_t(1 << 1)));
      DxbcOpAnd(alpha_test_mask_dest, alpha_test_mask_src, alpha_test_op_src);
      // Greater than.
      DxbcOpLT(alpha_test_op_dest, alpha_test_reference_src, alpha_src);
      DxbcOpOr(alpha_test_op_dest, alpha_test_op_src,
               DxbcSrc::LU(~uint32_t(1 << 2)));
      DxbcOpAnd(alpha_test_mask_dest, alpha_test_mask_src, alpha_test_op_src);
      // Discard the pixel if it has failed the test.
      if (edram_rov_used_) {
        DxbcOpRetC(false, alpha_test_mask_src);
      } else {
        DxbcOpDiscard(false, alpha_test_mask_src);
      }
    }
    // Close the "not always" check.
    DxbcOpEndIf();
    // Release alpha_test_temp.
    PopSystemTemp();
  }

  // Write the values to the render targets. Not applying the exponent bias yet
  // because the original 0 to 1 alpha value is needed for alpha to coverage,
  // which is done differently for ROV and RTV/DSV.
  if (edram_rov_used_) {
    CompletePixelShader_WriteToROV();
  } else {
    CompletePixelShader_WriteToRTVs();
  }
}

void DxbcShaderTranslator::CompleteShaderCode_ROV_DepthTo24BitSubroutine() {
  DxbcOpLabel(DxbcSrc::Label(label_rov_depth_to_24bit_));

  DxbcDest depth_dest(DxbcDest::R(system_temps_subroutine_, 0b0001));
  DxbcSrc depth_src(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
  DxbcDest temp_dest(DxbcDest::R(system_temps_subroutine_, 0b0010));
  DxbcSrc temp_src(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));

  // Extract the depth format to Y. Take 1 SGPR.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(temp_dest,
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_ROVDepthFloat24));
  // Convert according to the format. Release 1 SGPR.
  DxbcOpIf(true, temp_src);
  {
    // 20e4 conversion, using 1 VGPR.
    // CFloat24 from d3dref9.dll.
    // Assuming the depth is already clamped to [0, 2) (in all places, the depth
    // is written with the saturate flag set).

    // Check if the number is too small to be represented as normalized 20e4.
    // temp = f32 < 2^-14
    DxbcOpULT(temp_dest, depth_src, DxbcSrc::LU(0x38800000));
    // Handle denormalized numbers separately.
    DxbcOpIf(true, temp_src);
    {
      // temp = f32 >> 23
      DxbcOpUShR(temp_dest, depth_src, DxbcSrc::LU(23));
      // temp = 113 - (f32 >> 23)
      DxbcOpIAdd(temp_dest, DxbcSrc::LI(113), -temp_src);
      // Don't allow the shift to overflow, since in DXBC the lower 5 bits of
      // the shift amount are used (otherwise 0 becomes 8).
      // temp = min(113 - (f32 >> 23), 24)
      DxbcOpUMin(temp_dest, temp_src, DxbcSrc::LU(24));
      // biased_f32 = (f32 & 0x7FFFFF) | 0x800000
      DxbcOpBFI(depth_dest, DxbcSrc::LU(9), DxbcSrc::LU(23), DxbcSrc::LU(1),
                depth_src);
      // biased_f32 =
      //     ((f32 & 0x7FFFFF) | 0x800000) >> min(113 - (f32 >> 23), 24)
      DxbcOpUShR(depth_dest, depth_src, temp_src);
    }
    // Not denormalized?
    DxbcOpElse();
    {
      // Bias the exponent.
      // biased_f32 = f32 + (-112 << 23)
      // (left shift of a negative value is undefined behavior)
      DxbcOpIAdd(depth_dest, depth_src, DxbcSrc::LU(0xC8000000u));
    }
    // Close the denormal check.
    DxbcOpEndIf();
    // Build the 20e4 number.
    // temp = (biased_f32 >> 3) & 1
    DxbcOpUBFE(temp_dest, DxbcSrc::LU(1), DxbcSrc::LU(3), depth_src);
    // f24 = biased_f32 + 3
    DxbcOpIAdd(depth_dest, depth_src, DxbcSrc::LU(3));
    // f24 = biased_f32 + 3 + ((biased_f32 >> 3) & 1)
    DxbcOpIAdd(depth_dest, depth_src, temp_src);
    // f24 = ((biased_f32 + 3 + ((biased_f32 >> 3) & 1)) >> 3) & 0xFFFFFF
    DxbcOpUBFE(depth_dest, DxbcSrc::LU(24), DxbcSrc::LU(3), depth_src);
  }
  DxbcOpElse();
  {
    // Unorm24 conversion.

    // Multiply by float(0xFFFFFF).
    DxbcOpMul(depth_dest, depth_src, DxbcSrc::LF(16777215.0f));
    // Round to the nearest even integer. This seems to be the correct way:
    // rounding towards zero gives 0xFF instead of 0x100 in clear shaders in,
    // for instance, Halo 3, but other clear shaders in it are also broken if
    // 0.5 is added before ftou instead of round_ne.
    DxbcOpRoundNE(depth_dest, depth_src);
    // Convert to fixed-point.
    DxbcOpFToU(depth_dest, depth_src);
  }
  DxbcOpEndIf();

  DxbcOpRet();
}

void DxbcShaderTranslator::
    CompleteShaderCode_ROV_DepthStencilSampleSubroutine() {
  DxbcOpLabel(DxbcSrc::Label(label_rov_depth_stencil_sample_));
  // Load the old depth/stencil value to VGPR [0].z.
  // VGPR [0].x = new depth
  // VGPR [0].z = old depth/stencil
  DxbcOpLdUAVTyped(DxbcDest::R(system_temps_subroutine_, 0b0100),
                   DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY), 1,
                   DxbcSrc::U(ROV_GetEDRAMUAVIndex(),
                              uint32_t(UAVRegister::kEDRAM), DxbcSrc::kXXXX));
  // Extract the old depth part to VGPR [0].w.
  // VGPR [0].x = new depth
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  DxbcOpUShR(DxbcDest::R(system_temps_subroutine_, 0b1000),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
             DxbcSrc::LU(8));
  // Get the difference between the new and the old depth, > 0 - greater, == 0 -
  // equal, < 0 - less, to VGPR [1].x.
  // VGPR [0].x = new depth
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // VGPR [1].x = depth difference
  DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
             -DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW));
  // Check if the depth is "less" or "greater or equal" to VGPR [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth difference less than 0
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // VGPR [1].x = depth difference
  DxbcOpILT(DxbcDest::R(system_temps_subroutine_, 0b0010),
            DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX),
            DxbcSrc::LI(0));
  // Choose the passed depth function bits for "less" or for "greater" to VGPR
  // [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth function passed bits for "less" or "greater"
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // VGPR [1].x = depth difference
  DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0010),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
             DxbcSrc::LU(kSysFlag_ROVDepthPassIfLess),
             DxbcSrc::LU(kSysFlag_ROVDepthPassIfGreater));
  // Do the "equal" testing to VGPR [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth function passed bits
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0010),
             DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
             DxbcSrc::LU(kSysFlag_ROVDepthPassIfEqual));
  // Mask the resulting bits with the ones that should pass to VGPR [0].y.
  // VGPR [0].x = new depth
  // VGPR [0].y = masked depth function passed bits
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0010),
            DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp));
  // Set bit 0 of the result to 0 (passed) or 1 (reject) based on the result of
  // the depth test.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0010),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
             DxbcSrc::LU(0), DxbcSrc::LU(1));
  // Extract the depth write flag to SGPR [1].x.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // VGPR [0].w = old depth
  // SGPR [1].x = depth write mask
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_ROVDepthWrite));
  // If depth writing is disabled, don't change the depth.
  // VGPR [0].x = new depth
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0001),
             DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW));
  // Create packed depth/stencil, with the stencil value unchanged at this
  // point.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  DxbcOpBFI(DxbcDest::R(system_temps_subroutine_, 0b0001), DxbcSrc::LU(24),
            DxbcSrc::LU(8),
            DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
            DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
  // Extract the stencil test bit to SGPR [0].w.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  // SGPR [0].w = stencil test enabled
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1000),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_ROVStencilTest));
  // Check if stencil test is enabled.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth test failure
  // VGPR [0].z = old depth/stencil
  DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW));
  {
    // Check the current face to get the reference and apply the read mask.
    DxbcOpIf(true, DxbcSrc::V(uint32_t(InOutRegister::kPSInFrontFace),
                              DxbcSrc::kXXXX));
    DxbcSrc stencil_front_src(
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_EDRAMStencil_Front_Vec));
    DxbcSrc stencil_back_src(
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_EDRAMStencil_Back_Vec));
    system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
    for (uint32_t i = 0; i < 2; ++i) {
      if (i) {
        // Go to the back face.
        DxbcOpElse();
      }
      DxbcSrc stencil_side_src(i ? stencil_back_src : stencil_front_src);
      // Copy the read-masked stencil reference to VGPR [0].w.
      // VGPR [0].x = new depth/stencil
      // VGPR [0].y = depth test failure
      // VGPR [0].z = old depth/stencil
      // VGPR [0].w = read-masked stencil reference
      DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                stencil_side_src.Select(kSysConst_EDRAMStencil_Reference_Comp),
                stencil_side_src.Select(kSysConst_EDRAMStencil_ReadMask_Comp));
      // Read-mask the old stencil value to VGPR [1].x.
      // VGPR [0].x = new depth/stencil
      // VGPR [0].y = depth test failure
      // VGPR [0].z = old depth/stencil
      // VGPR [0].w = read-masked stencil reference
      // VGPR [1].x = read-masked old stencil
      DxbcOpAnd(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
                DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
                stencil_side_src.Select(kSysConst_EDRAMStencil_ReadMask_Comp));
    }
    // Close the face check.
    DxbcOpEndIf();
    // Get the difference between the new and the old stencil, > 0 - greater,
    // == 0 - equal, < 0 - less, to VGPR [0].w.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil difference
    DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_, 0b1000),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
               -DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    // Check if the stencil is "less" or "greater or equal" to VGPR [1].x.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil difference
    // VGPR [1].x = stencil difference less than 0
    DxbcOpILT(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
              DxbcSrc::LI(0));
    // Choose the passed depth function bits for "less" or for "greater" to VGPR
    // [0].y.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil difference
    // VGPR [1].x = stencil function passed bits for "less" or "greater"
    DxbcOpMovC(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
               DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX),
               DxbcSrc::LU(0b001), DxbcSrc::LU(0b100));
    // Do the "equal" testing to VGPR [0].w.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil function passed bits
    DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b1000),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
               DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX),
               DxbcSrc::LU(0b010));
    // Get the comparison function and the operations for the current face to
    // VGPR [1].x.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil function passed bits
    // VGPR [1].x = stencil function and operations
    system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
    DxbcOpMovC(
        DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
        DxbcSrc::V(uint32_t(InOutRegister::kPSInFrontFace), DxbcSrc::kXXXX),
        stencil_front_src.Select(kSysConst_EDRAMStencil_FuncOps_Comp),
        stencil_back_src.Select(kSysConst_EDRAMStencil_FuncOps_Comp));
    // Mask the resulting bits with the ones that should pass to VGPR [0].w (the
    // comparison function is in the low 3 bits of the constant, and only ANDing
    // 3-bit values with it, so safe not to UBFE the function).
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil test result
    // VGPR [1].x = stencil function and operations
    DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1000),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
              DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    // Choose the stencil pass operation depending on whether depth test has
    // failed.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil test result
    // VGPR [1].x = stencil function and operations
    // VGPR [1].y = pass or depth fail operation shift
    DxbcOpMovC(DxbcDest::R(system_temps_subroutine_ + 1, 0b0010),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
               DxbcSrc::LU(9), DxbcSrc::LU(6));
    // Merge the depth/stencil test results to VGPR [0].y.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil test result
    // VGPR [1].x = stencil function and operations
    // VGPR [1].y = pass or depth fail operation shift
    DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0010),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
               DxbcSrc::LU(1));
    // Choose the final operation to according to whether the stencil test has
    // passed.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil operation shift
    // VGPR [1].x = stencil function and operations
    DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b1000),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
               DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kYYYY),
               DxbcSrc::LU(3));
    // Extract the needed stencil operation to VGPR [0].w.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = stencil operation
    DxbcOpUBFE(DxbcDest::R(system_temps_subroutine_, 0b1000), DxbcSrc::LU(3),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
               DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    // Open the stencil operation switch for writing the new stencil (not caring
    // about bits 8:31) to VGPR [0].w.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    DxbcOpSwitch(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW));
    {
      // Zero.
      DxbcOpCase(DxbcSrc::LU(uint32_t(StencilOp::kZero)));
      {
        DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b1000),
                  DxbcSrc::LU(0));
      }
      DxbcOpBreak();
      // Replace.
      DxbcOpCase(DxbcSrc::LU(uint32_t(StencilOp::kReplace)));
      {
        system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
        DxbcOpMovC(
            DxbcDest::R(system_temps_subroutine_, 0b1000),
            DxbcSrc::V(uint32_t(InOutRegister::kPSInFrontFace), DxbcSrc::kXXXX),
            stencil_front_src.Select(kSysConst_EDRAMStencil_Reference_Comp),
            stencil_back_src.Select(kSysConst_EDRAMStencil_Reference_Comp));
      }
      DxbcOpBreak();
      // Increment and clamp.
      DxbcOpCase(DxbcSrc::LU(uint32_t(StencilOp::kIncrementClamp)));
      {
        // Clear the upper bits for saturation.
        DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
                  DxbcSrc::LU(UINT8_MAX));
        // Increment.
        DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                   DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
                   DxbcSrc::LI(1));
        // Clamp.
        DxbcOpIMin(DxbcDest::R(system_temps_subroutine_, 0b1000),
                   DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
                   DxbcSrc::LI(UINT8_MAX));
      }
      DxbcOpBreak();
      // Decrement and clamp.
      DxbcOpCase(DxbcSrc::LU(uint32_t(StencilOp::kDecrementClamp)));
      {
        // Clear the upper bits for saturation.
        DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
                  DxbcSrc::LU(UINT8_MAX));
        // Decrement.
        DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                   DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
                   DxbcSrc::LI(-1));
        // Clamp.
        DxbcOpIMax(DxbcDest::R(system_temps_subroutine_, 0b1000),
                   DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
                   DxbcSrc::LI(0));
      }
      DxbcOpBreak();
      // Invert.
      DxbcOpCase(DxbcSrc::LU(uint32_t(StencilOp::kInvert)));
      {
        DxbcOpNot(DxbcDest::R(system_temps_subroutine_, 0b1000),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
      }
      DxbcOpBreak();
      // Increment/decrement and wrap.
      for (uint32_t i = 0; i < 2; ++i) {
        DxbcOpCase(DxbcSrc::LU(uint32_t(i ? StencilOp::kDecrementWrap
                                          : StencilOp::kIncrementWrap)));
        {
          DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                     DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
                     DxbcSrc::LI(i ? -1 : 1));
        }
        DxbcOpBreak();
      }
      // Keep.
      DxbcOpDefault();
      {
        DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b1000),
                  DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
      }
      DxbcOpBreak();
    }
    // Close the new stencil switch.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = unmasked new stencil
    DxbcOpEndSwitch();
    // Select the stencil write mask for the face to VGPR [1].x.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = unmasked new stencil
    // VGPR [1].x = stencil write mask
    system_constants_used_ |= 1ull << kSysConst_EDRAMStencil_Index;
    DxbcOpMovC(
        DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
        DxbcSrc::V(uint32_t(InOutRegister::kPSInFrontFace), DxbcSrc::kXXXX),
        stencil_front_src.Select(kSysConst_EDRAMStencil_WriteMask_Comp),
        stencil_back_src.Select(kSysConst_EDRAMStencil_WriteMask_Comp));
    // Apply the write mask to the new stencil, also dropping the upper 24 bits.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = masked new stencil
    // VGPR [1].x = stencil write mask
    DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1000),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW),
              DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    // Invert the write mask for keeping the old stencil and the depth bits to
    // VGPR [1].x.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = masked new stencil
    // VGPR [1].x = inverted stencil write mask
    DxbcOpNot(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
              DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    // Remove the bits that will be replaced from the new combined
    // depth/stencil.
    // VGPR [0].x = masked new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    // VGPR [0].w = masked new stencil
    DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
              DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    // Merge the old and the new stencil.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    // VGPR [0].z = old depth/stencil
    DxbcOpOr(DxbcDest::R(system_temps_subroutine_, 0b0001),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW));
  }
  // Close the stencil test check.
  DxbcOpEndIf();

  // Check if the depth/stencil has failed not to modify the depth if it has.
  DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
  {
    // If the depth/stencil test has failed, don't change the depth.
    DxbcOpBFI(DxbcDest::R(system_temps_subroutine_, 0b0001), DxbcSrc::LU(8),
              DxbcSrc::LU(0),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
              DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
  }
  // Close the depth/stencil failure check.
  DxbcOpEndIf();
  // Check if need to write - if depth/stencil is different - to VGPR [0].z.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  // VGPR [0].z = whether depth/stencil has changed
  DxbcOpINE(DxbcDest::R(system_temps_subroutine_, 0b0100),
            DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
            DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
  // Check if need to write.
  // VGPR [0].x = new depth/stencil
  // VGPR [0].y = depth/stencil test failure
  DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
  {
    bool depth_stencil_early = ROV_IsDepthStencilEarly();
    if (depth_stencil_early) {
      // Get if early depth/stencil write is enabled to SGPR [0].z.
      // VGPR [0].x = new depth/stencil
      // VGPR [0].y = depth/stencil test failure
      // SGPR [0].z = whether early depth/stencil write is enabled
      system_constants_used_ |= 1ull << kSysConst_Flags_Index;
      DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0100),
                DxbcSrc::CB(cbuffer_index_system_constants_,
                            uint32_t(CbufferRegister::kSystemConstants),
                            kSysConst_Flags_Vec)
                    .Select(kSysConst_Flags_Comp),
                DxbcSrc::LU(kSysFlag_ROVDepthStencilEarlyWrite));
      // Check if need to write early.
      // VGPR [0].x = new depth/stencil
      // VGPR [0].y = depth/stencil test failure
      DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
    }
    // Write the new depth/stencil.
    // VGPR [0].x = new depth/stencil
    // VGPR [0].y = depth/stencil test failure
    DxbcOpStoreUAVTyped(
        DxbcDest::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM)),
        DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kYYYY), 1,
        DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
    if (depth_stencil_early) {
      // Need to still run the shader to know whether to write the depth/stencil
      // value.
      DxbcOpElse();
      // Set bit 4 of the result if need to write later (after checking if the
      // sample is not discarded by a kill instruction, alphatest or
      // alpha-to-coverage).
      // VGPR [0].x = new depth/stencil
      // VGPR [0].y = depth/stencil test failure, deferred write bits
      DxbcOpOr(DxbcDest::R(system_temps_subroutine_, 0b0010),
               DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
               DxbcSrc::LU(1 << 4));
      // Close the early depth/stencil check.
      DxbcOpEndIf();
    }
  }
  // Close the write check.
  DxbcOpEndIf();
  // End the subroutine.
  DxbcOpRet();
}

void DxbcShaderTranslator::CompleteShaderCode_ROV_ColorSampleSubroutine(
    uint32_t rt_index) {
  DxbcOpLabel(DxbcSrc::Label(label_rov_color_sample_[rt_index]));

  DxbcSrc keep_mask_vec_src(
      DxbcSrc::CB(cbuffer_index_system_constants_,
                  uint32_t(CbufferRegister::kSystemConstants),
                  kSysConst_EDRAMRTKeepMask_Vec + (rt_index >> 1)));
  uint32_t keep_mask_component = (rt_index & 1) * 2;
  uint32_t keep_mask_swizzle = (rt_index & 1) ? 0b1110 : 0b0100;

  DxbcSrc rt_format_flags_src(
      DxbcSrc::CB(cbuffer_index_system_constants_,
                  uint32_t(CbufferRegister::kSystemConstants),
                  kSysConst_EDRAMRTFormatFlags_Vec)
          .Select(rt_index));
  DxbcSrc rt_clamp_vec_src(
      DxbcSrc::CB(cbuffer_index_system_constants_,
                  uint32_t(CbufferRegister::kSystemConstants),
                  kSysConst_EDRAMRTClamp_Vec + rt_index));
  DxbcSrc rt_blend_factors_ops_src(
      DxbcSrc::CB(cbuffer_index_system_constants_,
                  uint32_t(CbufferRegister::kSystemConstants),
                  kSysConst_EDRAMRTBlendFactorsOps_Vec)
          .Select(rt_index));

  // ***************************************************************************
  // Checking if color loading must be done - if any component needs to be kept
  // or if blending is enabled.
  // ***************************************************************************

  // Check if need to keep any components to SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - whether any components must be kept (OR of keep masks).
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
  DxbcOpOr(DxbcDest::R(system_temps_subroutine_, 0b0100),
           keep_mask_vec_src.Select(keep_mask_component),
           keep_mask_vec_src.Select(keep_mask_component + 1));
  // Blending isn't done if it's 1 * source + 0 * destination. But since the
  // previous color also needs to be loaded if any original components need to
  // be kept, force the blend control to something with blending in this case
  // in SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - blending mode used to check if need to load.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
  DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0100),
             DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
             DxbcSrc::LU(0), rt_blend_factors_ops_src);
  // Get if the blend control requires loading the color to SGPR [0].z.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  // SGPR [0].z - whether need to load the color.
  DxbcOpINE(DxbcDest::R(system_temps_subroutine_, 0b0100),
            DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ),
            DxbcSrc::LU(0x00010001));
  // Check if need to do something with the previous color.
  // VGPRs [0].xy - packed source color/alpha if not blending.
  DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
  {
    // *************************************************************************
    // Loading the previous color to SGPR [0].zw.
    // *************************************************************************

    // Get if the format is 64bpp to SGPR [0].z.
    // VGPRs [0].xy - packed source color/alpha if not blending.
    // SGPR [0].z - whether the render target is 64bpp.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
    DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0100),
              rt_format_flags_src, DxbcSrc::LU(kRTFormatFlag_64bpp));
    // Check if the format is 64bpp.
    // VGPRs [0].xy - packed source color/alpha if not blending.
    DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
    {
      // Load the lower 32 bits of the 64bpp color to VGPR [0].z.
      // VGPRs [0].xy - packed source color/alpha if not blending.
      // VGPR [0].z - lower 32 bits of the packed color.
      DxbcOpLdUAVTyped(
          DxbcDest::R(system_temps_subroutine_, 0b0100),
          DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kWWWW), 1,
          DxbcSrc::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM),
                     DxbcSrc::kXXXX));
      // Get the address of the upper 32 bits of the color to VGPR [0].w.
      // VGPRs [0].xy - packed source color/alpha if not blending.
      // VGPR [0].z - lower 32 bits of the packed color.
      // VGPR [0].w - address of the upper 32 bits of the packed color.
      DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_, 0b1000),
                 DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kWWWW),
                 DxbcSrc::LU(1));
      // Load the upper 32 bits of the 64bpp color to VGPR [0].w.
      // VGPRs [0].xy - packed source color/alpha if not blending.
      // VGPRs [0].zw - packed destination color/alpha.
      DxbcOpLdUAVTyped(
          DxbcDest::R(system_temps_subroutine_, 0b1000),
          DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kWWWW), 1,
          DxbcSrc::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM),
                     DxbcSrc::kXXXX));
    }
    // The color is 32bpp.
    DxbcOpElse();
    {
      // Load the 32bpp color to VGPR [0].z.
      // VGPRs [0].xy - packed source color/alpha if not blending.
      // VGPR [0].z - packed 32bpp destination color.
      DxbcOpLdUAVTyped(
          DxbcDest::R(system_temps_subroutine_, 0b0100),
          DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ), 1,
          DxbcSrc::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM),
                     DxbcSrc::kXXXX));
      // Break register dependency in VGPR [0].w if the color is 32bpp.
      // VGPRs [0].xy - packed source color/alpha if not blending.
      // VGPRs [0].zw - packed destination color/alpha.
      DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b1000), DxbcSrc::LU(0));
    }
    // Close the color format check.
    DxbcOpEndIf();

    // Get if blending is enabled to SGPR [1].x.
    // VGPRs [0].xy - packed source color/alpha if not blending.
    // VGPRs [0].zw - packed destination color/alpha.
    // SGPR [1].x - whether blending is enabled.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
    DxbcOpINE(DxbcDest::R(system_temps_subroutine_ + 1, 0b0001),
              rt_blend_factors_ops_src, DxbcSrc::LU(0x00010001));
    // Check if need to blend.
    // VGPRs [0].xy - packed source color/alpha if not blending.
    // VGPRs [0].zw - packed destination color/alpha.
    DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kXXXX));
    {
      // Now, when blending is enabled, registers [0].xy are used as scratch.

      // Unpack the destination color to VGPRs [1].xyzw, using [0].xy as temps.
      // The destination color never needs clamping because out-of-range values
      // can't be loaded.
      // VGPRs [0].zw - packed destination color/alpha.
      // VGPRs [1].xyzw - destination color/alpha.
      ROV_UnpackColor(rt_index, system_temps_subroutine_, 2,
                      system_temps_subroutine_ + 1, system_temps_subroutine_, 0,
                      system_temps_subroutine_, 1);

      // ***********************************************************************
      // Color blending.
      // ***********************************************************************

      // Extract the color min/max bit to SGPR [0].x.
      // SGPR [0].x - whether min/max should be used for color.
      // VGPRs [0].zw - packed destination color/alpha.
      // VGPRs [1].xyzw - destination color/alpha.
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
      DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                rt_blend_factors_ops_src, DxbcSrc::LU(1 << (5 + 1)));
      // Check if need to do min/max for color.
      // VGPRs [0].zw - packed destination color/alpha.
      // VGPRs [1].xyzw - destination color/alpha.
      DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
      {
        // Extract the color min (0) or max (1) bit to SGPR [0].x.
        // SGPR [0].x - whether min or max should be used for color.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyzw - destination color/alpha.
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMRTBlendFactorsOps_Index;
        DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                  rt_blend_factors_ops_src, DxbcSrc::LU(1 << 5));
        // Check if need to do min or max for color.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyzw - destination color/alpha.
        DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        {
          // Do max of the colors without applying the factors to VGPRs [1].xyz.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - blended color, destination alpha.
          DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                    DxbcSrc::R(system_temps_color_[rt_index]),
                    DxbcSrc::R(system_temps_subroutine_ + 1));
        }
        // Need to do min.
        DxbcOpElse();
        {
          // Do min of the colors without applying the factors to VGPRs [1].xyz.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - blended color, destination alpha.
          DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                    DxbcSrc::R(system_temps_color_[rt_index]),
                    DxbcSrc::R(system_temps_subroutine_ + 1));
        }
        // Close the min or max check.
        DxbcOpEndIf();
      }
      // Need to do blend colors with the factors.
      DxbcOpElse();
      {
        // Extract the source color factor to SGPR [0].x.
        // SGPR [0].x - source color factor index.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyzw - destination color/alpha.
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMRTBlendFactorsOps_Index;
        DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                  rt_blend_factors_ops_src, DxbcSrc::LU((1 << 5) - 1));
        // Check if the source color factor is not zero - if it is, the source
        // must be ignored completely, and Infinity and NaN in it shouldn't
        // affect blending.
        DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        {
          // Open the switch for choosing the source color blend factor.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          DxbcOpSwitch(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Write the source color factor to VGPRs [2].xyz.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyzw - destination color/alpha.
            // VGPRs [2].xyz - unclamped source color factor.
            ROV_HandleColorBlendFactorCases(system_temps_color_[rt_index],
                                            system_temps_subroutine_ + 1,
                                            system_temps_subroutine_ + 2);
          }
          // Close the source color factor switch.
          DxbcOpEndSwitch();
          // Get if the render target color is fixed-point and the source color
          // factor needs clamping to SGPR [0].x.
          // SGPR [0].x - whether color is fixed-point.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - unclamped source color factor.
          system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                    rt_format_flags_src,
                    DxbcSrc::LU(kRTFormatFlag_FixedPointColor));
          // Check if the source color factor needs clamping.
          DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Clamp the source color factor in VGPRs [2].xyz.
            // SGPR [0].x - whether color is fixed-point.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyzw - destination color/alpha.
            // VGPRs [2].xyz - source color factor.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 2),
                      rt_clamp_vec_src.Select(0));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 2),
                      rt_clamp_vec_src.Select(2));
          }
          // Close the source color factor clamping check.
          DxbcOpEndIf();
          // Apply the factor to the source color.
          // SGPR [0].x - whether color is fixed-point.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - unclamped source color part without addition sign.
          DxbcOpMul(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                    DxbcSrc::R(system_temps_color_[rt_index]),
                    DxbcSrc::R(system_temps_subroutine_ + 2));
          // Check if the source color part needs clamping after the
          // multiplication.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - unclamped source color part without addition sign.
          DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Clamp the source color part.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyzw - destination color/alpha.
            // VGPRs [2].xyz - source color part without addition sign.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 2),
                      rt_clamp_vec_src.Select(0));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 2),
                      rt_clamp_vec_src.Select(2));
          }
          // Close the source color part clamping check.
          DxbcOpEndIf();
          // Extract the source color sign to SGPR [0].x.
          // SGPR [0].x - source color sign as zero for 1 and non-zero for -1.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - source color part without addition sign.
          system_constants_used_ |= 1ull
                                    << kSysConst_EDRAMRTBlendFactorsOps_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                    rt_blend_factors_ops_src, DxbcSrc::LU(1 << (5 + 2)));
          // Apply the source color sign.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - source color part.
          DxbcOpMovC(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                     DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                     -DxbcSrc::R(system_temps_subroutine_ + 2),
                     DxbcSrc::R(system_temps_subroutine_ + 2));
        }
        // The source color factor is zero.
        DxbcOpElse();
        {
          // Write zero to the source color part.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - source color part.
          DxbcOpMov(DxbcDest::R(system_temps_subroutine_ + 2, 0b0111),
                    DxbcSrc::LF(0.0f));
        }
        // Close the source color factor zero check.
        DxbcOpEndIf();

        // Extract the destination color factor to SGPR [0].x.
        // SGPR [0].x - destination color factor index.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyzw - destination color/alpha.
        // VGPRs [2].xyz - source color part.
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMRTBlendFactorsOps_Index;
        DxbcOpUBFE(DxbcDest::R(system_temps_subroutine_, 0b0001),
                   DxbcSrc::LU(5), DxbcSrc::LU(8), rt_blend_factors_ops_src);
        // Check if the destination color factor is not zero.
        DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        {
          // Open the switch for choosing the destination color blend factor.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - source color part.
          DxbcOpSwitch(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Write the destination color factor to VGPRs [3].xyz.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyzw - destination color/alpha.
            // VGPRs [2].xyz - source color part.
            // VGPRs [3].xyz - unclamped destination color factor.
            ROV_HandleColorBlendFactorCases(system_temps_color_[rt_index],
                                            system_temps_subroutine_ + 1,
                                            system_temps_subroutine_ + 3);
          }
          // Close the destination color factor switch.
          DxbcOpEndSwitch();
          // Get if the render target color is fixed-point and the destination
          // color factor needs clamping to SGPR [0].x.
          // SGPR [0].x - whether color is fixed-point.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyzw - destination color/alpha.
          // VGPRs [2].xyz - source color part.
          // VGPRs [3].xyz - unclamped destination color factor.
          system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                    rt_format_flags_src,
                    DxbcSrc::LU(kRTFormatFlag_FixedPointColor));
          // Check if the destination color factor needs clamping.
          DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Clamp the destination color factor in VGPRs [3].xyz.
            // SGPR [0].x - whether color is fixed-point.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyzw - destination color/alpha.
            // VGPRs [2].xyz - source color part.
            // VGPRs [3].xyz - destination color factor.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 3, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 3),
                      rt_clamp_vec_src.Select(0));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 3, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 3),
                      rt_clamp_vec_src.Select(2));
          }
          // Close the destination color factor clamping check.
          DxbcOpEndIf();
          // Apply the factor to the destination color in VGPRs [1].xyz.
          // SGPR [0].x - whether color is fixed-point.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - unclamped destination color part without addition
          //                 sign.
          // VGPR [1].w - destination alpha.
          // VGPRs [2].xyz - source color part.
          DxbcOpMul(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                    DxbcSrc::R(system_temps_subroutine_ + 1),
                    DxbcSrc::R(system_temps_subroutine_ + 3));
          // Check if the destination color part needs clamping after the
          // multiplication.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - unclamped destination color part without addition
          //                 sign.
          // VGPR [1].w - destination alpha.
          // VGPRs [2].xyz - source color part.
          DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Clamp the destination color part.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - destination color part without addition sign.
            // VGPR [1].w - destination alpha.
            // VGPRs [2].xyz - source color part.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 1),
                      rt_clamp_vec_src.Select(0));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                      DxbcSrc::R(system_temps_subroutine_ + 1),
                      rt_clamp_vec_src.Select(2));
          }
          // Close the destination color part clamping check.
          DxbcOpEndIf();
          // Extract the destination color sign to SGPR [0].x.
          // SGPR [0].x - destination color sign as zero for 1 and non-zero for
          //              -1.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - destination color part without addition sign.
          // VGPR [1].w - destination alpha.
          // VGPRs [2].xyz - source color part.
          system_constants_used_ |= 1ull
                                    << kSysConst_EDRAMRTBlendFactorsOps_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                    rt_blend_factors_ops_src, DxbcSrc::LU(1 << 5));
          // Select the sign for destination multiply-add as 1.0 or -1.0 to
          // SGPR [0].x.
          // SGPR [0].x - destination color sign as float.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - destination color part without addition sign.
          // VGPR [1].w - destination alpha.
          // VGPRs [2].xyz - source color part.
          DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0001),
                     DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                     DxbcSrc::LF(-1.0f), DxbcSrc::LF(1.0f));
          // Perform color blending to VGPRs [1].xyz.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - unclamped blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpMAd(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                    DxbcSrc::R(system_temps_subroutine_ + 1),
                    DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                    DxbcSrc::R(system_temps_subroutine_ + 2));
        }
        // The destination color factor is zero.
        DxbcOpElse();
        {
          // Write the source color part without applying the destination color.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - unclamped blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpMov(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                    DxbcSrc::R(system_temps_subroutine_ + 2));
        }
        // Close the destination color factor zero check.
        DxbcOpEndIf();

        // Clamp the color in VGPRs [1].xyz before packing.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyz - blended color.
        // VGPR [1].w - destination alpha.
        system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
        DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                  DxbcSrc::R(system_temps_subroutine_ + 1),
                  rt_clamp_vec_src.Select(0));
        DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 1, 0b0111),
                  DxbcSrc::R(system_temps_subroutine_ + 1),
                  rt_clamp_vec_src.Select(2));
      }
      // Close the color min/max enabled check.
      DxbcOpEndIf();

      // ***********************************************************************
      // Alpha blending.
      // ***********************************************************************

      // Extract the alpha min/max bit to SGPR [0].x.
      // SGPR [0].x - whether min/max should be used for alpha.
      // VGPRs [0].zw - packed destination color/alpha.
      // VGPRs [1].xyz - blended color.
      // VGPR [1].w - destination alpha.
      system_constants_used_ |= 1ull << kSysConst_EDRAMRTBlendFactorsOps_Index;
      DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                rt_blend_factors_ops_src, DxbcSrc::LU(1 << (21 + 1)));
      // Check if need to do min/max for alpha.
      // VGPRs [0].zw - packed destination color/alpha.
      // VGPRs [1].xyz - blended color.
      // VGPR [1].w - destination alpha.
      DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
      {
        // Extract the alpha min (0) or max (1) bit to SGPR [0].x.
        // SGPR [0].x - whether min or max should be used for alpha.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyz - blended color.
        // VGPR [1].w - destination alpha.
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMRTBlendFactorsOps_Index;
        DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0001),
                  rt_blend_factors_ops_src, DxbcSrc::LU(1 << 21));
        // Check if need to do min or max for alpha.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyz - blended color.
        // VGPR [1].w - destination alpha.
        DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        {
          // Do max of the alphas without applying the factors to VGPRs [1].xyz.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color/alpha.
          DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                    DxbcSrc::R(system_temps_color_[rt_index], DxbcSrc::kWWWW),
                    DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW));
        }
        // Need to do min.
        DxbcOpElse();
        {
          // Do min of the alphas without applying the factors to VGPRs [1].xyz.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color/alpha.
          DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                    DxbcSrc::R(system_temps_color_[rt_index], DxbcSrc::kWWWW),
                    DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW));
        }
        // Close the min or max check.
        DxbcOpEndIf();
      }
      // Need to do blend alphas with the factors.
      DxbcOpElse();
      {
        // Extract the source alpha factor to SGPR [0].x.
        // SGPR [0].x - source alpha factor index.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyz - blended color.
        // VGPR [1].w - destination alpha.
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMRTBlendFactorsOps_Index;
        DxbcOpUBFE(DxbcDest::R(system_temps_subroutine_, 0b0001),
                   DxbcSrc::LU(5), DxbcSrc::LU(16), rt_blend_factors_ops_src);
        // Check if the source alpha factor is not zero.
        DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        {
          // Open the switch for choosing the source alpha blend factor.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpSwitch(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          {
            // Write the source alpha factor to VGPR [0].x.
            // VGPR [0].x - unclamped source alpha factor.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - blended color.
            // VGPR [1].w - destination alpha.
            ROV_HandleAlphaBlendFactorCases(system_temps_color_[rt_index],
                                            system_temps_subroutine_ + 1,
                                            system_temps_subroutine_, 0);
          }
          // Close the source alpha factor switch.
          DxbcOpEndSwitch();
          // Get if the render target alpha is fixed-point and the source alpha
          // factor needs clamping to SGPR [0].y.
          // VGPR [0].x - unclamped source alpha factor.
          // SGPR [0].y - whether alpha is fixed-point.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0010),
                    rt_format_flags_src,
                    DxbcSrc::LU(kRTFormatFlag_FixedPointAlpha));
          // Check if the source alpha factor needs clamping.
          DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
          {
            // Clamp the source alpha factor in VGPR [0].x.
            // VGPR [0].x - source alpha factor.
            // SGPR [0].y - whether alpha is fixed-point.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - blended color.
            // VGPR [1].w - destination alpha.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_, 0b0001),
                      DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                      rt_clamp_vec_src.Select(1));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_, 0b0001),
                      DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                      rt_clamp_vec_src.Select(3));
          }
          // Close the source alpha factor clamping check.
          DxbcOpEndIf();
          // Apply the factor to the source alpha.
          // VGPR [0].x - unclamped source alpha part without addition sign.
          // SGPR [0].y - whether alpha is fixed-point.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpMul(DxbcDest::R(system_temps_subroutine_, 0b0001),
                    DxbcSrc::R(system_temps_color_[rt_index], DxbcSrc::kWWWW),
                    DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
          // Check if the source alpha part needs clamping after the
          // multiplication.
          // VGPR [0].x - unclamped source alpha part without addition sign.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
          {
            // Clamp the source alpha part.
            // VGPR [0].x - source alpha part without addition sign.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - blended color.
            // VGPR [1].w - destination alpha.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_, 0b0001),
                      DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                      rt_clamp_vec_src.Select(1));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_, 0b0001),
                      DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                      rt_clamp_vec_src.Select(3));
          }
          // Close the source alpha part clamping check.
          DxbcOpEndIf();
          // Extract the source alpha sign to SGPR [0].y.
          // VGPR [0].x - source alpha part without addition sign.
          // SGPR [0].y - source alpha sign as zero for 1 and non-zero for -1.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0010),
                    rt_blend_factors_ops_src, DxbcSrc::LU(1 << (21 + 2)));
          // Apply the source alpha sign.
          // VGPR [0].x - source alpha part.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0001),
                     DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
                     -DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX),
                     DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        }
        // The source alpha factor is zero.
        DxbcOpElse();
        {
          // Write zero to the source alpha part.
          // VGPR [0].x - source alpha part.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpMov(DxbcDest::R(system_temps_subroutine_, 0b0001),
                    DxbcSrc::LF(0.0f));
        }
        // Close the source alpha factor zero check.
        DxbcOpEndIf();

        // Extract the destination alpha factor to SGPR [0].y.
        // VGPR [0].x - source alpha part.
        // SGPR [0].y - destination alpha factor index.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyz - blended color.
        // VGPR [1].w - destination alpha.
        system_constants_used_ |= 1ull
                                  << kSysConst_EDRAMRTBlendFactorsOps_Index;
        DxbcOpUBFE(DxbcDest::R(system_temps_subroutine_, 0b0010),
                   DxbcSrc::LU(5), DxbcSrc::LU(24), rt_blend_factors_ops_src);
        // Check if the destination alpha factor is not zero.
        DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
        {
          // Open the switch for choosing the destination alpha blend factor.
          // VGPR [0].x - source alpha part.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          DxbcOpSwitch(DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
          {
            // Write the destination alpha factor to VGPR [0].y.
            // VGPR [0].x - source alpha part.
            // VGPR [0].y - unclamped destination alpha factor.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - blended color.
            // VGPR [1].w - destination alpha.
            ROV_HandleAlphaBlendFactorCases(system_temps_color_[rt_index],
                                            system_temps_subroutine_ + 1,
                                            system_temps_subroutine_, 1);
          }
          // Close the destination alpha factor switch.
          DxbcOpEndSwitch();
          // Get if the render target alpha is fixed-point and the destination
          // alpha factor needs clamping to SGPR [2].x.
          // VGPR [0].x - source alpha part.
          // VGPR [0].y - unclamped destination alpha factor.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha.
          // SGPR [2].x - whether alpha is fixed-point.
          system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_ + 2, 0b0001),
                    rt_format_flags_src,
                    DxbcSrc::LU(kRTFormatFlag_FixedPointAlpha));
          // Check if the destination alpha factor needs clamping.
          DxbcOpIf(true,
                   DxbcSrc::R(system_temps_subroutine_ + 2, DxbcSrc::kXXXX));
          {
            // Clamp the destination alpha factor in VGPR [0].y.
            // VGPR [0].x - source alpha part.
            // VGPR [0].y - destination alpha factor.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - blended color.
            // VGPR [1].w - destination alpha.
            // SGPR [2].x - whether alpha is fixed-point.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_, 0b0010),
                      DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
                      rt_clamp_vec_src.Select(1));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_, 0b0010),
                      DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
                      rt_clamp_vec_src.Select(3));
          }
          // Close the destination alpha factor clamping check.
          DxbcOpEndIf();
          // Apply the factor to the destination alpha in VGPR [1].w.
          // VGPR [0].x - source alpha part.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - unclamped destination alpha part without addition
          //              sign.
          // SGPR [2].x - whether alpha is fixed-point.
          DxbcOpMul(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                    DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW),
                    DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
          // Check if the destination alpha part needs clamping after the
          // multiplication.
          // VGPR [0].x - source alpha part.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - unclamped destination alpha part without addition
          //              sign.
          DxbcOpIf(true,
                   DxbcSrc::R(system_temps_subroutine_ + 2, DxbcSrc::kXXXX));
          {
            // Clamp the destination alpha part.
            // VGPR [0].x - source alpha part.
            // VGPRs [0].zw - packed destination color/alpha.
            // VGPRs [1].xyz - blended color.
            // VGPR [1].w - destination alpha part without addition sign.
            system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
            DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                      DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW),
                      rt_clamp_vec_src.Select(1));
            DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                      DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW),
                      rt_clamp_vec_src.Select(3));
          }
          // Close the destination alpha factor clamping check.
          DxbcOpEndIf();
          // Extract the destination alpha sign to SGPR [0].y.
          // VGPR [0].x - source alpha part.
          // SGPR [0].y - destination alpha sign as zero for 1 and non-zero for
          //              -1.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha part without addition sign.
          system_constants_used_ |= 1ull
                                    << kSysConst_EDRAMRTBlendFactorsOps_Index;
          DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0010),
                    rt_blend_factors_ops_src, DxbcSrc::LU(1 << 21));
          // Select the sign for destination multiply-add as 1.0 or -1.0 to
          // SGPR [0].y.
          // VGPR [0].x - source alpha part.
          // SGPR [0].y - destination alpha sign as float.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - destination alpha part without addition sign.
          DxbcOpMovC(DxbcDest::R(system_temps_subroutine_, 0b0010),
                     DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
                     DxbcSrc::LF(-1.0f), DxbcSrc::LF(1.0f));
          // Perform alpha blending to VGPR [1].w.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - unclamped blended alpha.
          DxbcOpMAd(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                    DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW),
                    DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY),
                    DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        }
        // The destination alpha factor is zero.
        DxbcOpElse();
        {
          // Write the source alpha part without applying the destination alpha.
          // VGPRs [0].zw - packed destination color/alpha.
          // VGPRs [1].xyz - blended color.
          // VGPR [1].w - unclamped blended alpha.
          DxbcOpMov(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                    DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
        }
        // Close the destination alpha factor zero check.
        DxbcOpEndIf();

        // Clamp the alpha in VGPR [1].w before packing.
        // VGPRs [0].zw - packed destination color/alpha.
        // VGPRs [1].xyzw - blended color/alpha.
        system_constants_used_ |= 1ull << kSysConst_EDRAMRTClamp_Index;
        DxbcOpMax(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                  DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW),
                  rt_clamp_vec_src.Select(1));
        DxbcOpMin(DxbcDest::R(system_temps_subroutine_ + 1, 0b1000),
                  DxbcSrc::R(system_temps_subroutine_ + 1, DxbcSrc::kWWWW),
                  rt_clamp_vec_src.Select(3));
      }
      // Close the alpha min/max enabled check.
      DxbcOpEndIf();

      // Pack the new color/alpha to VGPRs [0].xy, using VGPRs [2].xy as
      // temporary.
      // VGPRs [0].xy - packed new color/alpha.
      // VGPRs [0].zw - packed old color/alpha.
      ROV_PackPreClampedColor(
          rt_index, system_temps_subroutine_ + 1, system_temps_subroutine_, 0,
          system_temps_subroutine_ + 2, 0, system_temps_subroutine_ + 2, 1);
    }
    // Close the blending check.
    DxbcOpEndIf();

    // *************************************************************************
    // Write mask application
    // *************************************************************************

    // Apply the keep mask to the previous packed color/alpha in VGPRs [0].zw.
    // VGPRs [0].xy - packed new color/alpha.
    // VGPRs [0].zw - masked packed old color/alpha.
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
    DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b1100),
              DxbcSrc::R(system_temps_subroutine_),
              keep_mask_vec_src.Swizzle(keep_mask_swizzle << 4));
    // Invert the keep mask into SGPRs [1].xy.
    // VGPRs [0].xy - packed new color/alpha.
    // VGPRs [0].zw - masked packed old color/alpha.
    // SGPRs [1].xy - inverted keep mask (write mask).
    system_constants_used_ |= 1ull << kSysConst_EDRAMRTKeepMask_Index;
    DxbcOpNot(DxbcDest::R(system_temps_subroutine_ + 1, 0b0011),
              keep_mask_vec_src.Swizzle(keep_mask_swizzle));
    // Apply the write mask to the new color/alpha in VGPRs [0].xy.
    // VGPRs [0].xy - masked packed new color/alpha.
    // VGPRs [0].zw - masked packed old color/alpha.
    DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0011),
              DxbcSrc::R(system_temps_subroutine_),
              DxbcSrc::R(system_temps_subroutine_ + 1));
    // Combine the masked colors into VGPRs [0].xy.
    // VGPRs [0].xy - packed resulting color/alpha.
    DxbcOpOr(DxbcDest::R(system_temps_subroutine_, 0b0011),
             DxbcSrc::R(system_temps_subroutine_),
             DxbcSrc::R(system_temps_subroutine_, 0b1110));
  }
  // Close the previous color load check.
  DxbcOpEndIf();

  // ***************************************************************************
  // Writing the color
  // ***************************************************************************

  // Get if the format is 64bpp to SGPR [0].z.
  // VGPRs [0].xy - packed resulting color/alpha.
  // SGPR [0].z - whether the render target is 64bpp.
  system_constants_used_ |= 1ull << kSysConst_EDRAMRTFormatFlags_Index;
  DxbcOpAnd(DxbcDest::R(system_temps_subroutine_, 0b0100), rt_format_flags_src,
            DxbcSrc::LU(kRTFormatFlag_64bpp));
  // Check if the format is 64bpp.
  // VGPRs [0].xy - packed resulting color/alpha.
  DxbcOpIf(true, DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ));
  {
    // Store the lower 32 bits of the 64bpp color.
    DxbcOpStoreUAVTyped(
        DxbcDest::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM)),
        DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kWWWW), 1,
        DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
    // Get the address of the upper 32 bits of the color to VGPR [0].z (can't
    // use [0].x because components when not blending, packing is done once for
    // all samples).
    // VGPRs [0].xy - packed resulting color/alpha.
    // VGPR [0].z - address of the upper 32 bits of the packed color.
    DxbcOpIAdd(DxbcDest::R(system_temps_subroutine_, 0b0100),
               DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kWWWW),
               DxbcSrc::LU(1));
    // Store the upper 32 bits of the 64bpp color.
    DxbcOpStoreUAVTyped(
        DxbcDest::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM)),
        DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kZZZZ), 1,
        DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kYYYY));
  }
  // The color is 32bpp.
  DxbcOpElse();
  {
    // Store the 32bpp color.
    DxbcOpStoreUAVTyped(
        DxbcDest::U(ROV_GetEDRAMUAVIndex(), uint32_t(UAVRegister::kEDRAM)),
        DxbcSrc::R(system_temp_rov_params_, DxbcSrc::kZZZZ), 1,
        DxbcSrc::R(system_temps_subroutine_, DxbcSrc::kXXXX));
  }
  // Close the 64bpp/32bpp conditional.
  DxbcOpEndIf();

  // End the subroutine.
  DxbcOpRet();
}

}  // namespace gpu
}  // namespace xe
