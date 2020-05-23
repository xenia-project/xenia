/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"
#include "xenia/gpu/dxbc_shader_translator.h"

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }
  UpdateInstructionPredicationAndEmitDisassembly(instr.is_predicated,
                                                 instr.predicate_condition);

  uint32_t used_result_components = instr.result.GetUsedResultComponents();
  uint32_t needed_words = GetVertexFormatNeededWords(
      instr.attributes.data_format, used_result_components);
  if (!needed_words) {
    // Nothing to load - just constant 0/1 writes, or the swizzle includes only
    // components that don't exist in the format (writing zero instead of them).
    // Unpacking assumes at least some word is needed.
    StoreResult(instr.result, DxbcSrc::LF(0.0f));
    return;
  }

  // Create a 2-component DxbcSrc for the fetch constant (vf0 is in [0].xy of
  // the fetch constants array, vf1 is in [0].zw, vf2 is in [1].xy).
  if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
    cbuffer_index_fetch_constants_ = cbuffer_count_++;
  }
  DxbcSrc fetch_constant_src(
      DxbcSrc::CB(cbuffer_index_fetch_constants_,
                  uint32_t(CbufferRegister::kFetchConstants),
                  instr.operands[1].storage_index >> 1)
          .Swizzle((instr.operands[1].storage_index & 1) ? 0b10101110
                                                         : 0b00000100));

  // TODO(Triang3l): Verify the fetch constant type (that it's a vertex fetch,
  // not a texture fetch), here instead of dropping draws with invalid vertex
  // fetch constants on the CPU when proper bound checks are added - vfetch may
  // be conditional, so fetch constants may also be used conditionally.

  // - Load the byte address in physical memory to system_temp_result_.w (so
  //   it's not overwritten by data loads until the last one).

  DxbcDest address_dest(DxbcDest::R(system_temp_result_, 0b1000));
  DxbcSrc address_src(DxbcSrc::R(system_temp_result_, DxbcSrc::kWWWW));
  if (instr.attributes.stride) {
    // Convert the index to an integer by flooring or by rounding to the nearest
    // (as floor(index + 0.5) because rounding to the nearest even makes no
    // sense for addressing, both 1.5 and 2.5 would be 2).
    // http://web.archive.org/web/20100302145413/http://msdn.microsoft.com:80/en-us/library/bb313960.aspx
    {
      bool index_operand_temp_pushed = false;
      DxbcSrc index_operand(
          LoadOperand(instr.operands[0], 0b0001, index_operand_temp_pushed)
              .SelectFromSwizzled(0));
      if (instr.attributes.is_index_rounded) {
        DxbcOpAdd(address_dest, index_operand, DxbcSrc::LF(0.5f));
        DxbcOpRoundNI(address_dest, address_src);
      } else {
        DxbcOpRoundNI(address_dest, index_operand);
      }
      if (index_operand_temp_pushed) {
        PopSystemTemp();
      }
    }
    DxbcOpFToI(address_dest, address_src);
    // Extract the byte address from the fetch constant to
    // system_temp_result_.z.
    DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0100),
              fetch_constant_src.SelectFromSwizzled(0),
              DxbcSrc::LU(~uint32_t(3)));
    // Merge the index and the base address.
    DxbcOpIMAd(address_dest, address_src,
               DxbcSrc::LU(instr.attributes.stride * sizeof(uint32_t)),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ));
  } else {
    // Fetching from the same location - extract the byte address of the
    // beginning of the buffer.
    DxbcOpAnd(address_dest, fetch_constant_src.SelectFromSwizzled(0),
              DxbcSrc::LU(~uint32_t(3)));
  }
  // Add the word offset from the instruction, plus the offset of the first
  // needed word within the element.
  uint32_t first_word_index;
  xe::bit_scan_forward(needed_words, &first_word_index);
  int32_t first_word_buffer_offset =
      instr.attributes.offset + int32_t(first_word_index);
  if (first_word_buffer_offset) {
    // Add the constant word offset.
    DxbcOpIAdd(address_dest, address_src,
               DxbcSrc::LI(first_word_buffer_offset * sizeof(uint32_t)));
  }

  // - Load needed words to system_temp_result_, words 0, 1, 2, 3 to X, Y, Z, W
  //   respectively.

  // FIXME(Triang3l): Bound checking is not done here, but haven't encountered
  // any games relying on out-of-bounds access. On Adreno 200 on Android (LG
  // P705), however, words (not full elements) out of glBufferData bounds
  // contain 0.

  // Loading the FXC way, Load4.xyw becomes Load2 and Load - would be a
  // compromise between AMD, where there are load_dwordx2/3/4, and Nvidia, where
  // a ByteAddressBuffer is more like an R32_UINT buffer.

  // Depending on whether the shared memory is bound as an SRV or as a UAV (if
  // memexport is used), fetch from the appropriate binding. Extract whether
  // shared memory is a UAV to system_temp_result_.x and check. In the `if`, put
  // the more likely case (SRV), in the `else`, the less likely one (UAV).
  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
            DxbcSrc::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_Flags_Vec)
                .Select(kSysConst_Flags_Comp),
            DxbcSrc::LU(kSysFlag_SharedMemoryIsUAV));
  DxbcOpIf(false, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
  for (uint32_t i = 0; i < 2; ++i) {
    if (i) {
      DxbcOpElse();
    }
    DxbcSrc shared_memory_src(
        i ? DxbcSrc::U(0, uint32_t(UAVRegister::kSharedMemory))
          : DxbcSrc::T(0, uint32_t(SRVMainRegister::kSharedMemory)));
    uint32_t needed_words_remaining = needed_words;
    uint32_t word_index_previous = first_word_index;
    while (needed_words_remaining) {
      uint32_t word_index;
      xe::bit_scan_forward(needed_words_remaining, &word_index);
      uint32_t word_count;
      xe::bit_scan_forward(~(needed_words_remaining >> word_index),
                           &word_count);
      needed_words_remaining &=
          ~((uint32_t(1) << (word_index + word_count)) - uint32_t(1));
      if (word_index != word_index_previous) {
        // Go to the word in the buffer.
        DxbcOpIAdd(
            address_dest, address_src,
            DxbcSrc::LU((word_index - word_index_previous) * sizeof(uint32_t)));
        word_index_previous = word_index;
      }
      // Can ld_raw either to the first multiple components, or to any scalar
      // component.
      DxbcDest words_result_dest(DxbcDest::R(
          system_temp_result_, ((1 << word_count) - 1) << word_index));
      if (!word_index || word_count == 1) {
        // Read directly to system_temp_result_.
        DxbcOpLdRaw(words_result_dest, address_src, shared_memory_src);
      } else {
        // Read to the first components of a temporary register.
        uint32_t load_temp = PushSystemTemp();
        DxbcOpLdRaw(DxbcDest::R(load_temp, (1 << word_count) - 1), address_src,
                    shared_memory_src);
        // Copy to system_temp_result_.
        DxbcOpMov(words_result_dest,
                  DxbcSrc::R(load_temp,
                             (DxbcSrc::kXYZW & ((1 << (word_count * 2)) - 1))
                                 << (word_index * 2)));
        // Release load_temp.
        PopSystemTemp();
      }
    }
  }
  DxbcOpEndIf();

  DxbcSrc result_src(DxbcSrc::R(system_temp_result_));

  // - Endian swap the words.

  {
    uint32_t swap_temp = PushSystemTemp();

    // Extract the endianness from the fetch constant.
    uint32_t endian_temp, endian_temp_component;
    if (needed_words == 0b1111) {
      endian_temp = PushSystemTemp();
      endian_temp_component = 0;
    } else {
      endian_temp = swap_temp;
      xe::bit_scan_forward(~needed_words, &endian_temp_component);
    }
    DxbcOpAnd(DxbcDest::R(endian_temp, 1 << endian_temp_component),
              fetch_constant_src.SelectFromSwizzled(1), DxbcSrc::LU(0b11));
    DxbcSrc endian_src(DxbcSrc::R(endian_temp).Select(endian_temp_component));

    DxbcDest swap_temp_dest(DxbcDest::R(swap_temp, needed_words));
    DxbcSrc swap_temp_src(DxbcSrc::R(swap_temp));
    DxbcDest swap_result_dest(DxbcDest::R(system_temp_result_, needed_words));

    // 8-in-16 or one half of 8-in-32.
    DxbcOpSwitch(endian_src);
    DxbcOpCase(DxbcSrc::LU(uint32_t(Endian128::k8in16)));
    DxbcOpCase(DxbcSrc::LU(uint32_t(Endian128::k8in32)));
    // Temp = X0Z0.
    DxbcOpAnd(swap_temp_dest, result_src, DxbcSrc::LU(0x00FF00FF));
    // Result = YZW0.
    DxbcOpUShR(swap_result_dest, result_src, DxbcSrc::LU(8));
    // Result = Y0W0.
    DxbcOpAnd(swap_result_dest, result_src, DxbcSrc::LU(0x00FF00FF));
    // Result = YXWZ.
    DxbcOpUMAd(swap_result_dest, swap_temp_src, DxbcSrc::LU(256), result_src);
    DxbcOpBreak();
    DxbcOpEndSwitch();

    // 16-in-32 or another half of 8-in-32.
    DxbcOpSwitch(endian_src);
    DxbcOpCase(DxbcSrc::LU(uint32_t(Endian128::k8in32)));
    DxbcOpCase(DxbcSrc::LU(uint32_t(Endian128::k16in32)));
    // Temp = ZW00.
    DxbcOpUShR(swap_temp_dest, result_src, DxbcSrc::LU(16));
    // Result = ZWXY.
    DxbcOpBFI(swap_result_dest, DxbcSrc::LU(16), DxbcSrc::LU(16), result_src,
              swap_temp_src);
    DxbcOpBreak();
    DxbcOpEndSwitch();

    // Release endian_temp (if allocated) and swap_temp.
    PopSystemTemp((endian_temp != swap_temp) ? 2 : 1);
  }

  // - Unpack the format.

  uint32_t used_format_components =
      used_result_components &
      ((1 << GetVertexFormatComponentCount(instr.attributes.data_format)) - 1);
  DxbcDest result_unpacked_dest(
      DxbcDest::R(system_temp_result_, used_format_components));
  // If needed_words is not zero (checked in the beginning), this must not be
  // zero too. For simplicity, it's assumed that something will be unpacked
  // here.
  assert_not_zero(used_format_components);
  uint32_t packed_widths[4] = {}, packed_offsets[4] = {};
  uint32_t packed_swizzle = DxbcSrc::kXXXX;
  switch (instr.attributes.data_format) {
    case VertexFormat::k_8_8_8_8:
      packed_widths[0] = packed_widths[1] = packed_widths[2] =
          packed_widths[3] = 8;
      packed_offsets[1] = 8;
      packed_offsets[2] = 16;
      packed_offsets[3] = 24;
      break;
    case VertexFormat::k_2_10_10_10:
      packed_widths[0] = packed_widths[1] = packed_widths[2] = 10;
      packed_widths[3] = 2;
      packed_offsets[1] = 10;
      packed_offsets[2] = 20;
      packed_offsets[3] = 30;
      break;
    case VertexFormat::k_10_11_11:
      packed_widths[0] = packed_widths[1] = 11;
      packed_widths[2] = 10;
      packed_offsets[1] = 11;
      packed_offsets[2] = 22;
      break;
    case VertexFormat::k_11_11_10:
      packed_widths[0] = 10;
      packed_widths[1] = packed_widths[2] = 11;
      packed_offsets[1] = 10;
      packed_offsets[2] = 21;
      break;
    case VertexFormat::k_16_16:
      packed_widths[0] = packed_widths[1] = 16;
      packed_offsets[1] = 16;
      break;
    case VertexFormat::k_16_16_16_16:
      packed_widths[0] = packed_widths[1] = packed_widths[2] =
          packed_widths[3] = 16;
      packed_offsets[1] = packed_offsets[3] = 16;
      packed_swizzle = 0b01010000;
      break;
    default:
      // Not a packed integer format.
      break;
  }
  if (packed_widths[0]) {
    // Handle packed integer formats.
    if (instr.attributes.is_signed) {
      DxbcOpIBFE(result_unpacked_dest, DxbcSrc::LP(packed_widths),
                 DxbcSrc::LP(packed_offsets),
                 DxbcSrc::R(system_temp_result_, packed_swizzle));
      DxbcOpIToF(result_unpacked_dest, result_src);
      if (!instr.attributes.is_integer) {
        float packed_scales[4] = {};
        uint32_t packed_scales_mask = 0b0000;
        for (uint32_t i = 0; i < 4; ++i) {
          if (!(used_format_components & (1 << i))) {
            continue;
          }
          if (packed_widths[i] > 2) {
            packed_scales[i] =
                1.0f / float((uint32_t(1) << (packed_widths[i] - 1)) - 1);
            packed_scales_mask |= 1 << i;
          }
        }
        if (packed_scales_mask) {
          DxbcOpMul(DxbcDest::R(system_temp_result_, packed_scales_mask),
                    result_src, DxbcSrc::LP(packed_scales));
        }
        // Treat both -(2^(n-1)) and -(2^(n-1)-1) as -1, according to Direct3D
        // snorm to float conversion rules.
        DxbcOpMax(result_unpacked_dest, result_src, DxbcSrc::LF(-1.0f));
      }
    } else {
      DxbcOpUBFE(result_unpacked_dest, DxbcSrc::LP(packed_widths),
                 DxbcSrc::LP(packed_offsets),
                 DxbcSrc::R(system_temp_result_, packed_swizzle));
      DxbcOpUToF(result_unpacked_dest, result_src);
      if (!instr.attributes.is_integer) {
        float packed_scales[4] = {};
        uint32_t packed_scales_mask = 0b0000;
        for (uint32_t i = 0; i < 4; ++i) {
          if (!(used_format_components & (1 << i))) {
            continue;
          }
          if (packed_widths[i] > 1) {
            packed_scales[i] =
                1.0f / float((uint32_t(1) << packed_widths[i]) - 1);
            packed_scales_mask |= 1 << i;
          }
        }
        if (packed_scales_mask) {
          DxbcOpMul(DxbcDest::R(system_temp_result_, packed_scales_mask),
                    result_src, DxbcSrc::LP(packed_scales));
        }
      }
    }
  } else {
    switch (instr.attributes.data_format) {
      case VertexFormat::k_16_16_FLOAT:
      case VertexFormat::k_16_16_16_16_FLOAT:
        // FIXME(Triang3l): This converts from D3D10+ float16 with NaNs instead
        // of Xbox 360 float16 with extended range. However, haven't encountered
        // games relying on that yet.
        DxbcOpUBFE(result_unpacked_dest, DxbcSrc::LU(16),
                   DxbcSrc::LU(0, 16, 0, 16),
                   DxbcSrc::R(system_temp_result_, 0b01010000));
        DxbcOpF16ToF32(result_unpacked_dest, result_src);
        break;
      case VertexFormat::k_32:
      case VertexFormat::k_32_32:
      case VertexFormat::k_32_32_32_32:
        if (instr.attributes.is_signed) {
          DxbcOpIToF(result_unpacked_dest, result_src);
        } else {
          DxbcOpUToF(result_unpacked_dest, result_src);
        }
        if (!instr.attributes.is_integer) {
          DxbcOpMul(
              result_unpacked_dest, result_src,
              DxbcSrc::LF(instr.attributes.is_signed ? (1.0f / 2147483647.0f)
                                                     : (1.0f / 4294967295.0f)));
          // No need to clamp to -1 if signed - 1/(2^31-1) is rounded to
          // 1/(2^31) as float32.
        }
        break;
      case VertexFormat::k_32_FLOAT:
      case VertexFormat::k_32_32_FLOAT:
      case VertexFormat::k_32_32_32_32_FLOAT:
      case VertexFormat::k_32_32_32_FLOAT:
        // Already in the needed result components.
        break;
      default:
        // Packed integer or unknown format.
        assert_not_zero(packed_widths[0]);
        break;
    }
  }

  // - Apply the exponent bias.

  if (instr.attributes.exp_adjust) {
    DxbcOpMul(result_unpacked_dest, result_src,
              DxbcSrc::LF(std::ldexp(1.0f, instr.attributes.exp_adjust)));
  }

  // - Write zeros to components not present in the format.

  uint32_t used_missing_components =
      used_result_components & ~used_format_components;
  if (used_missing_components) {
    DxbcOpMov(DxbcDest::R(system_temp_result_, used_missing_components),
              DxbcSrc::LF(0.0f));
  }

  StoreResult(instr.result, DxbcSrc::R(system_temp_result_));
}

