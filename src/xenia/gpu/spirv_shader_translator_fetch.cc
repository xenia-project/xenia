/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <climits>
#include <cmath>
#include <memory>
#include <sstream>
#include <utility>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {

void SpirvShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition);

  uint32_t used_result_components = instr.result.GetUsedResultComponents();
  uint32_t needed_words = xenos::GetVertexFormatNeededWords(
      instr.attributes.data_format, used_result_components);
  // If this is vfetch_full, the address may still be needed for vfetch_mini -
  // don't exit before calculating the address.
  if (!needed_words && instr.is_mini_fetch) {
    // Nothing to load - just constant 0/1 writes, or the swizzle includes only
    // components that don't exist in the format (writing zero instead of them).
    // Unpacking assumes at least some word is needed.
    StoreResult(instr.result, spv::NoResult);
    return;
  }

  EnsureBuildPointAvailable();

  uint32_t fetch_constant_word_0_index = instr.operands[1].storage_index << 1;

  spv::Id address;
  if (instr.is_mini_fetch) {
    // `base + index * stride` loaded by vfetch_full.
    address = builder_->createLoad(var_main_vfetch_address_, spv::NoPrecision);
  } else {
    // Get the base address in dwords from the bits 2:31 of the first fetch
    // constant word.
    id_vector_temp_.clear();
    // The only element of the fetch constant buffer.
    id_vector_temp_.push_back(const_int_0_);
    // Vector index.
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int(fetch_constant_word_0_index >> 2)));
    // Component index.
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int(fetch_constant_word_0_index & 3)));
    spv::Id fetch_constant_word_0 = builder_->createLoad(
        builder_->createAccessChain(spv::StorageClassUniform,
                                    uniform_fetch_constants_, id_vector_temp_),
        spv::NoPrecision);
    // TODO(Triang3l): Verify the fetch constant type (that it's a vertex fetch,
    // not a texture fetch) here instead of dropping draws with invalid vertex
    // fetch constants on the CPU when proper bound checks are added - vfetch
    // may be conditional, so fetch constants may also be used conditionally.
    address = builder_->createUnaryOp(
        spv::OpBitcast, type_int_,
        builder_->createBinOp(spv::OpShiftRightLogical, type_uint_,
                              fetch_constant_word_0,
                              builder_->makeUintConstant(2)));
    if (instr.attributes.stride) {
      // Convert the index to an integer by flooring or by rounding to the
      // nearest (as floor(index + 0.5) because rounding to the nearest even
      // makes no sense for addressing, both 1.5 and 2.5 would be 2).
      spv::Id index = GetOperandComponents(
          LoadOperandStorage(instr.operands[0]), instr.operands[0], 0b0001);
      if (instr.attributes.is_index_rounded) {
        index = builder_->createNoContractionBinOp(
            spv::OpFAdd, type_float_, index, builder_->makeFloatConstant(0.5f));
      }
      index = builder_->createUnaryOp(
          spv::OpConvertFToS, type_int_,
          builder_->createUnaryBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                           GLSLstd450Floor, index));
      if (instr.attributes.stride > 1) {
        index = builder_->createBinOp(
            spv::OpIMul, type_int_, index,
            builder_->makeIntConstant(int(instr.attributes.stride)));
      }
      address = builder_->createBinOp(spv::OpIAdd, type_int_, address, index);
    }
    // Store the address for the subsequent vfetch_mini.
    builder_->createStore(address, var_main_vfetch_address_);
  }

  if (!needed_words) {
    // The vfetch_full address has been loaded for the subsequent vfetch_mini,
    // but there's no data to load.
    StoreResult(instr.result, spv::NoResult);
    return;
  }

  // Load the needed words.
  unsigned int word_composite_indices[4] = {};
  spv::Id word_composite_constituents[4];
  uint32_t word_count = 0;
  uint32_t words_remaining = needed_words;
  uint32_t word_index;
  while (xe::bit_scan_forward(words_remaining, &word_index)) {
    words_remaining &= ~(1 << word_index);
    spv::Id word_address = address;
    // Add the word offset from the instruction (signed), plus the offset of the
    // word within the element.
    int32_t word_offset = instr.attributes.offset + word_index;
    if (word_offset) {
      word_address =
          builder_->createBinOp(spv::OpIAdd, type_int_, word_address,
                                builder_->makeIntConstant(int(word_offset)));
    }
    word_composite_indices[word_index] = word_count;
    // FIXME(Triang3l): Bound checking is not done here, but haven't encountered
    // any games relying on out-of-bounds access. On Adreno 200 on Android (LG
    // P705), however, words (not full elements) out of glBufferData bounds
    // contain 0.
    word_composite_constituents[word_count++] =
        LoadUint32FromSharedMemory(word_address);
  }
  spv::Id words;
  if (word_count > 1) {
    // Copying from the array to id_vector_temp_ now, not in the loop above,
    // because of the LoadUint32FromSharedMemory call (potentially using
    // id_vector_temp_ internally).
    id_vector_temp_.clear();
    id_vector_temp_.insert(id_vector_temp_.cend(), word_composite_constituents,
                           word_composite_constituents + word_count);
    words = builder_->createCompositeConstruct(
        type_uint_vectors_[word_count - 1], id_vector_temp_);
  } else {
    words = word_composite_constituents[0];
  }

  // Endian swap the words, getting the endianness from bits 0:1 of the second
  // fetch constant word.
  uint32_t fetch_constant_word_1_index = fetch_constant_word_0_index + 1;
  id_vector_temp_.clear();
  // The only element of the fetch constant buffer.
  id_vector_temp_.push_back(const_int_0_);
  // Vector index.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(fetch_constant_word_1_index >> 2)));
  // Component index.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(fetch_constant_word_1_index & 3)));
  spv::Id fetch_constant_word_1 = builder_->createLoad(
      builder_->createAccessChain(spv::StorageClassUniform,
                                  uniform_fetch_constants_, id_vector_temp_),
      spv::NoPrecision);
  words = EndianSwap32Uint(
      words, builder_->createBinOp(spv::OpBitwiseAnd, type_uint_,
                                   fetch_constant_word_1,
                                   builder_->makeUintConstant(0b11)));

  spv::Id result = spv::NoResult;

  // Convert the format.
  uint32_t used_format_components =
      used_result_components & ((1 << xenos::GetVertexFormatComponentCount(
                                     instr.attributes.data_format)) -
                                1);
  // If needed_words is not zero (checked in the beginning), this must not be
  // zero too. For simplicity, it's assumed that something will be unpacked
  // here.
  assert_not_zero(used_format_components);
  uint32_t used_format_component_count = xe::bit_count(used_format_components);
  spv::Id result_type = type_float_vectors_[used_format_component_count - 1];
  bool format_is_packed = false;
  int packed_widths[4] = {}, packed_offsets[4] = {};
  uint32_t packed_words[4] = {};
  switch (instr.attributes.data_format) {
    case xenos::VertexFormat::k_8_8_8_8:
      format_is_packed = true;
      packed_widths[0] = packed_widths[1] = packed_widths[2] =
          packed_widths[3] = 8;
      packed_offsets[1] = 8;
      packed_offsets[2] = 16;
      packed_offsets[3] = 24;
      break;
    case xenos::VertexFormat::k_2_10_10_10:
      format_is_packed = true;
      packed_widths[0] = packed_widths[1] = packed_widths[2] = 10;
      packed_widths[3] = 2;
      packed_offsets[1] = 10;
      packed_offsets[2] = 20;
      packed_offsets[3] = 30;
      break;
    case xenos::VertexFormat::k_10_11_11:
      format_is_packed = true;
      packed_widths[0] = packed_widths[1] = 11;
      packed_widths[2] = 10;
      packed_offsets[1] = 11;
      packed_offsets[2] = 22;
      break;
    case xenos::VertexFormat::k_11_11_10:
      format_is_packed = true;
      packed_widths[0] = 10;
      packed_widths[1] = packed_widths[2] = 11;
      packed_offsets[1] = 10;
      packed_offsets[2] = 21;
      break;
    case xenos::VertexFormat::k_16_16:
      format_is_packed = true;
      packed_widths[0] = packed_widths[1] = 16;
      packed_offsets[1] = 16;
      break;
    case xenos::VertexFormat::k_16_16_16_16:
      format_is_packed = true;
      packed_widths[0] = packed_widths[1] = packed_widths[2] =
          packed_widths[3] = 16;
      packed_offsets[1] = packed_offsets[3] = 16;
      packed_words[2] = packed_words[3] = 1;
      break;

    case xenos::VertexFormat::k_16_16_FLOAT:
    case xenos::VertexFormat::k_16_16_16_16_FLOAT: {
      // FIXME(Triang3l): This converts from GLSL float16 with NaNs instead of
      // Xbox 360 float16 with extended range. However, haven't encountered
      // games relying on that yet.
      spv::Id word_needed_component_values[2] = {};
      for (uint32_t i = 0; i < 2; ++i) {
        uint32_t word_needed_components =
            (used_format_components >> (i * 2)) & 0b11;
        if (!word_needed_components) {
          continue;
        }
        spv::Id word;
        if (word_count > 1) {
          word = builder_->createCompositeExtract(words, type_uint_,
                                                  word_composite_indices[i]);
        } else {
          word = words;
        }
        word = builder_->createUnaryBuiltinCall(type_float2_,
                                                ext_inst_glsl_std_450_,
                                                GLSLstd450UnpackHalf2x16, word);
        if (word_needed_components != 0b11) {
          // If only one of two components is needed, extract it.
          word = builder_->createCompositeExtract(
              word, type_float_, (word_needed_components & 0b01) ? 0 : 1);
        }
        word_needed_component_values[i] = word;
      }
      if (word_needed_component_values[1] == spv::NoResult) {
        result = word_needed_component_values[0];
      } else if (word_needed_component_values[0] == spv::NoResult) {
        result = word_needed_component_values[1];
      } else {
        // Bypassing the assertion in spv::Builder::createCompositeConstruct as
        // of November 5, 2020 - can construct vectors by concatenating vectors,
        // not just from individual scalars.
        std::unique_ptr<spv::Instruction> composite_construct_op =
            std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                               result_type,
                                               spv::OpCompositeConstruct);
        composite_construct_op->addIdOperand(word_needed_component_values[0]);
        composite_construct_op->addIdOperand(word_needed_component_values[1]);
        result = composite_construct_op->getResultId();
        builder_->getBuildPoint()->addInstruction(
            std::move(composite_construct_op));
      }
    } break;

    case xenos::VertexFormat::k_32:
    case xenos::VertexFormat::k_32_32:
    case xenos::VertexFormat::k_32_32_32_32:
      assert_true(used_format_components == needed_words);
      if (instr.attributes.is_signed) {
        result = builder_->createUnaryOp(
            spv::OpBitcast, type_int_vectors_[used_format_component_count - 1],
            words);
        result =
            builder_->createUnaryOp(spv::OpConvertSToF, result_type, result);
      } else {
        result =
            builder_->createUnaryOp(spv::OpConvertUToF, result_type, words);
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          switch (instr.attributes.signed_rf_mode) {
            case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne:
              result = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, result_type, result,
                  builder_->makeFloatConstant(1.0f / 2147483647.0f));
              // No need to clamp to -1 if signed - 1/(2^31-1) is rounded to
              // 1/(2^31) as float32.
              break;
            case xenos::SignedRepeatingFractionMode::kNoZero: {
              result = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, result_type, result,
                  builder_->makeFloatConstant(1.0f / 2147483647.5f));
              spv::Id const_no_zero =
                  builder_->makeFloatConstant(0.5f / 2147483647.5f);
              if (used_format_component_count > 1) {
                id_vector_temp_.clear();
                id_vector_temp_.insert(id_vector_temp_.cend(),
                                       used_format_component_count,
                                       const_no_zero);
                const_no_zero = builder_->makeCompositeConstant(
                    result_type, id_vector_temp_);
              }
              result = builder_->createNoContractionBinOp(
                  spv::OpFAdd, result_type, result, const_no_zero);
            } break;
            default:
              assert_unhandled_case(instr.attributes.signed_rf_mode);
          }
        } else {
          result = builder_->createNoContractionBinOp(
              spv::OpVectorTimesScalar, result_type, result,
              builder_->makeFloatConstant(1.0f / 4294967295.0f));
        }
      }
      break;

    case xenos::VertexFormat::k_32_FLOAT:
    case xenos::VertexFormat::k_32_32_FLOAT:
    case xenos::VertexFormat::k_32_32_32_32_FLOAT:
    case xenos::VertexFormat::k_32_32_32_FLOAT:
      assert_true(used_format_components == needed_words);
      result = builder_->createUnaryOp(
          spv::OpBitcast, type_float_vectors_[word_count - 1], words);
      break;

    default:
      assert_unhandled_case(instr.attributes.data_format);
  }

  if (format_is_packed) {
    assert_true(result == spv::NoResult);
    // Extract the components from the words as individual ints or uints.
    if (instr.attributes.is_signed) {
      // Sign-extending extraction - in GLSL the sign-extending overload accepts
      // int.
      words = builder_->createUnaryOp(spv::OpBitcast,
                                      type_int_vectors_[word_count - 1], words);
    }
    int extracted_widths[4] = {};
    spv::Id extracted_components[4] = {};
    uint32_t extracted_component_count = 0;
    unsigned int extraction_word_current_index = UINT_MAX;
    // Default is `words` itself if 1 word loaded.
    spv::Id extraction_word_current = words;
    for (uint32_t i = 0; i < 4; ++i) {
      if (!(used_format_components & (1 << i))) {
        continue;
      }
      if (word_count > 1) {
        unsigned int extraction_word_new_index =
            word_composite_indices[packed_words[i]];
        if (extraction_word_current_index != extraction_word_new_index) {
          extraction_word_current_index = extraction_word_new_index;
          extraction_word_current = builder_->createCompositeExtract(
              words, instr.attributes.is_signed ? type_int_ : type_uint_,
              extraction_word_new_index);
        }
      }
      int extraction_width = packed_widths[i];
      assert_not_zero(extraction_width);
      extracted_widths[extracted_component_count] = extraction_width;
      extracted_components[extracted_component_count] = builder_->createTriOp(
          instr.attributes.is_signed ? spv::OpBitFieldSExtract
                                     : spv::OpBitFieldUExtract,
          instr.attributes.is_signed ? type_int_ : type_uint_,
          extraction_word_current, builder_->makeIntConstant(packed_offsets[i]),
          builder_->makeIntConstant(extraction_width));
      ++extracted_component_count;
    }
    // Combine extracted components into a vector.
    assert_true(extracted_component_count == used_format_component_count);
    if (used_format_component_count > 1) {
      id_vector_temp_.clear();
      id_vector_temp_.insert(
          id_vector_temp_.cend(), extracted_components,
          extracted_components + used_format_component_count);
      result = builder_->createCompositeConstruct(
          instr.attributes.is_signed
              ? type_int_vectors_[used_format_component_count - 1]
              : type_uint_vectors_[used_format_component_count - 1],
          id_vector_temp_);
    } else {
      result = extracted_components[0];
    }
    // Convert to floating-point.
    result = builder_->createUnaryOp(
        instr.attributes.is_signed ? spv::OpConvertSToF : spv::OpConvertUToF,
        result_type, result);
    // Normalize.
    if (!instr.attributes.is_integer) {
      float packed_scales[4];
      bool packed_scales_same = true;
      for (uint32_t i = 0; i < used_format_component_count; ++i) {
        int extracted_width = extracted_widths[i];
        // The signed case would result in 1.0 / 0.0 for 1-bit components, but
        // there are no Xenos formats with them.
        assert_true(extracted_width >= 2);
        packed_scales_same &= extracted_width != extracted_widths[0];
        float packed_scale_inv;
        if (instr.attributes.is_signed) {
          packed_scale_inv = float((uint32_t(1) << (extracted_width - 1)) - 1);
          if (instr.attributes.signed_rf_mode ==
              xenos::SignedRepeatingFractionMode::kNoZero) {
            packed_scale_inv += 0.5f;
          }
        } else {
          packed_scale_inv = float((uint32_t(1) << extracted_width) - 1);
        }
        packed_scales[i] = 1.0f / packed_scale_inv;
      }
      spv::Id const_packed_scale =
          builder_->makeFloatConstant(packed_scales[0]);
      spv::Op packed_scale_mul_op;
      if (used_format_component_count > 1) {
        if (packed_scales_same) {
          packed_scale_mul_op = spv::OpVectorTimesScalar;
        } else {
          packed_scale_mul_op = spv::OpFMul;
          id_vector_temp_.clear();
          id_vector_temp_.push_back(const_packed_scale);
          for (uint32_t i = 1; i < used_format_component_count; ++i) {
            id_vector_temp_.push_back(
                builder_->makeFloatConstant(packed_scales[i]));
          }
          const_packed_scale =
              builder_->makeCompositeConstant(result_type, id_vector_temp_);
        }
      } else {
        packed_scale_mul_op = spv::OpFMul;
      }
      result = builder_->createNoContractionBinOp(
          packed_scale_mul_op, result_type, result, const_packed_scale);
      if (instr.attributes.is_signed) {
        switch (instr.attributes.signed_rf_mode) {
          case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne: {
            // Treat both -(2^(n-1)) and -(2^(n-1)-1) as -1. Using regular FMax,
            // not NMax, because the number is known not to be NaN.
            spv::Id const_minus_1 = builder_->makeFloatConstant(-1.0f);
            if (used_format_component_count > 1) {
              id_vector_temp_.clear();
              id_vector_temp_.resize(used_format_component_count,
                                     const_minus_1);
              const_minus_1 =
                  builder_->makeCompositeConstant(result_type, id_vector_temp_);
            }
            result = builder_->createBinBuiltinCall(
                result_type, ext_inst_glsl_std_450_, GLSLstd450FMax, result,
                const_minus_1);
          } break;
          case xenos::SignedRepeatingFractionMode::kNoZero:
            id_vector_temp_.clear();
            for (uint32_t i = 0; i < used_format_component_count; ++i) {
              id_vector_temp_.push_back(
                  builder_->makeFloatConstant(0.5f * packed_scales[i]));
            }
            result = builder_->createNoContractionBinOp(
                spv::OpFAdd, result_type, result,
                used_format_component_count > 1
                    ? builder_->makeCompositeConstant(result_type,
                                                      id_vector_temp_)
                    : id_vector_temp_[0]);
            break;
          default:
            assert_unhandled_case(instr.attributes.signed_rf_mode);
        }
      }
    }
  }

  if (result != spv::NoResult) {
    // Apply the exponent bias.
    if (instr.attributes.exp_adjust) {
      result = builder_->createNoContractionBinOp(
          spv::OpVectorTimesScalar, builder_->getTypeId(result), result,
          builder_->makeFloatConstant(
              std::ldexp(1.0f, instr.attributes.exp_adjust)));
    }

    // If any components not present in the format were requested, pad the
    // resulting vector with zeros.
    uint32_t used_missing_components =
        used_result_components & ~used_format_components;
    if (used_missing_components) {
      // Bypassing the assertion in spv::Builder::createCompositeConstruct as of
      // November 5, 2020 - can construct vectors by concatenating vectors, not
      // just from individual scalars.
      std::unique_ptr<spv::Instruction> composite_construct_op =
          std::make_unique<spv::Instruction>(
              builder_->getUniqueId(),
              type_float_vectors_[xe::bit_count(used_result_components) - 1],
              spv::OpCompositeConstruct);
      composite_construct_op->addIdOperand(result);
      composite_construct_op->addIdOperand(
          const_float_vectors_0_[xe::bit_count(used_missing_components) - 1]);
      result = composite_construct_op->getResultId();
      builder_->getBuildPoint()->addInstruction(
          std::move(composite_construct_op));
    }
  }
  StoreResult(instr.result, result);
}

void SpirvShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition);

  EnsureBuildPointAvailable();

  // Handle the instructions for setting the register LOD.
  switch (instr.opcode) {
    case ucode::FetchOpcode::kSetTextureLod:
      builder_->createStore(
          GetOperandComponents(LoadOperandStorage(instr.operands[0]),
                               instr.operands[0], 0b0001),
          var_main_tfetch_lod_);
      return;
    case ucode::FetchOpcode::kSetTextureGradientsHorz:
      builder_->createStore(
          GetOperandComponents(LoadOperandStorage(instr.operands[0]),
                               instr.operands[0], 0b0111),
          var_main_tfetch_gradients_h_);
      return;
    case ucode::FetchOpcode::kSetTextureGradientsVert:
      builder_->createStore(
          GetOperandComponents(LoadOperandStorage(instr.operands[0]),
                               instr.operands[0], 0b0111),
          var_main_tfetch_gradients_v_);
      return;
    default:
      break;
  }

  // Handle instructions that store something.
  uint32_t used_result_components = instr.result.GetUsedResultComponents();
  uint32_t used_result_nonzero_components = instr.GetNonZeroResultComponents();
  switch (instr.opcode) {
    case ucode::FetchOpcode::kTextureFetch:
      break;
    case ucode::FetchOpcode::kGetTextureBorderColorFrac:
      // TODO(Triang3l): Bind a black texture with a white border to calculate
      // the border color fraction (in the X component of the result).
      assert_always();
      EmitTranslationError("getBCF is unimplemented", false);
      used_result_nonzero_components = 0;
      break;
    case ucode::FetchOpcode::kGetTextureComputedLod:
      break;
    case ucode::FetchOpcode::kGetTextureGradients:
      break;
    case ucode::FetchOpcode::kGetTextureWeights:
      // FIXME(Triang3l): Currently disregarding the LOD completely in
      // getWeights because the needed code would be very complicated, while
      // getWeights is mostly used for things like PCF of shadow maps, that
      // don't have mips. The LOD would be needed for the mip lerp factor in W
      // of the return value and to choose the LOD where interpolation would
      // take place for XYZ. That would require either implementing the LOD
      // calculation algorithm using the ALU (since the `lod` instruction is
      // limited to pixel shaders and can't be used when there's control flow
      // divergence, unlike explicit gradients), or sampling a texture filled
      // with LOD numbers (easier and more consistent - unclamped LOD doesn't
      // make sense for getWeights anyway). The same applies to offsets.
      used_result_nonzero_components &= ~uint32_t(0b1000);
      break;
    default:
      assert_unhandled_case(instr.opcode);
      EmitTranslationError("Unknown texture fetch operation");
      used_result_nonzero_components = 0;
  }
  uint32_t used_result_component_count = xe::bit_count(used_result_components);
  if (!used_result_nonzero_components) {
    // Nothing to fetch, only constant 0/1 writes - simplify the rest of the
    // function so it doesn't have to handle this case.
    if (used_result_components) {
      StoreResult(instr.result,
                  const_float_vectors_0_[used_result_component_count - 1]);
    }
    return;
  }

  spv::Id result[] = {const_float_0_, const_float_0_, const_float_0_,
                      const_float_0_};

  if (instr.opcode == ucode::FetchOpcode::kGetTextureGradients) {
    // Doesn't need the texture, handle separately.
    spv::Id operand_0_storage = LoadOperandStorage(instr.operands[0]);
    bool derivative_function_x_used =
        (used_result_nonzero_components & 0b0011) != 0;
    bool derivative_function_y_used =
        (used_result_nonzero_components & 0b1100) != 0;
    spv::Id derivative_function_x = spv::NoResult;
    spv::Id derivative_function_y = spv::NoResult;
    if (derivative_function_x_used && derivative_function_y_used) {
      spv::Id derivative_function =
          GetOperandComponents(operand_0_storage, instr.operands[0], 0b0011);
      derivative_function_x =
          builder_->createCompositeExtract(derivative_function, type_float_, 0);
      derivative_function_y =
          builder_->createCompositeExtract(derivative_function, type_float_, 1);
    } else {
      if (derivative_function_x_used) {
        derivative_function_x =
            GetOperandComponents(operand_0_storage, instr.operands[0], 0b0001);
      }
      if (derivative_function_y_used) {
        derivative_function_y =
            GetOperandComponents(operand_0_storage, instr.operands[0], 0b0010);
      }
    }
    builder_->addCapability(spv::CapabilityDerivativeControl);
    uint32_t derivative_components_remaining = used_result_nonzero_components;
    uint32_t derivative_component_index;
    while (xe::bit_scan_forward(derivative_components_remaining,
                                &derivative_component_index)) {
      derivative_components_remaining &=
          ~(UINT32_C(1) << derivative_component_index);
      result[derivative_component_index] = builder_->createUnaryOp(
          (derivative_component_index & 0b01) ? spv::OpDPdyCoarse
                                              : spv::OpDPdxCoarse,
          type_float_,
          (derivative_component_index & 0b10) ? derivative_function_y
                                              : derivative_function_x);
    }
  } else {
    // kTextureFetch, kGetTextureComputedLod or kGetTextureWeights.

    // Whether to use gradients (implicit or explicit) for LOD calculation.
    bool use_computed_lod =
        instr.attributes.use_computed_lod &&
        (is_pixel_shader() || instr.attributes.use_register_gradients);
    if (instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod &&
        (!use_computed_lod || instr.attributes.use_register_gradients)) {
      assert_always();
      EmitTranslationError(
          "getCompTexLOD used with explicit LOD or gradients - contradicts "
          "MSDN",
          false);
      StoreResult(instr.result,
                  const_float_vectors_0_[used_result_component_count - 1]);
      return;
    }

    uint32_t fetch_constant_index = instr.operands[1].storage_index;
    uint32_t fetch_constant_word_0_index = 6 * fetch_constant_index;

    spv::Id sampler = spv::NoResult;
    spv::Id image_2d_array_or_cube_unsigned = spv::NoResult;
    spv::Id image_2d_array_or_cube_signed = spv::NoResult;
    spv::Id image_3d_unsigned = spv::NoResult;
    spv::Id image_3d_signed = spv::NoResult;
    if (instr.opcode != ucode::FetchOpcode::kGetTextureWeights) {
      bool bindings_set_up = true;
      // While GL_ARB_texture_query_lod specifies the value for
      // GL_NEAREST_MIPMAP_NEAREST and GL_LINEAR_MIPMAP_NEAREST minifying
      // functions as rounded (unlike the `lod` instruction in Direct3D 10.1+,
      // which is not defined for point sampling), the XNA assembler doesn't
      // accept MipFilter overrides for getCompTexLOD - probably should be
      // linear only, though not known exactly.
      //
      // 4D5307F2 uses vertex displacement map textures for tessellated models
      // like the beehive tree with explicit LOD with point sampling (they store
      // values packed in two components), however, the fetch constant has
      // anisotropic filtering enabled. However, Direct3D 12 doesn't allow
      // mixing anisotropic and point filtering. Possibly anistropic filtering
      // should be disabled when explicit LOD is used - do this here.
      size_t sampler_index = FindOrAddSamplerBinding(
          fetch_constant_index, instr.attributes.mag_filter,
          instr.attributes.min_filter,
          instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod
              ? xenos::TextureFilter::kLinear
              : instr.attributes.mip_filter,
          use_computed_lod ? instr.attributes.aniso_filter
                           : xenos::AnisoFilter::kDisabled);
      xenos::FetchOpDimension dimension_2d_array_or_cube =
          instr.dimension == xenos::FetchOpDimension::k3DOrStacked
              ? xenos::FetchOpDimension::k2D
              : instr.dimension;
      size_t image_2d_array_or_cube_unsigned_index = FindOrAddTextureBinding(
          fetch_constant_index, dimension_2d_array_or_cube, false);
      size_t image_2d_array_or_cube_signed_index = FindOrAddTextureBinding(
          fetch_constant_index, dimension_2d_array_or_cube, true);
      if (sampler_index == SIZE_MAX ||
          image_2d_array_or_cube_unsigned_index == SIZE_MAX ||
          image_2d_array_or_cube_signed_index == SIZE_MAX) {
        bindings_set_up = false;
      }
      size_t image_3d_unsigned_index = SIZE_MAX;
      size_t image_3d_signed_index = SIZE_MAX;
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        image_3d_unsigned_index = FindOrAddTextureBinding(
            fetch_constant_index, xenos::FetchOpDimension::k3DOrStacked, false);
        image_3d_signed_index = FindOrAddTextureBinding(
            fetch_constant_index, xenos::FetchOpDimension::k3DOrStacked, true);
        if (image_3d_unsigned_index == SIZE_MAX ||
            image_3d_signed_index == SIZE_MAX) {
          bindings_set_up = false;
        }
      }
      if (!bindings_set_up) {
        // Too many image or sampler bindings used.
        StoreResult(instr.result,
                    const_float_vectors_0_[used_result_component_count - 1]);
        return;
      }
      sampler = builder_->createLoad(sampler_bindings_[sampler_index].variable,
                                     spv::NoPrecision);
      const TextureBinding& image_2d_array_or_cube_unsigned_binding =
          texture_bindings_[image_2d_array_or_cube_unsigned_index];
      image_2d_array_or_cube_unsigned = builder_->createLoad(
          image_2d_array_or_cube_unsigned_binding.variable, spv::NoPrecision);
      const TextureBinding& image_2d_array_or_cube_signed_binding =
          texture_bindings_[image_2d_array_or_cube_signed_index];
      image_2d_array_or_cube_signed = builder_->createLoad(
          image_2d_array_or_cube_signed_binding.variable, spv::NoPrecision);
      if (image_3d_unsigned_index != SIZE_MAX) {
        const TextureBinding& image_3d_unsigned_binding =
            texture_bindings_[image_3d_unsigned_index];
        image_3d_unsigned = builder_->createLoad(
            image_3d_unsigned_binding.variable, spv::NoPrecision);
      }
      if (image_3d_signed_index != SIZE_MAX) {
        const TextureBinding& image_3d_signed_binding =
            texture_bindings_[image_3d_signed_index];
        image_3d_signed = builder_->createLoad(image_3d_signed_binding.variable,
                                               spv::NoPrecision);
      }
    }

    // Get offsets applied to the coordinates before sampling.
    // FIXME(Triang3l): Offsets need to be applied at the LOD being fetched, not
    // at LOD 0. However, since offsets have granularity of 0.5, not 1, on the
    // Xenos, they can't be passed directly as ConstOffset to the image sample
    // instruction (plus-minus 0.5 offsets are very common in games). But
    // offsetting at mip levels is a rare usage case, mostly offsets are used
    // for things like shadow maps and blur, where there are no mips.
    float offset_values[3] = {};
    // MSDN doesn't list offsets as getCompTexLOD parameters.
    if (instr.opcode != ucode::FetchOpcode::kGetTextureComputedLod) {
      // Add a small epsilon to the offset (1.5/4 the fixed-point texture
      // coordinate ULP with 8-bit subtexel precision - shouldn't significantly
      // effect the fixed-point conversion; 1/4 is also not enough with 3x
      // resolution scaling very noticeably on the weapon in 4D5307E6, at least
      // on the Direct3D 12 backend) to resolve ambiguity when fetching
      // point-sampled textures between texels. This applies to both normalized
      // (58410954 Xbox Live Arcade logo, coordinates interpolated between
      // vertices with half-pixel offset) and unnormalized (4D5307E6 lighting
      // G-buffer reading, ps_param_gen pixels) coordinates. On Nvidia Pascal,
      // without this adjustment, blockiness is visible in both cases. Possibly
      // there is a better way, however, an attempt was made to error-correct
      // division by adding the difference between original and re-denormalized
      // coordinates, but on Nvidia, `mul` (on Direct3D 12) and internal
      // multiplication in texture sampling apparently round differently, so
      // `mul` gives a value that would be floored as expected, but the
      // left/upper pixel is still sampled instead.
      const float kRoundingOffset = 1.5f / 1024.0f;
      switch (instr.dimension) {
        case xenos::FetchOpDimension::k1D:
          offset_values[0] = instr.attributes.offset_x + kRoundingOffset;
          if (instr.opcode == ucode::FetchOpcode::kGetTextureWeights) {
            // For coordinate lerp factors. This needs to be done separately for
            // point mag/min filters, but they're currently not handled here
            // anyway.
            offset_values[0] -= 0.5f;
          }
          break;
        case xenos::FetchOpDimension::k2D:
          offset_values[0] = instr.attributes.offset_x + kRoundingOffset;
          offset_values[1] = instr.attributes.offset_y + kRoundingOffset;
          if (instr.opcode == ucode::FetchOpcode::kGetTextureWeights) {
            offset_values[0] -= 0.5f;
            offset_values[1] -= 0.5f;
          }
          break;
        case xenos::FetchOpDimension::k3DOrStacked:
          offset_values[0] = instr.attributes.offset_x + kRoundingOffset;
          offset_values[1] = instr.attributes.offset_y + kRoundingOffset;
          offset_values[2] = instr.attributes.offset_z + kRoundingOffset;
          if (instr.opcode == ucode::FetchOpcode::kGetTextureWeights) {
            offset_values[0] -= 0.5f;
            offset_values[1] -= 0.5f;
            offset_values[2] -= 0.5f;
          }
          break;
        case xenos::FetchOpDimension::kCube:
          // Applying the rounding epsilon to cube maps too for potential game
          // passes processing cube map faces themselves.
          offset_values[0] = instr.attributes.offset_x + kRoundingOffset;
          offset_values[1] = instr.attributes.offset_y + kRoundingOffset;
          if (instr.opcode == ucode::FetchOpcode::kGetTextureWeights) {
            offset_values[0] -= 0.5f;
            offset_values[1] -= 0.5f;
            // The logic for ST weights is the same for all faces.
            // FIXME(Triang3l): If LOD calculation is added to getWeights, face
            // offset probably will need to be handled too (if the hardware
            // supports it at all, though MSDN lists OffsetZ in tfetchCube).
          } else {
            offset_values[2] = instr.attributes.offset_z;
          }
          break;
      }
    }
    uint32_t offsets_not_zero = 0b000;
    for (uint32_t i = 0; i < 3; ++i) {
      if (offset_values[i]) {
        offsets_not_zero |= 1 << i;
      }
    }

    // Fetch constant word usage:
    // - 2: Size (needed only once).
    // - 3: Exponent adjustment (needed only once).
    // - 4: Conditionally for 3D kTextureFetch: stacked texture filtering modes.
    //      Unconditionally LOD kTextureFetch: LOD and gradient exponent bias,
    //      result exponent bias.
    // - 5: Dimensionality (3D or 2D stacked - needed only once).

    // Load the texture size and whether it's 3D or stacked if needed.
    // 1D: X - width.
    // 2D, cube: X - width, Y - height (cube maps probably can be only square,
    //           but for simplicity).
    // 3D: X - width, Y - height, Z - depth.
    uint32_t size_needed_components = 0b000;
    bool data_is_3d_needed = false;
    if (instr.opcode == ucode::FetchOpcode::kGetTextureWeights) {
      // Size needed for denormalization for coordinate lerp factor.
      // FIXME(Triang3l): Currently disregarding the LOD completely in
      // getWeights. However, if the LOD lerp factor and the LOD where filtering
      // would happen are ever calculated, all components of the size may be
      // needed for ALU LOD calculation with normalized coordinates (or, if a
      // texture filled with LOD indices is used, coordinates will need to be
      // normalized as normally).
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
          // Stacked and 3D textures are fetched from different bindings - the
          // check is always needed.
          data_is_3d_needed = true;
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
      // Stacked and 3D textures have different size packing - need to get
      // whether the texture is 3D unconditionally.
      data_is_3d_needed = true;
    }
    spv::Id data_is_3d = spv::NoResult;
    if (data_is_3d_needed) {
      // Get the data dimensionality from the bits 9:10 of the fetch constant
      // word 5.
      id_vector_temp_.clear();
      id_vector_temp_.push_back(const_int_0_);
      id_vector_temp_.push_back(builder_->makeIntConstant(
          int((fetch_constant_word_0_index + 5) >> 2)));
      id_vector_temp_.push_back(builder_->makeIntConstant(
          int((fetch_constant_word_0_index + 5) & 3)));
      spv::Id fetch_constant_word_5 =
          builder_->createLoad(builder_->createAccessChain(
                                   spv::StorageClassUniform,
                                   uniform_fetch_constants_, id_vector_temp_),
                               spv::NoPrecision);
      spv::Id data_dimension = builder_->createTriOp(
          spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_5,
          builder_->makeUintConstant(9), builder_->makeUintConstant(2));
      data_is_3d = builder_->createBinOp(
          spv::OpIEqual, type_bool_, data_dimension,
          builder_->makeUintConstant(
              static_cast<unsigned int>(xenos::DataDimension::k3D)));
    }
    spv::Id size[3] = {};
    if (size_needed_components) {
      // Get the size from the fetch constant word 2.
      id_vector_temp_.clear();
      id_vector_temp_.push_back(const_int_0_);
      id_vector_temp_.push_back(builder_->makeIntConstant(
          int((fetch_constant_word_0_index + 2) >> 2)));
      id_vector_temp_.push_back(builder_->makeIntConstant(
          int((fetch_constant_word_0_index + 2) & 3)));
      spv::Id fetch_constant_word_2 =
          builder_->createLoad(builder_->createAccessChain(
                                   spv::StorageClassUniform,
                                   uniform_fetch_constants_, id_vector_temp_),
                               spv::NoPrecision);
      switch (instr.dimension) {
        case xenos::FetchOpDimension::k1D: {
          if (size_needed_components & 0b1) {
            size[0] = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                const_uint_0_,
                builder_->makeUintConstant(xenos::kTexture1DMaxWidthLog2));
          }
          assert_zero(size_needed_components & 0b110);
        } break;
        case xenos::FetchOpDimension::k2D:
        case xenos::FetchOpDimension::kCube: {
          if (size_needed_components & 0b1) {
            size[0] = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                const_uint_0_,
                builder_->makeUintConstant(
                    xenos::kTexture2DCubeMaxWidthHeightLog2));
          }
          if (size_needed_components & 0b10) {
            spv::Id width_height_bit_count = builder_->makeUintConstant(
                xenos::kTexture2DCubeMaxWidthHeightLog2);
            size[1] = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                width_height_bit_count, width_height_bit_count);
          }
          assert_zero(size_needed_components & 0b100);
        } break;
        case xenos::FetchOpDimension::k3DOrStacked: {
          if (size_needed_components & 0b1) {
            spv::Id size_3d =
                builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_,
                                      fetch_constant_word_2, const_uint_0_,
                                      builder_->makeUintConstant(
                                          xenos::kTexture3DMaxWidthHeightLog2));
            spv::Id size_2d = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                const_uint_0_,
                builder_->makeUintConstant(
                    xenos::kTexture2DCubeMaxWidthHeightLog2));
            assert_true(data_is_3d != spv::NoResult);
            size[0] = builder_->createTriOp(spv::OpSelect, type_uint_,
                                            data_is_3d, size_3d, size_2d);
          }
          if (size_needed_components & 0b10) {
            spv::Id width_height_bit_count_3d =
                builder_->makeUintConstant(xenos::kTexture3DMaxWidthHeightLog2);
            spv::Id size_3d = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                width_height_bit_count_3d, width_height_bit_count_3d);
            spv::Id width_height_bit_count_2d = builder_->makeUintConstant(
                xenos::kTexture2DCubeMaxWidthHeightLog2);
            spv::Id size_2d = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                width_height_bit_count_2d, width_height_bit_count_2d);
            assert_true(data_is_3d != spv::NoResult);
            size[1] = builder_->createTriOp(spv::OpSelect, type_uint_,
                                            data_is_3d, size_3d, size_2d);
          }
          if (size_needed_components & 0b100) {
            spv::Id size_3d = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                builder_->makeUintConstant(xenos::kTexture3DMaxWidthHeightLog2 *
                                           2),
                builder_->makeUintConstant(xenos::kTexture3DMaxDepthLog2));
            spv::Id size_2d = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, fetch_constant_word_2,
                builder_->makeUintConstant(
                    xenos::kTexture2DCubeMaxWidthHeightLog2 * 2),
                builder_->makeUintConstant(xenos::kTexture2DMaxStackDepthLog2));
            assert_true(data_is_3d != spv::NoResult);
            size[2] = builder_->createTriOp(spv::OpSelect, type_uint_,
                                            data_is_3d, size_3d, size_2d);
          }
        } break;
      }
      {
        uint32_t size_remaining_components = size_needed_components;
        uint32_t size_component_index;
        while (xe::bit_scan_forward(size_remaining_components,
                                    &size_component_index)) {
          size_remaining_components &= ~(UINT32_C(1) << size_component_index);
          spv::Id& size_component_ref = size[size_component_index];
          // Fetch constants store size minus 1 - add 1.
          size_component_ref =
              builder_->createBinOp(spv::OpIAdd, type_uint_, size_component_ref,
                                    builder_->makeUintConstant(1));
          // Convert the size to float for multiplication or division.
          size_component_ref = builder_->createUnaryOp(
              spv::OpConvertUToF, type_float_, size_component_ref);
        }
      }
    }

    // FIXME(Triang3l): Mip lerp factor needs to be calculated, and the
    // coordinate lerp factors should be calculated at the mip level texels
    // would be sampled from. That would require some way of calculating the
    // LOD that would be applicable to explicit gradients and vertex shaders.
    // Also, with point sampling, possibly lerp factors need to be 0. W (mip
    // lerp factor) should have been masked out previously because it's not
    // supported currently.
    assert_false(instr.opcode == ucode::FetchOpcode::kGetTextureWeights &&
                 (used_result_nonzero_components & 0b1000));

    // Load the needed original values of the coordinates operand.
    uint32_t coordinates_needed_components =
        instr.opcode == ucode::FetchOpcode::kGetTextureWeights
            ? used_result_nonzero_components
            : ((UINT32_C(1)
                << xenos::GetFetchOpDimensionComponentCount(instr.dimension)) -
               1);
    assert_not_zero(coordinates_needed_components);
    spv::Id coordinates_operand =
        GetOperandComponents(LoadOperandStorage(instr.operands[0]),
                             instr.operands[0], coordinates_needed_components);
    spv::Id coordinates[] = {const_float_0_, const_float_0_, const_float_0_};
    if (xe::bit_count(coordinates_needed_components) > 1) {
      uint32_t coordinates_remaining_components = coordinates_needed_components;
      uint32_t coordinate_component_index;
      uint32_t coordinate_operand_component_index = 0;
      while (xe::bit_scan_forward(coordinates_remaining_components,
                                  &coordinate_component_index)) {
        coordinates_remaining_components &=
            ~(UINT32_C(1) << coordinate_component_index);
        coordinates[coordinate_component_index] =
            builder_->createCompositeExtract(
                coordinates_operand, type_float_,
                coordinate_operand_component_index++);
      }
    } else {
      uint32_t coordinate_component_index;
      xe::bit_scan_forward(coordinates_needed_components,
                           &coordinate_component_index);
      coordinates[coordinate_component_index] = coordinates_operand;
    }

    // TODO(Triang3l): Reverting the resolution scale.

    if (instr.opcode == ucode::FetchOpcode::kGetTextureWeights) {
      // FIXME(Triang3l): Filtering modes should possibly be taken into account,
      // but for simplicity, not doing that - from a high level point of view,
      // would be useless to get weights that will always be zero.
      uint32_t coordinates_remaining_components = coordinates_needed_components;
      uint32_t coordinate_component_index;
      while (xe::bit_scan_forward(coordinates_remaining_components,
                                  &coordinate_component_index)) {
        coordinates_remaining_components &=
            ~(UINT32_C(1) << coordinate_component_index);
        spv::Id result_component = coordinates[coordinate_component_index];
        // Need unnormalized coordinates.
        if (!instr.attributes.unnormalized_coordinates) {
          spv::Id size_component = size[coordinate_component_index];
          assert_true(size_component != spv::NoResult);
          result_component = builder_->createNoContractionBinOp(
              spv::OpFMul, type_float_, result_component, size_component);
        }
        float component_offset = offset_values[coordinate_component_index];
        if (component_offset) {
          result_component = builder_->createNoContractionBinOp(
              spv::OpFAdd, type_float_, result_component,
              builder_->makeFloatConstant(component_offset));
        }
        // 0.5 has already been subtracted via offsets previously.
        result_component = builder_->createUnaryBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450Fract,
            result_component);
        result[coordinate_component_index] = result_component;
      }
    } else {
      // kTextureFetch or kGetTextureComputedLod.

      // Normalize the XY coordinates, and apply the offset.
      for (uint32_t i = 0;
           i <= uint32_t(instr.dimension != xenos::FetchOpDimension::k1D);
           ++i) {
        spv::Id& coordinate_ref = coordinates[i];
        spv::Id component_offset =
            offset_values[i] ? builder_->makeFloatConstant(offset_values[i])
                             : spv::NoResult;
        spv::Id size_component = size[i];
        if (instr.attributes.unnormalized_coordinates) {
          if (component_offset != spv::NoResult) {
            coordinate_ref = builder_->createNoContractionBinOp(
                spv::OpFAdd, type_float_, coordinate_ref, component_offset);
          }
          assert_true(size_component != spv::NoResult);
          coordinate_ref = builder_->createNoContractionBinOp(
              spv::OpFDiv, type_float_, coordinate_ref, size_component);
        } else {
          if (component_offset != spv::NoResult) {
            assert_true(size_component != spv::NoResult);
            spv::Id component_offset_normalized =
                builder_->createNoContractionBinOp(
                    spv::OpFDiv, type_float_, component_offset, size_component);
            coordinate_ref = builder_->createNoContractionBinOp(
                spv::OpFAdd, type_float_, coordinate_ref,
                component_offset_normalized);
          }
        }
      }
      if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
        spv::Id& z_coordinate_ref = coordinates[2];
        spv::Id z_offset = offset_values[2]
                               ? builder_->makeFloatConstant(offset_values[2])
                               : spv::NoResult;
        spv::Id z_size = size[2];
        if (instr.attributes.unnormalized_coordinates) {
          // Apply the offset, and normalize the Z coordinate for a 3D texture.
          if (z_offset != spv::NoResult) {
            z_coordinate_ref = builder_->createNoContractionBinOp(
                spv::OpFAdd, type_float_, z_coordinate_ref, z_offset);
          }
          spv::Block& block_dimension_head = *builder_->getBuildPoint();
          spv::Block& block_dimension_merge = builder_->makeNewBlock();
          spv::Block& block_dimension_3d = builder_->makeNewBlock();
          builder_->createSelectionMerge(&block_dimension_merge,
                                         spv::SelectionControlDontFlattenMask);
          assert_true(data_is_3d != spv::NoResult);
          builder_->createConditionalBranch(data_is_3d, &block_dimension_3d,
                                            &block_dimension_merge);
          builder_->setBuildPoint(&block_dimension_3d);
          assert_true(z_size != spv::NoResult);
          spv::Id z_3d = builder_->createNoContractionBinOp(
              spv::OpFDiv, type_float_, z_coordinate_ref, z_size);
          builder_->createBranch(&block_dimension_merge);
          builder_->setBuildPoint(&block_dimension_merge);
          {
            std::unique_ptr<spv::Instruction> z_phi_op =
                std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                   type_float_, spv::OpPhi);
            z_phi_op->addIdOperand(z_3d);
            z_phi_op->addIdOperand(block_dimension_3d.getId());
            z_phi_op->addIdOperand(z_coordinate_ref);
            z_phi_op->addIdOperand(block_dimension_head.getId());
            z_coordinate_ref = z_phi_op->getResultId();
            builder_->getBuildPoint()->addInstruction(std::move(z_phi_op));
          }
        } else {
          // Denormalize the Z coordinate for a stacked texture, and apply the
          // offset.
          spv::Block& block_dimension_head = *builder_->getBuildPoint();
          spv::Block& block_dimension_merge = builder_->makeNewBlock();
          spv::Block* block_dimension_3d =
              z_offset != spv::NoResult ? &builder_->makeNewBlock() : nullptr;
          spv::Block& block_dimension_stacked = builder_->makeNewBlock();
          builder_->createSelectionMerge(&block_dimension_merge,
                                         spv::SelectionControlDontFlattenMask);
          assert_true(data_is_3d != spv::NoResult);
          builder_->createConditionalBranch(
              data_is_3d,
              block_dimension_3d ? block_dimension_3d : &block_dimension_merge,
              &block_dimension_stacked);
          // 3D case.
          spv::Id z_3d = z_coordinate_ref;
          if (block_dimension_3d) {
            builder_->setBuildPoint(block_dimension_3d);
            if (z_offset != spv::NoResult) {
              assert_true(z_size != spv::NoResult);
              spv::Id z_offset_normalized = builder_->createNoContractionBinOp(
                  spv::OpFDiv, type_float_, z_offset, z_size);
              z_3d = builder_->createNoContractionBinOp(
                  spv::OpFAdd, type_float_, z_3d, z_offset_normalized);
            }
            builder_->createBranch(&block_dimension_merge);
          }
          // Stacked case.
          builder_->setBuildPoint(&block_dimension_stacked);
          spv::Id z_stacked = z_coordinate_ref;
          assert_true(z_size != spv::NoResult);
          z_stacked = builder_->createNoContractionBinOp(
              spv::OpFMul, type_float_, z_stacked, z_size);
          if (z_offset != spv::NoResult) {
            z_stacked = builder_->createNoContractionBinOp(
                spv::OpFAdd, type_float_, z_stacked, z_offset);
          }
          builder_->createBranch(&block_dimension_merge);
          // Select one of the two.
          builder_->setBuildPoint(&block_dimension_merge);
          {
            std::unique_ptr<spv::Instruction> z_phi_op =
                std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                   type_float_, spv::OpPhi);
            z_phi_op->addIdOperand(z_3d);
            z_phi_op->addIdOperand((block_dimension_3d ? *block_dimension_3d
                                                       : block_dimension_head)
                                       .getId());
            z_phi_op->addIdOperand(z_stacked);
            z_phi_op->addIdOperand(block_dimension_stacked.getId());
            z_coordinate_ref = z_phi_op->getResultId();
            builder_->getBuildPoint()->addInstruction(std::move(z_phi_op));
          }
        }
      } else if (instr.dimension == xenos::FetchOpDimension::kCube) {
        // Transform the cube coordinates from 2D to 3D.
        // Move SC/TC from 1...2 to -1...1.
        spv::Id const_float_2 = builder_->makeFloatConstant(2.0f);
        spv::Id const_float_minus_3 = builder_->makeFloatConstant(-3.0f);
        for (uint32_t i = 0; i < 2; ++i) {
          coordinates[i] = builder_->createNoContractionBinOp(
              spv::OpFAdd, type_float_,
              builder_->createNoContractionBinOp(spv::OpFMul, type_float_,
                                                 coordinates[i], const_float_2),
              const_float_minus_3);
        }
        // Get the face index (floored, within 0...5 - OpConvertFToU is
        // undefined for out-of-range values, so clamping from both sides
        // manually).
        spv::Id face = coordinates[2];
        if (offset_values[2]) {
          face = builder_->createNoContractionBinOp(
              spv::OpFAdd, type_float_, face,
              builder_->makeFloatConstant(offset_values[2]));
        }
        face = builder_->createUnaryOp(
            spv::OpConvertFToU, type_uint_,
            builder_->createTriBuiltinCall(
                type_float_, ext_inst_glsl_std_450_, GLSLstd450NClamp, face,
                const_float_0_, builder_->makeFloatConstant(5.0f)));
        // Split the face index into the axis and the sign.
        spv::Id const_uint_1 = builder_->makeUintConstant(1);
        spv::Id face_axis = builder_->createBinOp(
            spv::OpShiftRightLogical, type_uint_, face, const_uint_1);
        spv::Id face_is_negative = builder_->createBinOp(
            spv::OpINotEqual, type_bool_,
            builder_->createBinOp(spv::OpBitwiseAnd, type_uint_, face,
                                  const_uint_1),
            const_uint_0_);
        spv::Id face_sign =
            builder_->createTriOp(spv::OpSelect, type_float_, face_is_negative,
                                  builder_->makeFloatConstant(-1.0f),
                                  builder_->makeFloatConstant(1.0f));
        // Remap the axes in a way opposite to the ALU cube instruction.
        spv::Id sc_negated = builder_->createNoContractionUnaryOp(
            spv::OpFNegate, type_float_, coordinates[0]);
        spv::Id tc_negated = builder_->createNoContractionUnaryOp(
            spv::OpFNegate, type_float_, coordinates[1]);
        spv::Block& block_ma_head = *builder_->getBuildPoint();
        spv::Block& block_ma_x = builder_->makeNewBlock();
        spv::Block& block_ma_y = builder_->makeNewBlock();
        spv::Block& block_ma_z = builder_->makeNewBlock();
        spv::Block& block_ma_merge = builder_->makeNewBlock();
        builder_->createSelectionMerge(&block_ma_merge,
                                       spv::SelectionControlMaskNone);
        {
          std::unique_ptr<spv::Instruction> ma_switch_op =
              std::make_unique<spv::Instruction>(spv::OpSwitch);
          ma_switch_op->addIdOperand(face_axis);
          // Make Z the default.
          ma_switch_op->addIdOperand(block_ma_z.getId());
          ma_switch_op->addImmediateOperand(0);
          ma_switch_op->addIdOperand(block_ma_x.getId());
          ma_switch_op->addImmediateOperand(1);
          ma_switch_op->addIdOperand(block_ma_y.getId());
          builder_->getBuildPoint()->addInstruction(std::move(ma_switch_op));
        }
        block_ma_x.addPredecessor(&block_ma_head);
        block_ma_y.addPredecessor(&block_ma_head);
        block_ma_z.addPredecessor(&block_ma_head);
        // X is the major axis case.
        builder_->setBuildPoint(&block_ma_x);
        spv::Id ma_x_y = tc_negated;
        spv::Id ma_x_z =
            builder_->createTriOp(spv::OpSelect, type_float_, face_is_negative,
                                  coordinates[0], sc_negated);
        builder_->createBranch(&block_ma_merge);
        // Y is the major axis case.
        builder_->setBuildPoint(&block_ma_y);
        spv::Id ma_y_x = coordinates[0];
        spv::Id ma_y_z =
            builder_->createTriOp(spv::OpSelect, type_float_, face_is_negative,
                                  tc_negated, coordinates[1]);
        builder_->createBranch(&block_ma_merge);
        // Z is the major axis case.
        builder_->setBuildPoint(&block_ma_z);
        spv::Id ma_z_x =
            builder_->createTriOp(spv::OpSelect, type_float_, face_is_negative,
                                  sc_negated, coordinates[0]);
        spv::Id ma_z_y = tc_negated;
        builder_->createBranch(&block_ma_merge);
        // Gather the coordinate components from the branches.
        builder_->setBuildPoint(&block_ma_merge);
        {
          std::unique_ptr<spv::Instruction> x_phi_op =
              std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                 type_float_, spv::OpPhi);
          x_phi_op->addIdOperand(face_sign);
          x_phi_op->addIdOperand(block_ma_x.getId());
          x_phi_op->addIdOperand(ma_y_x);
          x_phi_op->addIdOperand(block_ma_y.getId());
          x_phi_op->addIdOperand(ma_z_x);
          x_phi_op->addIdOperand(block_ma_z.getId());
          coordinates[0] = x_phi_op->getResultId();
          builder_->getBuildPoint()->addInstruction(std::move(x_phi_op));
        }
        {
          std::unique_ptr<spv::Instruction> y_phi_op =
              std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                 type_float_, spv::OpPhi);
          y_phi_op->addIdOperand(ma_x_y);
          y_phi_op->addIdOperand(block_ma_x.getId());
          y_phi_op->addIdOperand(face_sign);
          y_phi_op->addIdOperand(block_ma_y.getId());
          y_phi_op->addIdOperand(ma_z_y);
          y_phi_op->addIdOperand(block_ma_z.getId());
          coordinates[1] = y_phi_op->getResultId();
          builder_->getBuildPoint()->addInstruction(std::move(y_phi_op));
        }
        {
          std::unique_ptr<spv::Instruction> z_phi_op =
              std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                 type_float_, spv::OpPhi);
          z_phi_op->addIdOperand(ma_x_z);
          z_phi_op->addIdOperand(block_ma_x.getId());
          z_phi_op->addIdOperand(ma_y_z);
          z_phi_op->addIdOperand(block_ma_y.getId());
          z_phi_op->addIdOperand(face_sign);
          z_phi_op->addIdOperand(block_ma_z.getId());
          coordinates[2] = z_phi_op->getResultId();
          builder_->getBuildPoint()->addInstruction(std::move(z_phi_op));
        }
      }

      id_vector_temp_.clear();
      id_vector_temp_.push_back(
          builder_->makeIntConstant(kSystemConstantTextureSwizzledSigns));
      id_vector_temp_.push_back(
          builder_->makeIntConstant(fetch_constant_index >> 4));
      id_vector_temp_.push_back(
          builder_->makeIntConstant((fetch_constant_index >> 2) & 3));
      // All 32 bits containing the values for 4 fetch constants (use
      // OpBitFieldUExtract to get the signednesses for the specific components
      // of this texture).
      spv::Id swizzled_signs_word =
          builder_->createLoad(builder_->createAccessChain(
                                   spv::StorageClassUniform,
                                   uniform_system_constants_, id_vector_temp_),
                               spv::NoPrecision);
      uint32_t swizzled_signs_word_offset = 8 * (fetch_constant_index & 3);

      spv::Builder::TextureParameters texture_parameters = {};

      if (instr.opcode == ucode::FetchOpcode::kGetTextureComputedLod) {
        // kGetTextureComputedLod.

        // Check if the signed binding is needs to be accessed rather than the
        // unsigned (if all signednesses are signed).
        spv::Id swizzled_signs_all_signed = builder_->createBinOp(
            spv::OpIEqual, type_bool_,
            builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, swizzled_signs_word,
                builder_->makeUintConstant(swizzled_signs_word_offset),
                builder_->makeUintConstant(8)),
            builder_->makeUintConstant(uint32_t(xenos::TextureSign::kSigned) *
                                       0b01010101));

        // OpImageQueryLod doesn't need the array layer component.
        // So, 3 coordinate components for 3D cube, 2 in other cases (including
        // 1D, which are emulated as 2D arrays).
        // OpSampledImage must be in the same block as where its result is used.
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // Check if the texture is 3D or stacked.
          spv::Block& block_dimension_head = *builder_->getBuildPoint();
          spv::Block& block_dimension_3d_start = builder_->makeNewBlock();
          spv::Block& block_dimension_stacked_start = builder_->makeNewBlock();
          spv::Block& block_dimension_merge = builder_->makeNewBlock();
          builder_->createSelectionMerge(&block_dimension_merge,
                                         spv::SelectionControlDontFlattenMask);
          assert_true(data_is_3d != spv::NoResult);
          builder_->createConditionalBranch(data_is_3d,
                                            &block_dimension_3d_start,
                                            &block_dimension_stacked_start);

          // 3D.
          builder_->setBuildPoint(&block_dimension_3d_start);
          id_vector_temp_.clear();
          for (uint32_t i = 0; i < 3; ++i) {
            id_vector_temp_.push_back(coordinates[i]);
          }
          texture_parameters.coords =
              builder_->createCompositeConstruct(type_float3_, id_vector_temp_);
          spv::Id lod_3d = QueryTextureLod(texture_parameters,
                                           image_3d_unsigned, image_3d_signed,
                                           sampler, swizzled_signs_all_signed);
          // Get the actual build point for phi.
          spv::Block& block_dimension_3d_end = *builder_->getBuildPoint();
          builder_->createBranch(&block_dimension_merge);

          // 2D stacked.
          builder_->setBuildPoint(&block_dimension_stacked_start);
          id_vector_temp_.clear();
          for (uint32_t i = 0; i < 2; ++i) {
            id_vector_temp_.push_back(coordinates[i]);
          }
          texture_parameters.coords =
              builder_->createCompositeConstruct(type_float2_, id_vector_temp_);
          spv::Id lod_stacked = QueryTextureLod(
              texture_parameters, image_2d_array_or_cube_unsigned,
              image_2d_array_or_cube_signed, sampler,
              swizzled_signs_all_signed);
          // Get the actual build point for phi.
          spv::Block& block_dimension_stacked_end = *builder_->getBuildPoint();
          builder_->createBranch(&block_dimension_merge);

          // Choose between the 3D and the stacked result based on the actual
          // data dimensionality.
          builder_->setBuildPoint(&block_dimension_merge);
          {
            std::unique_ptr<spv::Instruction> dimension_phi_op =
                std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                   type_float_, spv::OpPhi);
            dimension_phi_op->addIdOperand(lod_3d);
            dimension_phi_op->addIdOperand(block_dimension_3d_end.getId());
            dimension_phi_op->addIdOperand(lod_stacked);
            dimension_phi_op->addIdOperand(block_dimension_stacked_end.getId());
            result[0] = dimension_phi_op->getResultId();
            builder_->getBuildPoint()->addInstruction(
                std::move(dimension_phi_op));
          }
        } else {
          uint32_t lod_query_coordinate_component_count =
              instr.dimension == xenos::FetchOpDimension::kCube ? 3 : 2;
          id_vector_temp_.clear();
          for (uint32_t i = 0; i < lod_query_coordinate_component_count; ++i) {
            id_vector_temp_.push_back(coordinates[i]);
          }
          texture_parameters.coords = builder_->createCompositeConstruct(
              type_float_vectors_[lod_query_coordinate_component_count - 1],
              id_vector_temp_);
          result[0] = QueryTextureLod(texture_parameters,
                                      image_2d_array_or_cube_unsigned,
                                      image_2d_array_or_cube_signed, sampler,
                                      swizzled_signs_all_signed);
        }
      } else {
        // kTextureFetch.
        assert_true(instr.opcode == ucode::FetchOpcode::kTextureFetch);

        // Extract the signedness for each component of the swizzled result, and
        // get which bindings (unsigned and signed) are needed.
        spv::Id swizzled_signs[4] = {};
        spv::Id result_is_signed[4] = {};
        spv::Id is_all_signed = spv::NoResult;
        spv::Id is_any_signed = spv::NoResult;
        spv::Id const_uint_2 = builder_->makeUintConstant(2);
        spv::Id const_uint_sign_signed =
            builder_->makeUintConstant(uint32_t(xenos::TextureSign::kSigned));
        {
          uint32_t result_remaining_components = used_result_nonzero_components;
          uint32_t result_component_index;
          while (xe::bit_scan_forward(result_remaining_components,
                                      &result_component_index)) {
            result_remaining_components &=
                ~(UINT32_C(1) << result_component_index);
            spv::Id result_component_sign = builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, swizzled_signs_word,
                builder_->makeUintConstant(swizzled_signs_word_offset +
                                           2 * result_component_index),
                const_uint_2);
            swizzled_signs[result_component_index] = result_component_sign;
            spv::Id is_component_signed = builder_->createBinOp(
                spv::OpIEqual, type_bool_, result_component_sign,
                const_uint_sign_signed);
            result_is_signed[result_component_index] = is_component_signed;
            if (is_all_signed != spv::NoResult) {
              is_all_signed =
                  builder_->createBinOp(spv::OpLogicalAnd, type_bool_,
                                        is_all_signed, is_component_signed);
            } else {
              is_all_signed = is_component_signed;
            }
            if (is_any_signed != spv::NoResult) {
              is_any_signed =
                  builder_->createBinOp(spv::OpLogicalOr, type_bool_,
                                        is_any_signed, is_component_signed);
            } else {
              is_any_signed = is_component_signed;
            }
          }
        }

        // Load the fetch constant word 4, needed unconditionally for LOD
        // biasing, for result exponent biasing, and conditionally for stacked
        // texture filtering.
        id_vector_temp_.clear();
        id_vector_temp_.push_back(const_int_0_);
        id_vector_temp_.push_back(builder_->makeIntConstant(
            int((fetch_constant_word_0_index + 4) >> 2)));
        id_vector_temp_.push_back(builder_->makeIntConstant(
            int((fetch_constant_word_0_index + 4) & 3)));
        spv::Id fetch_constant_word_4 =
            builder_->createLoad(builder_->createAccessChain(
                                     spv::StorageClassUniform,
                                     uniform_fetch_constants_, id_vector_temp_),
                                 spv::NoPrecision);
        spv::Id fetch_constant_word_4_signed = builder_->createUnaryOp(
            spv::OpBitcast, type_int_, fetch_constant_word_4);

        // Accumulate the explicit LOD (or LOD bias) sources (in D3D11.3
        // specification order: specified LOD + sampler LOD bias + instruction
        // LOD bias).
        // Fetch constant LOD (bits 12:21 of the word 4).
        spv::Id lod = builder_->createNoContractionBinOp(
            spv::OpFMul, type_float_,
            builder_->createUnaryOp(
                spv::OpConvertSToF, type_float_,
                builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,
                                      fetch_constant_word_4_signed,
                                      builder_->makeUintConstant(12),
                                      builder_->makeUintConstant(10))),
            builder_->makeFloatConstant(1.0f / 32.0f));
        // Register LOD.
        if (instr.attributes.use_register_lod) {
          lod = builder_->createNoContractionBinOp(
              spv::OpFAdd, type_float_,
              builder_->createLoad(var_main_tfetch_lod_, spv::NoPrecision),
              lod);
        }
        // Instruction LOD bias.
        if (instr.attributes.lod_bias) {
          lod = builder_->createNoContractionBinOp(
              spv::OpFAdd, type_float_, lod,
              builder_->makeFloatConstant(instr.attributes.lod_bias));
        }

        // Calculate the gradients for sampling the texture if needed.
        // 2D vectors for k1D (because 1D images are emulated as 2D arrays),
        // k2D.
        // 3D vectors for k3DOrStacked, kCube.
        spv::Id gradients_h = spv::NoResult, gradients_v = spv::NoResult;
        if (use_computed_lod) {
          // TODO(Triang3l): Gradient exponent adjustment is currently not done
          // in getCompTexLOD, so not doing it here too for now. Apply the
          // gradient exponent biases from the word 4 of the fetch constant in
          // the future when it's handled in getCompTexLOD somehow.
          spv::Id lod_gradient_scale = builder_->createUnaryBuiltinCall(
              type_float_, ext_inst_glsl_std_450_, GLSLstd450Exp2, lod);
          switch (instr.dimension) {
            case xenos::FetchOpDimension::k1D: {
              spv::Id gradient_h_1d, gradient_v_1d;
              if (instr.attributes.use_register_gradients) {
                id_vector_temp_.clear();
                // First component.
                id_vector_temp_.push_back(const_int_0_);
                gradient_h_1d = builder_->createLoad(
                    builder_->createAccessChain(spv::StorageClassFunction,
                                                var_main_tfetch_gradients_h_,
                                                id_vector_temp_),
                    spv::NoPrecision);
                gradient_v_1d = builder_->createLoad(
                    builder_->createAccessChain(spv::StorageClassFunction,
                                                var_main_tfetch_gradients_v_,
                                                id_vector_temp_),
                    spv::NoPrecision);
                if (instr.attributes.unnormalized_coordinates) {
                  // Normalize the gradients.
                  assert_true(size[0] != spv::NoResult);
                  gradient_h_1d = builder_->createNoContractionBinOp(
                      spv::OpFDiv, type_float_, gradient_h_1d, size[0]);
                  gradient_v_1d = builder_->createNoContractionBinOp(
                      spv::OpFDiv, type_float_, gradient_v_1d, size[0]);
                }
              } else {
                builder_->addCapability(spv::CapabilityDerivativeControl);
                gradient_h_1d = builder_->createUnaryOp(
                    spv::OpDPdxCoarse, type_float_, coordinates[0]);
                gradient_v_1d = builder_->createUnaryOp(
                    spv::OpDPdyCoarse, type_float_, coordinates[0]);
              }
              gradient_h_1d = builder_->createNoContractionBinOp(
                  spv::OpFMul, type_float_, gradient_h_1d, lod_gradient_scale);
              gradient_v_1d = builder_->createNoContractionBinOp(
                  spv::OpFMul, type_float_, gradient_v_1d, lod_gradient_scale);
              // 1D textures are sampled as 2D arrays - need 2-component
              // gradients.
              id_vector_temp_.clear();
              id_vector_temp_.push_back(gradient_h_1d);
              id_vector_temp_.push_back(const_float_0_);
              gradients_h = builder_->createCompositeConstruct(type_float2_,
                                                               id_vector_temp_);
              id_vector_temp_[0] = gradient_v_1d;
              gradients_v = builder_->createCompositeConstruct(type_float2_,
                                                               id_vector_temp_);
            } break;
            case xenos::FetchOpDimension::k2D: {
              if (instr.attributes.use_register_gradients) {
                for (uint32_t i = 0; i < 2; ++i) {
                  spv::Id register_gradient_3d =
                      builder_->createLoad(i ? var_main_tfetch_gradients_h_
                                             : var_main_tfetch_gradients_v_,
                                           spv::NoPrecision);
                  spv::Id register_gradient_x =
                      builder_->createCompositeExtract(register_gradient_3d,
                                                       type_float_, 0);
                  spv::Id register_gradient_y =
                      builder_->createCompositeExtract(register_gradient_3d,
                                                       type_float_, 1);
                  if (instr.attributes.unnormalized_coordinates) {
                    // Normalize the gradients.
                    assert_true(size[0] != spv::NoResult);
                    register_gradient_x = builder_->createNoContractionBinOp(
                        spv::OpFDiv, type_float_, register_gradient_x, size[0]);
                    assert_true(size[1] != spv::NoResult);
                    register_gradient_y = builder_->createNoContractionBinOp(
                        spv::OpFDiv, type_float_, register_gradient_y, size[1]);
                  }
                  id_vector_temp_.clear();
                  id_vector_temp_.push_back(register_gradient_x);
                  id_vector_temp_.push_back(register_gradient_y);
                  (i ? gradients_v : gradients_h) =
                      builder_->createCompositeConstruct(type_float2_,
                                                         id_vector_temp_);
                }
              } else {
                id_vector_temp_.clear();
                for (uint32_t i = 0; i < 2; ++i) {
                  id_vector_temp_.push_back(coordinates[i]);
                }
                spv::Id gradient_coordinate_vector =
                    builder_->createCompositeConstruct(type_float2_,
                                                       id_vector_temp_);
                builder_->addCapability(spv::CapabilityDerivativeControl);
                gradients_h =
                    builder_->createUnaryOp(spv::OpDPdxCoarse, type_float2_,
                                            gradient_coordinate_vector);
                gradients_v =
                    builder_->createUnaryOp(spv::OpDPdyCoarse, type_float2_,
                                            gradient_coordinate_vector);
              }
              gradients_h = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, type_float2_, gradients_h,
                  lod_gradient_scale);
              gradients_v = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, type_float2_, gradients_v,
                  lod_gradient_scale);
            } break;
            case xenos::FetchOpDimension::k3DOrStacked: {
              if (instr.attributes.use_register_gradients) {
                gradients_h = builder_->createLoad(var_main_tfetch_gradients_h_,
                                                   spv::NoPrecision);
                gradients_v = builder_->createLoad(var_main_tfetch_gradients_v_,
                                                   spv::NoPrecision);
                if (instr.attributes.unnormalized_coordinates) {
                  // Normalize the gradients.
                  for (uint32_t i = 0; i < 2; ++i) {
                    spv::Id& gradient_ref = i ? gradients_v : gradients_h;
                    id_vector_temp_.clear();
                    for (uint32_t j = 0; j < 3; ++j) {
                      assert_true(size[j] != spv::NoResult);
                      id_vector_temp_.push_back(
                          builder_->createNoContractionBinOp(
                              spv::OpFDiv, type_float_,
                              builder_->createCompositeExtract(gradient_ref,
                                                               type_float_, j),
                              size[j]));
                    }
                    gradient_ref = builder_->createCompositeConstruct(
                        type_float3_, id_vector_temp_);
                  }
                }
              } else {
                id_vector_temp_.clear();
                for (uint32_t i = 0; i < 3; ++i) {
                  id_vector_temp_.push_back(coordinates[i]);
                }
                spv::Id gradient_coordinate_vector =
                    builder_->createCompositeConstruct(type_float3_,
                                                       id_vector_temp_);
                builder_->addCapability(spv::CapabilityDerivativeControl);
                gradients_h =
                    builder_->createUnaryOp(spv::OpDPdxCoarse, type_float3_,
                                            gradient_coordinate_vector);
                gradients_v =
                    builder_->createUnaryOp(spv::OpDPdyCoarse, type_float3_,
                                            gradient_coordinate_vector);
              }
              gradients_h = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, type_float3_, gradients_h,
                  lod_gradient_scale);
              gradients_v = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, type_float3_, gradients_v,
                  lod_gradient_scale);
            } break;
            case xenos::FetchOpDimension::kCube: {
              if (instr.attributes.use_register_gradients) {
                // Register gradients are already in the cube space for cube
                // maps.
                // TODO(Triang3l): Are cube map register gradients unnormalized
                // if the coordinates themselves are unnormalized?
                gradients_h = builder_->createLoad(var_main_tfetch_gradients_h_,
                                                   spv::NoPrecision);
                gradients_v = builder_->createLoad(var_main_tfetch_gradients_v_,
                                                   spv::NoPrecision);
              } else {
                id_vector_temp_.clear();
                for (uint32_t i = 0; i < 3; ++i) {
                  id_vector_temp_.push_back(coordinates[i]);
                }
                spv::Id gradient_coordinate_vector =
                    builder_->createCompositeConstruct(type_float3_,
                                                       id_vector_temp_);
                builder_->addCapability(spv::CapabilityDerivativeControl);
                gradients_h =
                    builder_->createUnaryOp(spv::OpDPdxCoarse, type_float3_,
                                            gradient_coordinate_vector);
                gradients_v =
                    builder_->createUnaryOp(spv::OpDPdyCoarse, type_float3_,
                                            gradient_coordinate_vector);
              }
              gradients_h = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, type_float3_, gradients_h,
                  lod_gradient_scale);
              gradients_v = builder_->createNoContractionBinOp(
                  spv::OpVectorTimesScalar, type_float3_, gradients_v,
                  lod_gradient_scale);
            } break;
          }
        }

        // Sample the texture.
        spv::ImageOperandsMask image_operands_mask =
            use_computed_lod ? spv::ImageOperandsGradMask
                             : spv::ImageOperandsLodMask;
        spv::Id sample_result_unsigned, sample_result_signed;
        if (!use_computed_lod) {
          texture_parameters.lod = lod;
        }
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked) {
          // 3D (3 coordinate components, 3 gradient components, single fetch)
          // or 2D stacked (2 coordinate components + 1 array layer coordinate
          // component, 2 gradient components, two fetches if the Z axis is
          // linear-filtered).

          spv::Block& block_dimension_head = *builder_->getBuildPoint();
          spv::Block& block_dimension_3d_start = builder_->makeNewBlock();
          spv::Block& block_dimension_stacked_start = builder_->makeNewBlock();
          spv::Block& block_dimension_merge = builder_->makeNewBlock();
          builder_->createSelectionMerge(&block_dimension_merge,
                                         spv::SelectionControlDontFlattenMask);
          assert_true(data_is_3d != spv::NoResult);
          builder_->createConditionalBranch(data_is_3d,
                                            &block_dimension_3d_start,
                                            &block_dimension_stacked_start);

          // 3D.
          builder_->setBuildPoint(&block_dimension_3d_start);
          if (use_computed_lod) {
            texture_parameters.gradX = gradients_h;
            texture_parameters.gradY = gradients_v;
          }
          id_vector_temp_.clear();
          for (uint32_t i = 0; i < 3; ++i) {
            id_vector_temp_.push_back(coordinates[i]);
          }
          texture_parameters.coords =
              builder_->createCompositeConstruct(type_float3_, id_vector_temp_);
          spv::Id sample_result_unsigned_3d, sample_result_signed_3d;
          SampleTexture(texture_parameters, image_operands_mask,
                        image_3d_unsigned, image_3d_signed, sampler,
                        is_all_signed, is_any_signed, sample_result_unsigned_3d,
                        sample_result_signed_3d);
          // Get the actual build point after the SampleTexture call for phi.
          spv::Block& block_dimension_3d_end = *builder_->getBuildPoint();
          builder_->createBranch(&block_dimension_merge);

          // 2D stacked.
          builder_->setBuildPoint(&block_dimension_stacked_start);
          if (use_computed_lod) {
            // Extract 2D gradients for stacked textures which are 2D arrays.
            uint_vector_temp_.clear();
            uint_vector_temp_.push_back(0);
            uint_vector_temp_.push_back(1);
            texture_parameters.gradX = builder_->createRvalueSwizzle(
                spv::NoPrecision, type_float2_, gradients_h, uint_vector_temp_);
            texture_parameters.gradY = builder_->createRvalueSwizzle(
                spv::NoPrecision, type_float2_, gradients_v, uint_vector_temp_);
          }
          // Check if linear filtering is needed.
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
          spv::Id vol_filter_is_linear = spv::NoResult;
          if (use_computed_lod &&
              (vol_mag_filter_is_fetch_const || vol_min_filter_is_fetch_const ||
               vol_mag_filter_is_linear != vol_min_filter_is_linear)) {
            // Check if minifying along layers (derivative > 1 along any axis).
            spv::Id layer_max_gradient = builder_->createBinBuiltinCall(
                type_float_, ext_inst_glsl_std_450_, GLSLstd450NMax,
                builder_->createCompositeExtract(gradients_h, type_float_, 2),
                builder_->createCompositeExtract(gradients_v, type_float_, 2));
            if (!instr.attributes.unnormalized_coordinates) {
              // Denormalize the gradient if provided as normalized.
              assert_true(size[2] != spv::NoResult);
              layer_max_gradient = builder_->createNoContractionBinOp(
                  spv::OpFMul, type_float_, layer_max_gradient, size[2]);
            }
            // For NaN, considering that magnification is being done.
            spv::Id is_minifying_z = builder_->createBinOp(
                spv::OpFOrdLessThan, type_bool_, layer_max_gradient,
                builder_->makeFloatConstant(1.0f));
            // Choose what filter is actually used, the minification or the
            // magnification one.
            spv::Id vol_mag_filter_is_linear_loaded =
                vol_mag_filter_is_fetch_const
                    ? builder_->createBinOp(
                          spv::OpINotEqual, type_bool_,
                          builder_->createBinOp(
                              spv::OpBitwiseAnd, type_uint_,
                              fetch_constant_word_4,
                              builder_->makeUintConstant(UINT32_C(1) << 0)),
                          const_uint_0_)
                    : builder_->makeBoolConstant(vol_mag_filter_is_linear);
            spv::Id vol_min_filter_is_linear_loaded =
                vol_min_filter_is_fetch_const
                    ? builder_->createBinOp(
                          spv::OpINotEqual, type_bool_,
                          builder_->createBinOp(
                              spv::OpBitwiseAnd, type_uint_,
                              fetch_constant_word_4,
                              builder_->makeUintConstant(UINT32_C(1) << 1)),
                          const_uint_0_)
                    : builder_->makeBoolConstant(vol_min_filter_is_linear);
            vol_filter_is_linear =
                builder_->createTriOp(spv::OpSelect, type_bool_, is_minifying_z,
                                      vol_min_filter_is_linear_loaded,
                                      vol_mag_filter_is_linear_loaded);
          } else {
            // No gradients, or using the same filter overrides for magnifying
            // and minifying. Assume always magnifying if no gradients (LOD 0,
            // always <= 0). LOD is within 2D layers, not between them (unlike
            // in 3D textures, which have mips with depth reduced), so it
            // shouldn't have effect on filtering between layers.
            if (vol_mag_filter_is_fetch_const) {
              vol_filter_is_linear = builder_->createBinOp(
                  spv::OpINotEqual, type_bool_,
                  builder_->createBinOp(
                      spv::OpBitwiseAnd, type_uint_, fetch_constant_word_4,
                      builder_->makeUintConstant(UINT32_C(1) << 0)),
                  const_uint_0_);
            }
          }
          spv::Id layer_coordinate = coordinates[2];
          // Linear filtering may be needed either based on a dynamic condition
          // (the filtering mode is taken from the fetch constant, or it's
          // different for magnification and minification), or on a static one
          // (with gradients - specified in the instruction for both
          // magnification and minification as linear, without gradients -
          // specified for magnification as linear).
          // If the filter is linear, subtract 0.5 from the Z coordinate of the
          // first layer in filtering because 0.5 is in the middle of it.
          if (vol_filter_is_linear != spv::NoResult) {
            layer_coordinate = builder_->createTriOp(
                spv::OpSelect, type_float_, vol_filter_is_linear,
                builder_->createNoContractionBinOp(
                    spv::OpFSub, type_float_, layer_coordinate,
                    builder_->makeFloatConstant(0.5f)),
                layer_coordinate);
          } else if (vol_mag_filter_is_linear) {
            layer_coordinate = builder_->createNoContractionBinOp(
                spv::OpFSub, type_float_, layer_coordinate,
                builder_->makeFloatConstant(0.5f));
          }
          // Sample the first layer, needed regardless of whether filtering is
          // needed.
          // Floor the array layer (Vulkan does rounding to nearest or + 0.5 and
          // floor even for the layer index, but on the Xenos, addressing is
          // similar to that of 3D textures). This is needed for both point and
          // linear filtering (with linear, 0.5 was subtracted previously).
          spv::Id layer_0_coordinate = builder_->createUnaryBuiltinCall(
              type_float_, ext_inst_glsl_std_450_, GLSLstd450Floor,
              layer_coordinate);
          id_vector_temp_.clear();
          id_vector_temp_.push_back(coordinates[0]);
          id_vector_temp_.push_back(coordinates[1]);
          id_vector_temp_.push_back(layer_0_coordinate);
          texture_parameters.coords =
              builder_->createCompositeConstruct(type_float3_, id_vector_temp_);
          spv::Id sample_result_unsigned_stacked, sample_result_signed_stacked;
          SampleTexture(texture_parameters, image_operands_mask,
                        image_2d_array_or_cube_unsigned,
                        image_2d_array_or_cube_signed, sampler, is_all_signed,
                        is_any_signed, sample_result_unsigned_stacked,
                        sample_result_signed_stacked);
          // Sample the second layer if linear filtering is potentially needed
          // (conditionally or unconditionally, depending on whether the filter
          // needs to be chosen at runtime), and filter.
          if (vol_filter_is_linear != spv::NoResult ||
              vol_mag_filter_is_linear) {
            spv::Block& block_z_head = *builder_->getBuildPoint();
            spv::Block& block_z_linear = (vol_filter_is_linear != spv::NoResult)
                                             ? builder_->makeNewBlock()
                                             : block_z_head;
            spv::Block& block_z_merge = (vol_filter_is_linear != spv::NoResult)
                                            ? builder_->makeNewBlock()
                                            : block_z_head;
            if (vol_filter_is_linear != spv::NoResult) {
              builder_->createSelectionMerge(
                  &block_z_merge, spv::SelectionControlDontFlattenMask);
              builder_->createConditionalBranch(
                  vol_filter_is_linear, &block_z_linear, &block_z_merge);
              builder_->setBuildPoint(&block_z_linear);
            }
            spv::Id layer_1_coordinate = builder_->createBinOp(
                spv::OpFAdd, type_float_, layer_0_coordinate,
                builder_->makeFloatConstant(1.0f));
            id_vector_temp_.clear();
            id_vector_temp_.push_back(coordinates[0]);
            id_vector_temp_.push_back(coordinates[1]);
            id_vector_temp_.push_back(layer_1_coordinate);
            texture_parameters.coords = builder_->createCompositeConstruct(
                type_float3_, id_vector_temp_);
            spv::Id layer_lerp_factor = builder_->createUnaryBuiltinCall(
                type_float_, ext_inst_glsl_std_450_, GLSLstd450Fract,
                layer_coordinate);
            spv::Id sample_result_unsigned_stacked_filtered;
            spv::Id sample_result_signed_stacked_filtered;
            SampleTexture(
                texture_parameters, image_operands_mask,
                image_2d_array_or_cube_unsigned, image_2d_array_or_cube_signed,
                sampler, is_all_signed, is_any_signed,
                sample_result_unsigned_stacked_filtered,
                sample_result_signed_stacked_filtered, layer_lerp_factor,
                sample_result_unsigned_stacked, sample_result_signed_stacked);
            if (vol_filter_is_linear != spv::NoResult) {
              // Get the actual build point after the SampleTexture call for
              // phi.
              spv::Block& block_z_linear_end = *builder_->getBuildPoint();
              builder_->createBranch(&block_z_merge);
              builder_->setBuildPoint(&block_z_merge);
              {
                std::unique_ptr<spv::Instruction> filter_phi_op =
                    std::make_unique<spv::Instruction>(
                        builder_->getUniqueId(), type_float4_, spv::OpPhi);
                filter_phi_op->addIdOperand(
                    sample_result_unsigned_stacked_filtered);
                filter_phi_op->addIdOperand(block_z_linear_end.getId());
                filter_phi_op->addIdOperand(sample_result_unsigned_stacked);
                filter_phi_op->addIdOperand(block_z_head.getId());
                sample_result_unsigned_stacked = filter_phi_op->getResultId();
                builder_->getBuildPoint()->addInstruction(
                    std::move(filter_phi_op));
              }
              {
                std::unique_ptr<spv::Instruction> filter_phi_op =
                    std::make_unique<spv::Instruction>(
                        builder_->getUniqueId(), type_float4_, spv::OpPhi);
                filter_phi_op->addIdOperand(
                    sample_result_signed_stacked_filtered);
                filter_phi_op->addIdOperand(block_z_linear_end.getId());
                filter_phi_op->addIdOperand(sample_result_signed_stacked);
                filter_phi_op->addIdOperand(block_z_head.getId());
                sample_result_signed_stacked = filter_phi_op->getResultId();
                builder_->getBuildPoint()->addInstruction(
                    std::move(filter_phi_op));
              }
            } else {
              sample_result_unsigned_stacked =
                  sample_result_unsigned_stacked_filtered;
              sample_result_signed_stacked =
                  sample_result_signed_stacked_filtered;
            }
          }
          // Get the actual build point for phi.
          spv::Block& block_dimension_stacked_end = *builder_->getBuildPoint();
          builder_->createBranch(&block_dimension_merge);

          // Choose between the 3D and the stacked result based on the actual
          // data dimensionality.
          builder_->setBuildPoint(&block_dimension_merge);
          {
            std::unique_ptr<spv::Instruction> dimension_phi_op =
                std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                   type_float4_, spv::OpPhi);
            dimension_phi_op->addIdOperand(sample_result_unsigned_3d);
            dimension_phi_op->addIdOperand(block_dimension_3d_end.getId());
            dimension_phi_op->addIdOperand(sample_result_unsigned_stacked);
            dimension_phi_op->addIdOperand(block_dimension_stacked_end.getId());
            sample_result_unsigned = dimension_phi_op->getResultId();
            builder_->getBuildPoint()->addInstruction(
                std::move(dimension_phi_op));
          }
          {
            std::unique_ptr<spv::Instruction> dimension_phi_op =
                std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                   type_float4_, spv::OpPhi);
            dimension_phi_op->addIdOperand(sample_result_signed_3d);
            dimension_phi_op->addIdOperand(block_dimension_3d_end.getId());
            dimension_phi_op->addIdOperand(sample_result_signed_stacked);
            dimension_phi_op->addIdOperand(block_dimension_stacked_end.getId());
            sample_result_signed = dimension_phi_op->getResultId();
            builder_->getBuildPoint()->addInstruction(
                std::move(dimension_phi_op));
          }
        } else {
          if (use_computed_lod) {
            texture_parameters.gradX = gradients_h;
            texture_parameters.gradY = gradients_v;
          }
          id_vector_temp_.clear();
          for (uint32_t i = 0; i < 3; ++i) {
            id_vector_temp_.push_back(coordinates[i]);
          }
          texture_parameters.coords =
              builder_->createCompositeConstruct(type_float3_, id_vector_temp_);
          SampleTexture(texture_parameters, image_operands_mask,
                        image_2d_array_or_cube_unsigned,
                        image_2d_array_or_cube_signed, sampler, is_all_signed,
                        is_any_signed, sample_result_unsigned,
                        sample_result_signed);
        }

        // Swizzle the result components manually if needed, to `result`.
        // Because the same host format component may be replicated into
        // multiple guest components (such as for formats with less than 4
        // components), yet the signedness is per-guest-component, it's not
        // possible to apply the signedness to host components before swizzling,
        // so doing it during (for unsigned vs. signed) and after (for biased
        // and gamma) swizzling.
        if (!features_.image_view_format_swizzle) {
          id_vector_temp_.clear();
          id_vector_temp_.push_back(
              builder_->makeIntConstant(kSystemConstantTextureSwizzles));
          id_vector_temp_.push_back(
              builder_->makeIntConstant(fetch_constant_index >> 3));
          id_vector_temp_.push_back(
              builder_->makeIntConstant((fetch_constant_index >> 1) & 3));
          // All 32 bits containing the values (24 bits) for 2 fetch constants.
          spv::Id swizzle_word = builder_->createLoad(
              builder_->createAccessChain(spv::StorageClassUniform,
                                          uniform_system_constants_,
                                          id_vector_temp_),
              spv::NoPrecision);
          uint32_t swizzle_word_offset = 3 * 4 * (fetch_constant_index & 1);
          spv::Id const_float_1 = builder_->makeFloatConstant(1.0f);
          uint32_t result_remaining_components = used_result_nonzero_components;
          uint32_t result_component_index;
          while (xe::bit_scan_forward(result_remaining_components,
                                      &result_component_index)) {
            result_remaining_components &=
                ~(UINT32_C(1) << result_component_index);
            uint32_t swizzle_bit_0_value =
                UINT32_C(1)
                << (swizzle_word_offset + 3 * result_component_index);
            spv::Id swizzle_bit_0 = builder_->createBinOp(
                spv::OpINotEqual, type_bool_,
                builder_->createBinOp(
                    spv::OpBitwiseAnd, type_uint_, swizzle_word,
                    builder_->makeUintConstant(swizzle_bit_0_value)),
                const_uint_0_);
            // Bit 2 - X/Y/Z/W or 0/1.
            spv::Id swizzle_bit_2 = builder_->createBinOp(
                spv::OpINotEqual, type_bool_,
                builder_->createBinOp(
                    spv::OpBitwiseAnd, type_uint_, swizzle_word,
                    builder_->makeUintConstant(swizzle_bit_0_value << 2)),
                const_uint_0_);
            spv::Block& block_swizzle_head = *builder_->getBuildPoint();
            spv::Block& block_swizzle_constant = builder_->makeNewBlock();
            spv::Block& block_swizzle_component = builder_->makeNewBlock();
            spv::Block& block_swizzle_merge = builder_->makeNewBlock();
            builder_->createSelectionMerge(
                &block_swizzle_merge, spv::SelectionControlDontFlattenMask);
            builder_->createConditionalBranch(swizzle_bit_2,
                                              &block_swizzle_constant,
                                              &block_swizzle_component);
            // Constant values.
            builder_->setBuildPoint(&block_swizzle_constant);
            // Bit 0 - 0 or 1.
            spv::Id swizzle_result_constant =
                builder_->createTriOp(spv::OpSelect, type_float_, swizzle_bit_0,
                                      const_float_1, const_float_0_);
            builder_->createBranch(&block_swizzle_merge);
            // Fetched components.
            spv::Id swizzle_result_component;
            {
              builder_->setBuildPoint(&block_swizzle_component);
              // Select whether the result is signed or unsigned (or biased or
              // gamma-corrected) based on the post-swizzle signedness.
              spv::Id swizzle_sample_result = builder_->createTriOp(
                  spv::OpSelect, type_float4_,
                  builder_->smearScalar(
                      spv::NoPrecision,
                      result_is_signed[result_component_index], type_bool4_),
                  sample_result_signed, sample_result_unsigned);
              // Bit 0 - X or Y, Z or W, 0 or 1.
              spv::Id swizzle_x_or_y = builder_->createTriOp(
                  spv::OpSelect, type_float_, swizzle_bit_0,
                  builder_->createCompositeExtract(swizzle_sample_result,
                                                   type_float_, 1),
                  builder_->createCompositeExtract(swizzle_sample_result,
                                                   type_float_, 0));
              spv::Id swizzle_z_or_w = builder_->createTriOp(
                  spv::OpSelect, type_float_, swizzle_bit_0,
                  builder_->createCompositeExtract(swizzle_sample_result,
                                                   type_float_, 3),
                  builder_->createCompositeExtract(swizzle_sample_result,
                                                   type_float_, 2));
              // Bit 1 - X/Y or Z/W.
              spv::Id swizzle_bit_1 = builder_->createBinOp(
                  spv::OpINotEqual, type_bool_,
                  builder_->createBinOp(
                      spv::OpBitwiseAnd, type_uint_, swizzle_word,
                      builder_->makeUintConstant(swizzle_bit_0_value << 1)),
                  const_uint_0_);
              swizzle_result_component = builder_->createTriOp(
                  spv::OpSelect, type_float_, swizzle_bit_1, swizzle_z_or_w,
                  swizzle_x_or_y);
              builder_->createBranch(&block_swizzle_merge);
            }
            // Select between the constants and the fetched components.
            builder_->setBuildPoint(&block_swizzle_merge);
            {
              std::unique_ptr<spv::Instruction> swizzle_phi_op =
                  std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                     type_float_, spv::OpPhi);
              swizzle_phi_op->addIdOperand(swizzle_result_constant);
              swizzle_phi_op->addIdOperand(block_swizzle_constant.getId());
              swizzle_phi_op->addIdOperand(swizzle_result_component);
              swizzle_phi_op->addIdOperand(block_swizzle_component.getId());
              result[result_component_index] = swizzle_phi_op->getResultId();
              builder_->getBuildPoint()->addInstruction(
                  std::move(swizzle_phi_op));
            }
          }
        }

        // Apply the signednesses to all the needed components. If swizzling is
        // done in the shader rather than via the image view, unsigned or signed
        // source has already been selected into `result` - only need to bias or
        // to gamma-correct.
        spv::Id const_float_2 = builder_->makeFloatConstant(2.0f);
        spv::Id const_float_minus_1 = builder_->makeFloatConstant(-1.0f);
        {
          uint32_t result_remaining_components = used_result_nonzero_components;
          uint32_t result_component_index;
          while (xe::bit_scan_forward(result_remaining_components,
                                      &result_component_index)) {
            result_remaining_components &=
                ~(UINT32_C(1) << result_component_index);
            spv::Id sample_result_component_unsigned =
                features_.image_view_format_swizzle
                    ? builder_->createCompositeExtract(sample_result_unsigned,
                                                       type_float_,
                                                       result_component_index)
                    : result[result_component_index];
            spv::Block& block_sign_head = *builder_->getBuildPoint();
            spv::Block* block_sign_signed = features_.image_view_format_swizzle
                                                ? &builder_->makeNewBlock()
                                                : nullptr;
            spv::Block& block_sign_unsigned_biased = builder_->makeNewBlock();
            spv::Block& block_sign_gamma_start = builder_->makeNewBlock();
            spv::Block& block_sign_merge = builder_->makeNewBlock();
            builder_->createSelectionMerge(
                &block_sign_merge, spv::SelectionControlDontFlattenMask);
            {
              std::unique_ptr<spv::Instruction> sign_switch_op =
                  std::make_unique<spv::Instruction>(spv::OpSwitch);
              sign_switch_op->addIdOperand(
                  swizzled_signs[result_component_index]);
              // Make unsigned (do nothing, take the unsigned component in the
              // phi) the default, and also, if unsigned or signed has already
              // been selected in swizzling, make signed the default to since
              // it, just like unsigned, doesn't need any transformations.
              sign_switch_op->addIdOperand(block_sign_merge.getId());
              if (block_sign_signed) {
                sign_switch_op->addImmediateOperand(
                    uint32_t(xenos::TextureSign::kSigned));
                sign_switch_op->addIdOperand(block_sign_signed->getId());
              }
              sign_switch_op->addImmediateOperand(
                  uint32_t(xenos::TextureSign::kUnsignedBiased));
              sign_switch_op->addIdOperand(block_sign_unsigned_biased.getId());
              sign_switch_op->addImmediateOperand(
                  uint32_t(xenos::TextureSign::kGamma));
              sign_switch_op->addIdOperand(block_sign_gamma_start.getId());
              builder_->getBuildPoint()->addInstruction(
                  std::move(sign_switch_op));
            }
            if (block_sign_signed) {
              block_sign_signed->addPredecessor(&block_sign_head);
            }
            block_sign_unsigned_biased.addPredecessor(&block_sign_head);
            block_sign_gamma_start.addPredecessor(&block_sign_head);
            block_sign_merge.addPredecessor(&block_sign_head);
            // Signed.
            spv::Id sample_result_component_signed =
                sample_result_component_unsigned;
            if (block_sign_signed) {
              builder_->setBuildPoint(block_sign_signed);
              sample_result_component_signed = builder_->createCompositeExtract(
                  sample_result_signed, type_float_, result_component_index);
              builder_->createBranch(&block_sign_merge);
            }
            // Unsigned biased.
            builder_->setBuildPoint(&block_sign_unsigned_biased);
            spv::Id sample_result_component_unsigned_biased =
                builder_->createNoContractionBinOp(
                    spv::OpFMul, type_float_, sample_result_component_unsigned,
                    const_float_2);
            sample_result_component_unsigned_biased =
                builder_->createNoContractionBinOp(
                    spv::OpFAdd, type_float_,
                    sample_result_component_unsigned_biased,
                    const_float_minus_1);
            builder_->createBranch(&block_sign_merge);
            // Gamma.
            builder_->setBuildPoint(&block_sign_gamma_start);
            // TODO(Triang3l): Gamma resolve target as sRGB sampling.
            spv::Id sample_result_component_gamma =
                PWLGammaToLinear(sample_result_component_unsigned, false);
            // Get the current build point for the phi operation not to assume
            // that it will be the same as before PWLGammaToLinear.
            spv::Block& block_sign_gamma_end = *builder_->getBuildPoint();
            builder_->createBranch(&block_sign_merge);
            // Merge.
            builder_->setBuildPoint(&block_sign_merge);
            {
              std::unique_ptr<spv::Instruction> sign_phi_op =
                  std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                     type_float_, spv::OpPhi);
              if (block_sign_signed) {
                sign_phi_op->addIdOperand(sample_result_component_signed);
                sign_phi_op->addIdOperand(block_sign_signed->getId());
              }
              sign_phi_op->addIdOperand(
                  sample_result_component_unsigned_biased);
              sign_phi_op->addIdOperand(block_sign_unsigned_biased.getId());
              sign_phi_op->addIdOperand(sample_result_component_gamma);
              sign_phi_op->addIdOperand(block_sign_gamma_end.getId());
              sign_phi_op->addIdOperand(sample_result_component_unsigned);
              sign_phi_op->addIdOperand(block_sign_head.getId());
              result[result_component_index] = sign_phi_op->getResultId();
              builder_->getBuildPoint()->addInstruction(std::move(sign_phi_op));
            }
          }
        }

        // Apply the exponent bias from the bits 13:18 of the fetch constant
        // word 4.
        spv::Id result_exponent_bias = builder_->createBinBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450Ldexp,
            const_float_1_,
            builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,
                                  fetch_constant_word_4_signed,
                                  builder_->makeUintConstant(13),
                                  builder_->makeUintConstant(6)));
        {
          uint32_t result_remaining_components = used_result_nonzero_components;
          uint32_t result_component_index;
          while (xe::bit_scan_forward(result_remaining_components,
                                      &result_component_index)) {
            result_remaining_components &=
                ~(UINT32_C(1) << result_component_index);
            result[result_component_index] = builder_->createNoContractionBinOp(
                spv::OpFMul, type_float_, result[result_component_index],
                result_exponent_bias);
          }
        }
      }
    }
  }

  // Store the needed components of the result.
  spv::Id result_vector;
  if (used_result_component_count > 1) {
    id_vector_temp_.clear();
    uint32_t result_components_remaining = used_result_components;
    uint32_t result_component_index;
    while (xe::bit_scan_forward(result_components_remaining,
                                &result_component_index)) {
      result_components_remaining &= ~(UINT32_C(1) << result_component_index);
      id_vector_temp_.push_back(result[result_component_index]);
    }
    result_vector = builder_->createCompositeConstruct(
        type_float_vectors_[used_result_component_count - 1], id_vector_temp_);
  } else {
    uint32_t result_component_index;
    xe::bit_scan_forward(used_result_components, &result_component_index);
    result_vector = result[result_component_index];
  }
  StoreResult(instr.result, result_vector);
}

