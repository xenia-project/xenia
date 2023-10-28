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
#include "xenia/gpu/render_target_cache.h"

DEFINE_bool(
    ac6_ground_fix, false,
    "This fixes(hide) issues with black ground in AC6. Use only in AC6. "
    "Might cause issues in other titles.",
    "HACKS");

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
  // If this is vfetch_full, the address may still be needed for vfetch_mini -
  // don't exit before calculating the address.
  if (!needed_words && instr.is_mini_fetch) {
    // Nothing to load - just constant 0/1 writes, or the swizzle includes only
    // components that don't exist in the format (writing zero instead of them).
    // Unpacking assumes at least some word is needed.
    StoreResult(instr.result, dxbc::Src::LF(0.0f));
    return;
  }

  // Create a 2-component dxbc::Src for the fetch constant (vf0 is in [0].xy of
  // the fetch constants array, vf1 is in [0].zw, vf2 is in [1].xy).
  if (cbuffer_index_fetch_constants_ == kBindingIndexUnallocated) {
    cbuffer_index_fetch_constants_ = cbuffer_count_++;
  }
  dxbc::Src fetch_constant_src(dxbc::Src::CB(
      cbuffer_index_fetch_constants_,
      uint32_t(CbufferRegister::kFetchConstants),
      instr.operands[1].storage_index >> 1,
      (instr.operands[1].storage_index & 1) ? 0b10101110 : 0b00000100));

  // TODO(Triang3l): Verify the fetch constant type (that it's a vertex fetch,
  // not a texture fetch), here instead of dropping draws with invalid vertex
  // fetch constants on the CPU when proper bound checks are added - vfetch may
  // be conditional, so fetch constants may also be used conditionally.

  // - Load the part of the byte address in the physical memory that is the same
  //   in vfetch_full and vfetch_mini to system_temp_grad_v_vfetch_address_.w
  //   (the index operand GPR must not be reloaded in vfetch_mini because it
  //   might have been overwritten previously, but that shouldn't have effect on
  //   vfetch_mini).

  dxbc::Src address_src(
      dxbc::Src::R(system_temp_grad_v_vfetch_address_, dxbc::Src::kWWWW));
  if (!instr.is_mini_fetch) {
    dxbc::Dest address_dest(
        dxbc::Dest::R(system_temp_grad_v_vfetch_address_, 0b1000));
    if (instr.attributes.stride) {
      // Convert the index to an integer by flooring or by rounding to the
      // nearest (as floor(index + 0.5) because rounding to the nearest even
      // makes no sense for addressing, both 1.5 and 2.5 would be 2).
      {
        bool index_operand_temp_pushed = false;
        dxbc::Src index_operand(
            LoadOperand(instr.operands[0], 0b0001, index_operand_temp_pushed)
                .SelectFromSwizzled(0));
        if (instr.attributes.is_index_rounded) {
          a_.OpAdd(address_dest, index_operand, dxbc::Src::LF(0.5f));
          a_.OpRoundNI(address_dest, address_src);
        } else {
          // UGLY HACK. Remove ASAP.
          // Proper fix requires accurate RCP implementation.
          if (cvars::ac6_ground_fix) {
            a_.OpAdd(address_dest, index_operand, dxbc::Src::LF(0.00025f));
            a_.OpRoundNI(address_dest, address_src);
          } else {
            a_.OpRoundNI(address_dest, index_operand);
          }
        }
        if (index_operand_temp_pushed) {
          PopSystemTemp();
        }
      }
      a_.OpFToI(address_dest, address_src);
      // Extract the byte address from the fetch constant to
      // system_temp_result_.w (which is not used yet).
      a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b1000),
               fetch_constant_src.SelectFromSwizzled(0),
               dxbc::Src::LU(~uint32_t(3)));
      // Merge the index and the base address.
      a_.OpIMAd(address_dest, address_src,
                dxbc::Src::LU(instr.attributes.stride * sizeof(uint32_t)),
                dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));
    } else {
      // Fetching from the same location - extract the byte address of the
      // beginning of the buffer.
      a_.OpAnd(address_dest, fetch_constant_src.SelectFromSwizzled(0),
               dxbc::Src::LU(~uint32_t(3)));
    }
  }

  if (!needed_words) {
    // The vfetch_full address has been loaded for the subsequent vfetch_mini,
    // but there's no data to load.
    StoreResult(instr.result, dxbc::Src::LF(0.0f));
    return;
  }

  dxbc::Dest address_temp_dest(dxbc::Dest::R(system_temp_result_, 0b1000));
  dxbc::Src address_temp_src(
      dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));

  // - From now on, if any additional offset must be applied to the
  //   `base + index * stride` part of the address, it must be done by writing
  //   to system_temp_result_.w (address_temp_dest) instead of
  //   system_temp_grad_v_vfetch_address_.w (since it must stay the same for the
  //   vfetch_full and all its vfetch_mini invocations), and changing
  //   address_src to address_temp_src afterwards. system_temp_result_.w can be
  //   used for this purpose safely because it won't be overwritten until the
  //   last dword is loaded (after which the address won't be needed anymore).

  // Add the word offset from the instruction (signed), plus the offset of the
  // first needed word within the element.
  uint32_t first_word_index;
  xe::bit_scan_forward(needed_words, &first_word_index);
  int32_t first_word_buffer_offset =
      instr.attributes.offset + int32_t(first_word_index);
  if (first_word_buffer_offset) {
    // Add the constant word offset.
    a_.OpIAdd(address_temp_dest, address_src,
              dxbc::Src::LI(first_word_buffer_offset * sizeof(uint32_t)));
    address_src = address_temp_src;
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
  a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
           LoadFlagsSystemConstant(),
           dxbc::Src::LU(kSysFlag_SharedMemoryIsUAV));
  a_.OpIf(false, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
  if (srv_index_shared_memory_ == kBindingIndexUnallocated) {
    srv_index_shared_memory_ = srv_count_++;
  }
  if (uav_index_shared_memory_ == kBindingIndexUnallocated) {
    uav_index_shared_memory_ = uav_count_++;
  }
  for (uint32_t i = 0; i < 2; ++i) {
    if (i) {
      a_.OpElse();
    }
    dxbc::Src shared_memory_src(
        i ? dxbc::Src::U(uav_index_shared_memory_,
                         uint32_t(UAVRegister::kSharedMemory))
          : dxbc::Src::T(srv_index_shared_memory_,
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
        a_.OpIAdd(address_temp_dest, address_src,
                  dxbc::Src::LU((word_index - word_index_previous) *
                                sizeof(uint32_t)));
        address_src = address_temp_src;
        word_index_previous = word_index;
      }
      // Can ld_raw either to the first multiple components, or to any scalar
      // component.
      dxbc::Dest words_result_dest(dxbc::Dest::R(
          system_temp_result_, ((1 << word_count) - 1) << word_index));
      if (!word_index || word_count == 1) {
        // Read directly to system_temp_result_.
        a_.OpLdRaw(words_result_dest, address_src, shared_memory_src);
      } else {
        // Read to the first components of a temporary register.
        uint32_t load_temp = PushSystemTemp();
        a_.OpLdRaw(dxbc::Dest::R(load_temp, (1 << word_count) - 1), address_src,
                   shared_memory_src);
        // Copy to system_temp_result_.
        a_.OpMov(words_result_dest,
                 dxbc::Src::R(load_temp,
                              (dxbc::Src::kXYZW & ((1 << (word_count * 2)) - 1))
                                  << (word_index * 2)));
        // Release load_temp.
        PopSystemTemp();
      }
    }
  }
  a_.OpEndIf();

  dxbc::Src result_src(dxbc::Src::R(system_temp_result_));

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
    a_.OpAnd(dxbc::Dest::R(endian_temp, 1 << endian_temp_component),
             fetch_constant_src.SelectFromSwizzled(1), dxbc::Src::LU(0b11));
    dxbc::Src endian_src(
        dxbc::Src::R(endian_temp).Select(endian_temp_component));

    dxbc::Dest swap_temp_dest(dxbc::Dest::R(swap_temp, needed_words));
    dxbc::Src swap_temp_src(dxbc::Src::R(swap_temp));
    dxbc::Dest swap_result_dest(
        dxbc::Dest::R(system_temp_result_, needed_words));

    // 8-in-16 or one half of 8-in-32.
    a_.OpSwitch(endian_src);
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in16)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)));
    // Temp = X0Z0.
    a_.OpAnd(swap_temp_dest, result_src, dxbc::Src::LU(0x00FF00FF));
    // Result = YZW0.
    a_.OpUShR(swap_result_dest, result_src, dxbc::Src::LU(8));
    // Result = Y0W0.
    a_.OpAnd(swap_result_dest, result_src, dxbc::Src::LU(0x00FF00FF));
    // Result = YXWZ.
    a_.OpUMAd(swap_result_dest, swap_temp_src, dxbc::Src::LU(256), result_src);
    a_.OpBreak();
    a_.OpEndSwitch();

    // 16-in-32 or another half of 8-in-32.
    a_.OpSwitch(endian_src);
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k8in32)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian128::k16in32)));
    // Temp = ZW00.
    a_.OpUShR(swap_temp_dest, result_src, dxbc::Src::LU(16));
    // Result = ZWXY.
    a_.OpBFI(swap_result_dest, dxbc::Src::LU(16), dxbc::Src::LU(16), result_src,
             swap_temp_src);
    a_.OpBreak();
    a_.OpEndSwitch();

    // Release endian_temp (if allocated) and swap_temp.
    PopSystemTemp((endian_temp != swap_temp) ? 2 : 1);
  }

  // - Unpack the format.

  uint32_t used_format_components =
      used_result_components & ((1 << xenos::GetVertexFormatComponentCount(
                                     instr.attributes.data_format)) -
                                1);
  dxbc::Dest result_unpacked_dest(
      dxbc::Dest::R(system_temp_result_, used_format_components));
  // If needed_words is not zero (checked in the beginning), this must not be
  // zero too. For simplicity, it's assumed that something will be unpacked
  // here.
  assert_not_zero(used_format_components);
  uint32_t packed_widths[4] = {}, packed_offsets[4] = {};
  uint32_t packed_swizzle = dxbc::Src::kXXXX;
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
      a_.OpIBFE(result_unpacked_dest, dxbc::Src::LP(packed_widths),
                dxbc::Src::LP(packed_offsets),
                dxbc::Src::R(system_temp_result_, packed_swizzle));
      a_.OpIToF(result_unpacked_dest, result_src);
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
              a_.OpMul(dxbc::Dest::R(system_temp_result_, packed_scales_mask),
                       result_src, dxbc::Src::LP(packed_scales));
            }
            // Treat both -(2^(n-1)) and -(2^(n-1)-1) as -1.
            a_.OpMax(result_unpacked_dest, result_src, dxbc::Src::LF(-1.0f));
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
            a_.OpMAd(result_unpacked_dest, result_src,
                     dxbc::Src::LP(packed_scales), dxbc::Src::LP(packed_zeros));
          } break;
          default:
            assert_unhandled_case(instr.attributes.signed_rf_mode);
        }
      }
    } else {
      a_.OpUBFE(result_unpacked_dest, dxbc::Src::LP(packed_widths),
                dxbc::Src::LP(packed_offsets),
                dxbc::Src::R(system_temp_result_, packed_swizzle));
      a_.OpUToF(result_unpacked_dest, result_src);
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
          a_.OpMul(dxbc::Dest::R(system_temp_result_, packed_scales_mask),
                   result_src, dxbc::Src::LP(packed_scales));
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
        a_.OpUBFE(result_unpacked_dest, dxbc::Src::LU(16),
                  dxbc::Src::LU(0, 16, 0, 16),
                  dxbc::Src::R(system_temp_result_, 0b01010000));
        a_.OpF16ToF32(result_unpacked_dest, result_src);
        break;
      case xenos::VertexFormat::k_32:
      case xenos::VertexFormat::k_32_32:
      case xenos::VertexFormat::k_32_32_32_32:
        if (instr.attributes.is_signed) {
          a_.OpIToF(result_unpacked_dest, result_src);
        } else {
          a_.OpUToF(result_unpacked_dest, result_src);
        }
        if (!instr.attributes.is_integer) {
          if (instr.attributes.is_signed) {
            switch (instr.attributes.signed_rf_mode) {
              case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne:
                a_.OpMul(result_unpacked_dest, result_src,
                         dxbc::Src::LF(1.0f / 2147483647.0f));
                // No need to clamp to -1 if signed - 1/(2^31-1) is rounded to
                // 1/(2^31) as float32.
                break;
              case xenos::SignedRepeatingFractionMode::kNoZero:
                a_.OpMAd(result_unpacked_dest, result_src,
                         dxbc::Src::LF(1.0f / 2147483647.5f),
                         dxbc::Src::LF(0.5f / 2147483647.5f));
                break;
              default:
                assert_unhandled_case(instr.attributes.signed_rf_mode);
            }
          } else {
            a_.OpMul(result_unpacked_dest, result_src,
                     dxbc::Src::LF(1.0f / 4294967295.0f));
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
    a_.OpMul(result_unpacked_dest, result_src,
             dxbc::Src::LF(std::ldexp(1.0f, instr.attributes.exp_adjust)));
  }

  // - Write zeros to components not present in the format.

  uint32_t used_missing_components =
      used_result_components & ~used_format_components;
  if (used_missing_components) {
    a_.OpMov(dxbc::Dest::R(system_temp_result_, used_missing_components),
             dxbc::Src::LF(0.0f));
  }

  StoreResult(instr.result, dxbc::Src::R(system_temp_result_));
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
    const TextureBinding& texture_binding = texture_bindings_[i];
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
  TextureBinding& new_texture_binding = texture_bindings_.emplace_back();
  if (!bindless_resources_used_) {
    new_texture_binding.bindful_srv_index = srv_count_++;
    texture_bindings_for_bindful_srv_indices_.insert(
        {new_texture_binding.bindful_srv_index, texture_binding_index});
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
    new_texture_binding.bindful_name =
        fmt::format("xe_texture{}_{}_{}", fetch_constant, dimension_name,
                    is_signed ? 's' : 'u');
  } else {
    new_texture_binding.bindful_srv_index = kBindingIndexUnallocated;
  }
  new_texture_binding.bindful_srv_rdef_name_ptr = 0;
  // Consistently 0 if not bindless as it may be used for hashing.
  new_texture_binding.bindless_descriptor_index =
      bindless_resources_used_ ? GetBindlessResourceCount() : 0;
  new_texture_binding.fetch_constant = fetch_constant;
  new_texture_binding.dimension = dimension;
  new_texture_binding.is_signed = is_signed;
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
  SamplerBinding& new_sampler_binding = sampler_bindings_.emplace_back();
  // Consistently 0 if not bindless as it may be used for hashing.
  new_sampler_binding.bindless_descriptor_index =
      bindless_resources_used_ ? GetBindlessResourceCount() : 0;
  new_sampler_binding.fetch_constant = fetch_constant;
  new_sampler_binding.mag_filter = mag_filter;
  new_sampler_binding.min_filter = min_filter;
  new_sampler_binding.mip_filter = mip_filter;
  new_sampler_binding.aniso_filter = aniso_filter;
  if (!bindless_resources_used_) {
    std::ostringstream name;
    name << "xe_sampler" << fetch_constant;
    if (aniso_filter == xenos::AnisoFilter::kDisabled ||
        aniso_filter == xenos::AnisoFilter::kUseFetchConst) {
      static const char kFilterSuffixes[] = {'p', 'l', 'b', 'f'};
      name << '_' << kFilterSuffixes[uint32_t(mag_filter)]
           << kFilterSuffixes[uint32_t(min_filter)]
           << kFilterSuffixes[uint32_t(mip_filter)];
    }
    if (aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
      if (aniso_filter == xenos::AnisoFilter::kDisabled) {
        name << "_a0";
      } else {
        name << "_a" << (UINT32_C(1) << (uint32_t(aniso_filter) - 1));
      }
    }
    new_sampler_binding.bindful_name = name.str();
  }
  return uint32_t(sampler_bindings_.size() - 1);
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
      a_.OpMov(dxbc::Dest::R(system_temp_grad_h_lod_, 0b1000),
               LoadOperand(instr.operands[0], 0b0001, lod_operand_temp_pushed)
                   .SelectFromSwizzled(0));
      if (lod_operand_temp_pushed) {
        PopSystemTemp();
      }
      return;
    }
    case FetchOpcode::kSetTextureGradientsHorz: {
      bool grad_operand_temp_pushed = false;
      a_.OpMov(
          dxbc::Dest::R(system_temp_grad_h_lod_, 0b0111),
          LoadOperand(instr.operands[0], 0b0111, grad_operand_temp_pushed));
      if (grad_operand_temp_pushed) {
        PopSystemTemp();
      }
      return;
    }
    case FetchOpcode::kSetTextureGradientsVert: {
      bool grad_operand_temp_pushed = false;
      a_.OpMov(
          dxbc::Dest::R(system_temp_grad_v_vfetch_address_, 0b0111),
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
    StoreResult(instr.result, dxbc::Src::LF(0.0f));
    return;
  }

  if (instr.opcode == FetchOpcode::kGetTextureGradients) {
    // Handle before doing anything that actually needs the texture.
    bool grad_operand_temp_pushed = false;
    dxbc::Src grad_operand = LoadOperand(
        instr.operands[0],
        ((used_result_nonzero_components & 0b0011) ? 0b0001 : 0) |
            ((used_result_nonzero_components & 0b1100) ? 0b0010 : 0),
        grad_operand_temp_pushed);
    if (used_result_nonzero_components & 0b0101) {
      a_.OpDerivRTXCoarse(
          dxbc::Dest::R(system_temp_result_,
                        used_result_nonzero_components & 0b0101),
          grad_operand.SwizzleSwizzled(0b010000));
    }
    if (used_result_nonzero_components & 0b1010) {
      a_.OpDerivRTYCoarse(
          dxbc::Dest::R(system_temp_result_,
                        used_result_nonzero_components & 0b1010),
          grad_operand.SwizzleSwizzled(0b01000000));
    }
    if (grad_operand_temp_pushed) {
      PopSystemTemp();
    }
    StoreResult(instr.result, dxbc::Src::R(system_temp_result_));
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
    StoreResult(instr.result, dxbc::Src::LF(0.0f));
    return;
  }

  if (instr.opcode != FetchOpcode::kTextureFetch &&
      instr.opcode != FetchOpcode::kGetTextureBorderColorFrac &&
      instr.opcode != FetchOpcode::kGetTextureComputedLod &&
      instr.opcode != FetchOpcode::kGetTextureWeights) {
    assert_unhandled_case(instr.opcode);
    EmitTranslationError("Unknown texture fetch operation");
    StoreResult(instr.result, dxbc::Src::LF(0.0f));
    return;
  }

  uint32_t tfetch_index = instr.operands[1].storage_index;

  // Whether to use gradients (implicit or explicit) for LOD calculation.
  bool use_computed_lod =
      instr.attributes.use_computed_lod &&
      (is_pixel_shader() || instr.attributes.use_register_gradients);
  if (instr.opcode == FetchOpcode::kGetTextureComputedLod &&
      (!use_computed_lod || instr.attributes.use_register_gradients)) {
    assert_always();
    EmitTranslationError(
        "getCompTexLOD used with explicit LOD or gradients - contradicts MSDN",
        false);
    StoreResult(instr.result, dxbc::Src::LF(0.0f));
    return;
  }

  // Get offsets applied to the coordinates before sampling.
  // `offsets` is used for float4 literal construction,
  // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched, not
  // at LOD 0. However, since offsets have granularity of 0.5, not 1, on the
  // Xbox 360, they can't be passed directly as AOffImmI to the `sample`
  // instruction (plus-minus 0.5 offsets are very common in games). But
  // offsetting at mip levels is a rare usage case, mostly offsets are used for
  // things like shadow maps and blur, where there are no mips.
  float offsets[3] = {};
  // MSDN doesn't list offsets as getCompTexLOD parameters.
  if (instr.opcode != FetchOpcode::kGetTextureComputedLod) {
    // Add a small epsilon to the offset (1.5/4 the fixed-point texture
    // coordinate ULP - shouldn't significantly effect the fixed-point
    // conversion; 1/4 is also not enough with 3x resolution scaling very
    // noticeably on the weapon in 4D5307E6) to resolve ambiguity when fetching
    // point-sampled textures between texels. This applies to both normalized
    // (58410954 Xbox Live Arcade logo, coordinates interpolated between
    // vertices with half-pixel offset) and unnormalized (4D5307E6 lighting
    // G-buffer reading, ps_param_gen pixels) coordinates. On Nvidia Pascal,
    // without this adjustment, blockiness is visible in both cases. Possibly
    // there is a better way, however, an attempt was made to error-correct
    // division by adding the difference between original and re-denormalized
    // coordinates, but on Nvidia, `mul` and internal multiplication in texture
    // sampling apparently round differently, so `mul` gives a value that would
    // be floored as expected, but the left/upper pixel is still sampled
    // instead.
    const float rounding_offset = 1.5f / 1024.0f;
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
  dxbc::Src offsets_src(
      dxbc::Src::LF(offsets[0], offsets[1], offsets[2], 0.0f));

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
        a_.OpUBFE(dxbc::Dest::R(size_and_is_3d_temp, 0b0001), dxbc::Src::LU(24),
                  dxbc::Src::LU(0),
                  RequestTextureFetchConstantWord(tfetch_index, 2));
        break;
      case xenos::FetchOpDimension::k2D:
      case xenos::FetchOpDimension::kCube:
        a_.OpUBFE(dxbc::Dest::R(size_and_is_3d_temp, size_needed_components),
                  dxbc::Src::LU(13, 13, 0, 0), dxbc::Src::LU(0, 13, 0, 0),
                  RequestTextureFetchConstantWord(tfetch_index, 2));
        break;
      case xenos::FetchOpDimension::k3DOrStacked:
        // tfetch3D is used for both stacked and 3D - first, check if 3D.
        a_.OpUBFE(dxbc::Dest::R(size_and_is_3d_temp, 0b1000), dxbc::Src::LU(2),
                  dxbc::Src::LU(9),
                  RequestTextureFetchConstantWord(tfetch_index, 5));
        a_.OpIEq(dxbc::Dest::R(size_and_is_3d_temp, 0b1000),
                 dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW),
                 dxbc::Src::LU(uint32_t(xenos::DataDimension::k3D)));
        if (size_needed_components & 0b0111) {
          // Even if depth isn't needed specifically for stacked or specifically
          // for 3D later, load both cases anyway to make sure the register is
          // always initialized.
          a_.OpIf(true, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
          // Load the 3D texture size.
          a_.OpUBFE(dxbc::Dest::R(size_and_is_3d_temp,
                                  size_needed_components & 0b0111),
                    dxbc::Src::LU(11, 11, 10, 0), dxbc::Src::LU(0, 11, 22, 0),
                    RequestTextureFetchConstantWord(tfetch_index, 2));
          a_.OpElse();
          // Load the 2D stacked texture size.
          a_.OpUBFE(dxbc::Dest::R(size_and_is_3d_temp,
                                  size_needed_components & 0b0111),
                    dxbc::Src::LU(13, 13, 6, 0), dxbc::Src::LU(0, 13, 26, 0),
                    RequestTextureFetchConstantWord(tfetch_index, 2));
          a_.OpEndIf();
        }
        break;
    }
    if (size_needed_components & 0b0111) {
      // Fetch constants store size minus 1 - add 1.
      a_.OpIAdd(
          dxbc::Dest::R(size_and_is_3d_temp, size_needed_components & 0b0111),
          dxbc::Src::R(size_and_is_3d_temp), dxbc::Src::LU(1));
      // Convert the size to float for multiplication/division.
      a_.OpUToF(
          dxbc::Dest::R(size_and_is_3d_temp, size_needed_components & 0b0111),
          dxbc::Src::R(size_and_is_3d_temp));
    }
  }
  uint32_t revert_resolution_scale_axes =
      cvars::draw_resolution_scaled_texture_offsets
          ? uint32_t(draw_resolution_scale_x_ > 1) |
                (uint32_t(draw_resolution_scale_y_ > 1) << 1)
          : 0;

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
    dxbc::Src coord_operand =
        LoadOperand(instr.operands[0], used_result_nonzero_components,
                    coord_operand_temp_pushed);
    dxbc::Src coord_src(coord_operand);
    // If needed, apply the resolution scale to the width / height and the
    // unnormalized coordinates.
    uint32_t resolution_scaled_result_components =
        used_result_nonzero_components & revert_resolution_scale_axes;
    uint32_t resolution_scaled_coord_components =
        instr.attributes.unnormalized_coordinates
            ? resolution_scaled_result_components
            : 0b0000;
    uint32_t resolution_scaled_size_components =
        size_needed_components & resolution_scaled_result_components;
    if (resolution_scaled_coord_components ||
        resolution_scaled_size_components) {
      if (resolution_scaled_coord_components &&
          (coord_src.type_ != dxbc::OperandType::kTemp ||
           coord_src.index_1d_.index_ != system_temp_result_)) {
        // Use system_temp_result_ as a temporary for conditionally
        // resolution-scaled coordinates.
        a_.OpMov(
            dxbc::Dest::R(system_temp_result_, used_result_nonzero_components),
            coord_src);
        coord_src = dxbc::Src::R(system_temp_result_);
      }
      // Using system_temp_result_.w as a temporary for the flag indicating
      // whether the texture was resolved - not involved in coordinate
      // calculations.
      assert_zero(used_result_nonzero_components & 0b1000);
      a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b1000),
               LoadSystemConstant(SystemConstants::Index::kTexturesResolved,
                                  offsetof(SystemConstants, textures_resolved),
                                  dxbc::Src::kXXXX),
               dxbc::Src::LU(uint32_t(1) << tfetch_index));
      a_.OpIf(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));
      // The texture is resolved - scale the coordinates and the size.
      dxbc::Src resolution_scale_src(
          dxbc::Src::LF(float(draw_resolution_scale_x_),
                        float(draw_resolution_scale_y_), 1.0f, 1.0f));
      if (resolution_scaled_coord_components) {
        a_.OpMul(dxbc::Dest::R(system_temp_result_,
                               resolution_scaled_coord_components),
                 coord_src, resolution_scale_src);
      }
      if (resolution_scaled_size_components) {
        a_.OpMul(dxbc::Dest::R(size_and_is_3d_temp,
                               resolution_scaled_size_components),
                 dxbc::Src::R(size_and_is_3d_temp), resolution_scale_src);
      }
      a_.OpEndIf();
    }
    uint32_t offsets_needed = offsets_not_zero & used_result_nonzero_components;
    if (!instr.attributes.unnormalized_coordinates || offsets_needed) {
      // Using system_temp_result_ as a temporary for coordinate denormalization
      // and offsetting. May already contain the coordinates loaded if
      // resolution scaling was applied to the coordinates.
      coord_src = dxbc::Src::R(system_temp_result_);
      dxbc::Dest coord_dest(
          dxbc::Dest::R(system_temp_result_, used_result_nonzero_components));
      if (instr.attributes.unnormalized_coordinates) {
        if (offsets_needed) {
          a_.OpAdd(coord_dest, coord_operand, offsets_src);
        }
      } else {
        assert_true((size_needed_components & used_result_nonzero_components) ==
                    used_result_nonzero_components);
        if (offsets_needed) {
          a_.OpMAd(coord_dest, coord_operand, dxbc::Src::R(size_and_is_3d_temp),
                   offsets_src);
        } else {
          a_.OpMul(coord_dest, coord_operand,
                   dxbc::Src::R(size_and_is_3d_temp));
        }
      }
    }
    // 0.5 has already been subtracted via offsets previously.
    a_.OpFrc(dxbc::Dest::R(system_temp_result_, used_result_nonzero_components),
             coord_src);
    if (coord_operand_temp_pushed) {
      PopSystemTemp();
    }
  } else {
    // - Component signedness, for selecting the SRV, and if data is needed.

    dxbc::Src signs_uint_src(
        GetSystemConstantSrc(offsetof(SystemConstants, texture_swizzled_signs) +
                                 sizeof(uint32_t) * (tfetch_index >> 2),
                             dxbc::Src::kXXXX));
    uint32_t signs_shift = (tfetch_index & 3) * 8;
    uint32_t signs_temp = UINT32_MAX;
    if (instr.opcode == FetchOpcode::kTextureFetch) {
      signs_temp = PushSystemTemp();
      MarkSystemConstantUsed(SystemConstants::Index::kTextureSwizzledSigns);
      a_.OpUBFE(dxbc::Dest::R(signs_temp, used_result_nonzero_components),
                dxbc::Src::LU(2),
                dxbc::Src::LU(signs_shift, signs_shift + 2, signs_shift + 4,
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
    dxbc::Src coord_operand = LoadOperand(
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
    uint32_t normalized_components_with_offsets =
        normalized_components & offsets_not_zero;
    uint32_t normalized_components_with_scaled_offsets =
        normalized_components_with_offsets & revert_resolution_scale_axes;
    uint32_t normalized_components_with_unscaled_offsets =
        normalized_components_with_offsets &
        ~normalized_components_with_scaled_offsets;
    uint32_t normalized_components_without_offsets =
        normalized_components & ~normalized_components_with_offsets;
    if (instr.attributes.unnormalized_coordinates) {
      // Unnormalized coordinates - normalize XY, and if 3D, normalize Z.
      assert_not_zero(normalized_components);
      assert_true((size_needed_components & normalized_components) ==
                  normalized_components);
      if (normalized_components_with_offsets) {
        // Apply the offsets to components to normalize where needed, or just
        // copy the components to coord_and_sampler_temp where not.
        // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched.
        if (normalized_components_with_scaled_offsets) {
          // Using coord_and_sampler_temp.w as a temporary for the needed
          // resolution scale inverse - sampler not loaded yet.
          a_.OpAnd(
              dxbc::Dest::R(coord_and_sampler_temp, 0b1000),
              LoadSystemConstant(SystemConstants::Index::kTexturesResolved,
                                 offsetof(SystemConstants, textures_resolved),
                                 dxbc::Src::kXXXX),
              dxbc::Src::LU(uint32_t(1) << tfetch_index));
          a_.OpIf(true, dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW));
          a_.OpAdd(
              dxbc::Dest::R(coord_and_sampler_temp,
                            normalized_components_with_scaled_offsets),
              coord_operand,
              dxbc::Src::LF(offsets[0] / draw_resolution_scale_x_,
                            offsets[1] / draw_resolution_scale_y_, 0.0f, 0.0f));
          a_.OpElse();
          a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp,
                                 normalized_components_with_scaled_offsets),
                   coord_operand, offsets_src);
          a_.OpEndIf();
        }
        if (normalized_components_with_unscaled_offsets) {
          a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp,
                                 normalized_components_with_unscaled_offsets),
                   coord_operand, offsets_src);
        }
        if (normalized_components_without_offsets) {
          a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp,
                                 normalized_components_without_offsets),
                   coord_operand);
        }
        assert_not_zero(normalized_components & 0b011);
        a_.OpDiv(dxbc::Dest::R(coord_and_sampler_temp,
                               normalized_components & 0b011),
                 dxbc::Src::R(coord_and_sampler_temp),
                 dxbc::Src::R(size_and_is_3d_temp));
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Normalize if 3D.
          assert_true((size_needed_components & 0b1100) == 0b1100);
          a_.OpIf(true, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
          a_.OpDiv(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                   dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ),
                   dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kZZZZ));
          a_.OpEndIf();
        }
      } else {
        a_.OpDiv(dxbc::Dest::R(coord_and_sampler_temp, normalized_components),
                 coord_operand, dxbc::Src::R(size_and_is_3d_temp));
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Don't normalize if stacked.
          assert_true((size_needed_components & 0b1000) == 0b1000);
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ),
                    coord_operand.SelectFromSwizzled(2));
        }
      }
    } else {
      // Normalized coordinates - apply offsets to XY or copy them to
      // coord_and_sampler_temp, and if stacked, denormalize Z.
      if (normalized_components_with_offsets) {
        // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched.
        assert_true(
            (size_needed_components & normalized_components_with_offsets) ==
            normalized_components_with_offsets);
        a_.OpDiv(dxbc::Dest::R(coord_and_sampler_temp,
                               normalized_components_with_offsets),
                 offsets_src, dxbc::Src::R(size_and_is_3d_temp));
        if (normalized_components_with_scaled_offsets) {
          // Using coord_and_sampler_temp.w as a temporary for the needed
          // resolution scale inverse - sampler not loaded yet.
          a_.OpAnd(
              dxbc::Dest::R(coord_and_sampler_temp, 0b1000),
              LoadSystemConstant(SystemConstants::Index::kTexturesResolved,
                                 offsetof(SystemConstants, textures_resolved),
                                 dxbc::Src::kXXXX),
              dxbc::Src::LU(uint32_t(1) << tfetch_index));
          a_.OpIf(true, dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW));
          a_.OpMAd(dxbc::Dest::R(coord_and_sampler_temp,
                                 normalized_components_with_scaled_offsets),
                   dxbc::Src::R(coord_and_sampler_temp),
                   dxbc::Src::LF(1.0f / draw_resolution_scale_x_,
                                 1.0f / draw_resolution_scale_y_, 1.0f, 1.0f),
                   coord_operand);
          a_.OpElse();
          a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp,
                                 normalized_components_with_scaled_offsets),
                   coord_operand, dxbc::Src::R(coord_and_sampler_temp));
          a_.OpEndIf();
        }
        if (normalized_components_with_unscaled_offsets) {
          a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp,
                                 normalized_components_with_unscaled_offsets),
                   coord_operand, dxbc::Src::R(coord_and_sampler_temp));
        }
      }
      // 3D/stacked without offset is handled separately.
      if (normalized_components_without_offsets & 0b011) {
        a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp,
                               normalized_components_without_offsets & 0b011),
                 coord_operand);
      }
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        assert_true((size_needed_components & 0b1100) == 0b1100);
        if (normalized_components_with_offsets & 0b100) {
          // Denormalize and offset Z (re-apply the offset not to lose precision
          // as a result of division) if stacked.
          a_.OpIf(false, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
          a_.OpMAd(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                   coord_operand.SelectFromSwizzled(2),
                   dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kZZZZ),
                   dxbc::Src::LF(offsets[2]));
          a_.OpEndIf();
        } else {
          // Denormalize Z if stacked, and revert to normalized if 3D.
          a_.OpMul(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                   coord_operand.SelectFromSwizzled(2),
                   dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kZZZZ));
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW),
                    coord_operand.SelectFromSwizzled(2),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
        }
      }
    }
    switch (instr.dimension) {
      case xenos::FetchOpDimension::k1D:
        // Pad to 2D array coordinates.
        a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp, 0b0110),
                 dxbc::Src::LF(0.0f));
        break;
      case xenos::FetchOpDimension::k2D:
        // Pad to 2D array coordinates.
        a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                 dxbc::Src::LF(0.0f));
        break;
      case xenos::FetchOpDimension::kCube: {
        // Transform from the major axis SC/TC plus 1 into cube coordinates.
        // Move SC/TC from 1...2 to -1...1.
        a_.OpMAd(dxbc::Dest::R(coord_and_sampler_temp, 0b0011),
                 dxbc::Src::R(coord_and_sampler_temp), dxbc::Src::LF(2.0f),
                 dxbc::Src::LF(-3.0f));
        // Get the face index (floored, within 0...5) as an integer to
        // coord_and_sampler_temp.z.
        if (offsets[2]) {
          a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                   coord_operand.SelectFromSwizzled(2),
                   dxbc::Src::LF(offsets[2]));
          a_.OpFToU(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
        } else {
          a_.OpFToU(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    coord_operand.SelectFromSwizzled(2));
        }
        a_.OpUMin(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                  dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ),
                  dxbc::Src::LU(5));
        // Split the face index into axis and sign (0 - positive, 1 - negative)
        // to coord_and_sampler_temp.zw (sign in W so it won't be overwritten).
        // Fine to overwrite W at this point, the sampler index hasn't been
        // loaded yet.
        a_.OpUBFE(dxbc::Dest::R(coord_and_sampler_temp, 0b1100),
                  dxbc::Src::LU(0, 0, 2, 1), dxbc::Src::LU(0, 0, 1, 0),
                  dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
        // Remap the axes in a way opposite to the ALU cube instruction.
        a_.OpSwitch(dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
        a_.OpCase(dxbc::Src::LU(0));
        {
          // X is the major axis.
          // Y = -TC (TC overwritten).
          a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp, 0b0010),
                   -dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kYYYY));
          // Z = neg ? SC : -SC.
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kXXXX),
                    -dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kXXXX));
          // X = neg ? -1 : 1 (SC overwritten).
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0001),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW),
                    dxbc::Src::LF(-1.0f), dxbc::Src::LF(1.0f));
        }
        a_.OpBreak();
        a_.OpCase(dxbc::Src::LU(1));
        {
          // Y is the major axis.
          // X = SC (already there).
          // Z = neg ? -TC : TC.
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW),
                    -dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kYYYY),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kYYYY));
          // Y = neg ? -1 : 1 (TC overwritten).
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0010),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW),
                    dxbc::Src::LF(-1.0f), dxbc::Src::LF(1.0f));
        }
        a_.OpBreak();
        a_.OpDefault();
        {
          // Z is the major axis.
          // X = neg ? -SC : SC (SC overwritten).
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0001),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW),
                    -dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kXXXX),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kXXXX));
          // Y = -TC (TC overwritten).
          a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp, 0b0010),
                   -dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kYYYY));
          // Z = neg ? -1 : 1.
          a_.OpMovC(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                    dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kWWWW),
                    dxbc::Src::LF(-1.0f), dxbc::Src::LF(1.0f));
        }
        a_.OpBreak();
        a_.OpEndSwitch();
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
      dxbc::Src sampler(
          dxbc::Src::S(sampler_binding_index, sampler_binding_index));
      if (bindless_resources_used_) {
        // Load the sampler index to coord_and_sampler_temp.w and use relative
        // sampler indexing.
        if (cbuffer_index_descriptor_indices_ == kBindingIndexUnallocated) {
          cbuffer_index_descriptor_indices_ = cbuffer_count_++;
        }
        uint32_t sampler_bindless_descriptor_index =
            sampler_bindings_[sampler_binding_index].bindless_descriptor_index;
        a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp, 0b1000),
                 dxbc::Src::CB(cbuffer_index_descriptor_indices_,
                               uint32_t(CbufferRegister::kDescriptorIndices),
                               sampler_bindless_descriptor_index >> 2)
                     .Select(sampler_bindless_descriptor_index & 3));
        sampler = dxbc::Src::S(0, dxbc::Index(coord_and_sampler_temp, 3));
      }
      // Check which SRV needs to be accessed - signed or unsigned. If there is
      // at least one non-signed component, will be using the unsigned one.
      uint32_t is_unsigned_temp = PushSystemTemp();
      MarkSystemConstantUsed(SystemConstants::Index::kTextureSwizzledSigns);
      a_.OpUBFE(dxbc::Dest::R(is_unsigned_temp, 0b0001), dxbc::Src::LU(8),
                dxbc::Src::LU(signs_shift), signs_uint_src);
      a_.OpINE(
          dxbc::Dest::R(is_unsigned_temp, 0b0001),
          dxbc::Src::R(is_unsigned_temp, dxbc::Src::kXXXX),
          dxbc::Src::LU(uint32_t(xenos::TextureSign::kSigned) * 0b01010101));
      if (bindless_resources_used_) {
        // Bindless path - select the SRV index between unsigned and signed to
        // query.
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Check if 3D.
          assert_true((size_needed_components & 0b1000) == 0b1000);
          a_.OpIf(true, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
        }
        for (uint32_t is_stacked = 0;
             is_stacked <
             (instr.dimension == xenos::FetchOpDimension::k3DOrStacked ? 2u
                                                                       : 1u);
             ++is_stacked) {
          xenos::FetchOpDimension srv_dimension = instr.dimension;
          if (is_stacked) {
            srv_dimension = xenos::FetchOpDimension::k2D;
            a_.OpElse();
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
          a_.OpMovC(
              dxbc::Dest::R(is_unsigned_temp, 0b0001),
              dxbc::Src::R(is_unsigned_temp, dxbc::Src::kXXXX),
              dxbc::Src::CB(cbuffer_index_descriptor_indices_,
                            uint32_t(CbufferRegister::kDescriptorIndices),
                            texture_bindless_descriptor_index_unsigned >> 2)
                  .Select(texture_bindless_descriptor_index_unsigned & 3),
              dxbc::Src::CB(cbuffer_index_descriptor_indices_,
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
          a_.OpLOD(
              dxbc::Dest::R(system_temp_result_, 0b0001),
              dxbc::Src::R(coord_and_sampler_temp), 3,
              dxbc::Src::T(*bindless_srv_index,
                           dxbc::Index(is_unsigned_temp, 0), dxbc::Src::kYYYY),
              sampler);
        }
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Close the 3D/stacked check.
          a_.OpEndIf();
        }
      } else {
        // Bindful path - conditionally query one of the SRVs.
        a_.OpIf(true, dxbc::Src::R(is_unsigned_temp, dxbc::Src::kXXXX));
        for (uint32_t is_signed = 0; is_signed < 2; ++is_signed) {
          if (is_signed) {
            a_.OpElse();
          }
          if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
            // Check if 3D.
            assert_true((size_needed_components & 0b1000) == 0b1000);
            a_.OpIf(true, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
          }
          for (uint32_t is_stacked = 0;
               is_stacked <
               (instr.dimension == xenos::FetchOpDimension::k3DOrStacked ? 2u
                                                                         : 1u);
               ++is_stacked) {
            if (is_stacked) {
              a_.OpElse();
            }
            assert_true(used_result_nonzero_components == 0b0001);
            uint32_t texture_binding_index = FindOrAddTextureBinding(
                tfetch_index,
                is_stacked ? xenos::FetchOpDimension::k2D : instr.dimension,
                is_signed != 0);
            a_.OpLOD(
                dxbc::Dest::R(system_temp_result_, 0b0001),
                dxbc::Src::R(coord_and_sampler_temp), 3,
                dxbc::Src::T(
                    texture_bindings_[texture_binding_index].bindful_srv_index,
                    uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                        texture_binding_index,
                    dxbc::Src::kYYYY),
                sampler);
          }
          if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
            // Close the 3D/stacked check.
            a_.OpEndIf();
          }
        }
        // Close the signedness check.
        a_.OpEndIf();
      }
      // Release is_unsigned_temp.
      PopSystemTemp();
    } else {
      // - Gradients or LOD to be passed to the sample_d/sample_l.

      dxbc::Src lod_src(dxbc::Src::LF(0.0f));
      uint32_t grad_component_count = 0;
      // Will be allocated for both explicit and computed LOD.
      uint32_t grad_h_lod_temp = UINT32_MAX;
      // Will be allocated for computed LOD only, and if not using basemap mip
      // filter.
      uint32_t grad_v_temp = UINT32_MAX;
      if (instr.attributes.mip_filter != xenos::TextureFilter::kBaseMap) {
        grad_h_lod_temp = PushSystemTemp();
        lod_src = dxbc::Src::R(grad_h_lod_temp, dxbc::Src::kWWWW);
        // Accumulate the explicit LOD sources (in D3D11.3 specification order:
        // specified LOD + sampler LOD bias + instruction LOD bias).
        dxbc::Dest lod_dest(dxbc::Dest::R(grad_h_lod_temp, 0b1000));
        // Fetch constant LOD bias * 32.
        a_.OpIBFE(lod_dest, dxbc::Src::LU(10), dxbc::Src::LU(12),
                  RequestTextureFetchConstantWord(tfetch_index, 4));
        a_.OpIToF(lod_dest, lod_src);
        if (instr.attributes.use_register_lod) {
          // Divide the fetch constant LOD bias by 32, and add the register LOD
          // and the instruction LOD bias.
          a_.OpMAd(lod_dest, lod_src, dxbc::Src::LF(1.0f / 32.0f),
                   dxbc::Src::R(system_temp_grad_h_lod_, dxbc::Src::kWWWW));
          if (instr.attributes.lod_bias) {
            a_.OpAdd(lod_dest, lod_src,
                     dxbc::Src::LF(instr.attributes.lod_bias));
          }
        } else {
          // Divide the fetch constant LOD by 32, and add the instruction LOD
          // bias.
          if (instr.attributes.lod_bias) {
            a_.OpMAd(lod_dest, lod_src, dxbc::Src::LF(1.0f / 32.0f),
                     dxbc::Src::LF(instr.attributes.lod_bias));
          } else {
            a_.OpMul(lod_dest, lod_src, dxbc::Src::LF(1.0f / 32.0f));
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
          a_.OpExp(lod_dest, lod_src);
          // FIXME(Triang3l): Gradient exponent adjustment is currently not done
          // in getCompTexLOD, so don't do it here too.
#if 0
          // Extract gradient exponent biases from the fetch constant and merge
          // them with the LOD bias.
          a_.OpIBFE(dxbc::Dest::R(grad_h_lod_temp, 0b0011), dxbc::Src::LU(5),
                    dxbc::Src::LU(22, 27, 0, 0),
                    RequestTextureFetchConstantWord(tfetch_index, 4));
          a_.OpIMAd(dxbc::Dest::R(grad_h_lod_temp, 0b0011),
                    dxbc::Src::R(grad_h_lod_temp),
                    dxbc::Src::LI(int32_t(1) << 23), dxbc::Src::LF(1.0f));
          a_.OpMul(dxbc::Dest::R(grad_v_temp, 0b1000), lod_src,
                   dxbc::Src::R(grad_h_lod_temp, dxbc::Src::kYYYY));
          a_.OpMul(lod_dest, lod_src,
                   dxbc::Src::R(grad_h_lod_temp, dxbc::Src::kXXXX));
#endif
          // Obtain the gradients and apply biases to them.
          if (instr.attributes.use_register_gradients) {
            // Register gradients are already in the cube space for cube maps.
            a_.OpMul(dxbc::Dest::R(grad_h_lod_temp, grad_mask),
                     dxbc::Src::R(system_temp_grad_h_lod_), lod_src);
            // FIXME(Triang3l): Gradient exponent adjustment is currently not
            // done in getCompTexLOD, so don't do it here too.
#if 0
            a_.OpMul(dxbc::Dest::R(grad_v_temp, grad_mask),
                     dxbc::Src::R(system_temp_grad_v_vfetch_address_),
                     dxbc::Src::R(grad_v_temp, dxbc::Src::kWWWW));
#else
            a_.OpMul(dxbc::Dest::R(grad_v_temp, grad_mask),
                     dxbc::Src::R(system_temp_grad_v_vfetch_address_), lod_src);
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
              a_.OpDiv(dxbc::Dest::R(grad_h_lod_temp, grad_norm_mask),
                       dxbc::Src::R(grad_h_lod_temp),
                       dxbc::Src::R(size_and_is_3d_temp));
              a_.OpDiv(dxbc::Dest::R(grad_v_temp, grad_norm_mask),
                       dxbc::Src::R(grad_v_temp),
                       dxbc::Src::R(size_and_is_3d_temp));
              // Normalize Z of the gradients for fetching from the 3D texture.
              assert_true((size_needed_components & 0b1100) == 0b1100);
              a_.OpIf(true,
                      dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
              a_.OpDiv(dxbc::Dest::R(grad_h_lod_temp, 0b0100),
                       dxbc::Src::R(grad_h_lod_temp, dxbc::Src::kZZZZ),
                       dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kZZZZ));
              a_.OpDiv(dxbc::Dest::R(grad_v_temp, 0b0100),
                       dxbc::Src::R(grad_v_temp, dxbc::Src::kZZZZ),
                       dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kZZZZ));
              a_.OpEndIf();
            }
          } else {
            // Coarse is according to the Direct3D 11.3 specification.
            a_.OpDerivRTXCoarse(dxbc::Dest::R(grad_h_lod_temp, grad_mask),
                                dxbc::Src::R(coord_and_sampler_temp));
            a_.OpMul(dxbc::Dest::R(grad_h_lod_temp, grad_mask),
                     dxbc::Src::R(grad_h_lod_temp), lod_src);
            a_.OpDerivRTYCoarse(dxbc::Dest::R(grad_v_temp, grad_mask),
                                dxbc::Src::R(coord_and_sampler_temp));
            // FIXME(Triang3l): Gradient exponent adjustment is currently not
            // done in getCompTexLOD, so don't do it here too.
#if 0
            a_.OpMul(dxbc::Dest::R(grad_v_temp, grad_mask),
                     dxbc::Src::R(grad_v_temp),
                     dxbc::Src::R(grad_v_temp, dxbc::Src::kWWWW));
#else
            a_.OpMul(dxbc::Dest::R(grad_v_temp, grad_mask),
                     dxbc::Src::R(grad_v_temp), lod_src);
#endif
          }
          if (instr.dimension == xenos::FetchOpDimension::k1D) {
            // Pad the gradients to 2D because 1D textures are fetched as 2D
            // arrays.
            a_.OpMov(dxbc::Dest::R(grad_h_lod_temp, 0b0010),
                     dxbc::Src::LF(0.0f));
            a_.OpMov(dxbc::Dest::R(grad_v_temp, 0b0010), dxbc::Src::LF(0.0f));
            grad_component_count = 2;
          }
        }
      }

      // - Data.

      // 4D5307F2 uses vertex displacement map textures for tessellated models
      // like the beehive tree with explicit LOD with point sampling (they store
      // values packed in two components), however, the fetch constant has
      // anisotropic filtering enabled. However, Direct3D 12 doesn't allow
      // mixing anisotropic and point filtering. Possibly anistropic filtering
      // should be disabled when explicit LOD is used - do this here.
      uint32_t sampler_binding_index = FindOrAddSamplerBinding(
          tfetch_index, instr.attributes.mag_filter,
          instr.attributes.min_filter, instr.attributes.mip_filter,
          use_computed_lod ? instr.attributes.aniso_filter
                           : xenos::AnisoFilter::kDisabled);
      dxbc::Src sampler(
          dxbc::Src::S(sampler_binding_index, sampler_binding_index));
      if (bindless_resources_used_) {
        // Load the sampler index to coord_and_sampler_temp.w and use relative
        // sampler indexing.
        if (cbuffer_index_descriptor_indices_ == kBindingIndexUnallocated) {
          cbuffer_index_descriptor_indices_ = cbuffer_count_++;
        }
        uint32_t sampler_bindless_descriptor_index =
            sampler_bindings_[sampler_binding_index].bindless_descriptor_index;
        a_.OpMov(dxbc::Dest::R(coord_and_sampler_temp, 0b1000),
                 dxbc::Src::CB(cbuffer_index_descriptor_indices_,
                               uint32_t(CbufferRegister::kDescriptorIndices),
                               sampler_bindless_descriptor_index >> 2)
                     .Select(sampler_bindless_descriptor_index & 3));
        sampler = dxbc::Src::S(0, dxbc::Index(coord_and_sampler_temp, 3));
      }

      // Break result register dependencies because textures will be sampled
      // conditionally, including the primary signs.
      a_.OpMov(
          dxbc::Dest::R(system_temp_result_, used_result_nonzero_components),
          dxbc::Src::LF(0.0f));

      // Extract whether each component is signed.
      uint32_t is_signed_temp = PushSystemTemp();
      a_.OpIEq(dxbc::Dest::R(is_signed_temp, used_result_nonzero_components),
               dxbc::Src::R(signs_temp),
               dxbc::Src::LU(uint32_t(xenos::TextureSign::kSigned)));

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
      dxbc::Src layer_lerp_factor_src(dxbc::Src::LF(0.0f));
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
              dxbc::Src::R(srv_selection_temp, dxbc::Src::kZZZZ);
          // Initialize to point sampling, and break register dependency for 3D.
          a_.OpMov(dxbc::Dest::R(srv_selection_temp, 0b0100),
                   dxbc::Src::LF(0.0f));
          assert_true((size_needed_components & 0b1000) == 0b1000);
          a_.OpIf(false, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
          // Check if minifying along layers (derivative > 1 along any axis).
          a_.OpMax(dxbc::Dest::R(srv_selection_temp, 0b1000),
                   dxbc::Src::R(grad_h_lod_temp, dxbc::Src::kZZZZ),
                   dxbc::Src::R(grad_v_temp, dxbc::Src::kZZZZ));
          if (!instr.attributes.unnormalized_coordinates) {
            // Denormalize the gradient if provided as normalized.
            assert_true((size_needed_components & 0b0100) == 0b0100);
            a_.OpMul(dxbc::Dest::R(srv_selection_temp, 0b1000),
                     dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW),
                     dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kZZZZ));
          }
          // For NaN, considering that magnification is being done. Zero
          // srv_selection_temp.w means magnifying, non-zero means minifying.
          a_.OpLT(dxbc::Dest::R(srv_selection_temp, 0b1000),
                  dxbc::Src::LF(1.0f),
                  dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW));
          if (vol_mag_filter_is_fetch_const || vol_min_filter_is_fetch_const) {
            a_.OpIf(false, dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW));
            // Write the magnification filter to srv_selection_temp.w. In the
            // "if" rather than "else" because this is more likely to happen if
            // the layer is constant.
            if (vol_mag_filter_is_fetch_const) {
              a_.OpAnd(dxbc::Dest::R(srv_selection_temp, 0b1000),
                       RequestTextureFetchConstantWord(tfetch_index, 4),
                       dxbc::Src::LU(1));
            } else {
              a_.OpMov(dxbc::Dest::R(srv_selection_temp, 0b1000),
                       dxbc::Src::LU(uint32_t(vol_mag_filter_is_linear)));
            }
            a_.OpElse();
            // Write the minification filter to srv_selection_temp.w.
            if (vol_min_filter_is_fetch_const) {
              a_.OpUBFE(dxbc::Dest::R(srv_selection_temp, 0b1000),
                        dxbc::Src::LU(1), dxbc::Src::LU(1),
                        RequestTextureFetchConstantWord(tfetch_index, 4));
            } else {
              a_.OpMov(dxbc::Dest::R(srv_selection_temp, 0b1000),
                       dxbc::Src::LU(uint32_t(vol_min_filter_is_linear)));
            }
            // Close the magnification check.
            a_.OpEndIf();
            // Check if the filter is linear.
            a_.OpIf(true, dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW));
          } else if (vol_mag_filter_is_linear) {
            assert_false(vol_min_filter_is_linear);
            // Both overridden, one (magnification) is linear, another
            // (minification) is not - handle linear filtering if magnifying.
            a_.OpIf(false, dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW));
          } else {
            assert_true(vol_min_filter_is_linear);
            assert_false(vol_mag_filter_is_linear);
            // Both overridden, one (minification) is linear, another
            // (magnification) is not - handle linear filtering if minifying.
            a_.OpIf(true, dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW));
          }
          // For linear filtering, subtract 0.5 from the coordinates and store
          // the lerp factor. Flooring will be done later.
          a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                   dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ),
                   dxbc::Src::LF(-0.5f));
          a_.OpFrc(dxbc::Dest::R(srv_selection_temp, 0b0100),
                   dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
          // Close the linear check.
          a_.OpEndIf();
          // Close the stacked check.
          a_.OpEndIf();
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
                dxbc::Src::R(srv_selection_temp, dxbc::Src::kZZZZ);
            // Initialize to point sampling, and break register dependency for
            // 3D.
            a_.OpMov(dxbc::Dest::R(srv_selection_temp, 0b0100),
                     dxbc::Src::LF(0.0f));
            assert_true((size_needed_components & 0b1000) == 0b1000);
            a_.OpIf(false, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
            if (vol_mag_filter_is_fetch_const) {
              // Extract the magnification filtering mode from the fetch
              // constant.
              a_.OpAnd(dxbc::Dest::R(srv_selection_temp, 0b1000),
                       RequestTextureFetchConstantWord(tfetch_index, 4),
                       dxbc::Src::LU(1));
              // Check if it's linear.
              a_.OpIf(true, dxbc::Src::R(srv_selection_temp, dxbc::Src::kWWWW));
            }
            // For linear filtering, subtract 0.5 from the coordinates and store
            // the lerp factor. Flooring will be done later.
            a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                     dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ),
                     dxbc::Src::LF(-0.5f));
            a_.OpFrc(dxbc::Dest::R(srv_selection_temp, 0b0100),
                     dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
            if (vol_mag_filter_is_fetch_const) {
              // Close the fetch constant linear filtering mode check.
              a_.OpEndIf();
            }
            // Close the stacked check.
            a_.OpEndIf();
          }
        }
      }
      // Check if any component is not signed, and if any component is signed.
      uint32_t result_first_component;
      xe::bit_scan_forward(used_result_nonzero_components,
                           &result_first_component);
      dxbc::Src is_all_signed_src(
          dxbc::Src::R(is_signed_temp).Select(result_first_component));
      dxbc::Src is_any_signed_src(
          dxbc::Src::R(is_signed_temp).Select(result_first_component));
      if (used_result_nonzero_components != (1 << result_first_component)) {
        // Multiple components fetched - need to merge.
        if (srv_selection_temp == UINT32_MAX) {
          srv_selection_temp = PushSystemTemp();
        }
        dxbc::Dest is_all_signed_dest(
            dxbc::Dest::R(srv_selection_temp, 0b0001));
        dxbc::Dest is_any_signed_dest(
            dxbc::Dest::R(srv_selection_temp, 0b0010));
        uint32_t result_remaining_components =
            used_result_nonzero_components &
            ~(uint32_t(1) << result_first_component);
        uint32_t result_component;
        while (xe::bit_scan_forward(result_remaining_components,
                                    &result_component)) {
          result_remaining_components &= ~(uint32_t(1) << result_component);
          a_.OpAnd(is_all_signed_dest, is_all_signed_src,
                   dxbc::Src::R(is_signed_temp).Select(result_component));
          a_.OpOr(is_any_signed_dest, is_any_signed_src,
                  dxbc::Src::R(is_signed_temp).Select(result_component));
          // For the first component, both sources must both be two is_signed
          // components, to initialize.
          is_all_signed_src =
              dxbc::Src::R(srv_selection_temp, dxbc::Src::kXXXX);
          is_any_signed_src =
              dxbc::Src::R(srv_selection_temp, dxbc::Src::kYYYY);
        }
      }

      // Sample the texture - choose between 3D and stacked, and then sample
      // unsigned and signed SRVs and choose between them.

      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        assert_true((size_needed_components & 0b1000) == 0b1000);
        // The first fetch attempt will be for the 3D SRV.
        a_.OpIf(true, dxbc::Src::R(size_and_is_3d_temp, dxbc::Src::kWWWW));
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
              layer_lerp_factor_src.type_ != dxbc::OperandType::kImmediate32;
          a_.OpElse();
          // Floor the array layer (Direct3D 12 does rounding to nearest even
          // for the layer index, but on the Xbox 360, addressing is similar to
          // that of 3D textures). This is needed for both point and linear
          // filtering (with linear, 0.5 was subtracted previously).
          a_.OpRoundNI(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                       dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ));
        }
        uint32_t texture_binding_index_unsigned =
            FindOrAddTextureBinding(tfetch_index, srv_dimension, false);
        uint32_t texture_binding_index_signed =
            FindOrAddTextureBinding(tfetch_index, srv_dimension, true);
        const TextureBinding& texture_binding_unsigned =
            texture_bindings_[texture_binding_index_unsigned];
        const TextureBinding& texture_binding_signed =
            texture_bindings_[texture_binding_index_signed];
        dxbc::Src srv_unsigned(dxbc::Src::LF(0.0f)),
            srv_signed(dxbc::Src::LF(0.0f));
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
          srv_unsigned = dxbc::Src::T(*bindless_srv_index,
                                      dxbc::Index(srv_selection_temp, 3));
          srv_signed = srv_unsigned;
        } else {
          srv_unsigned =
              dxbc::Src::T(texture_binding_unsigned.bindful_srv_index,
                           uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                               texture_binding_index_unsigned);
          srv_signed =
              dxbc::Src::T(texture_binding_signed.bindful_srv_index,
                           uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                               texture_binding_index_signed);
        }
        for (uint32_t layer = 0; layer < (layer_lerp_needed ? 2u : 1u);
             ++layer) {
          uint32_t layer_value_temp = system_temp_result_;
          if (layer) {
            layer_value_temp = PushSystemTemp();
            // Check if the lerp factor is not zero (or NaN).
            a_.OpNE(dxbc::Dest::R(layer_value_temp, 0b0001),
                    layer_lerp_factor_src, dxbc::Src::LF(0.0f));
            // If the lerp factor is not zero, sample the next layer.
            a_.OpIf(true, dxbc::Src::R(layer_value_temp, dxbc::Src::kXXXX));
            // Go to the next layer.
            a_.OpAdd(dxbc::Dest::R(coord_and_sampler_temp, 0b0100),
                     dxbc::Src::R(coord_and_sampler_temp, dxbc::Src::kZZZZ),
                     dxbc::Src::LF(1.0f));
          }
          // Always 3 coordinate components (1D and 2D are padded to 2D arrays,
          // 3D and cube have 3 coordinate dimensions).
          a_.OpIf(false, is_all_signed_src);
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
              a_.OpMov(
                  dxbc::Dest::R(srv_selection_temp, 0b1000),
                  dxbc::Src::CB(cbuffer_index_descriptor_indices_,
                                uint32_t(CbufferRegister::kDescriptorIndices),
                                texture_bindless_descriptor_index >> 2)
                      .Select(texture_bindless_descriptor_index & 3));
            }
            if (grad_v_temp != UINT32_MAX) {
              assert_not_zero(grad_component_count);
              a_.OpSampleD(dxbc::Dest::R(layer_value_temp,
                                         used_result_nonzero_components),
                           dxbc::Src::R(coord_and_sampler_temp), 3,
                           srv_unsigned, sampler, dxbc::Src::R(grad_h_lod_temp),
                           dxbc::Src::R(grad_v_temp), srv_grad_component_count);
            } else {
              a_.OpSampleL(dxbc::Dest::R(layer_value_temp,
                                         used_result_nonzero_components),
                           dxbc::Src::R(coord_and_sampler_temp), 3,
                           srv_unsigned, sampler, lod_src);
            }
          }
          a_.OpEndIf();
          a_.OpIf(true, is_any_signed_src);
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
              a_.OpMov(
                  dxbc::Dest::R(srv_selection_temp, 0b1000),
                  dxbc::Src::CB(cbuffer_index_descriptor_indices_,
                                uint32_t(CbufferRegister::kDescriptorIndices),
                                texture_bindless_descriptor_index >> 2)
                      .Select(texture_bindless_descriptor_index & 3));
            }
            if (grad_v_temp != UINT32_MAX) {
              assert_not_zero(grad_component_count);
              a_.OpSampleD(
                  dxbc::Dest::R(signed_temp, used_result_nonzero_components),
                  dxbc::Src::R(coord_and_sampler_temp), 3, srv_signed, sampler,
                  dxbc::Src::R(grad_h_lod_temp), dxbc::Src::R(grad_v_temp),
                  srv_grad_component_count);
            } else {
              a_.OpSampleL(
                  dxbc::Dest::R(signed_temp, used_result_nonzero_components),
                  dxbc::Src::R(coord_and_sampler_temp), 3, srv_signed, sampler,
                  lod_src);
            }
            a_.OpMovC(
                dxbc::Dest::R(layer_value_temp, used_result_nonzero_components),
                dxbc::Src::R(is_signed_temp), dxbc::Src::R(signed_temp),
                dxbc::Src::R(layer_value_temp));
            // Release signed_temp.
            PopSystemTemp();
          }
          a_.OpEndIf();
          if (layer) {
            assert_true(layer_value_temp != system_temp_result_);
            // Interpolate between the two layers.
            a_.OpAdd(
                dxbc::Dest::R(layer_value_temp, used_result_nonzero_components),
                dxbc::Src::R(layer_value_temp),
                -dxbc::Src::R(system_temp_result_));
            a_.OpMAd(dxbc::Dest::R(system_temp_result_,
                                   used_result_nonzero_components),
                     dxbc::Src::R(layer_value_temp), layer_lerp_factor_src,
                     dxbc::Src::R(system_temp_result_));
            // Close the linear filtering check.
            a_.OpEndIf();
            // Release the allocated layer_value_temp.
            PopSystemTemp();
          }
        }
      }
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        // Close the stacked/3D check.
        a_.OpEndIf();
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
        dxbc::Dest component_dest(dxbc::Dest::R(system_temp_result_, 1 << i));
        dxbc::Src component_src(dxbc::Src::R(system_temp_result_).Select(i));
        a_.OpSwitch(dxbc::Src::R(signs_temp).Select(i));
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::TextureSign::kUnsignedBiased)));
        a_.OpMAd(component_dest, component_src, dxbc::Src::LF(2.0f),
                 dxbc::Src::LF(-1.0f));
        a_.OpBreak();
        a_.OpCase(dxbc::Src::LU(uint32_t(xenos::TextureSign::kGamma)));
        uint32_t gamma_temp = PushSystemTemp();
        if (gamma_render_target_as_srgb_) {
          // Check if the texture has sRGB rather that piecewise linear gamma.
          // More likely that it's just a texture with PWL, put this case in the
          // `if`, with `else` for sRGB resolved render targets.
          a_.OpAnd(
              dxbc::Dest::R(gamma_temp, 0b0001),
              LoadSystemConstant(SystemConstants::Index::kTexturesResolved,
                                 offsetof(SystemConstants, textures_resolved),
                                 dxbc::Src::kXXXX),
              dxbc::Src::LU(uint32_t(1) << tfetch_index));
          a_.OpIf(false, dxbc::Src::R(gamma_temp, dxbc::Src::kXXXX));
        }
        // Convert from piecewise linear.
        PWLGammaToLinear(system_temp_result_, i, system_temp_result_, i, false,
                         gamma_temp, 0, gamma_temp, 1);
        if (gamma_render_target_as_srgb_) {
          a_.OpElse();
          // Convert from sRGB.
          a_.OpMov(component_dest, component_src, true);
          a_.OpGE(dxbc::Dest::R(gamma_temp, 0b0001),
                  dxbc::Src::LF(RenderTargetCache::kSrgbToLinearThreshold),
                  component_src);
          a_.OpIf(true, dxbc::Src::R(gamma_temp, dxbc::Src::kXXXX));
          // sRGB <= kSrgbToLinearThreshold case - linear scale.
          a_.OpMul(component_dest, component_src,
                   dxbc::Src::LF(1.0f /
                                 RenderTargetCache::kSrgbToLinearDenominator1));
          a_.OpElse();
          // sRGB > kSrgbToLinearThreshold case.
          // 0 and 1 must be exactly achievable - only convert when the
          // saturated value is < 1.
          a_.OpLT(dxbc::Dest::R(gamma_temp, 0b0001), component_src,
                  dxbc::Src::LF(1.0f));
          a_.OpIf(true, dxbc::Src::R(gamma_temp, dxbc::Src::kXXXX));
          a_.OpMAd(component_dest, component_src,
                   dxbc::Src::LF(1.0f /
                                 RenderTargetCache::kSrgbToLinearDenominator2),
                   dxbc::Src::LF(RenderTargetCache::kSrgbToLinearOffset /
                                 RenderTargetCache::kSrgbToLinearDenominator2));
          a_.OpLog(component_dest, component_src);
          a_.OpMul(component_dest, component_src,
                   dxbc::Src::LF(RenderTargetCache::kSrgbToLinearExponent));
          a_.OpExp(component_dest, component_src);
          // Close the < 1 check.
          a_.OpEndIf();
          // Close the sRGB <= kSrgbToLinearThreshold check.
          a_.OpEndIf();
          // Close the PWL or sRGB check.
          a_.OpEndIf();
        }
        // Release gamma_temp.
        PopSystemTemp();
        a_.OpBreak();
        a_.OpEndSwitch();
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
    a_.OpIBFE(dxbc::Dest::R(exp_adjust_temp, 0b0001), dxbc::Src::LU(6),
              dxbc::Src::LU(13),
              RequestTextureFetchConstantWord(tfetch_index, 3));
    a_.OpIMAd(dxbc::Dest::R(exp_adjust_temp, 0b0001),
              dxbc::Src::R(exp_adjust_temp, dxbc::Src::kXXXX),
              dxbc::Src::LI(int32_t(1) << 23), dxbc::Src::LF(1.0f));
    a_.OpMul(dxbc::Dest::R(system_temp_result_, used_result_nonzero_components),
             dxbc::Src::R(system_temp_result_),
             dxbc::Src::R(exp_adjust_temp, dxbc::Src::kXXXX));
    // Release exp_adjust_temp.
    PopSystemTemp();
  }

  uint32_t used_result_zero_components =
      used_result_components & ~used_result_nonzero_components;
  if (used_result_zero_components) {
    a_.OpMov(dxbc::Dest::R(system_temp_result_, used_result_zero_components),
             dxbc::Src::LF(0.0f));
  }
  StoreResult(instr.result, dxbc::Src::R(system_temp_result_));
}

}  // namespace gpu
}  // namespace xe
