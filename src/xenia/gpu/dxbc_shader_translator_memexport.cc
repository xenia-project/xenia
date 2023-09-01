/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <array>
#include <cstdint>
#include <functional>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/gpu/dxbc_shader_translator.h"

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::ExportToMemory(uint8_t export_eM) {
  if (!export_eM) {
    return;
  }

  assert_zero(export_eM & ~current_shader().memexport_eM_written());

  // Check if memory export is allowed in this invocation.
  a_.OpIf(true, dxbc::Src::R(system_temp_memexport_enabled_and_eM_written_,
                             dxbc::Src::kXXXX));

  // Check if the address with the correct sign and exponent was written, and
  // that the index doesn't overflow the mantissa bits.
  {
    uint32_t address_check_temp = PushSystemTemp();
    a_.OpUShR(dxbc::Dest::R(address_check_temp),
              dxbc::Src::R(system_temp_memexport_address_),
              dxbc::Src::LU(30, 23, 23, 23));
    a_.OpIEq(dxbc::Dest::R(address_check_temp),
             dxbc::Src::R(address_check_temp),
             dxbc::Src::LU(0x1, 0x96, 0x96, 0x96));
    a_.OpAnd(dxbc::Dest::R(address_check_temp, 0b0011),
             dxbc::Src::R(address_check_temp),
             dxbc::Src::R(address_check_temp, 0b1110));
    a_.OpAnd(dxbc::Dest::R(address_check_temp, 0b0001),
             dxbc::Src::R(address_check_temp, dxbc::Src::kXXXX),
             dxbc::Src::R(address_check_temp, dxbc::Src::kYYYY));
    a_.OpIf(true, dxbc::Src::R(address_check_temp, dxbc::Src::kXXXX));
    // Release address_check_temp.
    PopSystemTemp();
  }

  uint8_t eM_remaining;
  uint32_t eM_index;

  // Swap red and blue components if needed.
  {
    uint32_t red_blue_swap_temp = PushSystemTemp();
    a_.OpIBFE(dxbc::Dest::R(red_blue_swap_temp, 0b0001), dxbc::Src::LU(1),
              dxbc::Src::LU(19),
              dxbc::Src::R(system_temp_memexport_address_, dxbc::Src::kZZZZ));
    a_.OpIf(true, dxbc::Src::R(red_blue_swap_temp, dxbc::Src::kXXXX));
    // Release red_blue_swap_temp.
    PopSystemTemp();

    eM_remaining = export_eM;
    while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
      eM_remaining &= ~(uint8_t(1) << eM_index);
      a_.OpMov(
          dxbc::Dest::R(system_temps_memexport_data_[eM_index], 0b0101),
          dxbc::Src::R(system_temps_memexport_data_[eM_index], 0b11000110));
    }

    // Close the red/blue swap conditional.
    a_.OpEndIf();
  }

  uint32_t temp = PushSystemTemp();

  // Extract the color format and the numeric format.
  // temp.x = color format.
  // temp.y = numeric format is signed.
  // temp.z = numeric format is integer.
  a_.OpUBFE(dxbc::Dest::R(temp, 0b0111), dxbc::Src::LU(6, 1, 1, 0),
            dxbc::Src::LU(8, 16, 17, 0),
            dxbc::Src::R(system_temp_memexport_address_, dxbc::Src::kZZZZ));

  // Perform format packing.
  // After the switch, temp.x must contain log2 of the number of bytes in an
  // element, of UINT32_MAX if the format is unknown.
  a_.OpSwitch(dxbc::Src::R(temp, dxbc::Src::kXXXX));
  {
    dxbc::Dest element_size_dest(dxbc::Dest::R(temp, 0b0001));
    dxbc::Src num_format_signed(dxbc::Src::R(temp, dxbc::Src::kYYYY));
    dxbc::Src num_format_integer(dxbc::Src::R(temp, dxbc::Src::kZZZZ));

    auto flush_nan = [this, export_eM](uint32_t components) {
      uint8_t eM_remaining = export_eM;
      uint32_t eM_index;
      uint32_t is_nan_temp = PushSystemTemp();
      while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
        eM_remaining &= ~(uint8_t(1) << eM_index);
        uint32_t eM = system_temps_memexport_data_[eM_index];
        a_.OpNE(dxbc::Dest::R(is_nan_temp, components), dxbc::Src::R(eM),
                dxbc::Src::R(eM));
        a_.OpMovC(dxbc::Dest::R(eM, components), dxbc::Src::R(is_nan_temp),
                  dxbc::Src::LF(0.0f), dxbc::Src::R(eM));
      }
      // Release is_nan_temp.
      PopSystemTemp();
    };

    // The result will be in eM#.x. The widths must be without holes (R, RG,
    // RGB, RGBA), and expecting the widths to add up to the size of the stored
    // texel (8, 16 or 32 bits), as the unused upper bits will contain junk from
    // the sign extension of X if the number is signed.
    auto pack_8_16_32 = [&](std::array<uint32_t, 4> widths) {
      uint8_t eM_remaining;
      uint32_t eM_index;

      uint32_t components = 0;
      std::array<uint32_t, 4> offsets = {};
      for (uint32_t i = 0; i < 4; ++i) {
        if (widths[i]) {
          // Only formats for which max + 0.5 can be represented exactly.
          assert(widths[i] <= 23);
          components |= uint32_t(1) << i;
        }
        if (i) {
          offsets[i] = offsets[i - 1] + widths[i - 1];
        }
      }
      // Will be packing components into eM#.x starting from green, assume red
      // will already be there after the conversion.
      assert_not_zero(components & 0b1);

      flush_nan(components);

      a_.OpIf(true, num_format_signed);
      {
        // Signed.
        a_.OpIf(true, num_format_integer);
        {
          // Signed integer.
          float min_value[4] = {}, max_value[4] = {};
          for (uint32_t i = 0; i < 4; ++i) {
            if (widths[i]) {
              max_value[i] = float((uint32_t(1) << (widths[i] - 1)) - 1);
              min_value[i] = -1.0f - max_value[i];
            }
          }
          dxbc::Src min_value_src(dxbc::Src::LP(min_value));
          dxbc::Src max_value_src(dxbc::Src::LP(max_value));
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            a_.OpMax(dxbc::Dest::R(eM, components), min_value_src,
                     dxbc::Src::R(eM));
            a_.OpMin(dxbc::Dest::R(eM, components), max_value_src,
                     dxbc::Src::R(eM));
          }
        }
        a_.OpElse();
        {
          // Signed normalized.
          uint32_t scale_components = 0;
          float scale[4] = {};
          for (uint32_t i = 0; i < 4; ++i) {
            if (widths[i] > 2) {
              scale_components |= uint32_t(1) << i;
              scale[i] = float((uint32_t(1) << (widths[i] - 1)) - 1);
            }
          }
          dxbc::Src scale_src(dxbc::Src::LP(scale));
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            a_.OpMax(dxbc::Dest::R(eM, components), dxbc::Src::LF(-1.0f),
                     dxbc::Src::R(eM));
            a_.OpMin(dxbc::Dest::R(eM, components), dxbc::Src::LF(1.0f),
                     dxbc::Src::R(eM));
            if (scale_components) {
              a_.OpMul(dxbc::Dest::R(eM, scale_components), dxbc::Src::R(eM),
                       scale_src);
            }
          }
        }
        a_.OpEndIf();

        // Add plus/minus 0.5 before truncating according to the Direct3D format
        // conversion rules, and convert to signed integers.
        uint32_t round_bias_temp = PushSystemTemp();
        eM_remaining = export_eM;
        while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
          eM_remaining &= ~(uint8_t(1) << eM_index);
          uint32_t eM = system_temps_memexport_data_[eM_index];
          a_.OpBFI(dxbc::Dest::R(eM, components), dxbc::Src::LU(31),
                   dxbc::Src::LU(0), dxbc::Src::LF(0.5f), dxbc::Src::R(eM));
          a_.OpAdd(dxbc::Dest::R(eM, components), dxbc::Src::R(eM),
                   dxbc::Src::R(round_bias_temp));
          a_.OpFToI(dxbc::Dest::R(eM, components), dxbc::Src::R(eM));
        }
        // Release round_bias_temp.
        PopSystemTemp();
      }
      a_.OpElse();
      {
        // Unsigned.
        a_.OpIf(true, num_format_integer);
        {
          // Unsigned integer.
          float max_value[4];
          for (uint32_t i = 0; i < 4; ++i) {
            max_value[i] = float((uint32_t(1) << widths[i]) - 1);
          }
          dxbc::Src max_value_src(dxbc::Src::LP(max_value));
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            a_.OpMax(dxbc::Dest::R(eM, components), dxbc::Src::LF(0.0f),
                     dxbc::Src::R(eM));
            a_.OpMin(dxbc::Dest::R(eM, components), max_value_src,
                     dxbc::Src::R(eM));
          }
        }
        a_.OpElse();
        {
          // Unsigned normalized.
          uint32_t scale_components = 0;
          float scale[4] = {};
          for (uint32_t i = 0; i < 4; ++i) {
            if (widths[i] > 1) {
              scale_components |= uint32_t(1) << i;
              scale[i] = float((uint32_t(1) << widths[i]) - 1);
            }
          }
          dxbc::Src scale_src(dxbc::Src::LP(scale));
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            // Saturate.
            a_.OpMov(dxbc::Dest::R(eM, components), dxbc::Src::R(eM), true);
            if (scale_components) {
              a_.OpMul(dxbc::Dest::R(eM, scale_components), dxbc::Src::R(eM),
                       scale_src);
            }
          }
        }
        a_.OpEndIf();

        // Add 0.5 before truncating according to the Direct3D format conversion
        // rules, and convert to unsigned integers.
        eM_remaining = export_eM;
        while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
          eM_remaining &= ~(uint8_t(1) << eM_index);
          uint32_t eM = system_temps_memexport_data_[eM_index];
          a_.OpAdd(dxbc::Dest::R(eM, components), dxbc::Src::R(eM),
                   dxbc::Src::LF(0.5f));
          a_.OpFToU(dxbc::Dest::R(eM, components), dxbc::Src::R(eM));
        }
      }
      a_.OpEndIf();

      // Pack into 32 bits.
      for (uint32_t i = 0; i < 4; ++i) {
        if (!widths[i]) {
          continue;
        }
        dxbc::Src width_src(dxbc::Src::LU(widths[i]));
        dxbc::Src offset_src(dxbc::Src::LU(offsets[i]));
        eM_remaining = export_eM;
        while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
          eM_remaining &= ~(uint8_t(1) << eM_index);
          uint32_t eM = system_temps_memexport_data_[eM_index];
          a_.OpBFI(dxbc::Dest::R(eM, 0b0001), width_src, offset_src,
                   dxbc::Src::R(eM).Select(i),
                   dxbc::Src::R(eM, dxbc::Src::kXXXX));
        }
      }
    };

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8)));
    // TODO(Triang3l): Investigate how input should be treated for k_8_A, k_8_B,
    // k_8_8_8_8_A.
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_A)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_B)));
    {
      pack_8_16_32({8});
      a_.OpMov(element_size_dest, dxbc::Src::LU(0));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_1_5_5_5)));
    {
      pack_8_16_32({5, 5, 5, 1});
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_5_6_5)));
    {
      pack_8_16_32({5, 6, 5});
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_6_5_5)));
    {
      pack_8_16_32({5, 5, 6});
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_8_8_8)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_8_8_8_A)));
    a_.OpCase(
        dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_8_8_8_AS_16_16_16_16)));
    {
      pack_8_16_32({8, 8, 8, 8});
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_2_10_10_10)));
    a_.OpCase(dxbc::Src::LU(
        uint32_t(xenos::ColorFormat::k_2_10_10_10_AS_16_16_16_16)));
    {
      pack_8_16_32({10, 10, 10, 2});
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_8_8)));
    {
      pack_8_16_32({8, 8});
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_4_4_4_4)));
    {
      pack_8_16_32({4, 4, 4, 4});
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_10_11_11)));
    a_.OpCase(
        dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_10_11_11_AS_16_16_16_16)));
    {
      pack_8_16_32({11, 11, 10});
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_11_11_10)));
    a_.OpCase(
        dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_11_11_10_AS_16_16_16_16)));
    {
      pack_8_16_32({10, 11, 11});
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16)));
    {
      pack_8_16_32({16});
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16)));
    {
      pack_8_16_32({16, 16});
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16_16_16)));
    {
      flush_nan(0b1111);

      a_.OpIf(true, num_format_signed);
      {
        // Signed.
        a_.OpIf(true, num_format_integer);
        {
          // Signed integer.
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            a_.OpMax(dxbc::Dest::R(eM), dxbc::Src::LF(float(INT16_MIN)),
                     dxbc::Src::R(eM));
            a_.OpMin(dxbc::Dest::R(eM), dxbc::Src::LF(float(INT16_MAX)),
                     dxbc::Src::R(eM));
          }
        }
        a_.OpElse();
        {
          // Signed normalized.
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            a_.OpMax(dxbc::Dest::R(eM), dxbc::Src::LF(-1.0f), dxbc::Src::R(eM));
            a_.OpMin(dxbc::Dest::R(eM), dxbc::Src::LF(1.0f), dxbc::Src::R(eM));
            a_.OpMul(dxbc::Dest::R(eM), dxbc::Src::R(eM),
                     dxbc::Src::LF(float(INT16_MAX)));
          }
        }
        a_.OpEndIf();

        // Add plus/minus 0.5 before truncating according to the Direct3D format
        // conversion rules, and convert to signed integers.
        uint32_t round_bias_temp = PushSystemTemp();
        eM_remaining = export_eM;
        while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
          eM_remaining &= ~(uint8_t(1) << eM_index);
          uint32_t eM = system_temps_memexport_data_[eM_index];
          a_.OpBFI(dxbc::Dest::R(eM), dxbc::Src::LU(31), dxbc::Src::LU(0),
                   dxbc::Src::LF(0.5f), dxbc::Src::R(eM));
          a_.OpAdd(dxbc::Dest::R(eM), dxbc::Src::R(eM),
                   dxbc::Src::R(round_bias_temp));
          a_.OpFToI(dxbc::Dest::R(eM), dxbc::Src::R(eM));
        }
        // Release round_bias_temp.
        PopSystemTemp();
      }
      a_.OpElse();
      {
        // Unsigned.
        a_.OpIf(true, num_format_integer);
        {
          // Unsigned integer.
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            a_.OpMax(dxbc::Dest::R(eM), dxbc::Src::LF(0.0f), dxbc::Src::R(eM));
            a_.OpMin(dxbc::Dest::R(eM), dxbc::Src::LF(float(UINT16_MAX)),
                     dxbc::Src::R(eM));
          }
        }
        a_.OpElse();
        {
          // Unsigned normalized.
          eM_remaining = export_eM;
          while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
            eM_remaining &= ~(uint8_t(1) << eM_index);
            uint32_t eM = system_temps_memexport_data_[eM_index];
            // Saturate.
            a_.OpMov(dxbc::Dest::R(eM), dxbc::Src::R(eM), true);
            a_.OpMul(dxbc::Dest::R(eM), dxbc::Src::R(eM),
                     dxbc::Src::LF(float(UINT16_MAX)));
          }
        }
        a_.OpEndIf();

        // Add 0.5 before truncating according to the Direct3D format conversion
        // rules, and convert to unsigned integers.
        eM_remaining = export_eM;
        while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
          eM_remaining &= ~(uint8_t(1) << eM_index);
          uint32_t eM = system_temps_memexport_data_[eM_index];
          a_.OpAdd(dxbc::Dest::R(eM), dxbc::Src::R(eM), dxbc::Src::LF(0.5f));
          a_.OpFToU(dxbc::Dest::R(eM), dxbc::Src::R(eM));
        }
      }
      a_.OpEndIf();

      // Pack.
      eM_remaining = export_eM;
      while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
        eM_remaining &= ~(uint8_t(1) << eM_index);
        uint32_t eM = system_temps_memexport_data_[eM_index];
        a_.OpBFI(dxbc::Dest::R(eM, 0b0011), dxbc::Src::LU(16),
                 dxbc::Src::LU(16), dxbc::Src::R(eM, 0b1101),
                 dxbc::Src::R(eM, 0b1000));
      }

      a_.OpMov(element_size_dest, dxbc::Src::LU(3));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_FLOAT)));
    {
      // TODO(Triang3l): Use extended range conversion.
      eM_remaining = export_eM;
      while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
        eM_remaining &= ~(uint8_t(1) << eM_index);
        uint32_t eM = system_temps_memexport_data_[eM_index];
        a_.OpF32ToF16(dxbc::Dest::R(eM, 0b0001),
                      dxbc::Src::R(eM, dxbc::Src::kXXXX));
      }
      a_.OpMov(element_size_dest, dxbc::Src::LU(1));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16_FLOAT)));
    {
      // TODO(Triang3l): Use extended range conversion.
      eM_remaining = export_eM;
      while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
        eM_remaining &= ~(uint8_t(1) << eM_index);
        uint32_t eM = system_temps_memexport_data_[eM_index];
        a_.OpF32ToF16(dxbc::Dest::R(eM, 0b0011), dxbc::Src::R(eM));
        a_.OpBFI(dxbc::Dest::R(eM, 0b0001), dxbc::Src::LU(16),
                 dxbc::Src::LU(16), dxbc::Src::R(eM, dxbc::Src::kYYYY),
                 dxbc::Src::R(eM, dxbc::Src::kXXXX));
      }
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_16_16_16_16_FLOAT)));
    {
      // TODO(Triang3l): Use extended range conversion.
      eM_remaining = export_eM;
      while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
        eM_remaining &= ~(uint8_t(1) << eM_index);
        uint32_t eM = system_temps_memexport_data_[eM_index];
        a_.OpF32ToF16(dxbc::Dest::R(eM), dxbc::Src::R(eM));
        a_.OpBFI(dxbc::Dest::R(eM, 0b0011), dxbc::Src::LU(16),
                 dxbc::Src::LU(16), dxbc::Src::R(eM, 0b1101),
                 dxbc::Src::R(eM, 0b1000));
      }
      a_.OpMov(element_size_dest, dxbc::Src::LU(3));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_32_FLOAT)));
    {
      // Already in eM#.
      a_.OpMov(element_size_dest, dxbc::Src::LU(2));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_32_32_FLOAT)));
    {
      // Already in eM#.
      a_.OpMov(element_size_dest, dxbc::Src::LU(3));
    }
    a_.OpBreak();

    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::ColorFormat::k_32_32_32_32_FLOAT)));
    {
      // Already in eM#.
      a_.OpMov(element_size_dest, dxbc::Src::LU(4));
    }
    a_.OpBreak();

    a_.OpDefault();
    a_.OpMov(element_size_dest, dxbc::Src::LU(UINT32_MAX));
    a_.OpBreak();
  }
  // Close the color format switch.
  a_.OpEndSwitch();

  dxbc::Src element_size_src(dxbc::Src::R(temp, dxbc::Src::kXXXX));

  // Only temp.x is used currently (for the element size log2).

  // Do endian swap, using temp.y for the endianness value, and temp.z as a
  // temporary value.
  {
    dxbc::Dest endian_dest(dxbc::Dest::R(temp, 0b0010));
    dxbc::Src endian_src(dxbc::Src::R(temp, dxbc::Src::kYYYY));
    // Extract endianness into temp.y.
    a_.OpUBFE(endian_dest, dxbc::Src::LU(3), dxbc::Src::LU(0),
              dxbc::Src::R(system_temp_memexport_address_, dxbc::Src::kZZZZ));

    // Change 8-in-64 and 8-in-128 to 8-in-32.
    for (uint32_t i = 0; i < 2; ++i) {
      a_.OpIEq(dxbc::Dest::R(temp, 0b0100), endian_src,
               dxbc::Src::LU(uint32_t(i ? xenos::Endian128::k8in128
                                        : xenos::Endian128::k8in64)));
      eM_remaining = export_eM;
      while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
        eM_remaining &= ~(uint8_t(1) << eM_index);
        uint32_t eM = system_temps_memexport_data_[eM_index];
        a_.OpMovC(dxbc::Dest::R(eM), dxbc::Src::R(temp, dxbc::Src::kZZZZ),
                  dxbc::Src::R(eM, i ? 0b00011011 : 0b10110001),
                  dxbc::Src::R(eM));
      }
      a_.OpMovC(endian_dest, dxbc::Src::R(temp, dxbc::Src::kZZZZ),
                dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)), endian_src);
    }

    uint32_t swap_temp = PushSystemTemp();
    dxbc::Dest swap_temp_dest(dxbc::Dest::R(swap_temp));
    dxbc::Src swap_temp_src(dxbc::Src::R(swap_temp));

    // 8-in-16 or one half of 8-in-32.
    a_.OpSwitch(endian_src);
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in16)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)));
    eM_remaining = export_eM;
    while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
      eM_remaining &= ~(uint8_t(1) << eM_index);
      uint32_t eM = system_temps_memexport_data_[eM_index];
      dxbc::Dest eM_dest(dxbc::Dest::R(eM));
      dxbc::Src eM_src(dxbc::Src::R(eM));
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
    eM_remaining = export_eM;
    while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
      eM_remaining &= ~(uint8_t(1) << eM_index);
      uint32_t eM = system_temps_memexport_data_[eM_index];
      dxbc::Dest eM_dest(dxbc::Dest::R(eM));
      dxbc::Src eM_src(dxbc::Src::R(eM));
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

  // Extract the base index to temp.y and the index upper bound to temp.z.
  a_.OpUBFE(dxbc::Dest::R(temp, 0b0110), dxbc::Src::LU(23), dxbc::Src::LU(0),
            dxbc::Src::R(system_temp_memexport_address_, 0b1101 << 2));
  dxbc::Dest eM0_address_dest(dxbc::Dest::R(temp, 0b0010));
  dxbc::Src eM0_address_src(dxbc::Src::R(temp, dxbc::Src::kYYYY));
  dxbc::Src index_count_src(dxbc::Src::R(temp, dxbc::Src::kZZZZ));

  // Check if eM0 isn't out of bounds via temp.w - if it is, eM1...4 also are
  // (the base index can't be negative).
  a_.OpILT(dxbc::Dest::R(temp, 0b1000), eM0_address_src, index_count_src);
  a_.OpIf(true, dxbc::Src::R(temp, dxbc::Src::kWWWW));

  // Extract the base address to temp.w as bytes (30 lower bits to 30 upper bits
  // with 0 below).
  a_.OpIShL(dxbc::Dest::R(temp, 0b1000),
            dxbc::Src::R(system_temp_memexport_address_, dxbc::Src::kXXXX),
            dxbc::Src::LU(2));
  dxbc::Src base_address_src(dxbc::Src::R(temp, dxbc::Src::kWWWW));

  uint8_t export_eM14 = export_eM >> 1;
  assert_zero(export_eM14 >> 4);
  uint32_t eM14_address_temp = UINT32_MAX, store_eM14_temp = UINT32_MAX;
  if (export_eM14) {
    // Get eM1...4 indices and check if they're in bounds.
    eM14_address_temp = PushSystemTemp();
    dxbc::Dest eM14_address_dest(dxbc::Dest::R(eM14_address_temp, export_eM14));
    dxbc::Src eM14_address_src(dxbc::Src::R(eM14_address_temp));
    store_eM14_temp = PushSystemTemp();
    dxbc::Dest store_eM14_dest(dxbc::Dest::R(store_eM14_temp, export_eM14));
    dxbc::Src store_eM14_src(dxbc::Src::R(store_eM14_temp));
    a_.OpIAdd(eM14_address_dest, eM0_address_src, dxbc::Src::LU(1, 2, 3, 4));
    a_.OpILT(store_eM14_dest, eM14_address_src, index_count_src);
    // Check if eM1...4 were actually written by the invocation and merge the
    // result with store_eM14_temp.
    uint32_t eM14_written_temp = PushSystemTemp();
    a_.OpIBFE(dxbc::Dest::R(eM14_written_temp, export_eM14), dxbc::Src::LU(1),
              dxbc::Src::LU(1, 2, 3, 4),
              dxbc::Src::R(system_temp_memexport_enabled_and_eM_written_,
                           dxbc::Src::kYYYY));
    a_.OpAnd(store_eM14_dest, store_eM14_src, dxbc::Src::R(eM14_written_temp));
    // Release eM14_written_temp.
    PopSystemTemp();
    // Convert eM1...4 indices to global byte addresses.
    a_.OpIShL(eM14_address_dest, eM14_address_src, element_size_src);
    a_.OpIAdd(eM14_address_dest, base_address_src, eM14_address_src);
  }
  if (export_eM & 0b1) {
    // Convert eM0 index to a global byte address if it's needed.
    a_.OpIShL(eM0_address_dest, eM0_address_src, element_size_src);
    a_.OpIAdd(eM0_address_dest, base_address_src, eM0_address_src);
    // base_address_src and index_count_src are deallocated at this point (even
    // if eM0 isn't potentially written), temp.zw are now free.
    // Extract if eM0 was actually written by the invocation to temp.z.
    a_.OpIBFE(dxbc::Dest::R(temp, 0b0100), dxbc::Src::LU(1), dxbc::Src::LU(0),
              dxbc::Src::R(system_temp_memexport_enabled_and_eM_written_,
                           dxbc::Src::kYYYY));
  }
  dxbc::Src eM0_written_src(dxbc::Src::R(temp, dxbc::Src::kZZZZ));

  // Write depending on the element size.
  // No switch case will be entered for an unknown format (UINT32_MAX size
  // written), so writing won't be attempted for it.
  if (uav_index_shared_memory_ == kBindingIndexUnallocated) {
    uav_index_shared_memory_ = uav_count_++;
  }
  uint8_t eM14_remaining;
  uint32_t eM14_index;
  a_.OpSwitch(element_size_src);

  // 8bpp, 16bpp.
  dxbc::Dest atomic_dest(dxbc::Dest::U(
      uav_index_shared_memory_, uint32_t(UAVRegister::kSharedMemory), 0));
  for (uint32_t i = 0; i <= 1; ++i) {
    a_.OpCase(dxbc::Src::LU(i));
    dxbc::Src width_src(dxbc::Src::LU(8 << i));
    uint32_t sub_dword_temp = PushSystemTemp();
    if (export_eM & 0b1) {
      a_.OpIf(true, eM0_written_src);
      // sub_dword_temp.x = eM0 offset in the dword (8 << (byte_address & 3))
      // (assuming a little-endian host).
      a_.OpBFI(dxbc::Dest::R(sub_dword_temp, 0b0001), dxbc::Src::LU(2),
               dxbc::Src::LU(3), eM0_address_src, dxbc::Src::LU(0));
      // Keep only the dword part of the address.
      a_.OpAnd(eM0_address_dest, eM0_address_src, dxbc::Src::LU(~uint32_t(3)));
      // Erase the bits that will be replaced with eM0 via sub_dword_temp.y.
      a_.OpBFI(dxbc::Dest::R(sub_dword_temp, 0b0010), width_src,
               dxbc::Src::R(sub_dword_temp, dxbc::Src::kXXXX), dxbc::Src::LU(0),
               dxbc::Src::LU(UINT32_MAX));
      a_.OpAtomicAnd(atomic_dest, eM0_address_src, 0b0001,
                     dxbc::Src::R(sub_dword_temp, dxbc::Src::kYYYY));
      // Add the eM0 bits via sub_dword_temp.y.
      a_.OpBFI(dxbc::Dest::R(sub_dword_temp, 0b0010), width_src,
               dxbc::Src::R(sub_dword_temp, dxbc::Src::kXXXX),
               dxbc::Src::R(system_temps_memexport_data_[0], dxbc::Src::kXXXX),
               dxbc::Src::LU(0));
      a_.OpAtomicOr(atomic_dest, eM0_address_src, 0b0001,
                    dxbc::Src::R(sub_dword_temp, dxbc::Src::kYYYY));
      a_.OpEndIf();
    }
    if (export_eM14) {
      // sub_dword_temp = eM# offset in the dword (8 << (byte_address & 3))
      // (assuming a little-endian host).
      a_.OpBFI(dxbc::Dest::R(sub_dword_temp, export_eM14), dxbc::Src::LU(2),
               dxbc::Src::LU(3), dxbc::Src::R(eM14_address_temp),
               dxbc::Src::LU(0));
      // Keep only the dword part of the address.
      a_.OpAnd(dxbc::Dest::R(eM14_address_temp, export_eM14),
               dxbc::Src::R(eM14_address_temp), dxbc::Src::LU(~uint32_t(3)));
      uint32_t sub_dword_data_temp = PushSystemTemp();
      eM14_remaining = export_eM14;
      while (xe::bit_scan_forward(eM14_remaining, &eM14_index)) {
        eM14_remaining &= ~(uint8_t(1) << eM14_index);
        a_.OpIf(true, dxbc::Src::R(store_eM14_temp).Select(eM14_index));
        // Erase the bits that will be replaced with eM# via
        // sub_dword_data_temp.x.
        a_.OpBFI(dxbc::Dest::R(sub_dword_data_temp, 0b0001), width_src,
                 dxbc::Src::R(sub_dword_temp).Select(eM14_index),
                 dxbc::Src::LU(0), dxbc::Src::LU(UINT32_MAX));
        a_.OpAtomicAnd(
            atomic_dest, dxbc::Src::R(eM14_address_temp).Select(eM14_index),
            0b0001, dxbc::Src::R(sub_dword_data_temp, dxbc::Src::kXXXX));
        // Add the eM# bits via sub_dword_temp.y.
        a_.OpBFI(dxbc::Dest::R(sub_dword_data_temp, 0b0001), width_src,
                 dxbc::Src::R(sub_dword_temp).Select(eM14_index),
                 dxbc::Src::R(system_temps_memexport_data_[1 + eM14_index],
                              dxbc::Src::kXXXX),
                 dxbc::Src::LU(0));
        a_.OpAtomicOr(
            atomic_dest, dxbc::Src::R(eM14_address_temp).Select(eM14_index),
            0b0001, dxbc::Src::R(sub_dword_data_temp, dxbc::Src::kXXXX));
        a_.OpEndIf();
      }
      // Release sub_dword_data_temp.
      PopSystemTemp();
    }
    // Release sub_dword_temp.
    PopSystemTemp();
    a_.OpBreak();
  }

  // 32bpp, 64bpp, 128bpp.
  for (uint32_t i = 2; i <= 4; ++i) {
    a_.OpCase(dxbc::Src::LU(i));
    // Store (0b0001), Store2 (0b0011), Store4 (0b1111).
    uint32_t store_mask = (uint32_t(1) << (uint32_t(1) << (i - 2))) - 1;
    dxbc::Dest store_dest(dxbc::Dest::U(uav_index_shared_memory_,
                                        uint32_t(UAVRegister::kSharedMemory),
                                        store_mask));
    if (export_eM & 0b1) {
      a_.OpIf(true, eM0_written_src);
      a_.OpStoreRaw(store_dest, eM0_address_src,
                    dxbc::Src::R(system_temps_memexport_data_[0]));
      a_.OpEndIf();
    }
    eM14_remaining = export_eM14;
    while (xe::bit_scan_forward(eM14_remaining, &eM14_index)) {
      eM14_remaining &= ~(uint8_t(1) << eM14_index);
      a_.OpIf(true, dxbc::Src::R(store_eM14_temp).Select(eM14_index));
      a_.OpStoreRaw(store_dest,
                    dxbc::Src::R(eM14_address_temp).Select(eM14_index),
                    dxbc::Src::R(system_temps_memexport_data_[1 + eM14_index]));
      a_.OpEndIf();
    }
    a_.OpBreak();
  }

  // Close the element size switch.
  a_.OpEndSwitch();

  if (export_eM14) {
    // Release eM14_address_temp and store_eM14_temp.
    PopSystemTemp(2);
  }

  // Close the eM0 bounds check.
  a_.OpEndIf();

  // Release temp.
  PopSystemTemp();

  // Close the address correctness conditional.
  a_.OpEndIf();

  // Close the memory export allowed conditional.
  a_.OpEndIf();
}

}  // namespace gpu
}  // namespace xe