size_t SpirvShaderTranslator::FindOrAddTextureBinding(
    uint32_t fetch_constant, xenos::FetchOpDimension dimension,
    bool is_signed) {
  // 1D and 2D textures (including stacked ones) are treated as 2D arrays for
  // binding and coordinate simplicity.
  if (dimension == xenos::FetchOpDimension::k1D) {
    dimension = xenos::FetchOpDimension::k2D;
  }
  for (size_t i = 0; i < texture_bindings_.size(); ++i) {
    const TextureBinding& texture_binding = texture_bindings_[i];
    if (texture_binding.fetch_constant == fetch_constant &&
        texture_binding.dimension == dimension &&
        texture_binding.is_signed == is_signed) {
      return i;
    }
  }
  // TODO(Triang3l): Limit the total count to that actually supported by the
  // implementation.
  size_t new_texture_binding_index = texture_bindings_.size();
  TextureBinding& new_texture_binding = texture_bindings_.emplace_back();
  new_texture_binding.fetch_constant = fetch_constant;
  new_texture_binding.dimension = dimension;
  new_texture_binding.is_signed = is_signed;
  spv::Dim type_dimension;
  bool is_array;
  const char* dimension_name;
  switch (dimension) {
    case xenos::FetchOpDimension::k3DOrStacked:
      type_dimension = spv::Dim3D;
      is_array = false;
      dimension_name = "3d";
      break;
    case xenos::FetchOpDimension::kCube:
      type_dimension = spv::DimCube;
      is_array = false;
      dimension_name = "cube";
      break;
    default:
      type_dimension = spv::Dim2D;
      is_array = true;
      dimension_name = "2d";
  }
  new_texture_binding.variable = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassUniformConstant,
      builder_->makeImageType(type_float_, type_dimension, false, is_array,
                              false, 1, spv::ImageFormatUnknown),
      fmt::format("xe_texture{}_{}_{}", fetch_constant, dimension_name,
                  is_signed ? 's' : 'u')
          .c_str());
  builder_->addDecoration(
      new_texture_binding.variable, spv::DecorationDescriptorSet,
      int(is_vertex_shader() ? kDescriptorSetTexturesVertex
                             : kDescriptorSetTexturesPixel));
  builder_->addDecoration(new_texture_binding.variable, spv::DecorationBinding,
                          int(new_texture_binding_index));
  if (features_.spirv_version >= spv::Spv_1_4) {
    main_interface_.push_back(new_texture_binding.variable);
  }
  return new_texture_binding_index;
}

