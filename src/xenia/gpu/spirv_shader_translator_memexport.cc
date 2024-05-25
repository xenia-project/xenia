/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/gpu/ucode.h"

namespace xe {
namespace gpu {

void SpirvShaderTranslator::ExportToMemory(uint8_t export_eM) {
  if (!export_eM) {
    return;
  }

  assert_zero(export_eM & ~current_shader().memexport_eM_written());

  if (!IsMemoryExportSupported()) {
    return;
  }

  // Check if memory export is allowed in this guest shader invocation.
  std::optional<SpirvBuilder::IfBuilder> if_memexport_allowed;
  if (main_memexport_allowed_ != spv::NoResult) {
    if_memexport_allowed.emplace(main_memexport_allowed_,
                                 spv::SelectionControlDontFlattenMask,
                                 *builder_);
  }

  // If the pixel was killed (but the actual killing on the SPIR-V side has not
  // been performed yet because the device doesn't support demotion to helper
  // invocation that doesn't interfere with control flow), the current
  // invocation is not considered active anymore.
  std::optional<SpirvBuilder::IfBuilder> if_pixel_not_killed;
  if (var_main_kill_pixel_ != spv::NoResult) {
    if_pixel_not_killed.emplace(
        builder_->createUnaryOp(
            spv::OpLogicalNot, type_bool_,
            builder_->createLoad(var_main_kill_pixel_, spv::NoPrecision)),
        spv::SelectionControlDontFlattenMask, *builder_);
  }

  // Check if the address with the correct sign and exponent was written, and
  // that the index doesn't overflow the mantissa bits.
  // all((eA_vector >> uvec4(30, 23, 23, 23)) == uvec4(0x1, 0x96, 0x96, 0x96))
  spv::Id eA_vector = builder_->createUnaryOp(
      spv::OpBitcast, type_uint4_,
      builder_->createLoad(var_main_memexport_address_, spv::NoPrecision));
  id_vector_temp_.clear();
  id_vector_temp_.push_back(builder_->makeUintConstant(30));
  id_vector_temp_.push_back(builder_->makeUintConstant(23));
  id_vector_temp_.push_back(id_vector_temp_.back());
  id_vector_temp_.push_back(id_vector_temp_.back());
  spv::Id address_validation_shift =
      builder_->makeCompositeConstant(type_uint4_, id_vector_temp_);
  id_vector_temp_.clear();
  id_vector_temp_.push_back(builder_->makeUintConstant(0x1));
  id_vector_temp_.push_back(builder_->makeUintConstant(0x96));
  id_vector_temp_.push_back(id_vector_temp_.back());
  id_vector_temp_.push_back(id_vector_temp_.back());
  spv::Id address_validation_value =
      builder_->makeCompositeConstant(type_uint4_, id_vector_temp_);
  SpirvBuilder::IfBuilder if_address_valid(
      builder_->createUnaryOp(
          spv::OpAll, type_bool_,
          builder_->createBinOp(
              spv::OpIEqual, type_bool4_,
              builder_->createBinOp(spv::OpShiftRightLogical, type_uint4_,
                                    eA_vector, address_validation_shift),
              address_validation_value)),
      spv::SelectionControlDontFlattenMask, *builder_, 2, 1);

  using EMIdArray = std::array<spv::Id, ucode::kMaxMemExportElementCount>;

  auto for_each_eM = [&](std::function<void(uint32_t eM_index)> fn) {
    uint8_t eM_remaining = export_eM;
    uint32_t eM_index;
    while (xe::bit_scan_forward(eM_remaining, &eM_index)) {
      eM_remaining &= ~(uint8_t(1) << eM_index);
      fn(eM_index);
    }
  };

  // Load the original eM.
  EMIdArray eM_original;
  for_each_eM([&](uint32_t eM_index) {
    eM_original[eM_index] = builder_->createLoad(
        var_main_memexport_data_[eM_index], spv::NoPrecision);
  });

  // Swap red and blue if needed.
  spv::Id format_info =
      builder_->createCompositeExtract(eA_vector, type_uint_, 2);
  spv::Id swap_red_blue = builder_->createBinOp(
      spv::OpINotEqual, type_bool_,
      builder_->createBinOp(spv::OpBitwiseAnd, type_uint_, format_info,
                            builder_->makeUintConstant(uint32_t(1) << 19)),
      const_uint_0_);
  EMIdArray eM_swapped;
  uint_vector_temp_.clear();
  uint_vector_temp_.push_back(2);
  uint_vector_temp_.push_back(1);
  uint_vector_temp_.push_back(0);
  uint_vector_temp_.push_back(3);
  for_each_eM([&](uint32_t eM_index) {
    eM_swapped[eM_index] = builder_->createTriOp(
        spv::OpSelect, type_float4_, swap_red_blue,
        builder_->createRvalueSwizzle(spv::NoPrecision, type_float4_,
                                      eM_original[eM_index], uint_vector_temp_),
        eM_original[eM_index]);
  });

  // Extract the numeric format.
  spv::Id is_signed = builder_->createBinOp(
      spv::OpINotEqual, type_bool_,
      builder_->createBinOp(spv::OpBitwiseAnd, type_uint_, format_info,
                            builder_->makeUintConstant(uint32_t(1) << 16)),
      const_uint_0_);
  spv::Id is_norm = builder_->createBinOp(
      spv::OpIEqual, type_bool_,
      builder_->createBinOp(spv::OpBitwiseAnd, type_uint_, format_info,
                            builder_->makeUintConstant(uint32_t(1) << 17)),
      const_uint_0_);

  // Perform format packing.

  auto flush_nan = [&](const EMIdArray& eM) -> EMIdArray {
    EMIdArray eM_flushed;
    for_each_eM([&](uint32_t eM_index) {
      spv::Id element_unflushed = eM[eM_index];
      unsigned int component_count =
          builder_->getNumComponents(element_unflushed);
      eM_flushed[eM_index] = builder_->createTriOp(
          spv::OpSelect, type_float_vectors_[component_count - 1],
          builder_->createUnaryOp(spv::OpIsNan,
                                  type_bool_vectors_[component_count - 1],
                                  element_unflushed),
          const_float_vectors_0_[component_count - 1], element_unflushed);
    });
    return eM_flushed;
  };

  auto make_float_constant_vectors =
      [&](float value) -> std::array<spv::Id, 4> {
    std::array<spv::Id, 4> const_vectors;
    const_vectors[0] = builder_->makeFloatConstant(value);
    id_vector_temp_.clear();
    id_vector_temp_.push_back(const_vectors[0]);
    for (unsigned int component_count_minus_1 = 1; component_count_minus_1 < 4;
         ++component_count_minus_1) {
      id_vector_temp_.push_back(const_vectors[0]);
      const_vectors[component_count_minus_1] = builder_->makeCompositeConstant(
          type_float_vectors_[component_count_minus_1], id_vector_temp_);
    }
    return const_vectors;
  };
  std::array<spv::Id, 4> const_float_vectors_minus_1 =
      make_float_constant_vectors(-1.0f);
  std::array<spv::Id, 4> const_float_vectors_minus_0_5 =
      make_float_constant_vectors(-0.5f);
  std::array<spv::Id, 4> const_float_vectors_0_5 =
      make_float_constant_vectors(0.5f);

  // The widths must be without holes (R, RG, RGB, RGBA), and expecting the
  // widths to add up to the size of the stored texel (8, 16 or 32 bits), as the
  // unused upper bits will contain junk from the sign extension of X if the
  // number is signed.
  auto pack_8_16_32 = [&](std::array<uint32_t, 4> widths) -> EMIdArray {
    unsigned int component_count;
    std::array<uint32_t, 4> offsets{};
    for (component_count = 0; component_count < widths.size();
         ++component_count) {
      if (!widths[component_count]) {
        break;
      }
      // Only formats for which max + 0.5 can be represented exactly.
      assert(widths[component_count] <= 23);
      if (component_count) {
        offsets[component_count] =
            offsets[component_count - 1] + widths[component_count - 1];
      }
    }
    assert_not_zero(component_count);

    // Extract the needed components.
    EMIdArray eM_unflushed = eM_swapped;
    if (component_count < 4) {
      if (component_count == 1) {
        for_each_eM([&](uint32_t eM_index) {
          eM_unflushed[eM_index] = builder_->createCompositeExtract(
              eM_unflushed[eM_index], type_float_, 0);
        });
      } else {
        uint_vector_temp_.clear();
        for (unsigned int component_index = 0;
             component_index < component_count; ++component_index) {
          uint_vector_temp_.push_back(component_index);
        }
        for_each_eM([&](uint32_t eM_index) {
          eM_unflushed[eM_index] = builder_->createRvalueSwizzle(
              spv::NoPrecision, type_float_vectors_[component_count - 1],
              eM_unflushed[eM_index], uint_vector_temp_);
        });
      }
    }

    // Flush NaNs.
    EMIdArray eM_flushed = flush_nan(eM_unflushed);

    // Convert to integers.
    SpirvBuilder::IfBuilder if_signed(
        is_signed, spv::SelectionControlDontFlattenMask, *builder_);
    EMIdArray eM_signed;
    {
      // Signed.
      SpirvBuilder::IfBuilder if_norm(
          is_norm, spv::SelectionControlDontFlattenMask, *builder_);
      EMIdArray eM_norm;
      {
        // Signed normalized.
        id_vector_temp_.clear();
        for (unsigned int component_index = 0;
             component_index < component_count; ++component_index) {
          id_vector_temp_.push_back(builder_->makeFloatConstant(
              float((uint32_t(1) << (widths[component_index] - 1)) - 1)));
        }
        spv::Id const_max_value =
            component_count > 1
                ? builder_->makeCompositeConstant(
                      type_float_vectors_[component_count - 1], id_vector_temp_)
                : id_vector_temp_.front();
        for_each_eM([&](uint32_t eM_index) {
          eM_norm[eM_index] = builder_->createNoContractionBinOp(
              spv::OpFMul, type_float_vectors_[component_count - 1],
              builder_->createTriBuiltinCall(
                  type_float_vectors_[component_count - 1],
                  ext_inst_glsl_std_450_, GLSLstd450FClamp,
                  eM_flushed[eM_index],
                  const_float_vectors_minus_1[component_count - 1],
                  const_float_vectors_1_[component_count - 1]),
              const_max_value);
        });
      }
      if_norm.makeEndIf();
      // All phi instructions must be in the beginning of the block.
      for_each_eM([&](uint32_t eM_index) {
        eM_signed[eM_index] =
            if_norm.createMergePhi(eM_norm[eM_index], eM_flushed[eM_index]);
      });
      // Convert to signed integer, adding plus/minus 0.5 before truncating
      // according to the Direct3D format conversion rules.
      for_each_eM([&](uint32_t eM_index) {
        eM_signed[eM_index] = builder_->createUnaryOp(
            spv::OpBitcast, type_uint_vectors_[component_count - 1],
            builder_->createUnaryOp(
                spv::OpConvertFToS, type_int_vectors_[component_count - 1],
                builder_->createNoContractionBinOp(
                    spv::OpFAdd, type_float_vectors_[component_count - 1],
                    eM_signed[eM_index],
                    builder_->createTriOp(
                        spv::OpSelect, type_float_vectors_[component_count - 1],
                        builder_->createBinOp(
                            spv::OpFOrdLessThan,
                            type_bool_vectors_[component_count - 1],
                            eM_signed[eM_index],
                            const_float_vectors_0_[component_count - 1]),
                        const_float_vectors_minus_0_5[component_count - 1],
                        const_float_vectors_0_5[component_count - 1]))));
      });
    }
    if_signed.makeBeginElse();
    EMIdArray eM_unsigned;
    {
      SpirvBuilder::IfBuilder if_norm(
          is_norm, spv::SelectionControlDontFlattenMask, *builder_);
      EMIdArray eM_norm;
      {
        // Unsigned normalized.
        id_vector_temp_.clear();
        for (unsigned int component_index = 0;
             component_index < component_count; ++component_index) {
          id_vector_temp_.push_back(builder_->makeFloatConstant(
              float((uint32_t(1) << widths[component_index]) - 1)));
        }
        spv::Id const_max_value =
            component_count > 1
                ? builder_->makeCompositeConstant(
                      type_float_vectors_[component_count - 1], id_vector_temp_)
                : id_vector_temp_.front();
        for_each_eM([&](uint32_t eM_index) {
          eM_norm[eM_index] = builder_->createNoContractionBinOp(
              spv::OpFMul, type_float_vectors_[component_count - 1],
              builder_->createTriBuiltinCall(
                  type_float_vectors_[component_count - 1],
                  ext_inst_glsl_std_450_, GLSLstd450FClamp,
                  eM_flushed[eM_index],
                  const_float_vectors_0_[component_count - 1],
                  const_float_vectors_1_[component_count - 1]),
              const_max_value);
        });
      }
      if_norm.makeEndIf();
      // All phi instructions must be in the beginning of the block.
      for_each_eM([&](uint32_t eM_index) {
        eM_unsigned[eM_index] =
            if_norm.createMergePhi(eM_norm[eM_index], eM_flushed[eM_index]);
      });
      // Convert to unsigned integer, adding 0.5 before truncating according to
      // the Direct3D format conversion rules.
      for_each_eM([&](uint32_t eM_index) {
        eM_unsigned[eM_index] = builder_->createUnaryOp(
            spv::OpConvertFToU, type_uint_vectors_[component_count - 1],
            builder_->createNoContractionBinOp(
                spv::OpFAdd, type_float_vectors_[component_count - 1],
                eM_unsigned[eM_index],
                const_float_vectors_0_5[component_count - 1]));
      });
    }
    if_signed.makeEndIf();
    EMIdArray eM_unpacked;
    for_each_eM([&](uint32_t eM_index) {
      eM_unpacked[eM_index] =
          if_signed.createMergePhi(eM_signed[eM_index], eM_unsigned[eM_index]);
    });

    // Pack into a 32-bit value, and pad to a 4-component vector for the phi.
    EMIdArray eM_packed;
    for_each_eM([&](uint32_t eM_index) {
      spv::Id element_unpacked = eM_unpacked[eM_index];
      eM_packed[eM_index] = component_count > 1
                                ? builder_->createCompositeExtract(
                                      element_unpacked, type_uint_, 0)
                                : element_unpacked;
      for (unsigned int component_index = 1; component_index < component_count;
           ++component_index) {
        eM_packed[eM_index] = builder_->createQuadOp(
            spv::OpBitFieldInsert, type_uint_, eM_packed[eM_index],
            builder_->createCompositeExtract(element_unpacked, type_uint_,
                                             component_index),
            builder_->makeUintConstant(offsets[component_index]),
            builder_->makeUintConstant(widths[component_index]));
      }
      id_vector_temp_.clear();
      id_vector_temp_.resize(4, const_uint_0_);
      id_vector_temp_.front() = eM_packed[eM_index];
      eM_packed[eM_index] =
          builder_->createCompositeConstruct(type_uint4_, id_vector_temp_);
    });

    return eM_packed;
  };

  SpirvBuilder::SwitchBuilder format_switch(
      builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_, format_info,
                            builder_->makeUintConstant(8),
                            builder_->makeUintConstant(6)),
      spv::SelectionControlDontFlattenMask, *builder_);