uint32_t DxbcShaderTranslator::FindOrAddTextureSRV(uint32_t fetch_constant,
                                                   TextureDimension dimension,
                                                   bool is_signed,
                                                   bool is_sign_required) {
  // 1D and 2D textures (including stacked ones) are treated as 2D arrays for
  // binding and coordinate simplicity.
  if (dimension == TextureDimension::k1D) {
    dimension = TextureDimension::k2D;
  }
  for (uint32_t i = 0; i < uint32_t(texture_srvs_.size()); ++i) {
    TextureSRV& texture_srv = texture_srvs_[i];
    if (texture_srv.fetch_constant == fetch_constant &&
        texture_srv.dimension == dimension &&
        texture_srv.is_signed == is_signed) {
      if (is_sign_required && !texture_srv.is_sign_required) {
        // kGetTextureComputedLod uses only the unsigned SRV, which means it
        // must be bound even when all components are signed.
        texture_srv.is_sign_required = true;
      }
      return i;
    }
  }
  if (texture_srvs_.size() >= kMaxTextureSRVs) {
    assert_always();
    return kMaxTextureSRVs - 1;
  }
  TextureSRV new_texture_srv;
  new_texture_srv.fetch_constant = fetch_constant;
  new_texture_srv.dimension = dimension;
  new_texture_srv.is_signed = is_signed;
  new_texture_srv.is_sign_required = is_sign_required;
  const char* dimension_name;
  switch (dimension) {
    case TextureDimension::k3D:
      dimension_name = "3d";
      break;
    case TextureDimension::kCube:
      dimension_name = "cube";
      break;
    default:
      dimension_name = "2d";
  }
  new_texture_srv.name = fmt::format("xe_texture{}_{}_{}", fetch_constant,
                                     dimension_name, is_signed ? 's' : 'u');
  uint32_t srv_index = uint32_t(texture_srvs_.size());
  texture_srvs_.emplace_back(std::move(new_texture_srv));
  return srv_index;
}