size_t SpirvShaderTranslator::FindOrAddSamplerBinding(
    uint32_t fetch_constant, xenos::TextureFilter mag_filter,
    xenos::TextureFilter min_filter, xenos::TextureFilter mip_filter,
    xenos::AnisoFilter aniso_filter) {
  if (aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    // TODO(Triang3l): Limit to what's actually supported by the implementation.
    aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  }
  for (size_t i = 0; i < sampler_bindings_.size(); ++i) {
    const SamplerBinding& sampler_binding = sampler_bindings_[i];
    if (sampler_binding.fetch_constant == fetch_constant &&
        sampler_binding.mag_filter == mag_filter &&
        sampler_binding.min_filter == min_filter &&
        sampler_binding.mip_filter == mip_filter &&
        sampler_binding.aniso_filter == aniso_filter) {
      return i;
    }
  }
  // TODO(Triang3l): Limit the total count to that actually supported by the
  // implementation.
  size_t new_sampler_binding_index = sampler_bindings_.size();
  SamplerBinding& new_sampler_binding = sampler_bindings_.emplace_back();
  new_sampler_binding.fetch_constant = fetch_constant;
  new_sampler_binding.mag_filter = mag_filter;
  new_sampler_binding.min_filter = min_filter;
  new_sampler_binding.mip_filter = mip_filter;
  new_sampler_binding.aniso_filter = aniso_filter;
  std::ostringstream name;
  static const char kFilterSuffixes[] = {'p', 'l', 'b', 'f'};
  name << "xe_sampler" << fetch_constant << '_'
       << kFilterSuffixes[uint32_t(mag_filter)]
       << kFilterSuffixes[uint32_t(min_filter)]
       << kFilterSuffixes[uint32_t(mip_filter)];
  if (aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    if (aniso_filter == xenos::AnisoFilter::kDisabled) {
      name << "_a0";
    } else {
      name << "_a" << (UINT32_C(1) << (uint32_t(aniso_filter) - 1));
    }
  }
  new_sampler_binding.variable = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassUniformConstant,
      builder_->makeSamplerType(), name.str().c_str());
  builder_->addDecoration(
      new_sampler_binding.variable, spv::DecorationDescriptorSet,
      int(is_vertex_shader() ? kDescriptorSetTexturesVertex
                             : kDescriptorSetTexturesPixel));
  // The binding indices will be specified later after all textures are added as
  // samplers are located after images in the descriptor set.
  if (features_.spirv_version >= spv::Spv_1_4) {
    main_interface_.push_back(new_sampler_binding.variable);
  }
  return new_sampler_binding_index;
}

