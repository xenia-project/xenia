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
  // multiplication, division, modulo - see isArithmeticOperation in
  // propagateNoContraction of glslang; though for some reason it's not applied
  // to SPIR-V OpDot, at least in the February 16, 2020 version installed on
  // http://shader-playground.timjones.io/) must have the NoContraction
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

  // Lookup table for variants of instructions with similar structure.
  static const unsigned int kOps[] = {
      static_cast<unsigned int>(spv::OpNop),                   // kAdd
      static_cast<unsigned int>(spv::OpNop),                   // kMul
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kMax
      static_cast<unsigned int>(spv::OpFOrdLessThan),          // kMin
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kSeq
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kSgt
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kSge
      static_cast<unsigned int>(spv::OpFUnordNotEqual),        // kSne
      static_cast<unsigned int>(GLSLstd450Fract),              // kFrc
      static_cast<unsigned int>(GLSLstd450Trunc),              // kTrunc
      static_cast<unsigned int>(GLSLstd450Floor),              // kFloor
      static_cast<unsigned int>(spv::OpNop),                   // kMad
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kCndEq
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kCndGe
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kCndGt
      static_cast<unsigned int>(spv::OpNop),                   // kDp4
      static_cast<unsigned int>(spv::OpNop),                   // kDp3
      static_cast<unsigned int>(spv::OpNop),                   // kDp2Add
      static_cast<unsigned int>(spv::OpNop),                   // kCube
      static_cast<unsigned int>(spv::OpNop),                   // kMax4
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kSetpEqPush
      static_cast<unsigned int>(spv::OpFUnordNotEqual),        // kSetpNePush
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kSetpGtPush
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kSetpGePush
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kKillEq
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kKillGt
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kKillGe
      static_cast<unsigned int>(spv::OpFUnordNotEqual),        // kKillNe
      static_cast<unsigned int>(spv::OpNop),                   // kDst
      static_cast<unsigned int>(spv::OpNop),                   // kMaxA
  };

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
    }
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
      return result;
    }

    case ucode::AluVectorOpcode::kMax:
    case ucode::AluVectorOpcode::kMin: {
      spv::Id operand_0 = GetOperandComponents(
          operand_storage[0], instr.vector_operands[0], used_result_components);
      // max is commonly used as mov.
      uint32_t identical = instr.vector_operands[0].GetIdenticalComponents(
                               instr.vector_operands[1]) &
                           used_result_components;
      if (identical == used_result_components) {
        // All components are identical - mov.
        return operand_0;
      }
      spv::Id operand_1 = GetOperandComponents(
          operand_storage[1], instr.vector_operands[1], used_result_components);
      // Shader Model 3 NaN behavior (a op b ? a : b, not SPIR-V FMax/FMin which
      // are undefined for NaN or NMax/NMin which return the non-NaN operand).
      spv::Op op = spv::Op(kOps[size_t(instr.vector_opcode)]);
      if (!identical) {
        // All components are different - max/min of the scalars or the entire
        // vectors.
        return builder_->createTriOp(
            spv::OpSelect, result_type,
            builder_->createBinOp(
                op, type_bool_vectors_[used_result_component_count - 1],
                operand_0, operand_1),
            operand_0, operand_1);
      }
      // Mixed identical and different components.
      assert_true(used_result_component_count > 1);
      id_vector_temp_.clear();
      id_vector_temp_.reserve(used_result_component_count);
      uint32_t components_remaining = used_result_components;
      for (uint32_t i = 0; i < used_result_component_count; ++i) {
        spv::Id result_component =
            builder_->createCompositeExtract(operand_0, type_float_, i);
        uint32_t component_index;
        xe::bit_scan_forward(components_remaining, &component_index);
        components_remaining &= ~(1 << component_index);
        if (!(identical & (1 << component_index))) {
          spv::Id operand_1_component =
              builder_->createCompositeExtract(operand_1, type_float_, i);
          result_component = builder_->createTriOp(
              spv::OpSelect, type_float_,
              builder_->createBinOp(op, type_bool_, result_component,
                                    operand_1_component),
              result_component, operand_1_component);
        }
        id_vector_temp_.push_back(result_component);
      }
      return builder_->createCompositeConstruct(result_type, id_vector_temp_);
    }

    case ucode::AluVectorOpcode::kSeq:
    case ucode::AluVectorOpcode::kSgt:
    case ucode::AluVectorOpcode::kSge:
    case ucode::AluVectorOpcode::kSne:
      return builder_->createTriOp(
          spv::OpSelect, result_type,
          builder_->createBinOp(
              spv::Op(kOps[size_t(instr.vector_opcode)]),
              type_bool_vectors_[used_result_component_count - 1],
              GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                                   used_result_components),
              GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                                   used_result_components)),
          const_float_vectors_1_[used_result_component_count - 1],
          const_float_vectors_0_[used_result_component_count - 1]);

    case ucode::AluVectorOpcode::kFrc:
    case ucode::AluVectorOpcode::kTrunc:
    case ucode::AluVectorOpcode::kFloor:
      id_vector_temp_.clear();
      id_vector_temp_.push_back(GetOperandComponents(operand_storage[0],
                                                     instr.vector_operands[0],
                                                     used_result_components));
      return builder_->createBuiltinCall(
          result_type, ext_inst_glsl_std_450_,
          GLSLstd450(kOps[size_t(instr.vector_opcode)]), id_vector_temp_);

    case ucode::AluVectorOpcode::kCndEq:
    case ucode::AluVectorOpcode::kCndGe:
    case ucode::AluVectorOpcode::kCndGt:
      return builder_->createTriOp(
          spv::OpSelect, result_type,
          builder_->createBinOp(
              spv::Op(kOps[size_t(instr.vector_opcode)]),
              type_bool_vectors_[used_result_component_count - 1],
              GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                                   used_result_components),
              const_float_vectors_0_[used_result_component_count - 1]),
          GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                               used_result_components),
          GetOperandComponents(operand_storage[2], instr.vector_operands[2],
                               used_result_components));

    case ucode::AluVectorOpcode::kDp4:
    case ucode::AluVectorOpcode::kDp3:
    case ucode::AluVectorOpcode::kDp2Add: {
      // Not using OpDot for predictable optimization (especially addition
      // order) and NoContraction (which, for some reason, isn't placed on dot
      // in glslang as of the February 16, 2020 version).
      uint32_t component_count;
      if (instr.vector_opcode == ucode::AluVectorOpcode::kDp2Add) {
        component_count = 2;
      } else if (instr.vector_opcode == ucode::AluVectorOpcode::kDp3) {
        component_count = 3;
      } else {
        component_count = 4;
      }
      uint32_t component_mask = (1 << component_count) - 1;
      spv::Id operands[2];
      for (uint32_t i = 0; i < 2; ++i) {
        operands[i] = GetOperandComponents(
            operand_storage[i], instr.vector_operands[i], component_mask);
      }
      uint32_t different =
          component_mask & ~instr.vector_operands[0].GetIdenticalComponents(
                               instr.vector_operands[1]);
      spv::Id result = spv::NoResult;
      for (uint32_t i = 0; i < component_count; ++i) {
        spv::Id operand_components[2];
        for (unsigned int j = 0; j < 2; ++j) {
          operand_components[j] =
              builder_->createCompositeExtract(operands[j], type_float_, i);
        }
        spv::Id product =
            builder_->createBinOp(spv::OpFMul, type_float_,
                                  operand_components[0], operand_components[1]);
        builder_->addDecoration(product, spv::DecorationNoContraction);
        if (different & (1 << i)) {
          // Shader Model 3: +0 or denormal * anything = +-0.
          // Check if the different components in any of the operands are zero,
          // even if the other is NaN - if min(|a|, |b|) is 0.
          for (uint32_t j = 0; j < 2; ++j) {
            if (instr.vector_operands[j].is_absolute_value &&
                !instr.vector_operands[j].is_negated) {
              continue;
            }
            id_vector_temp_.clear();
            id_vector_temp_.push_back(operand_components[j]);
            operand_components[j] =
                builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                            GLSLstd450FAbs, id_vector_temp_);
          }
          id_vector_temp_.clear();
          id_vector_temp_.reserve(2);
          id_vector_temp_.push_back(operand_components[0]);
          id_vector_temp_.push_back(operand_components[1]);
          product = builder_->createTriOp(
              spv::OpSelect, type_float_,
              builder_->createBinOp(spv::OpFOrdEqual, type_bool_,
                                    builder_->createBuiltinCall(
                                        type_float_, ext_inst_glsl_std_450_,
                                        GLSLstd450NMin, id_vector_temp_),
                                    const_float_0_),
              const_float_0_, product);
        }
        if (!i) {
          result = product;
          continue;
        }
        result =
            builder_->createBinOp(spv::OpFAdd, type_float_, result, product);
        builder_->addDecoration(result, spv::DecorationNoContraction);
      }
      if (instr.vector_opcode == ucode::AluVectorOpcode::kDp2Add) {
        result = builder_->createBinOp(
            spv::OpFAdd, type_float_, result,
            GetOperandComponents(operand_storage[2], instr.vector_operands[2],
                                 0b0001));
        builder_->addDecoration(result, spv::DecorationNoContraction);
      }
      return result;
    }

    // TODO(Triang3l): Handle all instructions.
    default:
      break;
  }

  // Invalid instruction.
  return spv::NoResult;
}

}  // namespace gpu
}  // namespace xe