uint32_t DxbcShaderTranslator::FindOrAddSamplerBinding(
    uint32_t fetch_constant, TextureFilter mag_filter, TextureFilter min_filter,
    TextureFilter mip_filter, AnisoFilter aniso_filter) {
  // In Direct3D 12, anisotropic filtering implies linear filtering.
  if (aniso_filter != AnisoFilter::kDisabled &&
      aniso_filter != AnisoFilter::kUseFetchConst) {
    mag_filter = TextureFilter::kLinear;
    min_filter = TextureFilter::kLinear;
    mip_filter = TextureFilter::kLinear;
    aniso_filter = std::min(aniso_filter, AnisoFilter::kMax_16_1);
  }

  for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
    const SamplerBinding& sampler_binding = sampler_bindings_[i];
    if (sampler_binding.fetch_constant == fetch_constant &&
        sampler_binding.mag_filter == mag_filter &&
        sampler_binding.min_filter == min_filter &&
        sampler_binding.mip_filter == mip_filter &&
        sampler_binding.aniso_filter == aniso_filter) {
      return i;
    }
  }

  if (sampler_bindings_.size() >= kMaxSamplerBindings) {
    assert_always();
    return kMaxSamplerBindings - 1;
  }

  std::ostringstream name;
  name << "xe_sampler" << fetch_constant;
  if (aniso_filter != AnisoFilter::kUseFetchConst) {
    if (aniso_filter == AnisoFilter::kDisabled) {
      name << "_a0";
    } else {
      name << "_a" << (1u << (uint32_t(aniso_filter) - 1));
    }
  }
  if (aniso_filter == AnisoFilter::kDisabled ||
      aniso_filter == AnisoFilter::kUseFetchConst) {
    static const char* kFilterSuffixes[] = {"p", "l", "b", "f"};
    name << "_" << kFilterSuffixes[uint32_t(mag_filter)]
         << kFilterSuffixes[uint32_t(min_filter)]
         << kFilterSuffixes[uint32_t(mip_filter)];
  }

  SamplerBinding new_sampler_binding;
  new_sampler_binding.fetch_constant = fetch_constant;
  new_sampler_binding.mag_filter = mag_filter;
  new_sampler_binding.min_filter = min_filter;
  new_sampler_binding.mip_filter = mip_filter;
  new_sampler_binding.aniso_filter = aniso_filter;
  new_sampler_binding.name = name.str();
  uint32_t sampler_register = uint32_t(sampler_bindings_.size());
  sampler_bindings_.emplace_back(std::move(new_sampler_binding));
  return sampler_register;
}

