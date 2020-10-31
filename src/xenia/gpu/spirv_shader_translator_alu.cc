/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <memory>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {

void SpirvShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  if (instr.IsNop()) {
    // Don't even disassemble or update predication.
    return;
  }

  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition);

  // Floating-point arithmetic operations (addition, subtraction, negation,
  // multiplication, dot product, division, modulo - see isArithmeticOperation
  // in propagateNoContraction of glslang) must have the NoContraction
  // decoration to prevent reordering to make sure floating-point calculations
  // are optimized predictably and exactly the same in different shaders to
  // allow for multipass rendering (in addition to the Invariant decoration on
  // outputs).

  // Whether the instruction has changed the predicate, and it needs to be
  // checked again later.
  bool predicate_written_vector = false;
  spv::Id vector_result =
      ProcessVectorAluOperation(instr, predicate_written_vector);
  // TODO(Triang3l): Process the ALU scalar operation.

  StoreResult(instr.vector_and_constant_result, vector_result);

  if (predicate_written_vector) {
    cf_exec_predicate_written_ = true;
    CloseInstructionPredication();
  }
}

spv::Id SpirvShaderTranslator::ProcessVectorAluOperation(
    const ParsedAluInstruction& instr, bool& predicate_written) {
  predicate_written = false;

  uint32_t used_result_components =
      instr.vector_and_constant_result.GetUsedResultComponents();
  if (!used_result_components &&
      !AluVectorOpHasSideEffects(instr.vector_opcode)) {
    return spv::NoResult;
  }
  uint32_t used_result_component_count = xe::bit_count(used_result_components);

  // Load operand storage without swizzle and sign modifiers.
  // A small shortcut, operands of cube are the same, but swizzled.
  uint32_t operand_count;
  if (instr.vector_opcode == ucode::AluVectorOpcode::kCube) {
    operand_count = 1;
  } else {
    operand_count = instr.vector_operand_count;
  }
  spv::Id operand_storage[3] = {};
  for (uint32_t i = 0; i < operand_count; ++i) {
    operand_storage[i] = LoadOperandStorage(instr.vector_operands[i]);
  }
  spv::Id result_type =
      used_result_component_count
          ? type_float_vectors_[used_result_component_count - 1]
          : spv::NoType;

  // In case the paired scalar instruction (if processed first) terminates the
  // block (like via OpKill).
  EnsureBuildPointAvailable();

  switch (instr.vector_opcode) {
    case ucode::AluVectorOpcode::kAdd: {
      spv::Id result = builder_->createBinOp(
          spv::OpFAdd, result_type,
          GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                               used_result_components),
          GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                               used_result_components));
      builder_->addDecoration(result, spv::DecorationNoContraction);
      return result;
    } break;
    case ucode::AluVectorOpcode::kMul:
    case ucode::AluVectorOpcode::kMad: {
      spv::Id multiplicands[2];
      for (uint32_t i = 0; i < 2; ++i) {
        multiplicands[i] =
            GetOperandComponents(operand_storage[i], instr.vector_operands[i],
                                 used_result_components);
      }
      spv::Id result = builder_->createBinOp(
          spv::OpFMul, result_type, multiplicands[0], multiplicands[1]);
      builder_->addDecoration(result, spv::DecorationNoContraction);
      uint32_t multiplicands_different =
          used_result_components &
          ~instr.vector_operands[0].GetIdenticalComponents(
              instr.vector_operands[1]);
      if (multiplicands_different) {
        // Shader Model 3: +0 or denormal * anything = +-0.
        spv::Id different_operands[2] = {multiplicands[0], multiplicands[1]};
        spv::Id different_result = result;
        uint32_t different_count = xe::bit_count(multiplicands_different);
        spv::Id different_type = type_float_vectors_[different_count - 1];
        // Extract the different components, if not all are different.
        if (multiplicands_different != used_result_components) {
          uint_vector_temp_.clear();
          uint_vector_temp_.reserve(different_count);
          uint32_t components_remaining = used_result_components;
          for (uint32_t i = 0; i < used_result_component_count; ++i) {
            uint32_t component;
            xe::bit_scan_forward(components_remaining, &component);
            components_remaining &= ~(1 << component);
            if (multiplicands_different & (1 << component)) {
              uint_vector_temp_.push_back(i);
            }
          }
          assert_true(uint_vector_temp_.size() == different_count);
          if (different_count > 1) {
            for (uint32_t i = 0; i < 2; ++i) {
              different_operands[i] = builder_->createRvalueSwizzle(
                  spv::NoPrecision, different_type, different_operands[i],
                  uint_vector_temp_);
            }
            different_result = builder_->createRvalueSwizzle(
                spv::NoPrecision, different_type, different_result,
                uint_vector_temp_);
          } else {
            for (uint32_t i = 0; i < 2; ++i) {
              different_operands[i] = builder_->createCompositeExtract(
                  different_operands[i], different_type, uint_vector_temp_[0]);
            }
            different_result = builder_->createCompositeExtract(
                different_result, different_type, uint_vector_temp_[0]);
          }
        }
        // Check if the different components in any of the operands are zero,
        // even if the other is NaN - if min(|a|, |b|) is 0.
        for (uint32_t i = 0; i < 2; ++i) {
          if (instr.vector_operands[i].is_absolute_value &&
              !instr.vector_operands[i].is_negated) {
            continue;
          }
          id_vector_temp_.clear();
          id_vector_temp_.push_back(different_operands[i]);
          different_operands[i] = builder_->createBuiltinCall(
              different_type, ext_inst_glsl_std_450_, GLSLstd450FAbs,
              id_vector_temp_);
        }
        id_vector_temp_.clear();
        id_vector_temp_.reserve(2);
        id_vector_temp_.push_back(different_operands[0]);
        id_vector_temp_.push_back(different_operands[1]);
        spv::Id different_abs_min =
            builder_->createBuiltinCall(different_type, ext_inst_glsl_std_450_,
                                        GLSLstd450NMin, id_vector_temp_);
        spv::Id different_zero = builder_->createBinOp(
            spv::OpFOrdEqual, type_bool_vectors_[different_count - 1],
            different_abs_min, const_float_vectors_0_[different_count - 1]);
        // Replace with +0.
        different_result = builder_->createTriOp(
            spv::OpSelect, different_type, different_zero,
            const_float_vectors_0_[different_count - 1], different_result);
        // Insert the different components back to the result.
        if (multiplicands_different != used_result_components) {
          if (different_count > 1) {
            std::unique_ptr<spv::Instruction> shuffle_op =
                std::make_unique<spv::Instruction>(
                    builder_->getUniqueId(), result_type, spv::OpVectorShuffle);
            shuffle_op->addIdOperand(result);
            shuffle_op->addIdOperand(different_result);
            uint32_t components_remaining = used_result_components;
            unsigned int different_shuffle_index = used_result_component_count;
            for (uint32_t i = 0; i < used_result_component_count; ++i) {
              uint32_t component;
              xe::bit_scan_forward(components_remaining, &component);
              components_remaining &= ~(1 << component);
              shuffle_op->addImmediateOperand(
                  (multiplicands_different & (1 << component))
                      ? different_shuffle_index++
                      : i);
            }
            result = shuffle_op->getResultId();
            builder_->getBuildPoint()->addInstruction(std::move(shuffle_op));
          } else {
            result = builder_->createCompositeInsert(
                different_result, result, result_type,
                xe::bit_count(used_result_components &
                              (multiplicands_different - 1)));
          }
        } else {
          result = different_result;
        }
      }
      if (instr.vector_opcode == ucode::AluVectorOpcode::kMad) {
        // Not replacing true `0 + term` with conditional selection of the term
        // because +0 + -0 should result in +0, not -0.
        result = builder_->createBinOp(
            spv::OpFAdd, result_type, result,
            GetOperandComponents(operand_storage[2], instr.vector_operands[2],
                                 used_result_components));
        builder_->addDecoration(result, spv::DecorationNoContraction);
      }
    } break;
    // TODO(Triang3l): Handle all instructions.
    default:
      break;
  }

  // Invalid instruction.
  return spv::NoResult;
}

}  // namespace gpu
}  // namespace xe