  struct FormatCase {
    EMIdArray eM_packed;
    uint32_t element_bytes_log2;
    spv::Id phi_parent;
  };
  std::vector<FormatCase> format_cases;
  // Must be called at the end of the switch case segment for the correct phi
  // parent.
  auto add_format_case = [&](const EMIdArray& eM_packed,
                             uint32_t element_bytes_log2) {
    FormatCase& format_case = format_cases.emplace_back();
    format_case.eM_packed = eM_packed;
    format_case.element_bytes_log2 = element_bytes_log2;
    format_case.phi_parent = builder_->getBuildPoint()->getId();
  };

  // k_8, k_8_A, k_8_B
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_8));
  // TODO(Triang3l): Investigate how input should be treated for k_8_A, k_8_B.
  format_switch.addCurrentCaseLiteral(
      static_cast<unsigned int>(xenos::ColorFormat::k_8_A));
  format_switch.addCurrentCaseLiteral(
      static_cast<unsigned int>(xenos::ColorFormat::k_8_B));
  add_format_case(pack_8_16_32({8}), 0);

  // k_1_5_5_5
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_1_5_5_5));
  add_format_case(pack_8_16_32({5, 5, 5, 1}), 1);

  // k_5_6_5
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_5_6_5));
  add_format_case(pack_8_16_32({5, 6, 5}), 1);

  // k_6_5_5
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_6_5_5));
  add_format_case(pack_8_16_32({5, 5, 6}), 1);

  // k_8_8_8_8, k_8_8_8_8_A, k_8_8_8_8_AS_16_16_16_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_8_8_8_8));
  // TODO(Triang3l): Investigate how input should be treated for k_8_8_8_8_A.
  format_switch.addCurrentCaseLiteral(
      static_cast<unsigned int>(xenos::ColorFormat::k_8_8_8_8_A));
  format_switch.addCurrentCaseLiteral(
      static_cast<unsigned int>(xenos::ColorFormat::k_8_8_8_8_AS_16_16_16_16));
  add_format_case(pack_8_16_32({8, 8, 8, 8}), 2);

  // k_2_10_10_10, k_2_10_10_10_AS_16_16_16_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_2_10_10_10));
  format_switch.addCurrentCaseLiteral(static_cast<unsigned int>(
      xenos::ColorFormat::k_2_10_10_10_AS_16_16_16_16));
  add_format_case(pack_8_16_32({10, 10, 10, 2}), 2);

  // k_8_8
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_8_8));
  add_format_case(pack_8_16_32({8, 8}), 1);

  // k_4_4_4_4
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_4_4_4_4));
  add_format_case(pack_8_16_32({4, 4, 4, 4}), 1);

  // k_10_11_11, k_10_11_11_AS_16_16_16_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_10_11_11));
  format_switch.addCurrentCaseLiteral(
      static_cast<unsigned int>(xenos::ColorFormat::k_10_11_11_AS_16_16_16_16));
  add_format_case(pack_8_16_32({11, 11, 10}), 2);

  // k_11_11_10, k_11_11_10_AS_16_16_16_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_11_11_10));
  format_switch.addCurrentCaseLiteral(
      static_cast<unsigned int>(xenos::ColorFormat::k_11_11_10_AS_16_16_16_16));
  add_format_case(pack_8_16_32({10, 11, 11}), 2);

  // k_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_16));
  add_format_case(pack_8_16_32({16}), 1);

  // k_16_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_16_16));
  add_format_case(pack_8_16_32({16, 16}), 2);

  // k_16_16_16_16
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_16_16_16_16));
  {
    // Flush NaNs.
    EMIdArray fixed16_flushed = flush_nan(eM_swapped);

    // Convert to integers.
    SpirvBuilder::IfBuilder if_signed(
        is_signed, spv::SelectionControlDontFlattenMask, *builder_);
    EMIdArray fixed16_signed;
    {
      // Signed.
      SpirvBuilder::IfBuilder if_norm(
          is_norm, spv::SelectionControlDontFlattenMask, *builder_);
      EMIdArray fixed16_norm;
      {
        // Signed normalized.
        id_vector_temp_.clear();
        id_vector_temp_.resize(4, builder_->makeFloatConstant(
                                      float((uint32_t(1) << (16 - 1)) - 1)));
        spv::Id const_snorm16_max_value =
            builder_->makeCompositeConstant(type_float4_, id_vector_temp_);
        for_each_eM([&](uint32_t eM_index) {
          fixed16_norm[eM_index] = builder_->createNoContractionBinOp(
              spv::OpFMul, type_float4_,
              builder_->createTriBuiltinCall(
                  type_float4_, ext_inst_glsl_std_450_, GLSLstd450FClamp,
                  fixed16_flushed[eM_index], const_float_vectors_minus_1[3],
                  const_float4_1_),
              const_snorm16_max_value);
        });
      }
      if_norm.makeEndIf();
      // All phi instructions must be in the beginning of the block.
      for_each_eM([&](uint32_t eM_index) {
        fixed16_signed[eM_index] = if_norm.createMergePhi(
            fixed16_norm[eM_index], fixed16_flushed[eM_index]);
      });
      // Convert to signed integer, adding plus/minus 0.5 before truncating
      // according to the Direct3D format conversion rules.
      for_each_eM([&](uint32_t eM_index) {
        fixed16_signed[eM_index] = builder_->createUnaryOp(
            spv::OpBitcast, type_uint4_,
            builder_->createUnaryOp(
                spv::OpConvertFToS, type_int4_,
                builder_->createNoContractionBinOp(
                    spv::OpFAdd, type_float4_, fixed16_signed[eM_index],
                    builder_->createTriOp(
                        spv::OpSelect, type_float4_,
                        builder_->createBinOp(spv::OpFOrdLessThan, type_bool4_,
                                              fixed16_signed[eM_index],
                                              const_float4_0_),
                        const_float_vectors_minus_0_5[3],
                        const_float_vectors_0_5[3]))));
      });
    }
    if_signed.makeBeginElse();
    EMIdArray fixed16_unsigned;
    {
      // Unsigned.
      SpirvBuilder::IfBuilder if_norm(
          is_norm, spv::SelectionControlDontFlattenMask, *builder_);
      EMIdArray fixed16_norm;
      {
        // Unsigned normalized.
        id_vector_temp_.clear();
        id_vector_temp_.resize(
            4, builder_->makeFloatConstant(float((uint32_t(1) << 16) - 1)));
        spv::Id const_unorm16_max_value =
            builder_->makeCompositeConstant(type_float4_, id_vector_temp_);
        for_each_eM([&](uint32_t eM_index) {
          fixed16_norm[eM_index] = builder_->createNoContractionBinOp(
              spv::OpFMul, type_float4_,
              builder_->createTriBuiltinCall(
                  type_float4_, ext_inst_glsl_std_450_, GLSLstd450FClamp,
                  fixed16_flushed[eM_index], const_float4_0_, const_float4_1_),
              const_unorm16_max_value);
        });
      }
      if_norm.makeEndIf();
      // All phi instructions must be in the beginning of the block.
      for_each_eM([&](uint32_t eM_index) {
        fixed16_unsigned[eM_index] = if_norm.createMergePhi(
            fixed16_norm[eM_index], fixed16_flushed[eM_index]);
      });
      // Convert to unsigned integer, adding 0.5 before truncating according to
      // the Direct3D format conversion rules.
      for_each_eM([&](uint32_t eM_index) {
        fixed16_unsigned[eM_index] = builder_->createUnaryOp(
            spv::OpConvertFToU, type_uint4_,
            builder_->createNoContractionBinOp(spv::OpFAdd, type_float4_,
                                               fixed16_unsigned[eM_index],
                                               const_float_vectors_0_5[3]));
      });
    }
    if_signed.makeEndIf();
    EMIdArray fixed16_unpacked;
    for_each_eM([&](uint32_t eM_index) {
      fixed16_unpacked[eM_index] = if_signed.createMergePhi(
          fixed16_signed[eM_index], fixed16_unsigned[eM_index]);
    });

    // Pack into two 32-bit values, and pad to a 4-component vector for the phi.
    EMIdArray fixed16_packed;
    spv::Id const_uint_16 = builder_->makeUintConstant(16);
    for_each_eM([&](uint32_t eM_index) {
      spv::Id fixed16_element_unpacked = fixed16_unpacked[eM_index];
      id_vector_temp_.clear();
      for (uint32_t component_index = 0; component_index < 2;
           ++component_index) {
        id_vector_temp_.push_back(builder_->createQuadOp(
            spv::OpBitFieldInsert, type_uint_,
            builder_->createCompositeExtract(fixed16_element_unpacked,
                                             type_uint_, 2 * component_index),
            builder_->createCompositeExtract(
                fixed16_element_unpacked, type_uint_, 2 * component_index + 1),
            const_uint_16, const_uint_16));
      }
      for (uint32_t component_index = 2; component_index < 4;
           ++component_index) {
        id_vector_temp_.push_back(const_uint_0_);
      }
      fixed16_packed[eM_index] =
          builder_->createCompositeConstruct(type_uint4_, id_vector_temp_);
    });

    add_format_case(fixed16_packed, 3);
  }

  // TODO(Triang3l): Use the extended range float16 conversion.

  // k_16_FLOAT
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_16_FLOAT));
  {
    EMIdArray format_packed_16_float;
    for_each_eM([&](uint32_t eM_index) {
      id_vector_temp_.clear();
      id_vector_temp_.push_back(builder_->createCompositeExtract(
          eM_swapped[eM_index], type_float_, 0));
      id_vector_temp_.push_back(const_float_0_);
      spv::Id format_packed_16_float_x = builder_->createUnaryBuiltinCall(
          type_uint_, ext_inst_glsl_std_450_, GLSLstd450PackHalf2x16,
          builder_->createCompositeConstruct(type_float2_, id_vector_temp_));
      id_vector_temp_.clear();
      id_vector_temp_.resize(4, const_uint_0_);
      id_vector_temp_.front() = format_packed_16_float_x;
      format_packed_16_float[eM_index] =
          builder_->createCompositeConstruct(type_uint4_, id_vector_temp_);
    });
    add_format_case(format_packed_16_float, 1);
  }

  // k_16_16_FLOAT
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_16_16_FLOAT));
  {
    EMIdArray format_packed_16_16_float;
    for_each_eM([&](uint32_t eM_index) {
      uint_vector_temp_.clear();
      uint_vector_temp_.push_back(0);
      uint_vector_temp_.push_back(1);
      spv::Id format_packed_16_16_float_xy = builder_->createUnaryBuiltinCall(
          type_uint_, ext_inst_glsl_std_450_, GLSLstd450PackHalf2x16,
          builder_->createRvalueSwizzle(spv::NoPrecision, type_float2_,
                                        eM_swapped[eM_index],
                                        uint_vector_temp_));
      id_vector_temp_.clear();
      id_vector_temp_.resize(4, const_uint_0_);
      id_vector_temp_.front() = format_packed_16_16_float_xy;
      format_packed_16_16_float[eM_index] =
          builder_->createCompositeConstruct(type_uint4_, id_vector_temp_);
    });
    add_format_case(format_packed_16_16_float, 2);
  }

  // k_16_16_16_16_FLOAT
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_16_16_16_16_FLOAT));
  {
    EMIdArray format_packed_16_16_16_16_float;
    for_each_eM([&](uint32_t eM_index) {
      spv::Id format_packed_16_16_16_16_float_xy_zw[2];
      for (uint32_t component_index = 0; component_index < 2;
           ++component_index) {
        uint_vector_temp_.clear();
        uint_vector_temp_.push_back(2 * component_index);
        uint_vector_temp_.push_back(2 * component_index + 1);
        format_packed_16_16_16_16_float_xy_zw[component_index] =
            builder_->createUnaryBuiltinCall(
                type_uint_, ext_inst_glsl_std_450_, GLSLstd450PackHalf2x16,
                builder_->createRvalueSwizzle(spv::NoPrecision, type_float2_,
                                              eM_swapped[eM_index],
                                              uint_vector_temp_));
      }
      id_vector_temp_.clear();
      id_vector_temp_.push_back(format_packed_16_16_16_16_float_xy_zw[0]);
      id_vector_temp_.push_back(format_packed_16_16_16_16_float_xy_zw[1]);
      id_vector_temp_.push_back(const_uint_0_);
      id_vector_temp_.push_back(const_uint_0_);
      format_packed_16_16_16_16_float[eM_index] =
          builder_->createCompositeConstruct(type_uint4_, id_vector_temp_);
    });
    add_format_case(format_packed_16_16_16_16_float, 3);
  }

  // k_32_FLOAT
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_32_FLOAT));
  {
    EMIdArray format_packed_32_float;
    for_each_eM([&](uint32_t eM_index) {
      format_packed_32_float[eM_index] = builder_->createUnaryOp(
          spv::OpBitcast, type_uint4_, eM_swapped[eM_index]);
    });
    add_format_case(format_packed_32_float, 2);
  }

  // k_32_32_FLOAT
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_32_32_FLOAT));
  {
    EMIdArray format_packed_32_32_float;
    for_each_eM([&](uint32_t eM_index) {
      format_packed_32_32_float[eM_index] = builder_->createUnaryOp(
          spv::OpBitcast, type_uint4_, eM_swapped[eM_index]);
    });
    add_format_case(format_packed_32_32_float, 3);
  }

  // k_32_32_32_32_FLOAT
  format_switch.makeBeginCase(
      static_cast<unsigned int>(xenos::ColorFormat::k_32_32_32_32_FLOAT));
  {
    EMIdArray format_packed_32_32_32_32_float;
    for_each_eM([&](uint32_t eM_index) {
      format_packed_32_32_32_32_float[eM_index] = builder_->createUnaryOp(
          spv::OpBitcast, type_uint4_, eM_swapped[eM_index]);
    });
    add_format_case(format_packed_32_32_32_32_float, 4);
  }

  format_switch.makeEndSwitch();

  // Select the result and the element size based on the format.
  // Phi must be the first instructions in a block.
  EMIdArray eM_packed;
  for_each_eM([&](uint32_t eM_index) {
    auto eM_packed_phi = std::make_unique<spv::Instruction>(
        builder_->getUniqueId(), type_uint4_, spv::OpPhi);
    // Default case for an invalid format.
    eM_packed_phi->addIdOperand(const_uint4_0_);
    eM_packed_phi->addIdOperand(format_switch.getDefaultPhiParent());
    for (const FormatCase& format_case : format_cases) {
      eM_packed_phi->addIdOperand(format_case.eM_packed[eM_index]);
      eM_packed_phi->addIdOperand(format_case.phi_parent);
    }
    eM_packed[eM_index] = eM_packed_phi->getResultId();
    builder_->getBuildPoint()->addInstruction(std::move(eM_packed_phi));
  });
  spv::Id element_bytes_log2;
  {
    auto element_bytes_log2_phi = std::make_unique<spv::Instruction>(
        builder_->getUniqueId(), type_uint_, spv::OpPhi);
    // Default case for an invalid format (doesn't enter any element size
    // conditional, skipped).
    element_bytes_log2_phi->addIdOperand(builder_->makeUintConstant(5));
    element_bytes_log2_phi->addIdOperand(format_switch.getDefaultPhiParent());
    for (const FormatCase& format_case : format_cases) {
      element_bytes_log2_phi->addIdOperand(
          builder_->makeUintConstant(format_case.element_bytes_log2));
      element_bytes_log2_phi->addIdOperand(format_case.phi_parent);
    }
    element_bytes_log2 = element_bytes_log2_phi->getResultId();
    builder_->getBuildPoint()->addInstruction(
        std::move(element_bytes_log2_phi));
  }

  // Endian-swap.
  spv::Id endian =
      builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_, format_info,
                            const_uint_0_, builder_->makeUintConstant(3));
  for_each_eM([&](uint32_t eM_index) {
    eM_packed[eM_index] = EndianSwap128Uint4(eM_packed[eM_index], endian);
  });

  // Load the index of eM0 in the stream.
  spv::Id eM0_index = builder_->createTriOp(
      spv::OpBitFieldUExtract, type_uint_,
      builder_->createCompositeExtract(eA_vector, type_uint_, 1), const_uint_0_,
      builder_->makeUintConstant(23));

  // Check how many elements starting from eM0 are within the bounds of the
  // stream, and from the eM# that were written, exclude the out-of-bounds ones.
  // The index can't be negative, and the index and the count are limited to 23
  // bits, so it's safe to use 32-bit signed subtraction and clamping to get the
  // remaining eM# count.
  spv::Id eM_indices_to_store = builder_->createTriOp(
      spv::OpBitFieldUExtract, type_uint_,
      builder_->createLoad(var_main_memexport_data_written_, spv::NoPrecision),
      const_uint_0_,
      builder_->createUnaryOp(
          spv::OpBitcast, type_uint_,
          builder_->createTriBuiltinCall(
              type_int_, ext_inst_glsl_std_450_, GLSLstd450SClamp,
              builder_->createBinOp(
                  spv::OpISub, type_int_,
                  builder_->createUnaryOp(
                      spv::OpBitcast, type_int_,
                      builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_,
                                            builder_->createCompositeExtract(
                                                eA_vector, type_uint_, 3),
                                            const_uint_0_,
                                            builder_->makeUintConstant(23))),
                  builder_->createUnaryOp(spv::OpBitcast, type_int_,
                                          eM0_index)),
              const_int_0_,
              builder_->makeIntConstant(ucode::kMaxMemExportElementCount))));

  // Get the eM0 address in bytes.
  // Left-shift the stream base address by 2 to both convert it from dwords to
  // bytes and drop the upper bits.
  spv::Id const_uint_2 = builder_->makeUintConstant(2);
  spv::Id eM0_address_bytes = builder_->createBinOp(
      spv::OpIAdd, type_uint_,
      builder_->createBinOp(
          spv::OpShiftLeftLogical, type_uint_,
          builder_->createCompositeExtract(eA_vector, type_uint_, 0),
          const_uint_2),
      builder_->createBinOp(spv::OpShiftLeftLogical, type_uint_, eM0_index,
                            element_bytes_log2));

  // Store based on the element size.
  auto store_needed_eM = [&](std::function<void(uint32_t eM_index)> fn) {
    for_each_eM([&](uint32_t eM_index) {
      SpirvBuilder::IfBuilder if_eM_needed(
          builder_->createBinOp(
              spv::OpINotEqual, type_bool_,
              builder_->createBinOp(spv::OpBitwiseAnd, type_uint_,
                                    eM_indices_to_store,
                                    builder_->makeUintConstant(1u << eM_index)),
              const_uint_0_),
          spv::SelectionControlDontFlattenMask, *builder_, 2, 1);
      fn(eM_index);
      if_eM_needed.makeEndIf();
    });
  };
  SpirvBuilder::SwitchBuilder element_size_switch(
      element_bytes_log2, spv::SelectionControlDontFlattenMask, *builder_);
  element_size_switch.makeBeginCase(0);
  {
    store_needed_eM([&](uint32_t eM_index) {
      spv::Id element_address_bytes =
          eM_index != 0 ? builder_->createBinOp(
                              spv::OpIAdd, type_uint_, eM0_address_bytes,
                              builder_->makeUintConstant(eM_index))
                        : eM0_address_bytes;
      // replace_shift = 8 * (element_address_bytes & 3)
      spv::Id replace_shift = builder_->createQuadOp(
          spv::OpBitFieldInsert, type_uint_, const_uint_0_,
          element_address_bytes, builder_->makeUintConstant(3), const_uint_2);
      StoreUint32ToSharedMemory(
          builder_->createBinOp(spv::OpShiftLeftLogical, type_uint_,
                                builder_->createCompositeExtract(
                                    eM_packed[eM_index], type_uint_, 0),
                                replace_shift),
          builder_->createUnaryOp(
              spv::OpBitcast, type_int_,
              builder_->createBinOp(spv::OpShiftRightLogical, type_uint_,
                                    element_address_bytes, const_uint_2)),
          builder_->createBinOp(spv::OpShiftLeftLogical, type_uint_,
                                builder_->makeUintConstant(0xFFu),
                                replace_shift));
    });
  }
  element_size_switch.makeBeginCase(1);
  {
    spv::Id const_uint_1 = builder_->makeUintConstant(1);
    spv::Id eM0_address_words = builder_->createBinOp(
        spv::OpShiftRightLogical, type_uint_, eM0_address_bytes, const_uint_1);
    store_needed_eM([&](uint32_t eM_index) {
      spv::Id element_address_words =
          eM_index != 0 ? builder_->createBinOp(
                              spv::OpIAdd, type_uint_, eM0_address_words,
                              builder_->makeUintConstant(eM_index))
                        : eM0_address_words;
      // replace_shift = 16 * (element_address_words & 1)
      spv::Id replace_shift = builder_->createQuadOp(
          spv::OpBitFieldInsert, type_uint_, const_uint_0_,
          element_address_words, builder_->makeUintConstant(4), const_uint_1);
      StoreUint32ToSharedMemory(
          builder_->createBinOp(spv::OpShiftLeftLogical, type_uint_,
                                builder_->createCompositeExtract(
                                    eM_packed[eM_index], type_uint_, 0),
                                replace_shift),
          builder_->createUnaryOp(
              spv::OpBitcast, type_int_,
              builder_->createBinOp(spv::OpShiftRightLogical, type_uint_,
                                    element_address_words, const_uint_1)),
          builder_->createBinOp(spv::OpShiftLeftLogical, type_uint_,
                                builder_->makeUintConstant(0xFFFFu),
                                replace_shift));
    });
  }
  element_size_switch.makeBeginCase(2);
  {
    spv::Id eM0_address_dwords = builder_->createBinOp(
        spv::OpShiftRightLogical, type_uint_, eM0_address_bytes, const_uint_2);
    store_needed_eM([&](uint32_t eM_index) {
      StoreUint32ToSharedMemory(
          builder_->createCompositeExtract(eM_packed[eM_index], type_uint_, 0),
          builder_->createUnaryOp(
              spv::OpBitcast, type_int_,
              eM_index != 0 ? builder_->createBinOp(
                                  spv::OpIAdd, type_uint_, eM0_address_dwords,
                                  builder_->makeUintConstant(eM_index))
                            : eM0_address_dwords));
    });
  }
  element_size_switch.makeBeginCase(3);
  {
    spv::Id eM0_address_dwords = builder_->createBinOp(
        spv::OpShiftRightLogical, type_uint_, eM0_address_bytes, const_uint_2);
    store_needed_eM([&](uint32_t eM_index) {
      spv::Id element_value = eM_packed[eM_index];
      spv::Id element_address_dwords_int = builder_->createUnaryOp(
          spv::OpBitcast, type_int_,
          eM_index != 0 ? builder_->createBinOp(
                              spv::OpIAdd, type_uint_, eM0_address_dwords,
                              builder_->makeUintConstant(2 * eM_index))
                        : eM0_address_dwords);
      StoreUint32ToSharedMemory(
          builder_->createCompositeExtract(element_value, type_uint_, 0),
          element_address_dwords_int);
      StoreUint32ToSharedMemory(
          builder_->createCompositeExtract(element_value, type_uint_, 1),
          builder_->createBinOp(spv::OpIAdd, type_int_,
                                element_address_dwords_int,
                                builder_->makeIntConstant(1)));
    });
  }
  element_size_switch.makeBeginCase(4);
  {
    spv::Id eM0_address_dwords = builder_->createBinOp(
        spv::OpShiftRightLogical, type_uint_, eM0_address_bytes, const_uint_2);
    store_needed_eM([&](uint32_t eM_index) {
      spv::Id element_value = eM_packed[eM_index];
      spv::Id element_address_dwords_int = builder_->createUnaryOp(
          spv::OpBitcast, type_int_,
          eM_index != 0 ? builder_->createBinOp(
                              spv::OpIAdd, type_uint_, eM0_address_dwords,
                              builder_->makeUintConstant(4 * eM_index))
                        : eM0_address_dwords);
      StoreUint32ToSharedMemory(
          builder_->createCompositeExtract(element_value, type_uint_, 0),
          element_address_dwords_int);
      for (uint32_t element_dword_index = 1; element_dword_index < 4;
           ++element_dword_index) {
        StoreUint32ToSharedMemory(
            builder_->createCompositeExtract(element_value, type_uint_,
                                             element_dword_index),
            builder_->createBinOp(spv::OpIAdd, type_int_,
                                  element_address_dwords_int,
                                  builder_->makeIntConstant(
                                      static_cast<int>(element_dword_index))));
      }
    });
  }
  element_size_switch.makeEndSwitch();

  // Close the conditionals for whether memory export is allowed in this
  // invocation.
  if_address_valid.makeEndIf();
  if (if_pixel_not_killed.has_value()) {
    if_pixel_not_killed->makeEndIf();
  }
  if (if_memexport_allowed.has_value()) {
    if_memexport_allowed->makeEndIf();
  }
}

}  // namespace gpu
}  // namespace xe