void DxbcShaderTranslator::TfetchCubeCoordToCubeDirection(uint32_t reg) {
  // This does the reverse of what's done by the ALU sequence for cubemap
  // coordinate calculation.
  //
  // The major axis depends on the face index (passed as a float in reg.z):
  // +X for 0, -X for 1, +Y for 2, -Y for 3, +Z for 4, -Z for 5.
  //
  // If the major axis is X:
  // * X is 1.0 or -1.0.
  // * Y is -T.
  // * Z is -S for positive X, +S for negative X.
  // If it's Y:
  // * X is +S.
  // * Y is 1.0 or -1.0.
  // * Z is +T for positive Y, -T for negative Y.
  // If it's Z:
  // * X is +S for positive Z, -S for negative Z.
  // * Y is -T.
  // * Z is 1.0 or -1.0.

  // Make 0, not 1.5, the center of S and T.
  // mad reg.xy__, reg.xy__, l(2.0, 2.0, _, _), l(-3.0, -3.0, _, _)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0x40000000u);
  shader_code_.push_back(0x40000000u);
  shader_code_.push_back(0x3F800000u);
  shader_code_.push_back(0x3F800000u);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0xC0400000u);
  shader_code_.push_back(0xC0400000u);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Clamp the face index to 0...5 for safety (in case an offset was applied).
  // max reg.z, reg.z, l(0.0)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;
  // min reg.z, reg.z, l(5.0)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x40A00000);
  ++stat_.instruction_count;
  ++stat_.float_instruction_count;

  // Allocate a register for major axis info.
  uint32_t major_axis_temp = PushSystemTemp();

  // Convert the face index to an integer.
  // ftou major_axis_temp.x, reg.z
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOU) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.conversion_instruction_count;

  // Split the face number into major axis number and direction.
  // ubfe major_axis_temp.x__w, l(2, _, _, 1), l(1, _, _, 0),
  //      major_axis_temp.x__x
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1001, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(2);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(1);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(0);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Make booleans for whether each axis is major.
  // ieq major_axis_temp.xyz_, major_axis_temp.xxx_, l(0, 1, 2, _)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(EncodeVectorSwizzledOperand(
      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
  shader_code_.push_back(0);
  shader_code_.push_back(1);
  shader_code_.push_back(2);
  shader_code_.push_back(0);
  ++stat_.instruction_count;
  ++stat_.int_instruction_count;

  // Replace the face index in the source/destination with 1.0 or -1.0 for
  // swizzling.
  // movc reg.z, major_axis_temp.w, l(-1.0), l(1.0)
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0xBF800000u);
  shader_code_.push_back(
      EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
  shader_code_.push_back(0x3F800000u);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Swizzle and negate the coordinates depending on which axis is major, but
  // don't negate according to the direction of the major axis (will be done
  // later).

  // X case.
  // movc reg.xyz_, major_axis_temp.xxx_, reg.zyx_, reg.xyz_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11000110, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;
  // movc reg._yz_, major_axis_temp._xx_, -reg._yz_, reg._yz_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Y case.
  // movc reg._yz_, major_axis_temp._yy_, reg._zy_, reg._yz_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0110, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11011000, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Z case.
  // movc reg.y, major_axis_temp.z, -reg.y, reg.y
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Flip coordinates according to the direction of the major axis.

  // Z needs to be flipped if the major axis is X or Y, so make an X || Y mask.
  // X is flipped only when the major axis is Z.
  // or major_axis_temp.x, major_axis_temp.x, major_axis_temp.y
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
  shader_code_.push_back(major_axis_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // If the major axis is positive, nothing needs to be flipped. We have
  // 0xFFFFFFFF/0 at this point in the major axis mask, but 1/0 in the major
  // axis direction (didn't include W in ieq to waste less scalar operations),
  // but AND would result in 1/0, which is fine for movc too.
  // and major_axis_temp.x_z_, major_axis_temp.x_z_, major_axis_temp.w_w_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
  shader_code_.push_back(major_axis_temp);
  ++stat_.instruction_count;
  ++stat_.uint_instruction_count;

  // Flip axes that need to be flipped.
  // movc reg.x_z_, major_axis_temp.z_x_, -reg.x_z_, reg.x_z_
  shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
  shader_code_.push_back(
      EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b11000110, 1));
  shader_code_.push_back(major_axis_temp);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1) |
      ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
  shader_code_.push_back(
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG));
  shader_code_.push_back(reg);
  shader_code_.push_back(
      EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
  shader_code_.push_back(reg);
  ++stat_.instruction_count;
  ++stat_.movc_instruction_count;

  // Release major_axis_temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }
  UpdateInstructionPredicationAndEmitDisassembly(instr.is_predicated,
                                                 instr.predicate_condition);

  bool store_result = false;
  // Whether the result is only in X and all components should be remapped to X
  // while storing.
  bool replicate_result = false;

  DxbcSourceOperand operand;
  uint32_t operand_length = 0;
  if (instr.operand_count >= 1) {
    LoadDxbcSourceOperand(instr.operands[0], operand);
    operand_length = DxbcSourceOperandLength(operand);
  }

  uint32_t tfetch_index = instr.operands[1].storage_index;
  // Fetch constants are laid out like:
  // tf0[0] tf0[1] tf0[2] tf0[3]
  // tf0[4] tf0[5] tf1[0] tf1[1]
  // tf1[2] tf1[3] tf1[4] tf1[5]
  uint32_t tfetch_pair_offset = (tfetch_index >> 1) * 3;

  // TODO(Triang3l): kGetTextureBorderColorFrac.
  if (!IsDxbcPixelShader() &&
      (instr.opcode == FetchOpcode::kGetTextureComputedLod ||
       instr.opcode == FetchOpcode::kGetTextureGradients)) {
    // Quickly skip everything if tried to get anything involving derivatives
    // not in a pixel shader because only the pixel shader has derivatives.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_result_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  } else if (instr.opcode == FetchOpcode::kTextureFetch ||
             instr.opcode == FetchOpcode::kGetTextureComputedLod ||
             instr.opcode == FetchOpcode::kGetTextureWeights) {
    store_result = true;

    // 0 is unsigned, 1 is signed.
    uint32_t srv_indices[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t srv_indices_stacked[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t sampler_register = UINT32_MAX;
    // Only the fetch constant needed for kGetTextureWeights.
    if (instr.opcode != FetchOpcode::kGetTextureWeights) {
      if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
        // The LOD is a scalar and it doesn't depend on the texture contents, so
        // require any variant - unsigned in this case because more texture
        // formats support it.
        srv_indices[0] =
            FindOrAddTextureSRV(tfetch_index, instr.dimension, false, true);
        if (instr.dimension == TextureDimension::k3D) {
          // 3D or 2D stacked is selected dynamically.
          srv_indices_stacked[0] = FindOrAddTextureSRV(
              tfetch_index, TextureDimension::k2D, false, true);
        }
      } else {
        srv_indices[0] =
            FindOrAddTextureSRV(tfetch_index, instr.dimension, false);
        srv_indices[1] =
            FindOrAddTextureSRV(tfetch_index, instr.dimension, true);
        if (instr.dimension == TextureDimension::k3D) {
          // 3D or 2D stacked is selected dynamically.
          srv_indices_stacked[0] =
              FindOrAddTextureSRV(tfetch_index, TextureDimension::k2D, false);
          srv_indices_stacked[1] =
              FindOrAddTextureSRV(tfetch_index, TextureDimension::k2D, true);
        }
      }
      sampler_register = FindOrAddSamplerBinding(
          tfetch_index, instr.attributes.mag_filter,
          instr.attributes.min_filter, instr.attributes.mip_filter,
          instr.attributes.aniso_filter);
    }

    uint32_t coord_temp = PushSystemTemp();
    // Move coordinates to pv temporarily so zeros can be added to expand them
    // to Texture2DArray coordinates and to apply offset. Or, if the instruction
    // is getWeights, move them to pv because their fractional part will be
    // returned.
    uint32_t coord_mask = 0b0111;
    switch (instr.dimension) {
      case TextureDimension::k1D:
        coord_mask = 0b0001;
        break;
      case TextureDimension::k2D:
        coord_mask = 0b0011;
        break;
      case TextureDimension::k3D:
        coord_mask = 0b0111;
        break;
      case TextureDimension::kCube:
        // Don't need the 3rd component for getWeights because it's the face
        // index, so it doesn't participate in bilinear filtering.
        coord_mask =
            instr.opcode == FetchOpcode::kGetTextureWeights ? 0b0011 : 0b0111;
        break;
    }
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
    shader_code_.push_back(coord_temp);
    UseDxbcSourceOperand(operand);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;

    // If 1D or 2D, fill the unused coordinates with zeros (sampling the only
    // row of the only slice). For getWeights, also clear the 4th component
    // because the coordinates will be returned.
    uint32_t coord_all_components_mask =
        instr.opcode == FetchOpcode::kGetTextureWeights ? 0b1111 : 0b0111;
    uint32_t coord_zero_mask = coord_all_components_mask & ~coord_mask;
    if (coord_zero_mask) {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
      shader_code_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, coord_zero_mask, 1));
      shader_code_.push_back(coord_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
    }

    // Get the offset to see if the size of the texture is needed.
    // It's probably applicable to tfetchCube too, we're going to assume it's
    // used for them the same way as for stacked textures.
    // http://web.archive.org/web/20090511231340/http://msdn.microsoft.com:80/en-us/library/bb313959.aspx
    // Adding 1/1024 - quarter of one fixed-point unit of subpixel precision
    // (not to touch rounding when the GPU is converting to fixed-point) - to
    // resolve the ambiguity when the texture coordinate is directly between two
    // pixels, which hurts nearest-neighbor sampling (fixes the XBLA logo being
    // blocky in Banjo-Kazooie and the outlines around things and overall
    // blockiness in Halo 3).
    // TODO(Triang3l): Investigate the sign of this epsilon, because in Halo 3
    // positive causes thin outlines with MSAA (with ROV), and negative causes
    // thick outlines with SSAA there.
    float offset_x = instr.attributes.offset_x + (1.0f / 1024.0f);
    if (instr.opcode == FetchOpcode::kGetTextureWeights) {
      // Bilinear filtering (for shadows, for instance, in Halo 3), 0.5 -
      // exactly the pixel.
      offset_x -= 0.5f;
    }
    float offset_y = 0.0f, offset_z = 0.0f;
    if (instr.dimension == TextureDimension::k2D ||
        instr.dimension == TextureDimension::k3D ||
        instr.dimension == TextureDimension::kCube) {
      offset_y = instr.attributes.offset_y + (1.0f / 1024.0f);
      if (instr.opcode == FetchOpcode::kGetTextureWeights) {
        offset_y -= 0.5f;
      }
      // Don't care about the Z offset for cubemaps when getting weights because
      // zero Z will be returned anyway (the face index doesn't participate in
      // bilinear filtering).
      if (instr.dimension == TextureDimension::k3D ||
          (instr.dimension == TextureDimension::kCube &&
           instr.opcode != FetchOpcode::kGetTextureWeights)) {
        offset_z = instr.attributes.offset_z;
        if (instr.dimension == TextureDimension::k3D) {
          // Z is the face index for cubemaps, so don't apply the epsilon to it.
          offset_z += 1.0f / 1024.0f;
          if (instr.opcode == FetchOpcode::kGetTextureWeights) {
            offset_z -= 0.5f;
          }
        }
      }
    }

    // Gather info about filtering across array layers.
    // Use the magnification filter when no derivatives:
    // https://stackoverflow.com/questions/40328956/difference-between-sample-and-samplelevel-wrt-texture-filtering
    bool vol_min_filter_applicable =
        instr.opcode == FetchOpcode::kTextureFetch &&
        (instr.attributes.use_register_gradients ||
         (instr.attributes.use_computed_lod && IsDxbcPixelShader()));
    bool has_vol_mag_filter =
        instr.attributes.vol_mag_filter != TextureFilter::kUseFetchConst;
    bool has_vol_min_filter =
        vol_min_filter_applicable
            ? instr.attributes.vol_min_filter != TextureFilter::kUseFetchConst
            : has_vol_mag_filter;
    bool vol_mag_filter_linear =
        instr.attributes.vol_mag_filter == TextureFilter::kLinear;
    bool vol_min_filter_linear =
        vol_min_filter_applicable
            ? instr.attributes.vol_min_filter == TextureFilter::kLinear
            : vol_mag_filter_linear;
    bool vol_mag_filter_point = has_vol_mag_filter && !vol_mag_filter_linear;
    bool vol_min_filter_point = has_vol_min_filter && !vol_min_filter_linear;

    // Get the texture size if needed, apply offset and switch between
    // normalized and unnormalized coordinates if needed. The offset is
    // fractional on the Xbox 360 (has 0.5 granularity), unlike in Direct3D 12,
    // and cubemaps possibly can have offset and their coordinates are different
    // than in Direct3D 12 (like an array texture rather than a direction).
    // getWeights instructions also need the texture size because they work like
    // frac(coord * texture_size).
    // TODO(Triang3l): Unnormalized coordinates should be disabled when the
    // wrap mode is not a clamped one, though it's probably a very rare case,
    // unlikely to be used on purpose.
    // http://web.archive.org/web/20090514012026/http://msdn.microsoft.com:80/en-us/library/bb313957.aspx
    uint32_t size_and_is_3d_temp = UINT32_MAX;
    // For stacked textures, if point sampling is not forced in the instruction:
    // X - whether linear filtering should be done across layers (for color
    //     grading LUTs in Unreal Engine 3 games and Burnout Revenge), unless
    //     the filter is known from the instruction for all cases.
    // Y - lerp factor between the two layers, unless only point sampling can be
    //     used.
    uint32_t vol_filter_temp = UINT32_MAX;
    bool vol_filter_temp_linear_test = D3D10_SB_INSTRUCTION_TEST_NONZERO;
    // With 1/1024 this will always be true anyway, but let's keep the shorter
    // path without the offset in case some day this hack won't be used anymore
    // somehow.
    bool has_offset = offset_x != 0.0f || offset_y != 0.0f || offset_z != 0.0f;
    if (instr.opcode == FetchOpcode::kGetTextureWeights || has_offset ||
        instr.attributes.unnormalized_coordinates ||
        instr.dimension == TextureDimension::k3D) {
      size_and_is_3d_temp = PushSystemTemp();
      if (instr.opcode == FetchOpcode::kTextureFetch &&
          instr.dimension == TextureDimension::k3D) {
        uint32_t vol_filter_temp_components = 0b0000;
        if (!has_vol_mag_filter || !has_vol_min_filter ||
            vol_mag_filter_linear != vol_min_filter_linear) {
          vol_filter_temp_components |= 0b0011;
        } else if (vol_mag_filter_linear || vol_min_filter_linear) {
          vol_filter_temp_components |= 0b0010;
        }
        // Initialize to 0 to break register dependency.
        if (vol_filter_temp_components != 0) {
          vol_filter_temp = PushSystemTemp(vol_filter_temp_components);
        }
      }

      // Will use fetch constants for the size and for stacked texture filter.
      if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
        cbuffer_index_fetch_constants_ = cbuffer_count_++;
      }

      // Get 2D texture size and array layer count, in bits 0:12, 13:25, 26:31
      // of dword 2 ([0].z or [2].x).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(13);
      shader_code_.push_back(instr.dimension != TextureDimension::k1D ? 13 : 0);
      shader_code_.push_back(instr.dimension == TextureDimension::k3D ? 6 : 0);
      shader_code_.push_back(0);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(13);
      shader_code_.push_back(26);
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                        2 - 2 * (tfetch_index & 1), 3));
      shader_code_.push_back(cbuffer_index_fetch_constants_);
      shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
      shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1) * 2);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;

      if (instr.dimension == TextureDimension::k3D) {
        // Write whether the texture is 3D to W if it's 3D/stacked, as
        // 0xFFFFFFFF for 3D or 0 for stacked. The dimension is in dword 5 in
        // bits 9:10.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        // Dword 5 is [1].y or [2].w.
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      1 + 2 * (tfetch_index & 1), 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + 1 + (tfetch_index & 1));
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3 << 9);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(uint32_t(Dimension::k3D) << 9);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;

        // Check if need to replace the size with 3D texture size.
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                               ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                   D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        ++stat_.instruction_count;
        ++stat_.dynamic_flow_control_count;

        // Extract 3D texture size (in the same constant, but 11:11:10).
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(11);
        shader_code_.push_back(11);
        shader_code_.push_back(10);
        shader_code_.push_back(0);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
        shader_code_.push_back(0);
        shader_code_.push_back(11);
        shader_code_.push_back(22);
        shader_code_.push_back(0);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                          2 - 2 * (tfetch_index & 1), 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1) * 2);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;

        // Done replacing.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
      }

      // Convert the size to float.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;

      // Add 1 to the size because fetch constants store size minus one.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(size_and_is_3d_temp);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0x3F800000);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;

      if (instr.opcode == FetchOpcode::kGetTextureWeights) {
        // Weights for bilinear filtering - need to get the fractional part of
        // unnormalized coordinates.

        if (instr.attributes.unnormalized_coordinates) {
          if (has_offset) {
            // Apply the offset.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_x));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_y));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_z));
            shader_code_.push_back(0);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        } else {
          // Unnormalize the coordinates and apply the offset.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(has_offset ? D3D10_SB_OPCODE_MAD
                                                     : D3D10_SB_OPCODE_MUL) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(has_offset ? 12
                                                                      : 7));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          if (has_offset) {
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_x));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_y));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_z));
            shader_code_.push_back(0);
          }
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;
        }
      } else {
        // Texture fetch - need to get normalized coordinates (with unnormalized
        // Z for stacked textures).

        if (has_offset || instr.attributes.unnormalized_coordinates) {
          // Take the reciprocal of the size to normalize the UV coordinates and
          // the offset.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_RCP) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
          shader_code_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, coord_mask & 0b0011, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;

          // Normalize the coordinates.
          if (instr.attributes.unnormalized_coordinates) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, coord_mask & 0b0011, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }

          // Apply the offset (coord = offset * 1/size + coord).
          if (has_offset) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, coord_mask & 0b0011, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_x));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&offset_y));
            shader_code_.push_back(0);
            shader_code_.push_back(0);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            shader_code_.push_back(EncodeVectorSwizzledOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
            shader_code_.push_back(coord_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        }

        if (instr.dimension == TextureDimension::k3D) {
          // Both 3D textures and 2D arrays have their Z coordinate normalized,
          // however, on PC, array elements have unnormalized indices.
          // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
          // The offset must be handled not only for 3D textures, but for the
          // array layer too - used in Halo 3.

          // Check if stacked.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
              ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                  D3D10_SB_INSTRUCTION_TEST_ZERO) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
          shader_code_.push_back(size_and_is_3d_temp);
          ++stat_.instruction_count;
          ++stat_.dynamic_flow_control_count;

          // Layers on the Xenos are indexed like texels, with 0.5 being exactly
          // layer 0, but in D3D10+ 0.0 is exactly layer 0. Halo 3 uses i + 0.5
          // offset for lightmap index, for instance.
          float offset_layer = offset_z - 0.5f;

          if (instr.attributes.unnormalized_coordinates) {
            if (offset_layer != 0.0f) {
              // Add the offset to the array layer.
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
              shader_code_.push_back(EncodeVectorMaskedOperand(
                  D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
              shader_code_.push_back(coord_temp);
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
              shader_code_.push_back(coord_temp);
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(
                  *reinterpret_cast<const uint32_t*>(&offset_layer));
              ++stat_.instruction_count;
              ++stat_.float_instruction_count;
            }
          } else {
            // Unnormalize the array layer and apply the offset.
            if (offset_layer != 0.0f) {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
            } else {
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            }
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            if (offset_layer != 0.0f) {
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(
                  *reinterpret_cast<const uint32_t*>(&offset_layer));
            }
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }

          if (vol_filter_temp != UINT32_MAX) {
            if (vol_min_filter_applicable) {
              if (!has_vol_mag_filter || !has_vol_min_filter ||
                  vol_mag_filter_linear != vol_min_filter_linear) {
                // Check if magnifying (derivative <= 1, according to OpenGL
                // rules) or minifying (> 1) the texture across Z. Get the
                // maximum of absolutes of the two derivatives of the array
                // layer, either explicit or implicit.
                if (instr.attributes.use_register_gradients) {
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
                  shader_code_.push_back(EncodeVectorMaskedOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                                             D3D10_SB_OPERAND_TYPE_TEMP, 2, 1) |
                                         ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
                          D3D10_SB_OPERAND_MODIFIER_ABS));
                  shader_code_.push_back(system_temp_grad_h_lod_);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                                             D3D10_SB_OPERAND_TYPE_TEMP, 2, 1) |
                                         ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
                          D3D10_SB_OPERAND_MODIFIER_ABS));
                  shader_code_.push_back(system_temp_grad_v_);
                  ++stat_.instruction_count;
                  ++stat_.float_instruction_count;
                } else {
                  for (uint32_t i = 0; i < 2; ++i) {
                    shader_code_.push_back(
                        ENCODE_D3D10_SB_OPCODE_TYPE(
                            i ? D3D11_SB_OPCODE_DERIV_RTY_COARSE
                              : D3D11_SB_OPCODE_DERIV_RTX_COARSE) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
                    shader_code_.push_back(EncodeVectorMaskedOperand(
                        D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
                    shader_code_.push_back(vol_filter_temp);
                    shader_code_.push_back(EncodeVectorSelectOperand(
                        D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
                    shader_code_.push_back(coord_temp);
                    ++stat_.instruction_count;
                    ++stat_.float_instruction_count;
                  }

                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
                  shader_code_.push_back(EncodeVectorMaskedOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                                             D3D10_SB_OPERAND_TYPE_TEMP, 0, 1) |
                                         ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
                          D3D10_SB_OPERAND_MODIFIER_ABS));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                                             D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
                                         ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
                          D3D10_SB_OPERAND_MODIFIER_ABS));
                  shader_code_.push_back(vol_filter_temp);
                  ++stat_.instruction_count;
                  ++stat_.float_instruction_count;
                }

                // Check if minifying.
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LT) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                shader_code_.push_back(vol_filter_temp);
                shader_code_.push_back(
                    EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                shader_code_.push_back(0x3F800000);
                shader_code_.push_back(EncodeVectorSelectOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
                shader_code_.push_back(vol_filter_temp);
                ++stat_.instruction_count;
                ++stat_.float_instruction_count;

                if (has_vol_mag_filter || has_vol_min_filter) {
                  if (has_vol_mag_filter && has_vol_min_filter) {
                    // Both from the instruction.
                    assert_true(vol_mag_filter_linear != vol_min_filter_linear);
                    if (vol_mag_filter_linear) {
                      // Either linear when minifying (non-zero) or linear when
                      // magnifying (zero).
                      vol_filter_temp_linear_test =
                          D3D10_SB_INSTRUCTION_TEST_ZERO;
                    }
                  } else {
                    // Check if need to use the filter from the fetch constant.
                    // Has mag filter - need the fetch constant filter when
                    // minifying (non-zero minification test result).
                    // Has min filter - need it when magnifying (zero).
                    shader_code_.push_back(
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                        ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                            has_vol_mag_filter
                                ? D3D10_SB_INSTRUCTION_TEST_NONZERO
                                : D3D10_SB_INSTRUCTION_TEST_ZERO) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
                    shader_code_.push_back(EncodeVectorSelectOperand(
                        D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
                    shader_code_.push_back(vol_filter_temp);
                    ++stat_.instruction_count;
                    ++stat_.dynamic_flow_control_count;

                    // Take the filter from the dword 4 of the fetch constant
                    // ([1].x or [2].z) if it's not in the instruction.
                    // Has mag filter - this will be executed for minification
                    // (bit 1).
                    // Has min filter - for magnification (bit 0).
                    shader_code_.push_back(
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
                    shader_code_.push_back(EncodeVectorMaskedOperand(
                        D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                    shader_code_.push_back(vol_filter_temp);
                    shader_code_.push_back(EncodeVectorSelectOperand(
                        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                        2 * (tfetch_index & 1), 3));
                    shader_code_.push_back(cbuffer_index_fetch_constants_);
                    shader_code_.push_back(
                        uint32_t(CbufferRegister::kFetchConstants));
                    shader_code_.push_back(tfetch_pair_offset + 1 +
                                           (tfetch_index & 1));
                    shader_code_.push_back(EncodeScalarOperand(
                        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                    shader_code_.push_back(has_vol_mag_filter ? (1 << 1)
                                                              : (1 << 0));
                    ++stat_.instruction_count;
                    ++stat_.uint_instruction_count;

                    // If not using the filter from the fetch constant, set the
                    // value from the instruction.
                    // Need to change this for:
                    // - Magnifying (zero set) and linear (non-zero needed) vol
                    //   mag filter.
                    // - Minifying (non-zero set) and point (zero needed) vol
                    //   min filter.
                    // Already the expected zero or non-zero value for:
                    // - Magnifying (zero set) and point (zero needed) vol mag
                    //   filter.
                    // - Minifying (non-zero set) and linear (non-zero needed)
                    //   vol min filter.
                    if (vol_mag_filter_linear || vol_min_filter_point) {
                      shader_code_.push_back(
                          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
                      ++stat_.instruction_count;

                      shader_code_.push_back(
                          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
                      shader_code_.push_back(EncodeVectorMaskedOperand(
                          D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                      shader_code_.push_back(vol_filter_temp);
                      shader_code_.push_back(EncodeScalarOperand(
                          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                      shader_code_.push_back(uint32_t(has_vol_mag_filter));
                      ++stat_.instruction_count;
                      ++stat_.mov_instruction_count;
                    }

                    // Close the fetch constant filter check.
                    shader_code_.push_back(
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
                    ++stat_.instruction_count;
                  }
                } else {
                  // Mask the bit offset (1 for vol_min_filter, 0 for
                  // vol_mag_filter) in the fetch constant.
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
                  shader_code_.push_back(EncodeVectorMaskedOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeScalarOperand(
                      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                  shader_code_.push_back(1);
                  ++stat_.instruction_count;
                  ++stat_.uint_instruction_count;

                  // Extract the filter from dword 4 of the fetch constant
                  // ([1].x or [2].z).
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
                  shader_code_.push_back(EncodeVectorMaskedOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeScalarOperand(
                      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                  shader_code_.push_back(1);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
                  shader_code_.push_back(vol_filter_temp);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                      2 * (tfetch_index & 1), 3));
                  shader_code_.push_back(cbuffer_index_fetch_constants_);
                  shader_code_.push_back(
                      uint32_t(CbufferRegister::kFetchConstants));
                  shader_code_.push_back(tfetch_pair_offset + 1 +
                                         (tfetch_index & 1));
                  ++stat_.instruction_count;
                  ++stat_.uint_instruction_count;
                }
              }
            } else {
              if (!has_vol_mag_filter) {
                // Extract the magnification filter when there are no
                // derivatives from bit 0 of dword 4 of the fetch constant
                // ([1].x or [2].z).
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
                shader_code_.push_back(vol_filter_temp);
                shader_code_.push_back(EncodeVectorSelectOperand(
                    D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                    2 * (tfetch_index & 1), 3));
                shader_code_.push_back(cbuffer_index_fetch_constants_);
                shader_code_.push_back(
                    uint32_t(CbufferRegister::kFetchConstants));
                shader_code_.push_back(tfetch_pair_offset + 1 +
                                       (tfetch_index & 1));
                shader_code_.push_back(
                    EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                shader_code_.push_back(1);
                ++stat_.instruction_count;
                ++stat_.uint_instruction_count;
              }
            }
          }

          if (!vol_mag_filter_point || !vol_min_filter_point) {
            if (!vol_mag_filter_linear || !vol_min_filter_linear) {
              // Check if using linear filtering between array layers.
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                  ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                      vol_filter_temp_linear_test) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
              shader_code_.push_back(vol_filter_temp);
              ++stat_.instruction_count;
              ++stat_.dynamic_flow_control_count;
            }

            // Floor the layer index to get the linear interpolation factor.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
            shader_code_.push_back(vol_filter_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(coord_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;

            // Get the fraction of the layer index, with i + 0.5 right between
            // layers, as the linear interpolation factor between layers Z and
            // Z + 1.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
            shader_code_.push_back(vol_filter_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1) |
                ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
            shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
                D3D10_SB_OPERAND_MODIFIER_NEG));
            shader_code_.push_back(vol_filter_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;

            // Floor the layer index again for sampling.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
            shader_code_.push_back(coord_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(coord_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;

            if (!vol_mag_filter_linear || !vol_min_filter_linear) {
              // Close the linear filtering check.
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
              ++stat_.instruction_count;
            }
          }

          if (instr.attributes.unnormalized_coordinates || offset_z != 0.0f) {
            // Handle 3D texture coordinates - may need to normalize and/or add
            // the offset. Check if 3D.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
            ++stat_.instruction_count;

            // Need 1/depth for both normalization and offset.
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_RCP) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
            shader_code_.push_back(size_and_is_3d_temp);
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;

            if (instr.attributes.unnormalized_coordinates) {
              // Normalize the W coordinate.
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
              shader_code_.push_back(EncodeVectorMaskedOperand(
                  D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
              shader_code_.push_back(coord_temp);
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
              shader_code_.push_back(coord_temp);
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
              shader_code_.push_back(size_and_is_3d_temp);
              ++stat_.instruction_count;
              ++stat_.float_instruction_count;
            }

            if (offset_z != 0.0f) {
              // Add normalized offset.
              shader_code_.push_back(
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
              shader_code_.push_back(EncodeVectorMaskedOperand(
                  D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
              shader_code_.push_back(coord_temp);
              shader_code_.push_back(
                  EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
              shader_code_.push_back(
                  *reinterpret_cast<const uint32_t*>(&offset_z));
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
              shader_code_.push_back(size_and_is_3d_temp);
              shader_code_.push_back(
                  EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
              shader_code_.push_back(coord_temp);
              ++stat_.instruction_count;
              ++stat_.float_instruction_count;
            }
          }

          // Close the 3D or stacked check.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }
      }
    }

    if (instr.opcode == FetchOpcode::kGetTextureWeights) {
      // Return the fractional part of unnormalized coordinates as bilinear
      // filtering weights.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, coord_mask, 1));
      shader_code_.push_back(system_temp_result_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(coord_temp);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } else {
      if (instr.dimension == TextureDimension::kCube) {
        // Convert cubemap coordinates passed as 2D array texture coordinates
        // plus 1 in ST to a 3D direction. We can't use a 2D array to emulate
        // cubemaps because at the edges, especially in pixel shader helper
        // invocations, the major axis changes, causing S/T to jump between 0
        // and 1, breaking gradient calculation and causing the 1x1 mipmap to be
        // sampled.
        TfetchCubeCoordToCubeDirection(coord_temp);
      }

      // Bias the register LOD if fetching with explicit LOD (so this is not
      // done two or four times due to 3D/stacked and unsigned/signed).
      uint32_t lod_temp = system_temp_grad_h_lod_, lod_temp_component = 3;
      if (instr.opcode == FetchOpcode::kTextureFetch &&
          instr.attributes.use_register_lod &&
          instr.attributes.lod_bias != 0.0f) {
        lod_temp = PushSystemTemp();
        lod_temp_component = 0;
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(lod_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(system_temp_grad_h_lod_);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(
            *reinterpret_cast<const uint32_t*>(&instr.attributes.lod_bias));
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }

      // Allocate the register for the value from the signed texture, and also
      // for the second array layer and lerping the layers.
      uint32_t signed_value_temp = instr.opcode == FetchOpcode::kTextureFetch
                                       ? PushSystemTemp()
                                       : UINT32_MAX;
      uint32_t vol_filter_lerp_temp = UINT32_MAX;
      if (vol_filter_temp != UINT32_MAX &&
          (!vol_mag_filter_point || !vol_min_filter_point)) {
        vol_filter_lerp_temp = PushSystemTemp();
      }

      // tfetch1D/2D/Cube just fetch directly. tfetch3D needs to fetch either
      // the 3D texture or the 2D stacked texture, so two sample instructions
      // selected conditionally are used in this case.
      if (instr.dimension == TextureDimension::k3D) {
        assert_true(size_and_is_3d_temp != UINT32_MAX);
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                               ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                   D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(size_and_is_3d_temp);
        ++stat_.instruction_count;
        ++stat_.dynamic_flow_control_count;
      }
      // Sample both 3D and 2D array bindings for tfetch3D.
      for (uint32_t i = 0;
           i < (instr.dimension == TextureDimension::k3D ? 2u : 1u); ++i) {
        if (i) {
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ELSE) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }
        if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
          // The non-pixel-shader case should be handled before because it
          // just returns a constant in this case.
          assert_true(IsDxbcPixelShader());
          uint32_t srv_index_current =
              i ? srv_indices_stacked[0] : srv_indices[0];
          replicate_result = true;
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_1_SB_OPCODE_LOD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
          shader_code_.push_back(system_temp_result_);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
          shader_code_.push_back(coord_temp);
          shader_code_.push_back(EncodeVectorSwizzledOperand(
              D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
          shader_code_.push_back(1 + srv_index_current);
          shader_code_.push_back(
              uint32_t(SRVMainRegister::kBoundTexturesStart) +
              srv_index_current);
          shader_code_.push_back(
              EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
          shader_code_.push_back(sampler_register);
          shader_code_.push_back(sampler_register);
          ++stat_.instruction_count;
          ++stat_.lod_instructions;
          // Apply the LOD bias if used.
          if (instr.attributes.lod_bias != 0.0f) {
            shader_code_.push_back(
                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
            shader_code_.push_back(EncodeVectorMaskedOperand(
                D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
            shader_code_.push_back(system_temp_result_);
            shader_code_.push_back(
                EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
            shader_code_.push_back(system_temp_result_);
            shader_code_.push_back(
                EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
            shader_code_.push_back(
                *reinterpret_cast<const uint32_t*>(&instr.attributes.lod_bias));
            ++stat_.instruction_count;
            ++stat_.float_instruction_count;
          }
        } else {
          // Sample both unsigned and signed, and for stacked textures, two
          // samples if filtering is needed.
          for (uint32_t j = 0; j < 2; ++j) {
            uint32_t srv_index_current =
                i ? srv_indices_stacked[j] : srv_indices[j];
            uint32_t target_temp_sign =
                j ? signed_value_temp : system_temp_result_;
            for (uint32_t k = 0;
                 k < (vol_filter_lerp_temp != UINT32_MAX ? 2u : 1u); ++k) {
              uint32_t target_temp_current =
                  k ? vol_filter_lerp_temp : target_temp_sign;
              if (k) {
                if (!vol_mag_filter_linear || !vol_min_filter_linear) {
                  // Check if array layer filtering is enabled and need one more
                  // sample.
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
                      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                          vol_filter_temp_linear_test) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
                  shader_code_.push_back(EncodeVectorSelectOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
                  shader_code_.push_back(vol_filter_temp);
                  ++stat_.instruction_count;
                  ++stat_.dynamic_flow_control_count;
                }

                // Go to the next array texture sample.
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
                shader_code_.push_back(coord_temp);
                shader_code_.push_back(EncodeVectorSelectOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
                shader_code_.push_back(coord_temp);
                shader_code_.push_back(
                    EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                shader_code_.push_back(0x3F800000);
                ++stat_.instruction_count;
                ++stat_.float_instruction_count;
              }
              if (instr.attributes.use_register_lod) {
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_L) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
                shader_code_.push_back(target_temp_current);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(coord_temp);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
                shader_code_.push_back(1 + srv_index_current);
                shader_code_.push_back(
                    uint32_t(SRVMainRegister::kBoundTexturesStart) +
                    srv_index_current);
                shader_code_.push_back(EncodeZeroComponentOperand(
                    D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
                shader_code_.push_back(sampler_register);
                shader_code_.push_back(sampler_register);
                shader_code_.push_back(EncodeVectorSelectOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, lod_temp_component, 1));
                shader_code_.push_back(lod_temp);
                ++stat_.instruction_count;
                ++stat_.texture_normal_instructions;
              } else if (instr.attributes.use_register_gradients) {
                // TODO(Triang3l): Apply the LOD bias somehow for register
                // gradients (possibly will require moving the bias to the
                // sampler, which may be not very good considering the sampler
                // count is very limited).
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_D) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(15));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
                shader_code_.push_back(target_temp_current);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(coord_temp);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
                shader_code_.push_back(1 + srv_index_current);
                shader_code_.push_back(
                    uint32_t(SRVMainRegister::kBoundTexturesStart) +
                    srv_index_current);
                shader_code_.push_back(EncodeZeroComponentOperand(
                    D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
                shader_code_.push_back(sampler_register);
                shader_code_.push_back(sampler_register);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(system_temp_grad_h_lod_);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(system_temp_grad_v_);
                ++stat_.instruction_count;
                ++stat_.texture_gradient_instructions;
              } else {
                // 3 different DXBC opcodes handled here:
                // - sample_l, when not using a computed LOD or not in a pixel
                //   shader, in this case, LOD (0 + bias) is sampled.
                // - sample, when sampling in a pixel shader (thus with
                //   derivatives) with a computed LOD.
                // - sample_b, when sampling in a pixel shader with a biased
                //   computed LOD.
                // Both sample_l and sample_b should add the LOD bias as the
                // last operand in our case.
                bool explicit_lod =
                    !instr.attributes.use_computed_lod || !IsDxbcPixelShader();
                if (explicit_lod) {
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_L) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
                } else if (instr.attributes.lod_bias != 0.0f) {
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE_B) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(13));
                } else {
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SAMPLE) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
                }
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
                shader_code_.push_back(target_temp_current);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(coord_temp);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 2));
                shader_code_.push_back(1 + srv_index_current);
                shader_code_.push_back(
                    uint32_t(SRVMainRegister::kBoundTexturesStart) +
                    srv_index_current);
                shader_code_.push_back(EncodeZeroComponentOperand(
                    D3D10_SB_OPERAND_TYPE_SAMPLER, 2));
                shader_code_.push_back(sampler_register);
                shader_code_.push_back(sampler_register);
                if (explicit_lod || instr.attributes.lod_bias != 0.0f) {
                  shader_code_.push_back(EncodeScalarOperand(
                      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                  shader_code_.push_back(*reinterpret_cast<const uint32_t*>(
                      &instr.attributes.lod_bias));
                }
                ++stat_.instruction_count;
                if (!explicit_lod && instr.attributes.lod_bias != 0.0f) {
                  ++stat_.texture_bias_instructions;
                } else {
                  ++stat_.texture_normal_instructions;
                }
              }
              if (k) {
                // b - a
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
                shader_code_.push_back(target_temp_current);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(target_temp_current);
                shader_code_.push_back(
                    EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP,
                                                kSwizzleXYZW, 1) |
                    ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
                shader_code_.push_back(
                    ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
                        D3D10_SB_OPERAND_MODIFIER_NEG));
                shader_code_.push_back(target_temp_sign);
                ++stat_.instruction_count;
                ++stat_.float_instruction_count;

                // a + (b - a) * factor
                shader_code_.push_back(
                    ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
                shader_code_.push_back(EncodeVectorMaskedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
                shader_code_.push_back(target_temp_sign);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(target_temp_current);
                shader_code_.push_back(EncodeVectorReplicatedOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
                shader_code_.push_back(vol_filter_temp);
                shader_code_.push_back(EncodeVectorSwizzledOperand(
                    D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
                shader_code_.push_back(target_temp_sign);
                ++stat_.instruction_count;
                ++stat_.float_instruction_count;

                if (!j) {
                  // Go back to the first layer to sample the signed texture.
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
                  shader_code_.push_back(EncodeVectorMaskedOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
                  shader_code_.push_back(coord_temp);
                  shader_code_.push_back(EncodeVectorSelectOperand(
                      D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
                  shader_code_.push_back(coord_temp);
                  shader_code_.push_back(EncodeScalarOperand(
                      D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
                  shader_code_.push_back(0xBF800000u);
                  ++stat_.instruction_count;
                  ++stat_.float_instruction_count;
                }

                if (!vol_mag_filter_linear || !vol_min_filter_linear) {
                  // Close the array layer filtering check.
                  shader_code_.push_back(
                      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
                  ++stat_.instruction_count;
                }
              }
            }
          }
        }
      }
      if (instr.dimension == TextureDimension::k3D) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
        ++stat_.instruction_count;
      }

      if (instr.opcode == FetchOpcode::kTextureFetch) {
        // Will take sign values and exponent bias from the fetch constant.
        if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
          cbuffer_index_fetch_constants_ = cbuffer_count_++;
        }

        assert_true(signed_value_temp != UINT32_MAX);
        uint32_t sign_temp = PushSystemTemp();

        // Choose between channels from the unsigned SRV and the signed one,
        // apply sign bias (2 * color - 1) and linearize gamma textures.
        // This is done before applying the exponent bias because biasing and
        // linearization must be done on color values in 0...1 range, and this
        // is closer to the storage format, while exponent bias is closer to the
        // actual usage in shaders.
        // TODO(Triang3l): Signs should be pre-swizzled.
        for (uint32_t i = 0; i < 4; ++i) {
          // Extract the sign values from dword 0 ([0].x or [1].z) of the fetch
          // constant (in bits 2:9, 2 bits per component) to sign_temp.x.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(2);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(2 + i * 2);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                        (tfetch_index & 1) * 2, 3));
          shader_code_.push_back(cbuffer_index_fetch_constants_);
          shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
          shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1));
          ++stat_.instruction_count;
          ++stat_.uint_instruction_count;

          // Get if the channel is signed to sign_temp.y.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(uint32_t(TextureSign::kSigned));
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;

          // Choose between the unsigned and the signed channel.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
          shader_code_.push_back(system_temp_result_);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(signed_value_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(system_temp_result_);
          ++stat_.instruction_count;
          ++stat_.movc_instruction_count;

          // Get if the channel is biased to sign_temp.y.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(uint32_t(TextureSign::kUnsignedBiased));
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;

          // Expand 0...1 to -1...1 (for normal and DuDv maps, for instance) to
          // sign_temp.z.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(system_temp_result_);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(0x40000000u);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(0xBF800000u);
          ++stat_.instruction_count;
          ++stat_.float_instruction_count;

          // Choose between the unbiased and the biased channel.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1 << i, 1));
          shader_code_.push_back(system_temp_result_);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, i, 1));
          shader_code_.push_back(system_temp_result_);
          ++stat_.instruction_count;
          ++stat_.movc_instruction_count;

          // Get if the channel is in PWL gamma to sign_temp.x.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
          shader_code_.push_back(
              EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
          shader_code_.push_back(sign_temp);
          shader_code_.push_back(
              EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
          shader_code_.push_back(uint32_t(TextureSign::kGamma));
          ++stat_.instruction_count;
          ++stat_.int_instruction_count;

          // Check if need to degamma.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IF) |
              ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                  D3D10_SB_INSTRUCTION_TEST_NONZERO) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_code_.push_back(
              EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
          shader_code_.push_back(sign_temp);
          ++stat_.instruction_count;
          ++stat_.dynamic_flow_control_count;

          // Degamma the channel.
          ConvertPWLGamma(false, system_temp_result_, i, system_temp_result_, i,
                          sign_temp, 0, sign_temp, 1);

          // Close the gamma conditional.
          shader_code_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDIF) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
          ++stat_.instruction_count;
        }

        // Release sign_temp.
        PopSystemTemp();

        // Apply exponent bias.
        uint32_t exp_adjust_temp = PushSystemTemp();
        // Get the bias value in bits 13:18 of dword 3, which is [0].w or [2].y.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(6);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(13);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                                      3 - 2 * (tfetch_index & 1), 3));
        shader_code_.push_back(cbuffer_index_fetch_constants_);
        shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
        shader_code_.push_back(tfetch_pair_offset + (tfetch_index & 1) * 2);
        ++stat_.instruction_count;
        ++stat_.uint_instruction_count;
        // Shift it into float exponent bits.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(23);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        // Add this to the exponent of 1.0.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(exp_adjust_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3F800000);
        ++stat_.instruction_count;
        ++stat_.int_instruction_count;
        // Multiply the value from the texture by 2.0^bias.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
        shader_code_.push_back(system_temp_result_);
        shader_code_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
        shader_code_.push_back(system_temp_result_);
        shader_code_.push_back(
            EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(exp_adjust_temp);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Release exp_adjust_temp.
        PopSystemTemp();
      }

      if (vol_filter_lerp_temp != UINT32_MAX) {
        PopSystemTemp();
      }
      if (signed_value_temp != UINT32_MAX) {
        PopSystemTemp();
      }
      if (lod_temp != system_temp_grad_h_lod_) {
        PopSystemTemp();
      }
    }

    if (vol_filter_temp != UINT32_MAX) {
      PopSystemTemp();
    }
    if (size_and_is_3d_temp != UINT32_MAX) {
      PopSystemTemp();
    }
    // Release coord_temp.
    PopSystemTemp();
  } else if (instr.opcode == FetchOpcode::kGetTextureGradients) {
    assert_true(IsDxbcPixelShader());
    store_result = true;
    // pv.xz = ddx(coord.xy)
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DERIV_RTX_COARSE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0101, 1));
    shader_code_.push_back(system_temp_result_);
    UseDxbcSourceOperand(operand, 0b01010000);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // pv.yw = ddy(coord.xy)
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DERIV_RTY_COARSE) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1010, 1));
    shader_code_.push_back(system_temp_result_);
    UseDxbcSourceOperand(operand, 0b01010000);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // Get the exponent bias (horizontal in bits 22:26, vertical in bits 27:31
    // of dword 4 ([1].x or [2].z) of the fetch constant).
    if (cbuffer_index_fetch_constants_ == kCbufferIndexUnallocated) {
      cbuffer_index_fetch_constants_ = cbuffer_count_++;
    }
    uint32_t exp_bias_temp = PushSystemTemp();
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_IBFE) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(17));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(5);
    shader_code_.push_back(5);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(22);
    shader_code_.push_back(27);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    shader_code_.push_back(EncodeVectorReplicatedOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, (tfetch_index & 1) * 2, 3));
    shader_code_.push_back(cbuffer_index_fetch_constants_);
    shader_code_.push_back(uint32_t(CbufferRegister::kFetchConstants));
    shader_code_.push_back(tfetch_pair_offset + 1 + (tfetch_index & 1));
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Shift the exponent bias into float exponent bits.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ISHL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(23);
    shader_code_.push_back(23);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Add the bias to the exponent of 1.0.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IADD) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(exp_bias_temp);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(0x3F800000);
    shader_code_.push_back(0);
    shader_code_.push_back(0);
    ++stat_.instruction_count;
    ++stat_.int_instruction_count;
    // Apply the exponent bias.
    shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                           ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1111, 1));
    shader_code_.push_back(system_temp_result_);
    shader_code_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
    shader_code_.push_back(system_temp_result_);
    shader_code_.push_back(
        EncodeVectorSwizzledOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b01000100, 1));
    shader_code_.push_back(exp_bias_temp);
    ++stat_.instruction_count;
    ++stat_.float_instruction_count;
    // Release exp_bias_temp.
    PopSystemTemp();
  } else if (instr.opcode == FetchOpcode::kSetTextureLod) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
    shader_code_.push_back(system_temp_grad_h_lod_);
    UseDxbcSourceOperand(operand, kSwizzleXYZW, 0);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  } else if (instr.opcode == FetchOpcode::kSetTextureGradientsHorz ||
             instr.opcode == FetchOpcode::kSetTextureGradientsVert) {
    shader_code_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_length));
    shader_code_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0111, 1));
    shader_code_.push_back(instr.opcode == FetchOpcode::kSetTextureGradientsVert
                               ? system_temp_grad_v_
                               : system_temp_grad_h_lod_);
    UseDxbcSourceOperand(operand);
    ++stat_.instruction_count;
    ++stat_.mov_instruction_count;
  }

  if (instr.operand_count >= 1) {
    UnloadDxbcSourceOperand(operand);
  }

  if (store_result) {
    StoreResult(instr.result,
                DxbcSrc::R(system_temp_result_,
                           replicate_result ? DxbcSrc::kXXXX : DxbcSrc::kXYZW));
  }
}

}  // namespace gpu
}  // namespace xe
