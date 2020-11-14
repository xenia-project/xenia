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
  uint32_t needed_words = xenos::GetVertexFormatNeededWords(
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
  if (cbuffer_index_fetch_constants_ == kBindingIndexUnallocated) {
    cbuffer_index_fetch_constants_ = cbuffer_count_++;
  }
  DxbcSrc fetch_constant_src(DxbcSrc::CB(
      cbuffer_index_fetch_constants_,
      uint32_t(CbufferRegister::kFetchConstants),
      instr.operands[1].storage_index >> 1,
      (instr.operands[1].storage_index & 1) ? 0b10101110 : 0b00000100));

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
  // Add the word offset from the instruction (signed), plus the offset of the
  // first needed word within the element.
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
  if (srv_index_shared_memory_ == kBindingIndexUnallocated) {
    srv_index_shared_memory_ = srv_count_++;
  }
  if (uav_index_shared_memory_ == kBindingIndexUnallocated) {
    uav_index_shared_memory_ = uav_count_++;
  }
  for (uint32_t i = 0; i < 2; ++i) {
    if (i) {
      DxbcOpElse();
    }
    DxbcSrc shared_memory_src(
        i ? DxbcSrc::U(uav_index_shared_memory_,
                       uint32_t(UAVRegister::kSharedMemory))
          : DxbcSrc::T(srv_index_shared_memory_,
                       uint32_t(SRVMainRegister::kSharedMemory)));
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
    DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k8in16)));
    DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k8in32)));
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
    DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k8in32)));
    DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::Endian128::k16in32)));
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
      used_result_components & ((1 << xenos::GetVertexFormatComponentCount(
                                     instr.attributes.data_format)) -
                                1);
  DxbcDest result_unpacked_dest(
      DxbcDest::R(system_temp_result_, used_format_components));
  // If needed_words is not zero (checked in the beginning), this must not be
  // zero too. For simplicity, it's assumed that something will be unpacked
  // here.
  assert_not_zero(used_format_components);
  uint32_t packed_widths[4] = {}, packed_offsets[4] = {};
  uint32_t packed_swizzle = DxbcSrc::kXXXX;
  switch (instr.attributes.data_format) {
    case xenos::VertexFormat::k_8_8_8_8:
      packed_widths[0] = packed_widths[1] = packed_widths[2] =
          packed_widths[3] = 8;
      packed_offsets[1] = 8;
      packed_offsets[2] = 16;
      packed_offsets[3] = 24;
      break;
    case xenos::VertexFormat::k_2_10_10_10:
      packed_widths[0] = packed_widths[1] = packed_widths[2] = 10;
      packed_widths[3] = 2;
      packed_offsets[1] = 10;
      packed_offsets[2] = 20;
      packed_offsets[3] = 30;
      break;
    case xenos::VertexFormat::k_10_11_11:
      packed_widths[0] = packed_widths[1] = 11;
      packed_widths[2] = 10;
      packed_offsets[1] = 11;
      packed_offsets[2] = 22;
      break;
    case xenos::VertexFormat::k_11_11_10:
      packed_widths[0] = 10;
      packed_widths[1] = packed_widths[2] = 11;
      packed_offsets[1] = 10;
      packed_offsets[2] = 21;
      break;
    case xenos::VertexFormat::k_16_16:
      packed_widths[0] = packed_widths[1] = 16;
      packed_offsets[1] = 16;
      break;
    case xenos::VertexFormat::k_16_16_16_16:
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
        switch (instr.attributes.signed_rf_mode) {
          case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne: {
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
            // Treat both -(2^(n-1)) and -(2^(n-1)-1) as -1.
            DxbcOpMax(result_unpacked_dest, result_src, DxbcSrc::LF(-1.0f));
          } break;
          case xenos::SignedRepeatingFractionMode::kNoZero: {
            float packed_zeros[4] = {};
            for (uint32_t i = 0; i < 4; ++i) {
              if (!(used_format_components & (1 << i))) {
                continue;
              }
              assert_not_zero(packed_widths[i]);
              packed_zeros[i] =
                  1.0f / float((uint32_t(1) << packed_widths[i]) - 1);
              packed_scales[i] = 2.0f * packed_zeros[i];
            }
            DxbcOpMAd(result_unpacked_dest, result_src,
                      DxbcSrc::LP(packed_scales), DxbcSrc::LP(packed_zeros));
          } break;
          default:
            assert_unhandled_case(instr.attributes.signed_rf_mode);
        }
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
      case xenos::VertexFormat::k_16_16_FLOAT:
      case xenos::VertexFormat::k_16_16_16_16_FLOAT:
        // FIXME(Triang3l): This converts from D3D10+ float16 with NaNs instead
        // of Xbox 360 float16 with extended range. However, haven't encountered
        // games relying on that yet.
        DxbcOpUBFE(result_unpacked_dest, DxbcSrc::LU(16),
                   DxbcSrc::LU(0, 16, 0, 16),
                   DxbcSrc::R(system_temp_result_, 0b01010000));
        DxbcOpF16ToF32(result_unpacked_dest, result_src);
        break;
      case xenos::VertexFormat::k_32:
      case xenos::VertexFormat::k_32_32:
      case xenos::VertexFormat::k_32_32_32_32:
        if (instr.attributes.is_signed) {
          DxbcOpIToF(result_unpacked_dest, result_src);
        } else {
          DxbcOpUToF(result_unpacked_dest, result_src);
        }
        if (!instr.attributes.is_integer) {
          if (instr.attributes.is_signed) {
            switch (instr.attributes.signed_rf_mode) {
              case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne:
                DxbcOpMul(result_unpacked_dest, result_src,
                          DxbcSrc::LF(1.0f / 2147483647.0f));
                // No need to clamp to -1 if signed - 1/(2^31-1) is rounded to
                // 1/(2^31) as float32.
                break;
              case xenos::SignedRepeatingFractionMode::kNoZero:
                DxbcOpMAd(result_unpacked_dest, result_src,
                          DxbcSrc::LF(1.0f / 2147483647.5f),
                          DxbcSrc::LF(0.5f / 2147483647.5f));
                break;
              default:
                assert_unhandled_case(instr.attributes.signed_rf_mode);
            }
          } else {
            DxbcOpMul(result_unpacked_dest, result_src,
                      DxbcSrc::LF(1.0f / 4294967295.0f));
          }
        }
        break;
      case xenos::VertexFormat::k_32_FLOAT:
      case xenos::VertexFormat::k_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_FLOAT:
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

uint32_t DxbcShaderTranslator::FindOrAddTextureBinding(
    uint32_t fetch_constant, xenos::FetchOpDimension dimension,
    bool is_signed) {
  // 1D and 2D textures (including stacked ones) are treated as 2D arrays for
  // binding and coordinate simplicity.
  if (dimension == xenos::FetchOpDimension::k1D) {
    dimension = xenos::FetchOpDimension::k2D;
  }
  uint32_t srv_index = UINT32_MAX;
  for (uint32_t i = 0; i < uint32_t(texture_bindings_.size()); ++i) {
    TextureBinding& texture_binding = texture_bindings_[i];
    if (texture_binding.fetch_constant == fetch_constant &&
        texture_binding.dimension == dimension &&
        texture_binding.is_signed == is_signed) {
      return i;
    }
  }
  if (texture_bindings_.size() >= kMaxTextureBindings) {
    assert_always();
    return kMaxTextureBindings - 1;
  }
  uint32_t texture_binding_index = uint32_t(texture_bindings_.size());
  TextureBinding new_texture_binding;
  if (!bindless_resources_used_) {
    new_texture_binding.bindful_srv_index = srv_count_++;
    texture_bindings_for_bindful_srv_indices_.insert(
        {new_texture_binding.bindful_srv_index, texture_binding_index});
  } else {
    new_texture_binding.bindful_srv_index = kBindingIndexUnallocated;
  }
  new_texture_binding.bindful_srv_rdef_name_offset = 0;
  // Consistently 0 if not bindless as it may be used for hashing.
  new_texture_binding.bindless_descriptor_index =
      bindless_resources_used_ ? GetBindlessResourceCount() : 0;
  new_texture_binding.fetch_constant = fetch_constant;
  new_texture_binding.dimension = dimension;
  new_texture_binding.is_signed = is_signed;
  const char* dimension_name;
  switch (dimension) {
    case xenos::FetchOpDimension::k3DOrStacked:
      dimension_name = "3d";
      break;
    case xenos::FetchOpDimension::kCube:
      dimension_name = "cube";
      break;
    default:
      dimension_name = "2d";
  }
  new_texture_binding.name = fmt::format("xe_texture{}_{}_{}", fetch_constant,
                                         dimension_name, is_signed ? 's' : 'u');
  texture_bindings_.emplace_back(std::move(new_texture_binding));
  return texture_binding_index;
}

uint32_t DxbcShaderTranslator::FindOrAddSamplerBinding(
    uint32_t fetch_constant, xenos::TextureFilter mag_filter,
    xenos::TextureFilter min_filter, xenos::TextureFilter mip_filter,
    xenos::AnisoFilter aniso_filter) {
  // In Direct3D 12, anisotropic filtering implies linear filtering.
  if (aniso_filter != xenos::AnisoFilter::kDisabled &&
      aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    mag_filter = xenos::TextureFilter::kLinear;
    min_filter = xenos::TextureFilter::kLinear;
    mip_filter = xenos::TextureFilter::kLinear;
    aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  }
  uint32_t sampler_index = UINT32_MAX;
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
  if (aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    if (aniso_filter == xenos::AnisoFilter::kDisabled) {
      name << "_a0";
    } else {
      name << "_a" << (1u << (uint32_t(aniso_filter) - 1));
    }
  }
  if (aniso_filter == xenos::AnisoFilter::kDisabled ||
      aniso_filter == xenos::AnisoFilter::kUseFetchConst) {
    static const char* kFilterSuffixes[] = {"p", "l", "b", "f"};
    name << "_" << kFilterSuffixes[uint32_t(mag_filter)]
         << kFilterSuffixes[uint32_t(min_filter)]
         << kFilterSuffixes[uint32_t(mip_filter)];
  }
  SamplerBinding new_sampler_binding;
  // Consistently 0 if not bindless as it may be used for hashing.
  new_sampler_binding.bindless_descriptor_index =
      bindless_resources_used_ ? GetBindlessResourceCount() : 0;
  new_sampler_binding.fetch_constant = fetch_constant;
  new_sampler_binding.mag_filter = mag_filter;
  new_sampler_binding.min_filter = min_filter;
  new_sampler_binding.mip_filter = mip_filter;
  new_sampler_binding.aniso_filter = aniso_filter;
  new_sampler_binding.name = name.str();
  uint32_t sampler_binding_index = uint32_t(sampler_bindings_.size());
  sampler_bindings_.emplace_back(std::move(new_sampler_binding));
  return sampler_binding_index;
}

void DxbcShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }
  UpdateInstructionPredicationAndEmitDisassembly(instr.is_predicated,
                                                 instr.predicate_condition);

  // Handle instructions for setting register LOD.
  switch (instr.opcode) {
    case FetchOpcode::kSetTextureLod: {
      bool lod_operand_temp_pushed = false;
      DxbcOpMov(DxbcDest::R(system_temp_grad_h_lod_, 0b1000),
                LoadOperand(instr.operands[0], 0b0001, lod_operand_temp_pushed)
                    .SelectFromSwizzled(0));
      if (lod_operand_temp_pushed) {
        PopSystemTemp();
      }
      return;
    }
    case FetchOpcode::kSetTextureGradientsHorz: {
      bool grad_operand_temp_pushed = false;
      DxbcOpMov(
          DxbcDest::R(system_temp_grad_h_lod_, 0b0111),
          LoadOperand(instr.operands[0], 0b0111, grad_operand_temp_pushed));
      if (grad_operand_temp_pushed) {
        PopSystemTemp();
      }
      return;
    }
    case FetchOpcode::kSetTextureGradientsVert: {
      bool grad_operand_temp_pushed = false;
      DxbcOpMov(
          DxbcDest::R(system_temp_grad_v_, 0b0111),
          LoadOperand(instr.operands[0], 0b0111, grad_operand_temp_pushed));
      if (grad_operand_temp_pushed) {
        PopSystemTemp();
      }
      return;
    }
    default:
      break;
  }

  // Handle instructions that store something.
  uint32_t used_result_components = instr.result.GetUsedResultComponents();
  uint32_t used_result_nonzero_components = instr.GetNonZeroResultComponents();
  // FIXME(Triang3l): Currently disregarding the LOD completely in getWeights
  // because the needed code would be very complicated, while getWeights is
  // mostly used for things like PCF of shadow maps, that don't have mips. The
  // LOD would be needed for the mip lerp factor in W of the return value and to
  // choose the LOD where interpolation would take place for XYZ. That would
  // require either implementing the LOD calculation algorithm using the ALU
  // (since the `lod` instruction is limited to pixel shaders and can't be used
  // when there's control flow divergence, unlike explicit gradients), or
  // sampling a texture filled with LOD numbers (easier and more consistent -
  // unclamped LOD doesn't make sense for getWeights anyway). The same applies
  // to offsets.
  if (instr.opcode == FetchOpcode::kGetTextureWeights) {
    used_result_nonzero_components &= ~uint32_t(0b1000);
  }
  if (!used_result_nonzero_components) {
    // Nothing to fetch, only constant 0/1 writes.
    StoreResult(instr.result, DxbcSrc::LF(0.0f));
    return;
  }

  if (instr.opcode == FetchOpcode::kGetTextureGradients) {
    // Handle before doing anything that actually needs the texture.
    bool grad_operand_temp_pushed = false;
    DxbcSrc grad_operand =
        LoadOperand(instr.operands[0], 0b0011, grad_operand_temp_pushed);
    if (used_result_components & 0b0101) {
      DxbcOpDerivRTXFine(
          DxbcDest::R(system_temp_result_, used_result_components & 0b0101),
          grad_operand.SwizzleSwizzled(0b010000));
    }
    if (used_result_components & 0b1010) {
      DxbcOpDerivRTYFine(
          DxbcDest::R(system_temp_result_, used_result_components & 0b1010),
          grad_operand.SwizzleSwizzled(0b01000000));
    }
    if (grad_operand_temp_pushed) {
      PopSystemTemp();
    }
    StoreResult(instr.result, DxbcSrc::R(system_temp_result_));
    return;
  }

  // Handle instructions that need the coordinates, the fetch constant, the LOD
  // and possibly the SRV - kTextureFetch, kGetTextureBorderColorFrac,
  // kGetTextureComputedLod, kGetTextureWeights.

  if (instr.opcode == FetchOpcode::kGetTextureBorderColorFrac) {
    // TODO(Triang3l): Bind a black texture with a white border to calculate the
    // border color fraction (in the X component of the result).
    assert_always();
    EmitTranslationError("getBCF is unimplemented", false);
    StoreResult(instr.result, DxbcSrc::LF(0.0f));
    return;
  }

  if (instr.opcode != FetchOpcode::kTextureFetch &&
      instr.opcode != FetchOpcode::kGetTextureBorderColorFrac &&
      instr.opcode != FetchOpcode::kGetTextureComputedLod &&
      instr.opcode != FetchOpcode::kGetTextureWeights) {
    assert_unhandled_case(instr.opcode);
    EmitTranslationError("Unknown texture fetch operation");
    StoreResult(instr.result, DxbcSrc::LF(0.0f));
    return;
  }

  uint32_t tfetch_index = instr.operands[1].storage_index;

  // Whether to use gradients (implicit or explicit) for LOD calculation.
  bool use_computed_lod =
      instr.attributes.use_computed_lod &&
      (IsDxbcPixelShader() || instr.attributes.use_register_gradients);
  if (instr.opcode == FetchOpcode::kGetTextureComputedLod &&
      (!use_computed_lod || instr.attributes.use_register_gradients)) {
    assert_always();
    EmitTranslationError(
        "getCompTexLOD used with explicit LOD or gradients - contradicts MSDN",
        false);
    StoreResult(instr.result, DxbcSrc::LF(0.0f));
    return;
  }

  // Get offsets applied to the coordinates before sampling.
  // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched, not
  // at LOD 0. However, since offsets have granularity of 0.5, not 1, on the
  // Xbox 360, they can't be passed directly as AOffImmI to the `sample`
  // instruction (plus-minus 0.5 offsets are very common in games). But
  // offsetting at mip levels is a rare usage case, mostly offsets are used for
  // things like shadow maps and blur, where there are no mips.
  float offsets[4] = {};
  // MSDN doesn't list offsets as getCompTexLOD parameters.
  if (instr.opcode != FetchOpcode::kGetTextureComputedLod) {
    // Add a small epsilon to the offset (1/4 the fixed-point texture coordinate
    // ULP - shouldn't significantly effect the fixed-point conversion) to
    // resolve ambiguity when fetching point-sampled textures between texels.
    // This applies to both normalized (Banjo-Kazooie Xbox Live Arcade logo,
    // coordinates interpolated between vertices with half-pixel offset) and
    // unnormalized (Halo 3 lighting G-buffer reading, ps_param_gen pixels)
    // coordinates. On Nvidia Pascal, without this adjustment, blockiness is
    // visible in both cases. Possibly there is a better way, however, an
    // attempt was made to error-correct division by adding the difference
    // between original and re-denormalized coordinates, but on Nvidia, `mul`
    // and internal multiplication in texture sampling apparently round
    // differently, so `mul` gives a value that would be floored as expected,
    // but the left/upper pixel is still sampled instead.
    const float rounding_offset = 1.0f / 1024.0f;
    switch (instr.dimension) {
      case xenos::FetchOpDimension::k1D:
        offsets[0] = instr.attributes.offset_x + rounding_offset;
        if (instr.opcode == FetchOpcode::kGetTextureWeights) {
          // For coordinate lerp factors. This needs to be done separately for
          // point mag/min filters, but they're currently not handled here
          // anyway.
          offsets[0] -= 0.5f;
        }
        break;
      case xenos::FetchOpDimension::k2D:
        offsets[0] = instr.attributes.offset_x + rounding_offset;
        offsets[1] = instr.attributes.offset_y + rounding_offset;
        if (instr.opcode == FetchOpcode::kGetTextureWeights) {
          offsets[0] -= 0.5f;
          offsets[1] -= 0.5f;
        }
        break;
      case xenos::FetchOpDimension::k3DOrStacked:
        offsets[0] = instr.attributes.offset_x + rounding_offset;
        offsets[1] = instr.attributes.offset_y + rounding_offset;
        offsets[2] = instr.attributes.offset_z + rounding_offset;
        if (instr.opcode == FetchOpcode::kGetTextureWeights) {
          offsets[0] -= 0.5f;
          offsets[1] -= 0.5f;
          offsets[2] -= 0.5f;
        }
        break;
      case xenos::FetchOpDimension::kCube:
        // Applying the rounding epsilon to cube maps too for potential game
        // passes processing cube map faces themselves.
        offsets[0] = instr.attributes.offset_x + rounding_offset;
        offsets[1] = instr.attributes.offset_y + rounding_offset;
        if (instr.opcode == FetchOpcode::kGetTextureWeights) {
          offsets[0] -= 0.5f;
          offsets[1] -= 0.5f;
          // The logic for ST weights is the same for all faces.
          // FIXME(Triang3l): If LOD calculation is added to getWeights, face
          // offset probably will need to be handled too (if the hardware
          // supports it at all, though MSDN lists OffsetZ in tfetchCube).
        } else {
          offsets[2] = instr.attributes.offset_z;
        }
        break;
    }
  }
  uint32_t offsets_not_zero = 0b000;
  for (uint32_t i = 0; i < 3; ++i) {
    if (offsets[i]) {
      offsets_not_zero |= 1 << i;
    }
  }

  // Load the texture size if needed.
  // 1D: X - width.
  // 2D, cube: X - width, Y - height (cube maps probably can be only square, but
  //           for simplicity).
  // 3D: X - width, Y - height, Z - depth, W - 0 if stacked 2D, 1 if 3D.
  uint32_t size_needed_components = 0b0000;
  if (instr.opcode == FetchOpcode::kGetTextureWeights) {
    // Size needed for denormalization for coordinate lerp factor.
    // FIXME(Triang3l): Currently disregarding the LOD completely in getWeights.
    // However, if the LOD lerp factor and the LOD where filtering would happen
    // are ever calculated, all components of the size may be needed for ALU LOD
    // calculation with normalized coordinates (or, if a texture filled with LOD
    // indices is used, coordinates will need to be normalized as normally).
    if (!instr.attributes.unnormalized_coordinates) {
      switch (instr.dimension) {
        case xenos::FetchOpDimension::k1D:
          size_needed_components |= used_result_nonzero_components & 0b0001;
          break;
        case xenos::FetchOpDimension::k2D:
        case xenos::FetchOpDimension::kCube:
          size_needed_components |= used_result_nonzero_components & 0b0011;
          break;
        case xenos::FetchOpDimension::k3DOrStacked:
          size_needed_components |= used_result_nonzero_components & 0b0111;
          break;
      }
    }
  } else {
    // Size needed for normalization (or, for stacked texture layers,
    // denormalization) and for offsets.
    size_needed_components |= offsets_not_zero;
    switch (instr.dimension) {
      case xenos::FetchOpDimension::k1D:
        if (instr.attributes.unnormalized_coordinates) {
          size_needed_components |= 0b0001;
        }
        break;
      case xenos::FetchOpDimension::k2D:
        if (instr.attributes.unnormalized_coordinates) {
          size_needed_components |= 0b0011;
        }
        break;
      case xenos::FetchOpDimension::k3DOrStacked:
        // Stacked and 3D textures are fetched from different SRVs - the check
        // is always needed.
        size_needed_components |= 0b1000;
        if (instr.attributes.unnormalized_coordinates) {
          // Need to normalize all (if 3D).
          size_needed_components |= 0b0111;
        } else {
          // Need to denormalize Z (if stacked).
          size_needed_components |= 0b0100;
        }
        break;
      case xenos::FetchOpDimension::kCube:
        if (instr.attributes.unnormalized_coordinates) {
          size_needed_components |= 0b0011;
        }
        // The size is not needed for face ID offset.
        size_needed_components &= 0b0011;
        break;
    }
  }
  if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked &&
      size_needed_components) {
    // Stacked and 3D textures have different size packing - need to get whether
    // the texture is 3D unconditionally.
    size_needed_components |= 0b1000;
  }
  uint32_t size_and_is_3d_temp =
      size_needed_components ? PushSystemTemp() : UINT32_MAX;
  if (size_needed_components) {
    switch (instr.dimension) {
      case xenos::FetchOpDimension::k1D:
        DxbcOpUBFE(DxbcDest::R(size_and_is_3d_temp, 0b0001), DxbcSrc::LU(24),
                   DxbcSrc::LU(0),
                   RequestTextureFetchConstantWord(tfetch_index, 2));
        break;
      case xenos::FetchOpDimension::k2D:
      case xenos::FetchOpDimension::kCube:
        DxbcOpUBFE(DxbcDest::R(size_and_is_3d_temp, size_needed_components),
                   DxbcSrc::LU(13, 13, 0, 0), DxbcSrc::LU(0, 13, 0, 0),
                   RequestTextureFetchConstantWord(tfetch_index, 2));
        break;
      case xenos::FetchOpDimension::k3DOrStacked:
        // tfetch3D is used for both stacked and 3D - first, check if 3D.
        DxbcOpUBFE(DxbcDest::R(size_and_is_3d_temp, 0b1000), DxbcSrc::LU(2),
                   DxbcSrc::LU(9),
                   RequestTextureFetchConstantWord(tfetch_index, 5));
        DxbcOpIEq(DxbcDest::R(size_and_is_3d_temp, 0b1000),
                  DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW),
                  DxbcSrc::LU(uint32_t(xenos::DataDimension::k3D)));
        if (size_needed_components & 0b0111) {
          // Even if depth isn't needed specifically for stacked or specifically
          // for 3D later, load both cases anyway to make sure the register is
          // always initialized.
          DxbcOpIf(true, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
          // Load the 3D texture size.
          DxbcOpUBFE(
              DxbcDest::R(size_and_is_3d_temp, size_needed_components & 0b0111),
              DxbcSrc::LU(11, 11, 10, 0), DxbcSrc::LU(0, 11, 22, 0),
              RequestTextureFetchConstantWord(tfetch_index, 2));
          DxbcOpElse();
          // Load the 2D stacked texture size.
          DxbcOpUBFE(
              DxbcDest::R(size_and_is_3d_temp, size_needed_components & 0b0111),
              DxbcSrc::LU(13, 13, 6, 0), DxbcSrc::LU(0, 13, 26, 0),
              RequestTextureFetchConstantWord(tfetch_index, 2));
          DxbcOpEndIf();
        }
        break;
    }
    if (size_needed_components & 0b0111) {
      // Fetch constants store size minus 1 - add 1.
      DxbcOpIAdd(
          DxbcDest::R(size_and_is_3d_temp, size_needed_components & 0b0111),
          DxbcSrc::R(size_and_is_3d_temp), DxbcSrc::LU(1));
      // Convert the size to float for multiplication/division.
      DxbcOpUToF(
          DxbcDest::R(size_and_is_3d_temp, size_needed_components & 0b0111),
          DxbcSrc::R(size_and_is_3d_temp));
    }
  }

  if (instr.opcode == FetchOpcode::kGetTextureWeights) {
    // FIXME(Triang3l): Mip lerp factor needs to be calculated, and the
    // coordinate lerp factors should be calculated at the mip level texels
    // would be sampled from. That would require some way of calculating the LOD
    // that would be applicable to explicit gradients and vertex shaders. Also,
    // with point sampling, possibly lerp factors need to be 0. W  (mip lerp
    // factor) should have been masked out previously because it's not supported
    // currently.
    assert_zero(used_result_nonzero_components & 0b1000);

    // FIXME(Triang3l): Filtering modes should possibly be taken into account,
    // but for simplicity, not doing that - from a high level point of view,
    // would be useless to get weights that will always be zero.

    // Need unnormalized coordinates.
    bool coord_operand_temp_pushed = false;
    DxbcSrc coord_operand =
        LoadOperand(instr.operands[0], used_result_nonzero_components,
                    coord_operand_temp_pushed);
    DxbcSrc coord_src(coord_operand);
    uint32_t offsets_needed = offsets_not_zero & used_result_nonzero_components;
    if (!instr.attributes.unnormalized_coordinates || offsets_needed) {
      // Using system_temp_result_ as a temporary for coordinate denormalization
      // and offsetting.
      coord_src = DxbcSrc::R(system_temp_result_);
      DxbcDest coord_dest(
          DxbcDest::R(system_temp_result_, used_result_nonzero_components));
      if (instr.attributes.unnormalized_coordinates) {
        if (offsets_needed) {
          DxbcOpAdd(coord_dest, coord_operand, DxbcSrc::LP(offsets));
        }
      } else {
        assert_true((size_needed_components & used_result_nonzero_components) ==
                    used_result_nonzero_components);
        if (offsets_needed) {
          DxbcOpMAd(coord_dest, coord_operand, DxbcSrc::R(size_and_is_3d_temp),
                    DxbcSrc::LP(offsets));
        } else {
          DxbcOpMul(coord_dest, coord_operand, DxbcSrc::R(size_and_is_3d_temp));
        }
      }
    }
    // 0.5 has already been subtracted via offsets previously.
    DxbcOpFrc(DxbcDest::R(system_temp_result_, used_result_nonzero_components),
              coord_src);
    if (coord_operand_temp_pushed) {
      PopSystemTemp();
    }
  } else {
    // - Component signedness, for selecting the SRV, and if data is needed.

    DxbcSrc signs_uint_src(
        DxbcSrc::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_TextureSwizzledSigns_Vec + (tfetch_index >> 4))
            .Select((tfetch_index >> 2) & 3));
    uint32_t signs_shift = (tfetch_index & 3) * 8;
    uint32_t signs_temp = UINT32_MAX;
    if (instr.opcode == FetchOpcode::kTextureFetch) {
      signs_temp = PushSystemTemp();
      system_constants_used_ |= 1ull << kSysConst_TextureSwizzledSigns_Index;
      DxbcOpUBFE(DxbcDest::R(signs_temp, used_result_nonzero_components),
                 DxbcSrc::LU(2),
                 DxbcSrc::LU(signs_shift, signs_shift + 2, signs_shift + 4,
                             signs_shift + 6),
                 signs_uint_src);
    }

    // - Coordinates.

    // Will need a temporary in all cases:
    // - 1D, 2D array - need to be padded to 2D array coordinates.
    // - 3D - Z needs to be unnormalized for stacked and normalized for 3D.
    // - Cube - coordinates need to be transformed into the cube space.
    // Bindless sampler index will be loaded to W after loading the coordinates
    // (so W can be used as a temporary for coordinate loading).
    uint32_t coord_and_sampler_temp = PushSystemTemp();

    // Need normalized coordinates (except for Z - keep it as is, will be
    // converted later according to whether the texture is 3D). For cube maps,
    // coordinates need to be transformed back into the cube space.
    bool coord_operand_temp_pushed = false;
    DxbcSrc coord_operand = LoadOperand(
        instr.operands[0],
        (1 << xenos::GetFetchOpDimensionComponentCount(instr.dimension)) - 1,
        coord_operand_temp_pushed);
    uint32_t normalized_components = 0b0000;
    switch (instr.dimension) {
      case xenos::FetchOpDimension::k1D:
        normalized_components = 0b0001;
        break;
      case xenos::FetchOpDimension::k2D:
      case xenos::FetchOpDimension::kCube:
        normalized_components = 0b0011;
        break;
      case xenos::FetchOpDimension::k3DOrStacked:
        normalized_components = 0b0111;
        break;
    }
    if (instr.attributes.unnormalized_coordinates) {
      // Unnormalized coordinates - normalize XY, and if 3D, normalize Z.
      assert_not_zero(normalized_components);
      assert_true((size_needed_components & normalized_components) ==
                  normalized_components);
      if (offsets_not_zero & normalized_components) {
        // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched.
        DxbcOpAdd(DxbcDest::R(coord_and_sampler_temp, normalized_components),
                  coord_operand, DxbcSrc::LP(offsets));
        assert_not_zero(normalized_components & 0b011);
        DxbcOpDiv(
            DxbcDest::R(coord_and_sampler_temp, normalized_components & 0b011),
            DxbcSrc::R(coord_and_sampler_temp),
            DxbcSrc::R(size_and_is_3d_temp));
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Normalize if 3D.
          assert_true((size_needed_components & 0b1100) == 0b1100);
          DxbcOpIf(true, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
          DxbcOpDiv(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                    DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ),
                    DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kZZZZ));
          DxbcOpEndIf();
        }
      } else {
        DxbcOpDiv(DxbcDest::R(coord_and_sampler_temp, normalized_components),
                  coord_operand, DxbcSrc::R(size_and_is_3d_temp));
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Don't normalize if stacked.
          assert_true((size_needed_components & 0b1000) == 0b1000);
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ),
                     coord_operand.SelectFromSwizzled(2));
        }
      }
    } else {
      // Normalized coordinates - apply offsets to XY or copy them to
      // coord_and_sampler_temp, and if stacked, denormalize Z.
      uint32_t coords_with_offset = offsets_not_zero & normalized_components;
      if (coords_with_offset) {
        // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched.
        assert_true((size_needed_components & coords_with_offset) ==
                    coords_with_offset);
        DxbcOpDiv(DxbcDest::R(coord_and_sampler_temp, coords_with_offset),
                  DxbcSrc::LP(offsets), DxbcSrc::R(size_and_is_3d_temp));
        DxbcOpAdd(DxbcDest::R(coord_and_sampler_temp, coords_with_offset),
                  coord_operand, DxbcSrc::R(coord_and_sampler_temp));
      }
      uint32_t coords_without_offset =
          ~coords_with_offset & normalized_components;
      // 3D/stacked without offset is handled separately.
      if (coords_without_offset & 0b011) {
        DxbcOpMov(
            DxbcDest::R(coord_and_sampler_temp, coords_without_offset & 0b011),
            coord_operand);
      }
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        assert_true((size_needed_components & 0b1100) == 0b1100);
        if (coords_with_offset & 0b100) {
          // Denormalize and offset Z (re-apply the offset not to lose precision
          // as a result of division) if stacked.
          DxbcOpIf(false, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
          DxbcOpMAd(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                    coord_operand.SelectFromSwizzled(2),
                    DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kZZZZ),
                    DxbcSrc::LF(offsets[2]));
          DxbcOpEndIf();
        } else {
          // Denormalize Z if stacked, and revert to normalized if 3D.
          DxbcOpMul(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                    coord_operand.SelectFromSwizzled(2),
                    DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kZZZZ));
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW),
                     coord_operand.SelectFromSwizzled(2),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
        }
      }
    }
    switch (instr.dimension) {
      case xenos::FetchOpDimension::k1D:
        // Pad to 2D array coordinates.
        DxbcOpMov(DxbcDest::R(coord_and_sampler_temp, 0b0110),
                  DxbcSrc::LF(0.0f));
        break;
      case xenos::FetchOpDimension::k2D:
        // Pad to 2D array coordinates.
        DxbcOpMov(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                  DxbcSrc::LF(0.0f));
        break;
      case xenos::FetchOpDimension::kCube: {
        // Transform from the major axis SC/TC plus 1 into cube coordinates.
        // Move SC/TC from 1...2 to -1...1.
        DxbcOpMAd(DxbcDest::R(coord_and_sampler_temp, 0b0011),
                  DxbcSrc::R(coord_and_sampler_temp), DxbcSrc::LF(2.0f),
                  DxbcSrc::LF(-3.0f));
        // Get the face index (floored, within 0...5) as an integer to
        // coord_and_sampler_temp.z.
        if (offsets[2]) {
          DxbcOpAdd(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                    coord_operand.SelectFromSwizzled(2),
                    DxbcSrc::LF(offsets[2]));
          DxbcOpFToU(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
        } else {
          DxbcOpFToU(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     coord_operand.SelectFromSwizzled(2));
        }
        DxbcOpUMin(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                   DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ),
                   DxbcSrc::LU(5));
        // Split the face index into axis and sign (0 - positive, 1 - negative)
        // to coord_and_sampler_temp.zw (sign in W so it won't be overwritten).
        // Fine to overwrite W at this point, the sampler index hasn't been
        // loaded yet.
        DxbcOpUBFE(DxbcDest::R(coord_and_sampler_temp, 0b1100),
                   DxbcSrc::LU(0, 0, 2, 1), DxbcSrc::LU(0, 0, 1, 0),
                   DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
        // Remap the axes in a way opposite to the ALU cube instruction.
        DxbcOpSwitch(DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
        DxbcOpCase(DxbcSrc::LU(0));
        {
          // X is the major axis.
          // Y = -TC (TC overwritten).
          DxbcOpMov(DxbcDest::R(coord_and_sampler_temp, 0b0010),
                    -DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kYYYY));
          // Z = neg ? SC : -SC.
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kWWWW),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kXXXX),
                     -DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kXXXX));
          // X = neg ? -1 : 1 (SC overwritten).
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0001),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kWWWW),
                     DxbcSrc::LF(-1.0f), DxbcSrc::LF(1.0f));
        }
        DxbcOpBreak();
        DxbcOpCase(DxbcSrc::LU(1));
        {
          // Y is the major axis.
          // X = SC (already there).
          // Z = neg ? -TC : TC.
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kWWWW),
                     -DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kYYYY),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kYYYY));
          // Y = neg ? -1 : 1 (TC overwritten).
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0010),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kWWWW),
                     DxbcSrc::LF(-1.0f), DxbcSrc::LF(1.0f));
        }
        DxbcOpBreak();
        DxbcOpDefault();
        {
          // Z is the major axis.
          // X = neg ? -SC : SC (SC overwritten).
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0001),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kWWWW),
                     -DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kXXXX),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kXXXX));
          // Y = -TC (TC overwritten).
          DxbcOpMov(DxbcDest::R(coord_and_sampler_temp, 0b0010),
                    -DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kYYYY));
          // Z = neg ? -1 : 1.
          DxbcOpMovC(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                     DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kWWWW),
                     DxbcSrc::LF(-1.0f), DxbcSrc::LF(1.0f));
        }
        DxbcOpBreak();
        DxbcOpEndSwitch();
      } break;
      default:
        break;
    }
    if (coord_operand_temp_pushed) {
      PopSystemTemp();
    }

    if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
      // Because the `lod` instruction is not defined for point sampling, and
      // since the return value can be used with bias later, forcing linear mip
      // filtering (the XNA assembler also doesn't accept MipFilter overrides
      // for getCompTexLOD).
      uint32_t sampler_binding_index = FindOrAddSamplerBinding(
          tfetch_index, instr.attributes.mag_filter,
          instr.attributes.min_filter, xenos::TextureFilter::kLinear,
          instr.attributes.aniso_filter);
      DxbcSrc sampler(DxbcSrc::S(sampler_binding_index, sampler_binding_index));
      if (bindless_resources_used_) {
        // Load the sampler index to coord_and_sampler_temp.w and use relative
        // sampler indexing.
        if (cbuffer_index_descriptor_indices_ == kBindingIndexUnallocated) {
          cbuffer_index_descriptor_indices_ = cbuffer_count_++;
        }
        uint32_t sampler_bindless_descriptor_index =
            sampler_bindings_[sampler_binding_index].bindless_descriptor_index;
        DxbcOpMov(DxbcDest::R(coord_and_sampler_temp, 0b1000),
                  DxbcSrc::CB(cbuffer_index_descriptor_indices_,
                              uint32_t(CbufferRegister::kDescriptorIndices),
                              sampler_bindless_descriptor_index >> 2)
                      .Select(sampler_bindless_descriptor_index & 3));
        sampler = DxbcSrc::S(0, DxbcIndex(coord_and_sampler_temp, 3));
      }
      // Check which SRV needs to be accessed - signed or unsigned. If there is
      // at least one non-signed component, will be using the unsigned one.
      uint32_t is_unsigned_temp = PushSystemTemp();
      system_constants_used_ |= 1ull << kSysConst_TextureSwizzledSigns_Index;
      DxbcOpUBFE(DxbcDest::R(is_unsigned_temp, 0b0001), DxbcSrc::LU(8),
                 DxbcSrc::LU(signs_shift), signs_uint_src);
      DxbcOpINE(
          DxbcDest::R(is_unsigned_temp, 0b0001),
          DxbcSrc::R(is_unsigned_temp, DxbcSrc::kXXXX),
          DxbcSrc::LU(uint32_t(xenos::TextureSign::kSigned) * 0b01010101));
      if (bindless_resources_used_) {
        // Bindless path - select the SRV index between unsigned and signed to
        // query.
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Check if 3D.
          assert_true((size_needed_components & 0b1000) == 0b1000);
          DxbcOpIf(true, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
        }
        for (uint32_t is_stacked = 0;
             is_stacked <
             (instr.dimension == xenos::FetchOpDimension::k3DOrStacked ? 2u
                                                                       : 1u);
             ++is_stacked) {
          xenos::FetchOpDimension srv_dimension = instr.dimension;
          if (is_stacked) {
            srv_dimension = xenos::FetchOpDimension::k2D;
            DxbcOpElse();
          }
          uint32_t texture_binding_index_unsigned =
              FindOrAddTextureBinding(tfetch_index, srv_dimension, false);
          uint32_t texture_binding_index_signed =
              FindOrAddTextureBinding(tfetch_index, srv_dimension, true);
          uint32_t texture_bindless_descriptor_index_unsigned =
              texture_bindings_[texture_binding_index_unsigned]
                  .bindless_descriptor_index;
          uint32_t texture_bindless_descriptor_index_signed =
              texture_bindings_[texture_binding_index_signed]
                  .bindless_descriptor_index;
          if (cbuffer_index_descriptor_indices_ == kBindingIndexUnallocated) {
            cbuffer_index_descriptor_indices_ = cbuffer_count_++;
          }
          DxbcOpMovC(
              DxbcDest::R(is_unsigned_temp, 0b0001),
              DxbcSrc::R(is_unsigned_temp, DxbcSrc::kXXXX),
              DxbcSrc::CB(cbuffer_index_descriptor_indices_,
                          uint32_t(CbufferRegister::kDescriptorIndices),
                          texture_bindless_descriptor_index_unsigned >> 2)
                  .Select(texture_bindless_descriptor_index_unsigned & 3),
              DxbcSrc::CB(cbuffer_index_descriptor_indices_,
                          uint32_t(CbufferRegister::kDescriptorIndices),
                          texture_bindless_descriptor_index_signed >> 2)
                  .Select(texture_bindless_descriptor_index_signed & 3));
          // Always 3 coordinate components (1D and 2D are padded to 2D
          // arrays, 3D and cube have 3 coordinate dimensions). Not caring
          // about normalization of the array layer because it doesn't
          // participate in LOD calculation in Direct3D 12.
          // The `lod` instruction returns the unclamped LOD (probably need
          // unclamped so it can be biased back into the range later) in the Y
          // component, and the resource swizzle is the return value swizzle.
          // FIXME(Triang3l): Gradient exponent adjustment from the fetch
          // constant needs to be applied here, would require math involving
          // SV_Position parity, replacing coordinates for one pixel with 0
          // and for another with the adjusted gradient, but possibly not used
          // by any games.
          assert_true(used_result_nonzero_components == 0b0001);
          uint32_t* bindless_srv_index = nullptr;
          switch (srv_dimension) {
            case xenos::FetchOpDimension::k1D:
            case xenos::FetchOpDimension::k2D:
              bindless_srv_index = &srv_index_bindless_textures_2d_;
              break;
            case xenos::FetchOpDimension::k3DOrStacked:
              bindless_srv_index = &srv_index_bindless_textures_3d_;
              break;
            case xenos::FetchOpDimension::kCube:
              bindless_srv_index = &srv_index_bindless_textures_cube_;
              break;
          }
          assert_not_null(bindless_srv_index);
          if (*bindless_srv_index == kBindingIndexUnallocated) {
            *bindless_srv_index = srv_count_++;
          }
          DxbcOpLOD(DxbcDest::R(system_temp_result_, 0b0001),
                    DxbcSrc::R(coord_and_sampler_temp), 3,
                    DxbcSrc::T(*bindless_srv_index,
                               DxbcIndex(is_unsigned_temp, 0), DxbcSrc::kYYYY),
                    sampler);
        }
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Close the 3D/stacked check.
          DxbcOpEndIf();
        }
      } else {
        // Bindful path - conditionally query one of the SRVs.
        DxbcOpIf(true, DxbcSrc::R(is_unsigned_temp, DxbcSrc::kXXXX));
        for (uint32_t is_signed = 0; is_signed < 2; ++is_signed) {
          if (is_signed) {
            DxbcOpElse();
          }
          if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
            // Check if 3D.
            assert_true((size_needed_components & 0b1000) == 0b1000);
            DxbcOpIf(true, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
          }
          for (uint32_t is_stacked = 0;
               is_stacked <
               (instr.dimension == xenos::FetchOpDimension::k3DOrStacked ? 2u
                                                                         : 1u);
               ++is_stacked) {
            if (is_stacked) {
              DxbcOpElse();
            }
            assert_true(used_result_nonzero_components == 0b0001);
            uint32_t texture_binding_index = FindOrAddTextureBinding(
                tfetch_index,
                is_stacked ? xenos::FetchOpDimension::k2D : instr.dimension,
                is_signed != 0);
            DxbcOpLOD(
                DxbcDest::R(system_temp_result_, 0b0001),
                DxbcSrc::R(coord_and_sampler_temp), 3,
                DxbcSrc::T(
                    texture_bindings_[texture_binding_index].bindful_srv_index,
                    uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                        texture_binding_index,
                    DxbcSrc::kYYYY),
                sampler);
          }
          if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
            // Close the 3D/stacked check.
            DxbcOpEndIf();
          }
        }
        // Close the signedness check.
        DxbcOpEndIf();
      }
      // Release is_unsigned_temp.
      PopSystemTemp();
    } else {
      // - Gradients or LOD to be passed to the sample_d/sample_l.

      DxbcSrc lod_src(DxbcSrc::LF(0.0f));
      uint32_t grad_component_count = 0;
      // Will be allocated for both explicit and computed LOD.
      uint32_t grad_h_lod_temp = UINT32_MAX;
      // Will be allocated for computed LOD only, and if not using basemap mip
      // filter.
      uint32_t grad_v_temp = UINT32_MAX;
      if (instr.attributes.mip_filter != xenos::TextureFilter::kBaseMap) {
        grad_h_lod_temp = PushSystemTemp();
        lod_src = DxbcSrc::R(grad_h_lod_temp, DxbcSrc::kWWWW);
        // Accumulate the explicit LOD sources (in D3D11.3 specification order:
        // specified LOD + sampler LOD bias + instruction LOD bias).
        DxbcDest lod_dest(DxbcDest::R(grad_h_lod_temp, 0b1000));
        // Fetch constant LOD bias * 32.
        DxbcOpIBFE(lod_dest, DxbcSrc::LU(10), DxbcSrc::LU(12),
                   RequestTextureFetchConstantWord(tfetch_index, 4));
        DxbcOpIToF(lod_dest, lod_src);
        if (instr.attributes.use_register_lod) {
          // Divide the fetch constant LOD bias by 32, and add the register LOD
          // and the instruction LOD bias.
          DxbcOpMAd(lod_dest, lod_src, DxbcSrc::LF(1.0f / 32.0f),
                    DxbcSrc::R(system_temp_grad_h_lod_, DxbcSrc::kWWWW));
          if (instr.attributes.lod_bias) {
            DxbcOpAdd(lod_dest, lod_src,
                      DxbcSrc::LF(instr.attributes.lod_bias));
          }
        } else {
          // Divide the fetch constant LOD by 32, and add the instruction LOD
          // bias.
          if (instr.attributes.lod_bias) {
            DxbcOpMAd(lod_dest, lod_src, DxbcSrc::LF(1.0f / 32.0f),
                      DxbcSrc::LF(instr.attributes.lod_bias));
          } else {
            DxbcOpMul(lod_dest, lod_src, DxbcSrc::LF(1.0f / 32.0f));
          }
        }
        if (use_computed_lod) {
          grad_v_temp = PushSystemTemp();
          switch (instr.dimension) {
            case xenos::FetchOpDimension::k1D:
              grad_component_count = 1;
              break;
            case xenos::FetchOpDimension::k2D:
              grad_component_count = 2;
              break;
            case xenos::FetchOpDimension::k3DOrStacked:
            case xenos::FetchOpDimension::kCube:
              grad_component_count = 3;
              break;
          }
          assert_not_zero(grad_component_count);
          uint32_t grad_mask = (1 << grad_component_count) - 1;
          // Convert the bias to a gradient scale.
          DxbcOpExp(lod_dest, lod_src);
          // FIXME(Triang3l): Gradient exponent adjustment is currently not done
          // in getCompTexLOD, so don't do it here too.
#if 0
          // Extract gradient exponent biases from the fetch constant and merge
          // them with the LOD bias.
          DxbcOpIBFE(DxbcDest::R(grad_h_lod_temp, 0b0011), DxbcSrc::LU(5),
                     DxbcSrc::LU(22, 27, 0, 0),
                     RequestTextureFetchConstantWord(tfetch_index, 4));
          DxbcOpIMAd(DxbcDest::R(grad_h_lod_temp, 0b0011),
                     DxbcSrc::R(grad_h_lod_temp), DxbcSrc::LI(int32_t(1) << 23),
                     DxbcSrc::LF(1.0f));
          DxbcOpMul(DxbcDest::R(grad_v_temp, 0b1000), lod_src,
                    DxbcSrc::R(grad_h_lod_temp, DxbcSrc::kYYYY));
          DxbcOpMul(lod_dest, lod_src,
                    DxbcSrc::R(grad_h_lod_temp, DxbcSrc::kXXXX));
#endif
          // Obtain the gradients and apply biases to them.
          if (instr.attributes.use_register_gradients) {
            // Register gradients are already in the cube space for cube maps.
            DxbcOpMul(DxbcDest::R(grad_h_lod_temp, grad_mask),
                      DxbcSrc::R(system_temp_grad_h_lod_), lod_src);
            // FIXME(Triang3l): Gradient exponent adjustment is currently not
            // done in getCompTexLOD, so don't do it here too.
#if 0
            DxbcOpMul(DxbcDest::R(grad_v_temp, grad_mask),
                      DxbcSrc::R(system_temp_grad_v_),
                      DxbcSrc::R(grad_v_temp, DxbcSrc::kWWWW));
#else
            DxbcOpMul(DxbcDest::R(grad_v_temp, grad_mask),
                      DxbcSrc::R(system_temp_grad_v_), lod_src);
#endif
            // TODO(Triang3l): Are cube map register gradients unnormalized if
            // the coordinates themselves are unnormalized?
            if (instr.attributes.unnormalized_coordinates &&
                instr.dimension != xenos::FetchOpDimension::kCube) {
              uint32_t grad_norm_mask = grad_mask;
              if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
                grad_norm_mask &= 0b0011;
              }
              assert_true((size_needed_components & grad_norm_mask) ==
                          grad_norm_mask);
              DxbcOpDiv(DxbcDest::R(grad_h_lod_temp, grad_norm_mask),
                        DxbcSrc::R(grad_h_lod_temp),
                        DxbcSrc::R(size_and_is_3d_temp));
              DxbcOpDiv(DxbcDest::R(grad_v_temp, grad_norm_mask),
                        DxbcSrc::R(grad_v_temp),
                        DxbcSrc::R(size_and_is_3d_temp));
              // Normalize Z of the gradients for fetching from the 3D texture.
              assert_true((size_needed_components & 0b1100) == 0b1100);
              DxbcOpIf(true, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
              DxbcOpDiv(DxbcDest::R(grad_h_lod_temp, 0b0100),
                        DxbcSrc::R(grad_h_lod_temp, DxbcSrc::kZZZZ),
                        DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kZZZZ));
              DxbcOpDiv(DxbcDest::R(grad_v_temp, 0b0100),
                        DxbcSrc::R(grad_v_temp, DxbcSrc::kZZZZ),
                        DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kZZZZ));
              DxbcOpEndIf();
            }
          } else {
            // Coarse is according to the Direct3D 11.3 specification.
            DxbcOpDerivRTXCoarse(DxbcDest::R(grad_h_lod_temp, grad_mask),
                                 DxbcSrc::R(coord_and_sampler_temp));
            DxbcOpMul(DxbcDest::R(grad_h_lod_temp, grad_mask),
                      DxbcSrc::R(grad_h_lod_temp), lod_src);
            DxbcOpDerivRTYCoarse(DxbcDest::R(grad_v_temp, grad_mask),
                                 DxbcSrc::R(coord_and_sampler_temp));
            // FIXME(Triang3l): Gradient exponent adjustment is currently not
            // done in getCompTexLOD, so don't do it here too.
#if 0
            DxbcOpMul(DxbcDest::R(grad_v_temp, grad_mask),
                      DxbcSrc::R(grad_v_temp),
                      DxbcSrc::R(grad_v_temp, DxbcSrc::kWWWW));
#else
            DxbcOpMul(DxbcDest::R(grad_v_temp, grad_mask),
                      DxbcSrc::R(grad_v_temp), lod_src);
#endif
          }
          if (instr.dimension == xenos::FetchOpDimension::k1D) {
            // Pad the gradients to 2D because 1D textures are fetched as 2D
            // arrays.
            DxbcOpMov(DxbcDest::R(grad_h_lod_temp, 0b0010), DxbcSrc::LF(0.0f));
            DxbcOpMov(DxbcDest::R(grad_v_temp, 0b0010), DxbcSrc::LF(0.0f));
            grad_component_count = 2;
          }
        }
      }

      // - Data.

      // Viva Pinata uses vertex displacement map textures for tessellated
      // models like the beehive tree with explicit LOD with point sampling
      // (they store values packed in two components), however, the fetch
      // constant has anisotropic filtering enabled. However, Direct3D 12
      // doesn't allow mixing anisotropic and point filtering. Possibly
      // anistropic filtering should be disabled when explicit LOD is used - do
      // this here.
      uint32_t sampler_binding_index = FindOrAddSamplerBinding(
          tfetch_index, instr.attributes.mag_filter,
          instr.attributes.min_filter, instr.attributes.mip_filter,
          use_computed_lod ? instr.attributes.aniso_filter
                           : xenos::AnisoFilter::kDisabled);
      DxbcSrc sampler(DxbcSrc::S(sampler_binding_index, sampler_binding_index));
      if (bindless_resources_used_) {
        // Load the sampler index to coord_and_sampler_temp.w and use relative
        // sampler indexing.
        if (cbuffer_index_descriptor_indices_ == kBindingIndexUnallocated) {
          cbuffer_index_descriptor_indices_ = cbuffer_count_++;
        }
        uint32_t sampler_bindless_descriptor_index =
            sampler_bindings_[sampler_binding_index].bindless_descriptor_index;
        DxbcOpMov(DxbcDest::R(coord_and_sampler_temp, 0b1000),
                  DxbcSrc::CB(cbuffer_index_descriptor_indices_,
                              uint32_t(CbufferRegister::kDescriptorIndices),
                              sampler_bindless_descriptor_index >> 2)
                      .Select(sampler_bindless_descriptor_index & 3));
        sampler = DxbcSrc::S(0, DxbcIndex(coord_and_sampler_temp, 3));
      }

      // Break result register dependencies because textures will be sampled
      // conditionally, including the primary signs.
      DxbcOpMov(
          DxbcDest::R(system_temp_result_, used_result_nonzero_components),
          DxbcSrc::LF(0.0f));

      // Extract whether each component is signed.
      uint32_t is_signed_temp = PushSystemTemp();
      DxbcOpIEq(DxbcDest::R(is_signed_temp, used_result_nonzero_components),
                DxbcSrc::R(signs_temp),
                DxbcSrc::LU(uint32_t(xenos::TextureSign::kSigned)));

      // Calculate the lerp factor between stacked texture layers if needed (or
      // 0 if point-sampled), and check which signedness SRVs need to be
      // sampled.
      // As a result, if srv_selection_temp is allocated at all:
      // - srv_selection_temp.x - if multiple components, whether all components
      //   are signed, wrapped by is_all_signed_src with a fallback for the
      //   single component case. If false, the unsigned SRV needs to be
      //   sampled.
      // - srv_selection_temp.y - if multiple components, whether any component
      //   is signed, wrapped by is_any_signed_src with a fallback for the
      //   single component case. If true, the signed SRV needs to be sampled.
      // - srv_selection_temp.z - if stacked and not forced to be point-sampled,
      //   the lerp factor between two layers, wrapped by layer_lerp_factor_src
      //   with l(0.0) fallback for the point sampling case.
      // - srv_selection_temp.w - first, scratch for calculations involving
      //   these, then, unsigned or signed SRV description index.
      DxbcSrc layer_lerp_factor_src(DxbcSrc::LF(0.0f));
      // W is always needed for bindless.
      uint32_t srv_selection_temp =
          bindless_resources_used_ ? PushSystemTemp() : UINT32_MAX;
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        bool vol_mag_filter_is_fetch_const =
            instr.attributes.vol_mag_filter ==
            xenos::TextureFilter::kUseFetchConst;
        bool vol_min_filter_is_fetch_const =
            instr.attributes.vol_min_filter ==
            xenos::TextureFilter::kUseFetchConst;
        bool vol_mag_filter_is_linear =
            instr.attributes.vol_mag_filter == xenos::TextureFilter::kLinear;
        bool vol_min_filter_is_linear =
            instr.attributes.vol_min_filter == xenos::TextureFilter::kLinear;
        if (grad_v_temp != UINT32_MAX &&
            (vol_mag_filter_is_fetch_const || vol_min_filter_is_fetch_const ||
             vol_mag_filter_is_linear != vol_min_filter_is_linear)) {
          if (srv_selection_temp == UINT32_MAX) {
            srv_selection_temp = PushSystemTemp();
          }
          layer_lerp_factor_src =
              DxbcSrc::R(srv_selection_temp, DxbcSrc::kZZZZ);
          // Initialize to point sampling, and break register dependency for 3D.
          DxbcOpMov(DxbcDest::R(srv_selection_temp, 0b0100), DxbcSrc::LF(0.0f));
          assert_true((size_needed_components & 0b1000) == 0b1000);
          DxbcOpIf(false, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
          // Check if minifying along layers (derivative > 1 along any axis).
          DxbcOpMax(DxbcDest::R(srv_selection_temp, 0b1000),
                    DxbcSrc::R(grad_h_lod_temp, DxbcSrc::kZZZZ),
                    DxbcSrc::R(grad_v_temp, DxbcSrc::kZZZZ));
          if (!instr.attributes.unnormalized_coordinates) {
            // Denormalize the gradient if provided as normalized.
            assert_true((size_needed_components & 0b0100) == 0b0100);
            DxbcOpMul(DxbcDest::R(srv_selection_temp, 0b1000),
                      DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW),
                      DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kZZZZ));
          }
          // For NaN, considering that magnification is being done. Zero
          // srv_selection_temp.w means magnifying, non-zero means minifying.
          DxbcOpLT(DxbcDest::R(srv_selection_temp, 0b1000), DxbcSrc::LF(1.0f),
                   DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW));
          if (vol_mag_filter_is_fetch_const || vol_min_filter_is_fetch_const) {
            DxbcOpIf(false, DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW));
            // Write the magnification filter to srv_selection_temp.w. In the
            // "if" rather than "else" because this is more likely to happen if
            // the layer is constant.
            if (vol_mag_filter_is_fetch_const) {
              DxbcOpAnd(DxbcDest::R(srv_selection_temp, 0b1000),
                        RequestTextureFetchConstantWord(tfetch_index, 4),
                        DxbcSrc::LU(1));
            } else {
              DxbcOpMov(DxbcDest::R(srv_selection_temp, 0b1000),
                        DxbcSrc::LU(uint32_t(vol_mag_filter_is_linear)));
            }
            DxbcOpElse();
            // Write the minification filter to srv_selection_temp.w.
            if (vol_min_filter_is_fetch_const) {
              DxbcOpUBFE(DxbcDest::R(srv_selection_temp, 0b1000),
                         DxbcSrc::LU(1), DxbcSrc::LU(1),
                         RequestTextureFetchConstantWord(tfetch_index, 4));
            } else {
              DxbcOpMov(DxbcDest::R(srv_selection_temp, 0b1000),
                        DxbcSrc::LU(uint32_t(vol_min_filter_is_linear)));
            }
            // Close the magnification check.
            DxbcOpEndIf();
            // Check if the filter is linear.
            DxbcOpIf(true, DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW));
          } else if (vol_mag_filter_is_linear) {
            assert_false(vol_min_filter_is_linear);
            // Both overridden, one (magnification) is linear, another
            // (minification) is not - handle linear filtering if magnifying.
            DxbcOpIf(false, DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW));
          } else {
            assert_true(vol_min_filter_is_linear);
            assert_false(vol_mag_filter_is_linear);
            // Both overridden, one (minification) is linear, another
            // (magnification) is not - handle linear filtering if minifying.
            DxbcOpIf(true, DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW));
          }
          // For linear filtering, subtract 0.5 from the coordinates and store
          // the lerp factor. Flooring will be done later.
          DxbcOpAdd(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                    DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ),
                    DxbcSrc::LF(-0.5f));
          DxbcOpFrc(DxbcDest::R(srv_selection_temp, 0b0100),
                    DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
          // Close the linear check.
          DxbcOpEndIf();
          // Close the stacked check.
          DxbcOpEndIf();
        } else {
          // No gradients, or using the same filter overrides for magnifying and
          // minifying. Assume always magnifying if no gradients (LOD 0, always
          // <= 0). LOD is within 2D layers, not between them (unlike in 3D
          // textures, which have mips with depth reduced).
          if (vol_mag_filter_is_fetch_const || vol_mag_filter_is_linear) {
            if (srv_selection_temp == UINT32_MAX) {
              srv_selection_temp = PushSystemTemp();
            }
            layer_lerp_factor_src =
                DxbcSrc::R(srv_selection_temp, DxbcSrc::kZZZZ);
            // Initialize to point sampling, and break register dependency for
            // 3D.
            DxbcOpMov(DxbcDest::R(srv_selection_temp, 0b0100),
                      DxbcSrc::LF(0.0f));
            assert_true((size_needed_components & 0b1000) == 0b1000);
            DxbcOpIf(false, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
            if (vol_mag_filter_is_fetch_const) {
              // Extract the magnification filtering mode from the fetch
              // constant.
              DxbcOpAnd(DxbcDest::R(srv_selection_temp, 0b1000),
                        RequestTextureFetchConstantWord(tfetch_index, 4),
                        DxbcSrc::LU(1));
              // Check if it's linear.
              DxbcOpIf(true, DxbcSrc::R(srv_selection_temp, DxbcSrc::kWWWW));
            }
            // For linear filtering, subtract 0.5 from the coordinates and store
            // the lerp factor. Flooring will be done later.
            DxbcOpAdd(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                      DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ),
                      DxbcSrc::LF(-0.5f));
            DxbcOpFrc(DxbcDest::R(srv_selection_temp, 0b0100),
                      DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
            if (vol_mag_filter_is_fetch_const) {
              // Close the fetch constant linear filtering mode check.
              DxbcOpEndIf();
            }
            // Close the stacked check.
            DxbcOpEndIf();
          }
        }
      }
      // Check if any component is not signed, and if any component is signed.
      uint32_t result_first_component;
      xe::bit_scan_forward(used_result_nonzero_components,
                           &result_first_component);
      DxbcSrc is_all_signed_src(
          DxbcSrc::R(is_signed_temp).Select(result_first_component));
      DxbcSrc is_any_signed_src(
          DxbcSrc::R(is_signed_temp).Select(result_first_component));
      if (used_result_nonzero_components != (1 << result_first_component)) {
        // Multiple components fetched - need to merge.
        if (srv_selection_temp == UINT32_MAX) {
          srv_selection_temp = PushSystemTemp();
        }
        DxbcDest is_all_signed_dest(DxbcDest::R(srv_selection_temp, 0b0001));
        DxbcDest is_any_signed_dest(DxbcDest::R(srv_selection_temp, 0b0010));
        uint32_t result_remaining_components =
            used_result_nonzero_components &
            ~(uint32_t(1) << result_first_component);
        uint32_t result_component;
        while (xe::bit_scan_forward(result_remaining_components,
                                    &result_component)) {
          result_remaining_components &= ~(uint32_t(1) << result_component);
          DxbcOpAnd(is_all_signed_dest, is_all_signed_src,
                    DxbcSrc::R(is_signed_temp).Select(result_component));
          DxbcOpOr(is_any_signed_dest, is_any_signed_src,
                   DxbcSrc::R(is_signed_temp).Select(result_component));
          // For the first component, both sources must both be two is_signed
          // components, to initialize.
          is_all_signed_src = DxbcSrc::R(srv_selection_temp, DxbcSrc::kXXXX);
          is_any_signed_src = DxbcSrc::R(srv_selection_temp, DxbcSrc::kYYYY);
        }
      }

      // Sample the texture - choose between 3D and stacked, and then sample
      // unsigned and signed SRVs and choose between them.

      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        assert_true((size_needed_components & 0b1000) == 0b1000);
        // The first fetch attempt will be for the 3D SRV.
        DxbcOpIf(true, DxbcSrc::R(size_and_is_3d_temp, DxbcSrc::kWWWW));
      }
      for (uint32_t is_stacked = 0;
           is_stacked <
           (instr.dimension == xenos::FetchOpDimension::k3DOrStacked ? 2u : 1u);
           ++is_stacked) {
        // i == 0 - 1D/2D/3D/cube.
        // i == 1 - 2D stacked.
        xenos::FetchOpDimension srv_dimension = instr.dimension;
        uint32_t srv_grad_component_count = grad_component_count;
        bool layer_lerp_needed = false;
        if (is_stacked) {
          srv_dimension = xenos::FetchOpDimension::k2D;
          srv_grad_component_count = 2;
          layer_lerp_needed =
              layer_lerp_factor_src.type_ != DxbcOperandType::kImmediate32;
          DxbcOpElse();
          // Floor the array layer (Direct3D 12 does rounding to nearest even
          // for the layer index, but on the Xbox 360, addressing is similar to
          // that of 3D textures). This is needed for both point and linear
          // filtering (with linear, 0.5 was subtracted previously).
          DxbcOpRoundNI(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                        DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ));
        }
        uint32_t texture_binding_index_unsigned =
            FindOrAddTextureBinding(tfetch_index, srv_dimension, false);
        uint32_t texture_binding_index_signed =
            FindOrAddTextureBinding(tfetch_index, srv_dimension, true);
        const TextureBinding& texture_binding_unsigned =
            texture_bindings_[texture_binding_index_unsigned];
        const TextureBinding& texture_binding_signed =
            texture_bindings_[texture_binding_index_signed];
        DxbcSrc srv_unsigned(DxbcSrc::LF(0.0f)), srv_signed(DxbcSrc::LF(0.0f));
        if (bindless_resources_used_) {
          uint32_t* bindless_srv_index = nullptr;
          switch (srv_dimension) {
            case xenos::FetchOpDimension::k1D:
            case xenos::FetchOpDimension::k2D:
              bindless_srv_index = &srv_index_bindless_textures_2d_;
              break;
            case xenos::FetchOpDimension::k3DOrStacked:
              bindless_srv_index = &srv_index_bindless_textures_3d_;
              break;
            case xenos::FetchOpDimension::kCube:
              bindless_srv_index = &srv_index_bindless_textures_cube_;
              break;
          }
          assert_not_null(bindless_srv_index);
          if (*bindless_srv_index == kBindingIndexUnallocated) {
            *bindless_srv_index = srv_count_++;
          }
          assert_true(srv_selection_temp != UINT32_MAX);
          srv_unsigned =
              DxbcSrc::T(*bindless_srv_index, DxbcIndex(srv_selection_temp, 3));
          srv_signed = srv_unsigned;
        } else {
          srv_unsigned =
              DxbcSrc::T(texture_binding_unsigned.bindful_srv_index,
                         uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                             texture_binding_index_unsigned);
          srv_signed =
              DxbcSrc::T(texture_binding_signed.bindful_srv_index,
                         uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                             texture_binding_index_signed);
        }
        for (uint32_t layer = 0; layer < (layer_lerp_needed ? 2u : 1u);
             ++layer) {
          uint32_t layer_value_temp = system_temp_result_;
          if (layer) {
            layer_value_temp = PushSystemTemp();
            // Check if the lerp factor is not zero (or NaN).
            DxbcOpNE(DxbcDest::R(layer_value_temp, 0b0001),
                     layer_lerp_factor_src, DxbcSrc::LF(0.0f));
            // If the lerp factor is not zero, sample the next layer.
            DxbcOpIf(true, DxbcSrc::R(layer_value_temp, DxbcSrc::kXXXX));
            // Go to the next layer.
            DxbcOpAdd(DxbcDest::R(coord_and_sampler_temp, 0b0100),
                      DxbcSrc::R(coord_and_sampler_temp, DxbcSrc::kZZZZ),
                      DxbcSrc::LF(1.0f));
          }
          // Always 3 coordinate components (1D and 2D are padded to 2D arrays,
          // 3D and cube have 3 coordinate dimensions).
          DxbcOpIf(false, is_all_signed_src);
          {
            // Sample the unsigned texture.
            if (bindless_resources_used_) {
              // Load the unsigned texture descriptor index.
              assert_true(srv_selection_temp != UINT32_MAX);
              if (cbuffer_index_descriptor_indices_ ==
                  kBindingIndexUnallocated) {
                cbuffer_index_descriptor_indices_ = cbuffer_count_++;
              }
              uint32_t texture_bindless_descriptor_index =
                  texture_binding_unsigned.bindless_descriptor_index;
              DxbcOpMov(
                  DxbcDest::R(srv_selection_temp, 0b1000),
                  DxbcSrc::CB(cbuffer_index_descriptor_indices_,
                              uint32_t(CbufferRegister::kDescriptorIndices),
                              texture_bindless_descriptor_index >> 2)
                      .Select(texture_bindless_descriptor_index & 3));
            }
            if (grad_v_temp != UINT32_MAX) {
              assert_not_zero(grad_component_count);
              DxbcOpSampleD(
                  DxbcDest::R(layer_value_temp, used_result_nonzero_components),
                  DxbcSrc::R(coord_and_sampler_temp), 3, srv_unsigned, sampler,
                  DxbcSrc::R(grad_h_lod_temp), DxbcSrc::R(grad_v_temp),
                  srv_grad_component_count);
            } else {
              DxbcOpSampleL(
                  DxbcDest::R(layer_value_temp, used_result_nonzero_components),
                  DxbcSrc::R(coord_and_sampler_temp), 3, srv_unsigned, sampler,
                  lod_src);
            }
          }
          DxbcOpEndIf();
          DxbcOpIf(true, is_any_signed_src);
          {
            // Sample the signed texture.
            uint32_t signed_temp = PushSystemTemp();
            if (bindless_resources_used_) {
              // Load the signed texture descriptor index.
              assert_true(srv_selection_temp != UINT32_MAX);
              if (cbuffer_index_descriptor_indices_ ==
                  kBindingIndexUnallocated) {
                cbuffer_index_descriptor_indices_ = cbuffer_count_++;
              }
              uint32_t texture_bindless_descriptor_index =
                  texture_binding_signed.bindless_descriptor_index;
              DxbcOpMov(
                  DxbcDest::R(srv_selection_temp, 0b1000),
                  DxbcSrc::CB(cbuffer_index_descriptor_indices_,
                              uint32_t(CbufferRegister::kDescriptorIndices),
                              texture_bindless_descriptor_index >> 2)
                      .Select(texture_bindless_descriptor_index & 3));
            }
            if (grad_v_temp != UINT32_MAX) {
              assert_not_zero(grad_component_count);
              DxbcOpSampleD(
                  DxbcDest::R(signed_temp, used_result_nonzero_components),
                  DxbcSrc::R(coord_and_sampler_temp), 3, srv_signed, sampler,
                  DxbcSrc::R(grad_h_lod_temp), DxbcSrc::R(grad_v_temp),
                  srv_grad_component_count);
            } else {
              DxbcOpSampleL(
                  DxbcDest::R(signed_temp, used_result_nonzero_components),
                  DxbcSrc::R(coord_and_sampler_temp), 3, srv_signed, sampler,
                  lod_src);
            }
            DxbcOpMovC(
                DxbcDest::R(layer_value_temp, used_result_nonzero_components),
                DxbcSrc::R(is_signed_temp), DxbcSrc::R(signed_temp),
                DxbcSrc::R(layer_value_temp));
            // Release signed_temp.
            PopSystemTemp();
          }
          DxbcOpEndIf();
          if (layer) {
            assert_true(layer_value_temp != system_temp_result_);
            // Interpolate between the two layers.
            DxbcOpAdd(
                DxbcDest::R(layer_value_temp, used_result_nonzero_components),
                DxbcSrc::R(layer_value_temp), -DxbcSrc::R(system_temp_result_));
            DxbcOpMAd(DxbcDest::R(system_temp_result_,
                                  used_result_nonzero_components),
                      DxbcSrc::R(layer_value_temp), layer_lerp_factor_src,
                      DxbcSrc::R(system_temp_result_));
            // Close the linear filtering check.
            DxbcOpEndIf();
            // Release the allocated layer_value_temp.
            PopSystemTemp();
          }
        }
      }
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        // Close the stacked/3D check.
        DxbcOpEndIf();
      }

      if (srv_selection_temp != UINT32_MAX) {
        PopSystemTemp();
      }
      // Release is_signed_temp.
      PopSystemTemp();

      // Release grad_h_lod_temp and grad_v_temp.
      if (grad_v_temp != UINT32_MAX) {
        PopSystemTemp();
      }
      if (grad_h_lod_temp != UINT32_MAX) {
        PopSystemTemp();
      }
    }

    // Release coord_and_sampler_temp.
    PopSystemTemp();

    // Apply the bias and gamma correction (gamma is after filtering here,
    // likely should be before, but it's outside Xenia's control for host
    // sampler filtering).
    if (instr.opcode == FetchOpcode::kTextureFetch) {
      assert_true(signs_temp != UINT32_MAX);
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(used_result_nonzero_components & (1 << i))) {
          continue;
        }
        DxbcDest component_dest(DxbcDest::R(system_temp_result_, 1 << i));
        DxbcSrc component_src(DxbcSrc::R(system_temp_result_).Select(i));
        DxbcOpSwitch(DxbcSrc::R(signs_temp).Select(i));
        DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::TextureSign::kUnsignedBiased)));
        DxbcOpMAd(component_dest, component_src, DxbcSrc::LF(2.0f),
                  DxbcSrc::LF(-1.0f));
        DxbcOpBreak();
        DxbcOpCase(DxbcSrc::LU(uint32_t(xenos::TextureSign::kGamma)));
        uint32_t gamma_temp = PushSystemTemp();
        ConvertPWLGamma(false, system_temp_result_, i, system_temp_result_, i,
                        gamma_temp, 0, gamma_temp, 1);
        // Release gamma_temp.
        PopSystemTemp();
        DxbcOpBreak();
        DxbcOpEndSwitch();
      }
    }
    if (signs_temp != UINT32_MAX) {
      PopSystemTemp();
    }
  }

  if (size_and_is_3d_temp != UINT32_MAX) {
    PopSystemTemp();
  }

  if (instr.opcode == FetchOpcode::kTextureFetch) {
    // Apply the result exponent bias.
    uint32_t exp_adjust_temp = PushSystemTemp();
    DxbcOpIBFE(DxbcDest::R(exp_adjust_temp, 0b0001), DxbcSrc::LU(6),
               DxbcSrc::LU(13),
               RequestTextureFetchConstantWord(tfetch_index, 3));
    DxbcOpIMAd(DxbcDest::R(exp_adjust_temp, 0b0001),
               DxbcSrc::R(exp_adjust_temp, DxbcSrc::kXXXX),
               DxbcSrc::LI(int32_t(1) << 23), DxbcSrc::LF(1.0f));
    DxbcOpMul(DxbcDest::R(system_temp_result_, used_result_nonzero_components),
              DxbcSrc::R(system_temp_result_),
              DxbcSrc::R(exp_adjust_temp, DxbcSrc::kXXXX));
    // Release exp_adjust_temp.
    PopSystemTemp();
  }

  uint32_t used_result_zero_components =
      used_result_components & ~used_result_nonzero_components;
  if (used_result_zero_components) {
    DxbcOpMov(DxbcDest::R(system_temp_result_, used_result_zero_components),
              DxbcSrc::LF(0.0f));
  }
  StoreResult(instr.result, DxbcSrc::R(system_temp_result_));
}

}  // namespace gpu
}  // namespace xe
