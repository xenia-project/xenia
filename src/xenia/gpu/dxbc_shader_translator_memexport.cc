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
    const dxbc::Src& is_integer, const dxbc::Src& is_signed) {
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
  a_.OpIf(true, is_signed);
  {
    float range[4];
    for (uint32_t i = 0; i < 4; ++i) {
      range[i] = bits[i] ? float((uint32_t(1) << (bits[i] - 1)) - 1) : 0.0f;
    }
    dxbc::Src range_src(dxbc::Src::LP(range));
    a_.OpIf(false, is_integer);
    for (uint32_t i = 0; i < eM_count; ++i) {
      uint32_t eM_temp = eM_temps[i];
      a_.OpMul(dxbc::Dest::R(eM_temp, mask), dxbc::Src::R(eM_temp), range_src);
    }
    a_.OpEndIf();
    for (uint32_t i = 0; i < eM_count; ++i) {
      dxbc::Dest eM_dest(dxbc::Dest::R(eM_temps[i], mask));
      dxbc::Src eM_src(dxbc::Src::R(eM_temps[i]));
      // TODO(Triang3l): NaN should become zero, not -range.
      a_.OpMax(eM_dest, eM_src, -range_src);
      a_.OpMin(eM_dest, eM_src, range_src);
    }
  }
  a_.OpElse();
  {
    float range[4];
    for (uint32_t i = 0; i < 4; ++i) {
      range[i] = float((uint32_t(1) << bits[i]) - 1);
    }
    dxbc::Src range_src(dxbc::Src::LP(range));
    a_.OpIf(false, is_integer);
    for (uint32_t i = 0; i < eM_count; ++i) {
      uint32_t eM_temp = eM_temps[i];
      a_.OpMul(dxbc::Dest::R(eM_temp, mask), dxbc::Src::R(eM_temp), range_src);
    }
    a_.OpEndIf();
    for (uint32_t i = 0; i < eM_count; ++i) {
      dxbc::Dest eM_dest(dxbc::Dest::R(eM_temps[i], mask));
      dxbc::Src eM_src(dxbc::Src::R(eM_temps[i]));
      a_.OpMax(eM_dest, eM_src, dxbc::Src::LF(0.0f));
      a_.OpMin(eM_dest, eM_src, range_src);
    }
  }
  a_.OpEndIf();
  for (uint32_t i = 0; i < eM_count; ++i) {
    uint32_t eM_temp = eM_temps[i];
    // Round to the nearest integer, according to the rules of handling integer
    // formats in Direct3D.
    // TODO(Triang3l): Round by adding +-0.5, not with round_ne.
    a_.OpRoundNE(dxbc::Dest::R(eM_temp, mask), dxbc::Src::R(eM_temp));
    a_.OpFToI(dxbc::Dest::R(eM_temp, mask), dxbc::Src::R(eM_temp));
    dxbc::Dest eM_packed_dest(dxbc::Dest::R(eM_temp, 0b0001));
    dxbc::Src eM_packed_src(dxbc::Src::R(eM_temp, dxbc::Src::kXXXX));
    uint32_t offset = bits[0];
    for (uint32_t j = 1; j < 4; ++j) {
      if (!bits[j]) {
        continue;
      }
      a_.OpBFI(eM_packed_dest, dxbc::Src::LU(bits[j]), dxbc::Src::LU(offset),
               dxbc::Src::R(eM_temp).Select(j), eM_packed_src);
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
  a_.OpAnd(dxbc::Dest::R(control_temp, 0b0001),
           dxbc::Src::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_Flags_Vec)
               .Select(kSysConst_Flags_Comp),
           dxbc::Src::LU(kSysFlag_SharedMemoryIsUAV));
  if (is_pixel_shader()) {
    // Disable memexport in pixel shaders with supersampling since VPOS is
    // ambiguous.
    if (edram_rov_used_) {
      system_constants_used_ |= 1ull
                                << kSysConst_EdramResolutionSquareScale_Index;
      a_.OpULT(dxbc::Dest::R(control_temp, 0b0010),
               dxbc::Src::CB(cbuffer_index_system_constants_,
                             uint32_t(CbufferRegister::kSystemConstants),
                             kSysConst_EdramResolutionSquareScale_Vec)
                   .Select(kSysConst_EdramResolutionSquareScale_Comp),
               dxbc::Src::LU(2));
      a_.OpAnd(dxbc::Dest::R(control_temp, 0b0001),
               dxbc::Src::R(control_temp, dxbc::Src::kXXXX),
               dxbc::Src::R(control_temp, dxbc::Src::kYYYY));
    } else {
      // Enough to check just Y because it's scaled for both 2x and 4x.
      system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
      a_.OpMovC(dxbc::Dest::R(control_temp, 0b0001),
                dxbc::Src::CB(cbuffer_index_system_constants_,
                              uint32_t(CbufferRegister::kSystemConstants),
                              kSysConst_SampleCountLog2_Vec)
                    .Select(kSysConst_SampleCountLog2_Comp + 1),
                dxbc::Src::LU(0), dxbc::Src::R(control_temp, dxbc::Src::kXXXX));
    }
  }
  // Check if memexport can be done.
  a_.OpIf(true, dxbc::Src::R(control_temp, dxbc::Src::kXXXX));
  // control_temp.x is now free.

  for (uint32_t i = 0; i < Shader::kMaxMemExports; ++i) {
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
    a_.OpAnd(dxbc::Dest::R(control_temp, 0b0001),
             dxbc::Src::R(eA_temp, dxbc::Src::kZZZZ),
             dxbc::Src::LU(uint32_t(1) << 19));
    for (uint32_t j = 0; j < eM_count; ++j) {
      uint32_t eM_temp = eM_temps[j];
      a_.OpMovC(dxbc::Dest::R(eM_temp, 0b0101),
                dxbc::Src::R(control_temp, dxbc::Src::kXXXX),
                dxbc::Src::R(eM_temp, 0b000010), dxbc::Src::R(eM_temp));
    }

    // Initialize element size in control_temp.x to 4 bytes as this is the most
    // common size.
    dxbc::Dest element_size_dest(dxbc::Dest::R(control_temp, 0b0001));
    dxbc::Src element_size_src(dxbc::Src::R(control_temp, dxbc::Src::kXXXX));
    a_.OpMov(element_size_dest, dxbc::Src::LU(4));

    // Each eM should get a packed value in the destination format now.

    // Extract format properties to control_temp.
    // Y - signedness if fixed-point.
    // Z - fractional/integer if fixed-point.
    // W - color format.
    a_.OpUBFE(dxbc::Dest::R(control_temp, 0b1110), dxbc::Src::LU(0, 1, 1, 6),
              dxbc::Src::LU(0, 16, 17, 8),
              dxbc::Src::R(eA_temp, dxbc::Src::kZZZZ));
    dxbc::Src is_signed(dxbc::Src::R(control_temp, dxbc::Src::kYYYY));
    dxbc::Src is_integer(dxbc::Src::R(control_temp, dxbc::Src::kZZZZ));
    // Convert and pack the format.
    a_.OpSwitch(dxbc::Src::R(control_temp, dxbc::Src::kWWWW));
    // control_temp.w is now free.
    {
      // k_8_8_8_8
      // k_8_8_8_8_AS_16_16_16_16
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_8_8_8)));
      a_.OpCase(dxbc::Src::LU(
          uint32_t(xenos::ColorFormat::k_8_8_8_8_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {8, 8, 8, 8};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      a_.OpBreak();

      // k_2_10_10_10
      // k_2_10_10_10_AS_16_16_16_16
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_2_10_10_10)));
      a_.OpCase(dxbc::Src::LU(
          uint32_t(xenos::ColorFormat::k_2_10_10_10_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {10, 10, 10, 2};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      a_.OpBreak();

      // k_10_11_11
      // k_10_11_11_AS_16_16_16_16
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_10_11_11)));
      a_.OpCase(dxbc::Src::LU(
          uint32_t(xenos::ColorFormat::k_10_11_11_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {11, 11, 10};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      a_.OpBreak();

      // k_11_11_10
      // k_11_11_10_AS_16_16_16_16
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_11_11_10)));
      a_.OpCase(dxbc::Src::LU(
          uint32_t(xenos::ColorFormat::k_11_11_10_AS_16_16_16_16)));
      {
        uint32_t bits[4] = {10, 11, 11};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      a_.OpBreak();

      // k_16_16
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16)));
      {
        uint32_t bits[4] = {16, 16};
        ExportToMemory_PackFixed32(eM_temps, eM_count, bits, is_integer,
                                   is_signed);
      }
      a_.OpBreak();

      // k_16_16_16_16
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16_16_16)));
      a_.OpMov(element_size_dest, dxbc::Src::LU(8));
      a_.OpIf(true, is_signed);
      {
        a_.OpIf(false, is_integer);
        for (uint32_t j = 0; j < eM_count; ++j) {
          uint32_t eM_temp = eM_temps[j];
          a_.OpMul(dxbc::Dest::R(eM_temp), dxbc::Src::R(eM_temp),
                   dxbc::Src::LF(32767.0f));
        }
        a_.OpEndIf();
        for (uint32_t j = 0; j < eM_count; ++j) {
          dxbc::Dest eM_dest(dxbc::Dest::R(eM_temps[j]));
          dxbc::Src eM_src(dxbc::Src::R(eM_temps[j]));
          // TODO(Triang3l): NaN should become zero, not -range.
          a_.OpMax(eM_dest, eM_src, dxbc::Src::LF(-32767.0f));
          a_.OpMin(eM_dest, eM_src, dxbc::Src::LF(32767.0f));
        }
      }
      a_.OpElse();
      {
        a_.OpIf(false, is_integer);
        for (uint32_t j = 0; j < eM_count; ++j) {
          uint32_t eM_temp = eM_temps[j];
          a_.OpMul(dxbc::Dest::R(eM_temp), dxbc::Src::R(eM_temp),
                   dxbc::Src::LF(65535.0f));
        }
        a_.OpEndIf();
        for (uint32_t j = 0; j < eM_count; ++j) {
          dxbc::Dest eM_dest(dxbc::Dest::R(eM_temps[j]));
          dxbc::Src eM_src(dxbc::Src::R(eM_temps[j]));
          a_.OpMax(eM_dest, eM_src, dxbc::Src::LF(0.0f));
          a_.OpMin(eM_dest, eM_src, dxbc::Src::LF(65535.0f));
        }
      }
      a_.OpEndIf();
      for (uint32_t j = 0; j < eM_count; ++j) {
        uint32_t eM_temp = eM_temps[j];
        // Round to the nearest integer, according to the rules of handling
        // integer formats in Direct3D.
        // TODO(Triang3l): Round by adding +-0.5, not with round_ne.
        a_.OpRoundNE(dxbc::Dest::R(eM_temp), dxbc::Src::R(eM_temp));
        a_.OpFToI(dxbc::Dest::R(eM_temp), dxbc::Src::R(eM_temp));
        a_.OpBFI(dxbc::Dest::R(eM_temp, 0b0011), dxbc::Src::LU(16),
                 dxbc::Src::LU(16), dxbc::Src::R(eM_temp, 0b1101),
                 dxbc::Src::R(eM_temp, 0b1000));
      }
      a_.OpBreak();

      // k_16_16_FLOAT
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16_FLOAT)));
      for (uint32_t j = 0; j < eM_count; ++j) {
        uint32_t eM_temp = eM_temps[j];
        a_.OpF32ToF16(dxbc::Dest::R(eM_temp, 0b0011), dxbc::Src::R(eM_temp));
        a_.OpBFI(dxbc::Dest::R(eM_temp, 0b0001), dxbc::Src::LU(16),
                 dxbc::Src::LU(16), dxbc::Src::R(eM_temp, dxbc::Src::kYYYY),
                 dxbc::Src::R(eM_temp, dxbc::Src::kXXXX));
      }
      a_.OpBreak();

      // k_16_16_16_16_FLOAT
      a_.OpCase(
          dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16_16_16_FLOAT)));
      a_.OpMov(element_size_dest, dxbc::Src::LU(8));
      for (uint32_t j = 0; j < eM_count; ++j) {
        uint32_t eM_temp = eM_temps[j];
        a_.OpF32ToF16(dxbc::Dest::R(eM_temp), dxbc::Src::R(eM_temp));
        a_.OpBFI(dxbc::Dest::R(eM_temp, 0b0011), dxbc::Src::LU(16),
                 dxbc::Src::LU(16), dxbc::Src::R(eM_temp, 0b1101),
                 dxbc::Src::R(eM_temp, 0b1000));
      }
      a_.OpBreak();

      // k_32_FLOAT
      // Already in the destination format, 4 bytes per element already
      // selected.

      // k_32_32_FLOAT
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_32_32_FLOAT)));
      a_.OpMov(element_size_dest, dxbc::Src::LU(8));
      // Already in the destination format.
      a_.OpBreak();

      // k_32_32_32_32_FLOAT
      a_.OpCase(
          dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_32_32_32_32_FLOAT)));
      a_.OpMov(element_size_dest, dxbc::Src::LU(16));
      // Already in the destination format.
      a_.OpBreak();
    }
    a_.OpEndSwitch();
    // control_temp.yz are now free.

    // Do endian swap.
    {
      dxbc::Dest endian_dest(dxbc::Dest::R(control_temp, 0b0010));
      dxbc::Src endian_src(dxbc::Src::R(control_temp, dxbc::Src::kYYYY));
      // Extract endianness into control_temp.y.
      a_.OpAnd(endian_dest, dxbc::Src::R(eA_temp, dxbc::Src::kZZZZ),
               dxbc::Src::LU(0b111));

      // Change 8-in-64 and 8-in-128 to 8-in-32.
      for (uint32_t j = 0; j < 2; ++j) {
        a_.OpIEq(dxbc::Dest::R(control_temp, 0b0100), endian_src,
                 dxbc::Src::LU(uint32_t(j ? xenos::Endian128::k8in128
                                          : xenos::Endian128::k8in64)));
        for (uint32_t k = 0; k < eM_count; ++k) {
          uint32_t eM_temp = eM_temps[k];
          a_.OpMovC(dxbc::Dest::R(eM_temp),
                    dxbc::Src::R(control_temp, dxbc::Src::kZZZZ),
                    dxbc::Src::R(eM_temp, j ? 0b00011011 : 0b10110001),
                    dxbc::Src::R(eM_temp));
        }
        a_.OpMovC(endian_dest, dxbc::Src::R(control_temp, dxbc::Src::kZZZZ),
                  dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)),
                  endian_src);
      }

      uint32_t swap_temp = PushSystemTemp();
      dxbc::Dest swap_temp_dest(dxbc::Dest::R(swap_temp));
      dxbc::Src swap_temp_src(dxbc::Src::R(swap_temp));

      // 8-in-16 or one half of 8-in-32.
      a_.OpSwitch(endian_src);
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in16)));
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)));
      for (uint32_t j = 0; j < eM_count; ++j) {
        dxbc::Dest eM_dest(dxbc::Dest::R(eM_temps[j]));
        dxbc::Src eM_src(dxbc::Src::R(eM_temps[j]));
        // Temp = X0Z0.
        a_.OpAnd(swap_temp_dest, eM_src, dxbc::Src::LU(0x00FF00FF));
        // eM = YZW0.
        a_.OpUShR(eM_dest, eM_src, dxbc::Src::LU(8));
        // eM = Y0W0.
        a_.OpAnd(eM_dest, eM_src, dxbc::Src::LU(0x00FF00FF));
        // eM = YXWZ.
        a_.OpUMAd(eM_dest, swap_temp_src, dxbc::Src::LU(256), eM_src);
      }
      a_.OpBreak();
      a_.OpEndSwitch();

      // 16-in-32 or another half of 8-in-32.
      a_.OpSwitch(endian_src);
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)));
      a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k16in32)));
      for (uint32_t j = 0; j < eM_count; ++j) {
        dxbc::Dest eM_dest(dxbc::Dest::R(eM_temps[j]));
        dxbc::Src eM_src(dxbc::Src::R(eM_temps[j]));
        // Temp = ZW00.
        a_.OpUShR(swap_temp_dest, eM_src, dxbc::Src::LU(16));
        // eM = ZWXY.
        a_.OpBFI(eM_dest, dxbc::Src::LU(16), dxbc::Src::LU(16), eM_src,
                 swap_temp_src);
      }
      a_.OpBreak();
      a_.OpEndSwitch();

      // Release swap_temp.
      PopSystemTemp();
    }
    // control_temp.yz are now free.

    dxbc::Dest address_dest(dxbc::Dest::R(eA_temp, 0b0001));
    dxbc::Src address_src(dxbc::Src::R(eA_temp, dxbc::Src::kXXXX));
    // Multiply the base address by dword size, also dropping the 0x40000000
    // bit.
    a_.OpIShL(address_dest, address_src, dxbc::Src::LU(2));
    // Drop the exponent in the element index.
    a_.OpAnd(dxbc::Dest::R(eA_temp, 0b0010),
             dxbc::Src::R(eA_temp, dxbc::Src::kYYYY),
             dxbc::Src::LU((1 << 23) - 1));
    // Add the offset of the first written element to the base address.
    a_.OpUMAd(address_dest, dxbc::Src::R(eA_temp, dxbc::Src::kYYYY),
              element_size_src, address_src);
    // Do the writes.
    dxbc::Src eM_written_src(
        dxbc::Src::R(system_temp_memexport_written_).Select(i >> 2));
    uint32_t eM_written_base = 1u << ((i & 3) << 3);
    for (uint32_t j = 0; j < eM_count; ++j) {
      // Go to the next eM#.
      uint32_t eM_relative_offset = eM_offsets[j] - (j ? eM_offsets[j - 1] : 0);
      if (eM_relative_offset) {
        if (eM_relative_offset == 1) {
          a_.OpIAdd(address_dest, element_size_src, address_src);
        } else {
          a_.OpUMAd(address_dest, dxbc::Src::LU(eM_relative_offset),
                    element_size_src, address_src);
        }
      }
      // Check if the eM# was actually written to on the execution path.
      a_.OpAnd(dxbc::Dest::R(control_temp, 0b0010), eM_written_src,
               dxbc::Src::LU(eM_written_base << eM_offsets[j]));
      a_.OpIf(true, dxbc::Src::R(control_temp, dxbc::Src::kYYYY));
      // Write the element of the needed size.
      dxbc::Src eM_src(dxbc::Src::R(eM_temps[j]));
      a_.OpSwitch(element_size_src);
      for (uint32_t k = 1; k <= 4; k <<= 1) {
        a_.OpCase(dxbc::Src::LU(k * 4));
        if (uav_index_shared_memory_ == kBindingIndexUnallocated) {
          uav_index_shared_memory_ = uav_count_++;
        }
        a_.OpStoreRaw(
            dxbc::Dest::U(uav_index_shared_memory_,
                          uint32_t(UAVRegister::kSharedMemory), (1 << k) - 1),
            address_src, eM_src);
        a_.OpBreak();
      }
      a_.OpEndSwitch();
      a_.OpEndIf();
    }
    // control_temp.y is now free.
  }

  // Close the memexport possibility check.
  a_.OpEndIf();

  // Release control_temp.
  PopSystemTemp();
}

}  // namespace gpu
}  // namespace xe