void SpirvShaderTranslator::SampleTexture(
    spv::Builder::TextureParameters& texture_parameters,
    spv::ImageOperandsMask image_operands_mask, spv::Id image_unsigned,
    spv::Id image_signed, spv::Id sampler, spv::Id is_all_signed,
    spv::Id is_any_signed, spv::Id& result_unsigned_out,
    spv::Id& result_signed_out, spv::Id lerp_factor,
    spv::Id lerp_first_unsigned, spv::Id lerp_first_signed) {
  for (uint32_t i = 0; i < 2; ++i) {
    spv::Block& block_sign_head = *builder_->getBuildPoint();
    spv::Block& block_sign = builder_->makeNewBlock();
    spv::Block& block_sign_merge = builder_->makeNewBlock();
    builder_->createSelectionMerge(&block_sign_merge,
                                   spv::SelectionControlDontFlattenMask);
    // Unsigned (i == 0) - if there are any non-signed components.
    // Signed (i == 1) - if there are any signed components.
    builder_->createConditionalBranch(i ? is_any_signed : is_all_signed,
                                      i ? &block_sign : &block_sign_merge,
                                      i ? &block_sign_merge : &block_sign);
    builder_->setBuildPoint(&block_sign);
    spv::Id image = i ? image_signed : image_unsigned;
    // OpSampledImage must be in the same block as where its result is used.
    texture_parameters.sampler = builder_->createBinOp(
        spv::OpSampledImage,
        builder_->makeSampledImageType(builder_->getTypeId(image)), image,
        sampler);
    spv::Id result = builder_->createTextureCall(
        spv::NoPrecision, type_float4_, false, false, false, false, false,
        texture_parameters, image_operands_mask);
    if (lerp_factor != spv::NoResult) {
      spv::Id lerp_first = i ? lerp_first_signed : lerp_first_unsigned;
      if (lerp_first != spv::NoResult) {
        spv::Id lerp_difference = builder_->createNoContractionBinOp(
            spv::OpVectorTimesScalar, type_float4_,
            builder_->createNoContractionBinOp(spv::OpFSub, type_float4_,
                                               result, lerp_first),
            lerp_factor);
        result = builder_->createNoContractionBinOp(spv::OpFAdd, type_float4_,
                                                    result, lerp_difference);
      }
    }
    builder_->createBranch(&block_sign_merge);
    builder_->setBuildPoint(&block_sign_merge);
    {
      std::unique_ptr<spv::Instruction> phi_op =
          std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                             type_float4_, spv::OpPhi);
      phi_op->addIdOperand(result);
      phi_op->addIdOperand(block_sign.getId());
      phi_op->addIdOperand(const_float4_0_);
      phi_op->addIdOperand(block_sign_head.getId());
      // This may overwrite the first lerp endpoint for the sign (such usage of
      // this function is allowed).
      (i ? result_signed_out : result_unsigned_out) = phi_op->getResultId();
      builder_->getBuildPoint()->addInstruction(std::move(phi_op));
    }
  }
}

