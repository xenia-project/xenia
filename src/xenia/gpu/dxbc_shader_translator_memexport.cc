/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::ExportToMemory_PackFixed32(
    const uint32_t* eM_temps, uint32_t eM_count, const uint32_t bits[4],
    const DxbcSrc& is_integer, const DxbcSrc& is_signed) {
  // Will insert with BFI - sign extension of red will be overwritten, not
  // truncated.
  assert_not_zero(bits[0]);
  assert_true(bits[0] + bits[1] + bits[2] + bits[3] == 32);
  uint32_t mask = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (bits[i]) {
      mask |= 1 << i;
    }
  }
  DxbcOpIf(true, is_signed);
  {
    float range[4];
    for (uint32_t i = 0; i < 4; ++i) {
      range[i] = bits[i] ? float((uint32_t(1) << (bits[i] - 1)) - 1) : 0.0f;
    }
    DxbcSrc range_src(DxbcSrc::LP(range));
    DxbcOpIf(false, is_integer);
    for (uint32_t i = 0; i < eM_count; ++i) {
      uint32_t eM_temp = eM_temps[i];
      DxbcOpMul(DxbcDest::R(eM_temp, mask), DxbcSrc::R(eM_temp), range_src);
    }
    DxbcOpEndIf();
    for (uint32_t i = 0; i < eM_count; ++i) {
      DxbcDest eM_dest(DxbcDest::R(eM_temps[i], mask));
      DxbcSrc eM_src(DxbcSrc::R(eM_temps[i]));
      // TODO(Triang3l): NaN should become zero, not -range.
      DxbcOpMax(eM_dest, eM_src, -range_src);
      DxbcOpMin(eM_dest, eM_src, range_src);
    }
  }
  DxbcOpElse();
  {
    float range[4];
    for (uint32_t i = 0; i < 4; ++i) {
      range[i] = float((uint32_t(1) << bits[i]) - 1);
    }
    DxbcSrc range_src(DxbcSrc::LP(range));
    DxbcOpIf(false, is_integer);
    for (uint32_t i = 0; i < eM_count; ++i) {
      uint32_t eM_temp = eM_temps[i];
      DxbcOpMul(DxbcDest::R(eM_temp, mask), DxbcSrc::R(eM_temp), range_src);
    }
    DxbcOpEndIf();
    for (uint32_t i = 0; i < eM_count; ++i) {
      DxbcDest eM_dest(DxbcDest::R(eM_temps[i], mask));
      DxbcSrc eM_src(DxbcSrc::R(eM_temps[i]));
      DxbcOpMax(eM_dest, eM_src, DxbcSrc::LF(0.0f));
      DxbcOpMin(eM_dest, eM_src, range_src);
    }
  }
  DxbcOpEndIf();
  for (uint32_t i = 0; i < eM_count; ++i) {
    uint32_t eM_temp = eM_temps[i];
    // Round to the nearest integer, according to the rules of handling integer
    // formats in Direct3D.
    // TODO(Triang3l): Round by adding +-0.5, not with round_ne.
    DxbcOpRoundNE(DxbcDest::R(eM_temp, mask), DxbcSrc::R(eM_temp));
    DxbcOpFToI(DxbcDest::R(eM_temp, mask), DxbcSrc::R(eM_temp));
    DxbcDest eM_packed_dest(DxbcDest::R(eM_temp, 0b0001));
    DxbcSrc eM_packed_src(DxbcSrc::R(eM_temp, DxbcSrc::kXXXX));
    uint32_t offset = bits[0];
    for (uint32_t j = 1; j < 4; ++j) {
      if (!bits[j]) {
        continue;
      }
      DxbcOpBFI(eM_packed_dest, DxbcSrc::LU(bits[j]), DxbcSrc::LU(offset),
                DxbcSrc::R(eM_temp).Select(j), eM_packed_src);
      offset += bits[j];
    }
  }
}

