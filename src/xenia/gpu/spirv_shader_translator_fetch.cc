/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <cmath>
#include <memory>
#include <utility>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {

void SpirvShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition);

  uint32_t used_result_components = instr.result.GetUsedResultComponents();
  uint32_t needed_words = xenos::GetVertexFormatNeededWords(
      instr.attributes.data_format, used_result_components);
  if (!needed_words) {
    // Nothing to load - just constant 0/1 writes, or the swizzle includes only
    // components that don't exist in the format (writing zero instead of them).
    // Unpacking assumes at least some word is needed.
    StoreResult(instr.result, spv::NoResult);
    return;
  }

  EnsureBuildPointAvailable();

  // Get the base address in dwords from the bits 2:31 of the first fetch
  // constant word.
  uint32_t fetch_constant_word_0_index = instr.operands[1].storage_index << 1;
  id_vector_temp_.clear();
  id_vector_temp_.reserve(3);
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
  // fetch constants on the CPU when proper bound checks are added - vfetch may
  // be conditional, so fetch constants may also be used conditionally.
  spv::Id address = builder_->createUnaryOp(
      spv::OpBitcast, type_int_,
      builder_->createBinOp(spv::OpShiftRightLogical, type_uint_,
                            fetch_constant_word_0,
                            builder_->makeUintConstant(2)));
  if (instr.attributes.stride) {
    // Convert the index to an integer by flooring or by rounding to the nearest
    // (as floor(index + 0.5) because rounding to the nearest even makes no
    // sense for addressing, both 1.5 and 2.5 would be 2).
    // http://web.archive.org/web/20100302145413/http://msdn.microsoft.com:80/en-us/library/bb313960.aspx
    spv::Id index = GetOperandComponents(LoadOperandStorage(instr.operands[0]),
                                         instr.operands[0], 0b0001);
    if (instr.attributes.is_index_rounded) {
      index = builder_->createBinOp(spv::OpFAdd, type_float_, index,
                                    builder_->makeFloatConstant(0.5f));
      builder_->addDecoration(index, spv::DecorationNoContraction);
    }
    id_vector_temp_.clear();
    id_vector_temp_.push_back(index);
    index = builder_->createUnaryOp(
        spv::OpConvertFToS, type_int_,
        builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                    GLSLstd450Floor, id_vector_temp_));
    if (instr.attributes.stride > 1) {
      index = builder_->createBinOp(
          spv::OpIMul, type_int_, index,
          builder_->makeIntConstant(int(instr.attributes.stride)));
    }
    address = builder_->createBinOp(spv::OpIAdd, type_int_, address, index);
  }
  // Add the word offset from the instruction (signed), plus the offset of the
  // first needed word within the element.
  uint32_t first_word_index;
  xe::bit_scan_forward(needed_words, &first_word_index);
  int32_t first_word_buffer_offset =
      instr.attributes.offset + int32_t(first_word_index);
  if (first_word_buffer_offset) {
    // Add the constant word offset.
    address = builder_->createBinOp(
        spv::OpIAdd, type_int_, address,
        builder_->makeIntConstant(int(first_word_buffer_offset)));
  }

  // Load the needed words.
  unsigned int word_composite_indices[4] = {};
  spv::Id word_composite_construct[4];
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
    word_composite_construct[word_count++] =
        LoadUint32FromSharedMemory(word_address);
  }
  spv::Id words;
  if (word_count > 1) {
    // Copying from the array to id_vector_temp_ now, not in the loop above,
    // because of the LoadUint32FromSharedMemory call (potentially using
    // id_vector_temp_ internally).
    id_vector_temp_.clear();
    id_vector_temp_.reserve(word_count);
    id_vector_temp_.insert(id_vector_temp_.cend(), word_composite_construct,
                           word_composite_construct + word_count);
    words = builder_->createCompositeConstruct(
        type_uint_vectors_[word_count - 1], id_vector_temp_);
  } else {
    words = word_composite_construct[0];
  }

  // Endian swap the words, getting the endianness from bits 0:1 of the second
  // fetch constant word.
  uint32_t fetch_constant_word_1_index = fetch_constant_word_0_index + 1;
  id_vector_temp_.clear();
  id_vector_temp_.reserve(3);
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
  switch (instr.attributes.data_format) {
      // TODO(Triang3l): All format conversion.

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
              result = builder_->createBinOp(
                  spv::OpVectorTimesScalar, result_type, result,
                  builder_->makeFloatConstant(1.0f / 2147483647.0f));
              builder_->addDecoration(result, spv::DecorationNoContraction);
              // No need to clamp to -1 if signed - 1/(2^31-1) is rounded to
              // 1/(2^31) as float32.
              break;
            case xenos::SignedRepeatingFractionMode::kNoZero: {
              result = builder_->createBinOp(
                  spv::OpVectorTimesScalar, result_type, result,
                  builder_->makeFloatConstant(1.0f / 2147483647.5f));
              builder_->addDecoration(result, spv::DecorationNoContraction);
              spv::Id const_no_zero =
                  builder_->makeFloatConstant(0.5f / 2147483647.5f);
              if (used_format_component_count > 1) {
                id_vector_temp_.clear();
                id_vector_temp_.reserve(used_format_component_count);
                id_vector_temp_.insert(id_vector_temp_.cend(),
                                       used_format_component_count,
                                       const_no_zero);
                const_no_zero = builder_->makeCompositeConstant(
                    result_type, id_vector_temp_);
              }
              result = builder_->createBinOp(spv::OpFAdd, result_type, result,
                                             const_no_zero);
              builder_->addDecoration(result, spv::DecorationNoContraction);
            } break;
            default:
              assert_unhandled_case(instr.attributes.signed_rf_mode);
          }
        } else {
          result = builder_->createBinOp(
              spv::OpVectorTimesScalar, result_type, result,
              builder_->makeFloatConstant(1.0f / 4294967295.0f));
          builder_->addDecoration(result, spv::DecorationNoContraction);
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
  }

  if (result != spv::NoResult) {
    // Apply the exponent bias.
    if (instr.attributes.exp_adjust) {
      result = builder_->createBinOp(spv::OpVectorTimesScalar,
                                     builder_->getTypeId(result), result,
                                     builder_->makeFloatConstant(std::ldexp(
                                         1.0f, instr.attributes.exp_adjust)));
      builder_->addDecoration(result, spv::DecorationNoContraction);
    }

    // If any components not present in the format were requested, pad the
    // resulting vector with zeros.
    uint32_t used_missing_components =
        used_result_components & ~used_format_components;
    if (used_missing_components) {
      // Bypassing the assertion in spv::Builder::createCompositeConstruct - can
      // construct vectors by concatenating vectors, not just from individual
      // scalars.
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

}  // namespace gpu
}  // namespace xe