spv::Id SpirvShaderTranslator::QueryTextureLod(
    spv::Builder::TextureParameters& texture_parameters, spv::Id image_unsigned,
    spv::Id image_signed, spv::Id sampler, spv::Id is_all_signed) {
  // OpSampledImage must be in the same block as where its result is used.
  spv::Block& block_sign_head = *builder_->getBuildPoint();
  spv::Block& block_sign_signed = builder_->makeNewBlock();
  spv::Block& block_sign_unsigned = builder_->makeNewBlock();
  spv::Block& block_sign_merge = builder_->makeNewBlock();
  builder_->createSelectionMerge(&block_sign_merge,
                                 spv::SelectionControlDontFlattenMask);
  builder_->createConditionalBranch(is_all_signed, &block_sign_signed,
                                    &block_sign_unsigned);
  builder_->setBuildPoint(&block_sign_signed);
  texture_parameters.sampler = builder_->createBinOp(
      spv::OpSampledImage,
      builder_->makeSampledImageType(builder_->getTypeId(image_signed)),
      image_signed, sampler);
  spv::Id lod_signed = builder_->createCompositeExtract(
      builder_->createTextureQueryCall(spv::OpImageQueryLod, texture_parameters,
                                       false),
      type_float_, 1);
  builder_->createBranch(&block_sign_merge);
  builder_->setBuildPoint(&block_sign_unsigned);
  texture_parameters.sampler = builder_->createBinOp(
      spv::OpSampledImage,
      builder_->makeSampledImageType(builder_->getTypeId(image_unsigned)),
      image_unsigned, sampler);
  spv::Id lod_unsigned = builder_->createCompositeExtract(
      builder_->createTextureQueryCall(spv::OpImageQueryLod, texture_parameters,
                                       false),
      type_float_, 1);
  builder_->createBranch(&block_sign_merge);
  builder_->setBuildPoint(&block_sign_merge);
  spv::Id result;
  {
    std::unique_ptr<spv::Instruction> sign_phi_op =
        std::make_unique<spv::Instruction>(builder_->getUniqueId(), type_float_,
                                           spv::OpPhi);
    sign_phi_op->addIdOperand(lod_signed);
    sign_phi_op->addIdOperand(block_sign_signed.getId());
    sign_phi_op->addIdOperand(lod_unsigned);
    sign_phi_op->addIdOperand(block_sign_unsigned.getId());
    result = sign_phi_op->getResultId();
    builder_->getBuildPoint()->addInstruction(std::move(sign_phi_op));
  }
  return result;
}

}  // namespace gpu
}  // namespace xe