void DxbcShaderTranslator::ExportToMemory() {
  if (system_temp_memexport_written_ == UINT32_MAX) {
    // No exports in the shader.
    return;
  }

  // Allocate a register for temporary values at various stages.
  uint32_t control_temp = PushSystemTemp();

  // Safety check if the shared memory is bound as UAV.
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(control_temp, 0b0001),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_SharedMemoryIsUAV));
  if (is_pixel_shader()) {
    // Disable memexport in pixel shaders with supersampling since VPOS is
    // ambiguous.
    if (edram_rov_used_) {
      system_constants_used_ |= 1ull
                                << kSysConst_EdramResolutionSquareScale_Index;
      DxbcOpULT(DxbcDest::R(control_temp, 0b0010),
                DxbcSrc::CB(cbuffer_index_system_constants_,
                            uint32_t(CbufferRegister::kSystemConstants),
                            kSysConst_EdramResolutionSquareScale_Vec)
                    .Select(kSysConst_EdramResolutionSquareScale_Comp),
                DxbcSrc::LU(2));
      DxbcOpAnd(DxbcDest::R(control_temp, 0b0001),
                DxbcSrc::R(control_temp, DxbcSrc::kXXXX),
                DxbcSrc::R(control_temp, DxbcSrc::kYYYY));
    } else {
      // Enough to check just Y because it's scaled for both 2x and 4x.
      system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
      DxbcOpMovC(DxbcDest::R(control_temp, 0b0001),
                 DxbcSrc::CB(cbuffer_index_system_constants_,
                             uint32_t(CbufferRegister::kSystemConstants),
                             kSysConst_SampleCountLog2_Vec)
                     .Select(kSysConst_SampleCountLog2_Comp + 1),
                 DxbcSrc::LU(0), DxbcSrc::R(control_temp, DxbcSrc::kXXXX));
    }
  }
  // Check if memexport can be done.
  DxbcOpIf(true, DxbcSrc::R(control_temp, DxbcSrc::kXXXX));
  // control_temp.x is now free.

  for (uint32_t i = 0; i < kMaxMemExports; ++i) {
    uint32_t eA_temp = system_temps_memexport_address_[i];
    if (eA_temp == UINT32_MAX) {
      // Export not used.
      continue;
    }
    // For simplicity of access, gather actually used eM# registers for this
    // export. Zero-initialize eM_offsets because excess elements of it may be
    // accessed, for stable caching.
    uint32_t eM_temps[5], eM_offsets[5] = {}, eM_count = 0;
    for (uint32_t j = 0; j < 5; ++j) {
      uint32_t eM_temp = system_temps_memexport_data_[i][j];
      if (eM_temp == UINT32_MAX) {
        continue;
      }
      eM_temps[eM_count] = eM_temp;
      eM_offsets[eM_count] = j;
      ++eM_count;
    }
    if (eM_count == 0) {
      continue;
    }

    // Swap red and blue if needed.
    DxbcOpAnd(DxbcDest::R(control_temp, 0b0001),
              DxbcSrc::R(eA_temp, DxbcSrc::kZZZZ),
              DxbcSrc::LU(uint32_t(1) << 19));
    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      DxbcOpMovC(DxbcDest::R(eM_temp, 0b0101),
                 DxbcSrc::R(control_temp, DxbcSrc::kXXXX),
                 DxbcSrc::R(eM_temp, 0b000010), DxbcSrc::R(eM_temp));
    }

    // Initialize element size in control_temp.x to 4 bytes as this is the most
    // common size.
    DxbcDest element_size_dest(DxbcDest::R(control_temp, 0b0001));
    DxbcSrc element_size_src(DxbcSrc::R(control_temp, DxbcSrc::kXXXX));
    DxbcOpMov(element_size_dest, DxbcSrc::LU(4));

    // Each eM should get a packed value in the destination format now.

    // Extract format properties to control_temp.
    // Y - signedness if fixed-point.
    // Z - fractional/integer if fixed-point.
    // W - color format.
    DxbcOpUBFE(DxbcDest::R(control_temp, 0b1110), DxbcSrc::LU(0, 1, 1, 6),
               DxbcSrc::LU(0, 16, 17, 8), DxbcSrc::R(eA_temp, DxbcSrc::kZZZZ));
    DxbcSrc is_signed(DxbcSrc::R(control_temp, DxbcSrc::kYYYY));
    DxbcSrc is_integer(DxbcSrc::R(control_temp, DxbcSrc::kZZZZ));
    // Convert and pack the format.
    DxbcOpSwitch(DxbcSrc::R(control_temp, DxbcSrc::kWWWW));
    // control_temp.w is now free.
    {
      // k_8_8_8_8
      // k_8_8_8_8_AS_16_16_16_16
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_8_8_8_8)));
      DxbcOpCase(
          DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_8_8_8_8_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {8, 8, 8, 8};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      DxbcOpBreak();

      // k_2_10_10_10
      // k_2_10_10_10_AS_16_16_16_16
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_2_10_10_10)));
      DxbcOpCase(DxbcSrc::LU(
          uint32_t(xenos::ColorFormat::k_2_10_10_10_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {10, 10, 10, 2};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      DxbcOpBreak();

      // k_10_11_11
      // k_10_11_11_AS_16_16_16_16
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_10_11_11)));
      DxbcOpCase(
          DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_10_11_11_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {11, 11, 10};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      DxbcOpBreak();

      // k_11_11_10
      // k_11_11_10_AS_16_16_16_16
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_11_11_10)));
      DxbcOpCase(
          DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_11_11_10_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {10, 11, 11};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      DxbcOpBreak();

      // k_16_16
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_16_16)));
      {
        uint32_t bits[4] = {16, 16};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      DxbcOpBreak();

      // k_16_16_16_16
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_16_16_16_16)));
      DxbcOpMov(element_size_dest, DxbcSrc::LU(8));
      DxbcOpIf(true, is_signed);
      {
        DxbcOpIf(false, is_integer);
        for (uint32_t j = 0; j < eM_count; ++j) {
          uint32_t eM_temp = eM_temps[j];
          DxbcOpMul(DxbcDest::R(eM_temp), DxbcSrc::R(eM_temp),
                    DxbcSrc::LF(32767.0f));
        }
        DxbcOpEndIf();
        for (uint32_t j = 0; j < eM_count; ++j) {
          DxbcDest eM_dest(DxbcDest::R(eM_temps[j]));
          DxbcSrc eM_src(DxbcSrc::R(eM_temps[j]));
          // TODO(Triang3l): NaN should become zero, not -range.
          DxbcOpMax(eM_dest, eM_src, DxbcSrc::LF(-32767.0f));
          DxbcOpMin(eM_dest, eM_src, DxbcSrc::LF(32767.0f));
        }
      }
      DxbcOpElse();
      {
        DxbcOpIf(false, is_integer);
        for (uint32_t j = 0; j < eM_count; ++j) {
          uint32_t eM_temp = eM_temps[j];
          DxbcOpMul(DxbcDest::R(eM_temp), DxbcSrc::R(eM_temp),
                    DxbcSrc::LF(65535.0f));
        }
        DxbcOpEndIf();
        for (uint32_t j = 0; j < eM_count; ++j) {
          DxbcDest eM_dest(DxbcDest::R(eM_temps[j]));
          DxbcSrc eM_src(DxbcSrc::R(eM_temps[j]));
          DxbcOpMax(eM_dest, eM_src, DxbcSrc::LF(0.0f));
          DxbcOpMin(eM_dest, eM_src, DxbcSrc::LF(65535.0f));
        }
      }
      DxbcOpEndIf();
      for (uint32_t j = 0; j < eM_count; ++j) {
        uint32_t eM_temp = eM_temps[j];
        // Round to the nearest integer, according to the rules of handling
        // integer formats in Direct3D.
        // TODO(Triang3l): Round by adding +-0.5, not with round_ne.
        DxbcOpRoundNE(DxbcDest::R(eM_temp), DxbcSrc::R(eM_temp));
        DxbcOpFToI(DxbcDest::R(eM_temp), DxbcSrc::R(eM_temp));
        DxbcOpBFI(DxbcDest::R(eM_temp, 0b0011), DxbcSrc::LU(16),
                  DxbcSrc::LU(16), DxbcSrc::R(eM_temp, 0b1101),
                  DxbcSrc::R(eM_temp, 0b1000));
      }
      DxbcOpBreak();

      // k_16_16_FLOAT
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_16_16_FLOAT)));
      for (uint32_t j = 0; j < eM_count; ++j) {
        uint32_t eM_temp = eM_temps[j];
        DxbcOpF32ToF16(DxbcDest::R(eM_temp, 0b0011), DxbcSrc::R(eM_temp));
        DxbcOpBFI(DxbcDest::R(eM_temp, 0b0001), DxbcSrc::LU(16),
                  DxbcSrc::LU(16), DxbcSrc::R(eM_temp, DxbcSrc::kYYYY),
                  DxbcSrc::R(eM_temp, DxbcSrc::kXXXX));
      }
      DxbcOpBreak();

      // k_16_16_16_16_FLOAT
      DxbcOpCase(
          DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_16_16_16_16_FLOAT)));
      DxbcOpMov(element_size_dest, DxbcSrc::LU(8));
      for (uint32_t j = 0; j < eM_count; ++j) {
        uint32_t eM_temp = eM_temps[j];
        DxbcOpF32ToF16(DxbcDest::R(eM_temp), DxbcSrc::R(eM_temp));
        DxbcOpBFI(DxbcDest::R(eM_temp, 0b0011), DxbcSrc::LU(16),
                  DxbcSrc::LU(16), DxbcSrc::R(eM_temp, 0b1101),
                  DxbcSrc::R(eM_temp, 0b1000));
      }
      DxbcOpBreak();

      // k_32_FLOAT
      // Already in the destination format, 4 bytes per element already
      // selected.

      // k_32_32_FLOAT
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_32_32_FLOAT)));
      DxbcOpMov(element_size_dest, DxbcSrc::LU(8));
      // Already in the destination format.
      DxbcOpBreak();

      // k_32_32_32_32_FLOAT
      DxbcOpCase(
          DxbcSrc::LU(uint32_t(xenos::ColorFormat::k_32_32_32_32_FLOAT)));
      DxbcOpMov(element_size_dest, DxbcSrc::LU(16));
      // Already in the destination format.
      DxbcOpBreak();
    }
    DxbcOpEndSwitch();
    // control_temp.yz are now free.

    // Do endian swap.
    {
      DxbcDest endian_dest(DxbcDest::R(control_temp, 0b0010));
      DxbcSrc endian_src(DxbcSrc::R(control_temp, DxbcSrc::kYYYY));
      // Extract endianness into control_temp.y.
      DxbcOpAnd(endian_dest, DxbcSrc::R(eA_temp, DxbcSrc::kZZZZ),
                DxbcSrc::LU(0b111));

      // Change 8-in-64 and 8-in-128 to 8-in-32.
      for (uint32_t j = 0; j < 2; ++j) {
        DxbcOpIEq(DxbcDest::R(control_temp, 0b0100), endian_src,
                  DxbcSrc::LU(uint32_t(j ? xenos::Endian128::k8in128
                                         : xenos::Endian128::k8in64)));
        for (uint32_t k = 0; k < eM_count; ++k) {
          uint32_t eM_temp = eM_temps[k];
          DxbcOpMovC(DxbcDest::R(eM_temp),
                     DxbcSrc::R(control_temp, DxbcSrc::kZZZZ),
                     DxbcSrc::R(eM_temp, j ? 0b00011011 : 0b10110001),
                     DxbcSrc::R(eM_temp));
        }
        DxbcOpMovC(endian_dest, DxbcSrc::R(control_temp, DxbcSrc::kZZZZ),
                   DxbcSrc::LU(uint32_t(xenos::Endian128::k8in32)), endian_src);
      }

      uint32_t swap_temp = PushSystemTemp();
      DxbcDest swap_temp_dest(DxbcDest::R(swap_temp));
      DxbcSrc swap_temp_src(DxbcSrc::R(swap_temp));

      // 8-in-16 or one half of 8-in-32.
      DxbcOpSwitch(endian_src);
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k8in16)));
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k8in32)));
      for (uint32_t j = 0; j < eM_count; ++j) {
        DxbcDest eM_dest(DxbcDest::R(eM_temps[j]));
        DxbcSrc eM_src(DxbcSrc::R(eM_temps[j]));
        // Temp = X0Z0.
        DxbcOpAnd(swap_temp_dest, eM_src, DxbcSrc::LU(0x00FF00FF));
        // eM = YZW0.
        DxbcOpUShR(eM_dest, eM_src, DxbcSrc::LU(8));
        // eM = Y0W0.
        DxbcOpAnd(eM_dest, eM_src, DxbcSrc::LU(0x00FF00FF));
        // eM = YXWZ.
        DxbcOpUMAd(eM_dest, swap_temp_src, DxbcSrc::LU(256), eM_src);
      }
      DxbcOpBreak();
      DxbcOpEndSwitch();

      // 16-in-32 or another half of 8-in-32.
      DxbcOpSwitch(endian_src);
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k8in32)));
      DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k16in32)));
      for (uint32_t j = 0; j < eM_count; ++j) {
        DxbcDest eM_dest(DxbcDest::R(eM_temps[j]));
        DxbcSrc eM_src(DxbcSrc::R(eM_temps[j]));
        // Temp = ZW00.
        DxbcOpUShR(swap_temp_dest, eM_src, DxbcSrc::LU(16));
        // eM = ZWXY.
        DxbcOpBFI(eM_dest, DxbcSrc::LU(16), DxbcSrc::LU(16), eM_src,
                  swap_temp_src);
      }
      DxbcOpBreak();
      DxbcOpEndSwitch();

      // Release swap_temp.
      PopSystemTemp();
    }
    // control_temp.yz are now free.

    DxbcDest address_dest(DxbcDest::R(eA_temp, 0b0001));
    DxbcSrc address_src(DxbcSrc::R(eA_temp, DxbcSrc::kXXXX));
    // Multiply the base address by dword size, also dropping the 0x40000000
    // bit.
    DxbcOpIShL(address_dest, address_src, DxbcSrc::LU(2));
    // Drop the exponent in the element index.
    DxbcOpAnd(DxbcDest::R(eA_temp, 0b0010), DxbcSrc::R(eA_temp, DxbcSrc::kYYYY),
              DxbcSrc::LU((1 << 23) - 1));
    // Add the offset of the first written element to the base address.
    DxbcOpUMAd(address_dest, DxbcSrc::R(eA_temp, DxbcSrc::kYYYY),
               element_size_src, address_src);
    // Do the writes.
    DxbcSrc eM_written_src(
        DxbcSrc::R(system_temp_memexport_written_).Select(i >> 2));
    uint32_t eM_written_base = 1u << ((i & 3) << 3);
    for (uint32_t j = 0; j < eM_count; ++j) {
      // Go to the next eM#.
      uint32_t eM_relative_offset = eM_offsets[j] - (j ? eM_offsets[j - 1] : 0);
      if (eM_relative_offset) {
        if (eM_relative_offset == 1) {
          DxbcOpIAdd(address_dest, element_size_src, address_src);
        } else {
          DxbcOpUMAd(address_dest, DxbcSrc::LU(eM_relative_offset),
                     element_size_src, address_src);
        }
      }
      // Check if the eM# was actually written to on the execution path.
      DxbcOpAnd(DxbcDest::R(control_temp, 0b0010), eM_written_src,
                DxbcSrc::LU(eM_written_base << eM_offsets[j]));
      DxbcOpIf(true, DxbcSrc::R(control_temp, DxbcSrc::kYYYY));
      // Write the element of the needed size.
      DxbcSrc eM_src(DxbcSrc::R(eM_temps[j]));
      DxbcOpSwitch(element_size_src);
      for (uint32_t k = 1; k <= 4; k <<= 1) {
        DxbcOpCase(DxbcSrc::LU(k * 4));
        if (uav_index_shared_memory_ == kBindingIndexUnallocated) {
          uav_index_shared_memory_ = uav_count_++;
        }
        DxbcOpStoreRaw(
            DxbcDest::U(uav_index_shared_memory_,
                        uint32_t(UAVRegister::kSharedMemory), (1 << k) - 1),
            address_src, eM_src);
        DxbcOpBreak();
      }
      DxbcOpEndSwitch();
      DxbcOpEndIf();
    }
    // control_temp.y is now free.
  }

  // Close the memexport possibility check.
  DxbcOpEndIf();

  // Release control_temp.
  PopSystemTemp();
}

}  // namespace gpu
}  // namespace xe
