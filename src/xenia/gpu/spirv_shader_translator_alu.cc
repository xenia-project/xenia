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
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kMaxA
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
            components_remaining &= ~(uint32_t(1) << component);
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
              components_remaining &= ~(uint32_t(1) << component);
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
    case ucode::AluVectorOpcode::kMin:
    case ucode::AluVectorOpcode::kMaxA: {
      bool is_maxa = instr.vector_opcode == ucode::AluVectorOpcode::kMaxA;
      spv::Id operand_0 = GetOperandComponents(
          operand_storage[0], instr.vector_operands[0],
          used_result_components | (is_maxa ? 0b1000 : 0b0000));
      spv::Id maxa_operand_0_w = spv::NoResult;
      if (is_maxa) {
        // a0 = (int)clamp(floor(src0.w + 0.5), -256.0, 255.0)
        int operand_0_num_components = builder_->getNumComponents(operand_0);
        if (operand_0_num_components > 1) {
          maxa_operand_0_w = builder_->createCompositeExtract(
              operand_0, type_float_,
              static_cast<unsigned int>(operand_0_num_components - 1));
        } else {
          maxa_operand_0_w = operand_0;
        }
        spv::Id maxa_address =
            builder_->createBinOp(spv::OpFAdd, type_float_, maxa_operand_0_w,
                                  builder_->makeFloatConstant(0.5f));
        builder_->addDecoration(maxa_address, spv::DecorationNoContraction);
        id_vector_temp_.clear();
        id_vector_temp_.push_back(maxa_address);
        maxa_address =
            builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                        GLSLstd450Floor, id_vector_temp_);
        id_vector_temp_.clear();
        id_vector_temp_.reserve(3);
        id_vector_temp_.push_back(maxa_address);
        id_vector_temp_.push_back(builder_->makeFloatConstant(-256.0f));
        id_vector_temp_.push_back(builder_->makeFloatConstant(255.0f));
        builder_->createStore(
            builder_->createUnaryOp(
                spv::OpConvertFToS, type_int_,
                builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                            GLSLstd450NClamp, id_vector_temp_)),
            var_main_address_absolute_);
      }
      if (!used_result_components) {
        // maxa returning nothing - can't load src1.
        return spv::NoResult;
      }
      // max is commonly used as mov.
      uint32_t identical = instr.vector_operands[0].GetIdenticalComponents(
                               instr.vector_operands[1]) &
                           used_result_components;
      spv::Id operand_0_per_component;
      if (is_maxa && !(used_result_components & 0b1000) &&
          (identical == used_result_components || !identical)) {
        // operand_0 and operand_1 have different lengths though if src0.w is
        // forced without W being in the write mask for maxa purposes -
        // shuffle/extract the needed part if src0.w is only needed for setting
        // a0.
        // This is only needed for cases without mixed identical and different
        // components - the mixed case uses CompositeExtract, which works fine.
        if (used_result_component_count > 1) {
          // Need all but the last (W) element of operand_0 as a vector.
          uint_vector_temp_.clear();
          uint_vector_temp_.reserve(used_result_component_count);
          for (unsigned int i = 0; i < used_result_component_count; ++i) {
            uint_vector_temp_.push_back(i);
          }
          operand_0_per_component = builder_->createRvalueSwizzle(
              spv::NoPrecision,
              type_float_vectors_[used_result_component_count - 1], operand_0,
              uint_vector_temp_);
        } else {
          // Need the non-W component as scalar.
          operand_0_per_component =
              builder_->createCompositeExtract(operand_0, type_float_, 0);
        }
      } else {
        operand_0_per_component = operand_0;
      }
      if (identical == used_result_components) {
        // All components are identical - mov (with the correct length in case
        // of maxa). Don't access operand_1 at all in this case (operand_0 is
        // already accessed for W in case of maxa).
        assert_true(builder_->getNumComponents(operand_0_per_component) ==
                    used_result_component_count);
        return operand_0_per_component;
      }
      spv::Id operand_1 = GetOperandComponents(
          operand_storage[1], instr.vector_operands[1], used_result_components);
      // Shader Model 3 NaN behavior (a op b ? a : b, not SPIR-V FMax/FMin which
      // are undefined for NaN or NMax/NMin which return the non-NaN operand).
      spv::Op op = spv::Op(kOps[size_t(instr.vector_opcode)]);
      if (!identical) {
        // All components are different - max/min of the scalars or the entire
        // vectors (with the correct length in case of maxa).
        assert_true(builder_->getNumComponents(operand_0_per_component) ==
                    used_result_component_count);
        return builder_->createTriOp(
            spv::OpSelect, result_type,
            builder_->createBinOp(
                op, type_bool_vectors_[used_result_component_count - 1],
                operand_0_per_component, operand_1),
            operand_0_per_component, operand_1);
      }
      // Mixed identical and different components.
      assert_true(used_result_component_count > 1);
      id_vector_temp_.clear();
      id_vector_temp_.reserve(used_result_component_count);
      uint32_t components_remaining = used_result_components;
      for (uint32_t i = 0; i < used_result_component_count; ++i) {
        // Composite extraction of operand_0[i] works fine even it's maxa with
        // src0.w forced without W being in the write mask - src0.w would be the
        // last, so all indices before it are still valid. Don't extract twice
        // if already extracted though.
        spv::Id result_component =
            ((used_result_components & 0b1000) &&
             i + 1 >= used_result_component_count &&
             maxa_operand_0_w != spv::NoResult)
                ? maxa_operand_0_w
                : builder_->createCompositeExtract(operand_0, type_float_, i);
        uint32_t component_index;
        xe::bit_scan_forward(components_remaining, &component_index);
        components_remaining &= ~(uint32_t(1) << component_index);
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
          // even if the other is NaN - if min(|a|, |b|) is 0, if yes, replace
          // the result with zero.
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

    case ucode::AluVectorOpcode::kCube: {
      // operands[0] is .z_xy.
      // Result is T coordinate, S coordinate, 2 * major axis, face ID.
      // Skipping the second component of the operand, so 120, not 230.
      spv::Id operand_vector = GetOperandComponents(
          operand_storage[0], instr.vector_operands[0], 0b1101);
      // Remapped from ZXY (Z_XY without the skipped component) to XYZ.
      spv::Id operand[3];
      for (unsigned int i = 0; i < 3; ++i) {
        operand[i] = builder_->createCompositeExtract(operand_vector,
                                                      type_float_, (i + 1) % 3);
      }
      spv::Id operand_abs[3];
      if (!instr.vector_operands[0].is_absolute_value ||
          instr.vector_operands[0].is_negated) {
        for (unsigned int i = 0; i < 3; ++i) {
          id_vector_temp_.clear();
          id_vector_temp_.push_back(operand[i]);
          operand_abs[i] =
              builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                          GLSLstd450FAbs, id_vector_temp_);
        }
      } else {
        for (unsigned int i = 0; i < 3; ++i) {
          operand_abs[i] = operand[i];
        }
      }
      spv::Id operand_neg[3] = {};
      if (used_result_components & 0b0001) {
        operand_neg[1] =
            builder_->createUnaryOp(spv::OpFNegate, type_float_, operand[1]);
        builder_->addDecoration(operand_neg[1], spv::DecorationNoContraction);
      }
      if (used_result_components & 0b0010) {
        operand_neg[0] =
            builder_->createUnaryOp(spv::OpFNegate, type_float_, operand[0]);
        builder_->addDecoration(operand_neg[0], spv::DecorationNoContraction);
        operand_neg[2] =
            builder_->createUnaryOp(spv::OpFNegate, type_float_, operand[2]);
        builder_->addDecoration(operand_neg[2], spv::DecorationNoContraction);
      }

      // Check if the major axis is Z (abs(z) >= abs(x) && abs(z) >= abs(y)).
      // Selection merge must be the penultimate instruction in the block, check
      // the condition before it.
      spv::Id ma_z_condition = builder_->createBinOp(
          spv::OpLogicalAnd, type_bool_,
          builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                operand_abs[2], operand_abs[0]),
          builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                operand_abs[2], operand_abs[1]));
      spv::Function& function = builder_->getBuildPoint()->getParent();
      spv::Block& ma_z_block = builder_->makeNewBlock();
      spv::Block& ma_yx_block = builder_->makeNewBlock();
      spv::Block* ma_merge_block =
          new spv::Block(builder_->getUniqueId(), function);
      {
        std::unique_ptr<spv::Instruction> selection_merge_op =
            std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
        selection_merge_op->addIdOperand(ma_merge_block->getId());
        selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
        builder_->getBuildPoint()->addInstruction(
            std::move(selection_merge_op));
      }
      builder_->createConditionalBranch(ma_z_condition, &ma_z_block,
                                        &ma_yx_block);

      builder_->setBuildPoint(&ma_z_block);
      // The major axis is Z.
      spv::Id ma_z_result[4] = {};
      // tc = -y
      ma_z_result[0] = operand_neg[1];
      // ma/2 = z
      ma_z_result[2] = operand[2];
      if (used_result_components & 0b1010) {
        spv::Id z_is_neg = builder_->createBinOp(
            spv::OpFOrdLessThan, type_bool_, operand[2], const_float_0_);
        if (used_result_components & 0b0010) {
          // sc = z < 0.0 ? -x : x
          ma_z_result[1] = builder_->createTriOp(
              spv::OpSelect, type_float_, z_is_neg, operand_neg[0], operand[0]);
        }
        if (used_result_components & 0b1000) {
          // id = z < 0.0 ? 5.0 : 4.0
          ma_z_result[3] =
              builder_->createTriOp(spv::OpSelect, type_float_, z_is_neg,
                                    builder_->makeFloatConstant(5.0f),
                                    builder_->makeFloatConstant(4.0f));
        }
      }
      builder_->createBranch(ma_merge_block);

      builder_->setBuildPoint(&ma_yx_block);
      // The major axis is not Z - create an inner conditional to check if the
      // major axis is Y (abs(y) >= abs(x)).
      // Selection merge must be the penultimate instruction in the block, check
      // the condition before it.
      spv::Id ma_y_condition =
          builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                operand_abs[1], operand_abs[0]);
      spv::Block& ma_y_block = builder_->makeNewBlock();
      spv::Block& ma_x_block = builder_->makeNewBlock();
      spv::Block& ma_yx_merge_block = builder_->makeNewBlock();
      {
        std::unique_ptr<spv::Instruction> selection_merge_op =
            std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
        selection_merge_op->addIdOperand(ma_yx_merge_block.getId());
        selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
        builder_->getBuildPoint()->addInstruction(
            std::move(selection_merge_op));
      }
      builder_->createConditionalBranch(ma_y_condition, &ma_y_block,
                                        &ma_x_block);

      builder_->setBuildPoint(&ma_y_block);
      // The major axis is Y.
      spv::Id ma_y_result[4] = {};
      // sc = x
      ma_y_result[1] = operand[0];
      // ma/2 = y
      ma_y_result[2] = operand[1];
      if (used_result_components & 0b1001) {
        spv::Id y_is_neg = builder_->createBinOp(
            spv::OpFOrdLessThan, type_bool_, operand[1], const_float_0_);
        if (used_result_components & 0b0001) {
          // tc = y < 0.0 ? -z : z
          ma_y_result[0] = builder_->createTriOp(
              spv::OpSelect, type_float_, y_is_neg, operand_neg[2], operand[2]);
          // id = y < 0.0 ? 3.0 : 2.0
          ma_y_result[3] =
              builder_->createTriOp(spv::OpSelect, type_float_, y_is_neg,
                                    builder_->makeFloatConstant(3.0f),
                                    builder_->makeFloatConstant(2.0f));
        }
      }
      builder_->createBranch(&ma_yx_merge_block);

      builder_->setBuildPoint(&ma_x_block);
      // The major axis is X.
      spv::Id ma_x_result[4] = {};
      // tc = -y
      ma_x_result[0] = operand_neg[1];
      // ma/2 = x
      ma_x_result[2] = operand[2];
      if (used_result_components & 0b1010) {
        spv::Id x_is_neg = builder_->createBinOp(
            spv::OpFOrdLessThan, type_bool_, operand[0], const_float_0_);
        if (used_result_components & 0b0010) {
          // sc = x < 0.0 ? z : -z
          ma_x_result[1] = builder_->createTriOp(
              spv::OpSelect, type_float_, x_is_neg, operand[2], operand_neg[2]);
        }
        if (used_result_components & 0b1000) {
          // id = x < 0.0 ? 1.0 : 0.0
          ma_x_result[3] =
              builder_->createTriOp(spv::OpSelect, type_float_, x_is_neg,
                                    const_float_1_, const_float_0_);
        }
      }
      builder_->createBranch(&ma_yx_merge_block);

      builder_->setBuildPoint(&ma_yx_merge_block);
      // The major axis is Y or X - choose the options of the result from Y and
      // X.
      spv::Id ma_yx_result[4] = {};
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(used_result_components & (1 << i))) {
          continue;
        }
        std::unique_ptr<spv::Instruction> phi_op =
            std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                               type_float_, spv::OpPhi);
        phi_op->addIdOperand(ma_y_result[i]);
        phi_op->addIdOperand(ma_y_block.getId());
        phi_op->addIdOperand(ma_x_result[i]);
        phi_op->addIdOperand(ma_x_block.getId());
        ma_yx_result[i] = phi_op->getResultId();
        builder_->getBuildPoint()->addInstruction(std::move(phi_op));
      }
      builder_->createBranch(ma_merge_block);

      function.addBlock(ma_merge_block);
      builder_->setBuildPoint(ma_merge_block);
      // Choose the result options from Z and YX cases.
      id_vector_temp_.clear();
      id_vector_temp_.reserve(used_result_component_count);
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(used_result_components & (1 << i))) {
          continue;
        }
        std::unique_ptr<spv::Instruction> phi_op =
            std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                               type_float_, spv::OpPhi);
        phi_op->addIdOperand(ma_z_result[i]);
        phi_op->addIdOperand(ma_z_block.getId());
        phi_op->addIdOperand(ma_yx_result[i]);
        phi_op->addIdOperand(ma_yx_merge_block.getId());
        id_vector_temp_.push_back(phi_op->getResultId());
        builder_->getBuildPoint()->addInstruction(std::move(phi_op));
      }
      assert_true(id_vector_temp_.size() == used_result_component_count);
      if (used_result_component_count == 1) {
        // Only one component - not composite.
        return id_vector_temp_[0];
      }
      return builder_->createCompositeConstruct(
          type_float_vectors_[used_result_component_count - 1],
          id_vector_temp_);
    }

    case ucode::AluVectorOpcode::kMax4: {
      // Find max of all different components of the first operand.
      // FIXME(Triang3l): Not caring about NaN because no info about the
      // correct order, just using NMax here, which replaces them with the
      // non-NaN component (however, there's one nice thing about it is that it
      // may be compiled into max3 + max on GCN).
      uint32_t components_remaining = 0b0000;
      for (uint32_t i = 0; i < 4; ++i) {
        SwizzleSource swizzle_source = instr.vector_operands[0].GetComponent(i);
        assert_true(swizzle_source >= SwizzleSource::kX &&
                    swizzle_source <= SwizzleSource::kW);
        components_remaining |=
            1 << (uint32_t(swizzle_source) - uint32_t(SwizzleSource::kX));
      }
      assert_not_zero(components_remaining);
      spv::Id operand =
          ApplyOperandModifiers(operand_storage[0], instr.vector_operands[0]);
      uint32_t component;
      xe::bit_scan_forward(components_remaining, &component);
      components_remaining &= ~(uint32_t(1) << component);
      spv::Id result = builder_->createCompositeExtract(
          operand, type_float_, static_cast<unsigned int>(component));
      while (xe::bit_scan_forward(components_remaining, &component)) {
        components_remaining &= ~(uint32_t(1) << component);
        id_vector_temp_.clear();
        id_vector_temp_.reserve(2);
        id_vector_temp_.push_back(result);
        id_vector_temp_.push_back(builder_->createCompositeExtract(
            operand, type_float_, static_cast<unsigned int>(component)));
        result =
            builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                        GLSLstd450NMax, id_vector_temp_);
      }
      return result;
    }

    case ucode::AluVectorOpcode::kSetpEqPush:
    case ucode::AluVectorOpcode::kSetpNePush:
    case ucode::AluVectorOpcode::kSetpGtPush:
    case ucode::AluVectorOpcode::kSetpGePush: {
      // X is only needed for the result, W is needed for the predicate.
      spv::Id operands[2];
      spv::Id operands_w[2];
      for (uint32_t i = 0; i < 2; ++i) {
        operands[i] =
            GetOperandComponents(operand_storage[i], instr.vector_operands[i],
                                 used_result_components ? 0b1001 : 0b1000);
        if (used_result_components) {
          operands_w[i] =
              builder_->createCompositeExtract(operands[i], type_float_, 1);
        } else {
          operands_w[i] = operands[i];
        }
      }
      spv::Op op = spv::Op(kOps[size_t(instr.vector_opcode)]);
      // p0 = src0.w == 0.0 && src1.w op 0.0
      builder_->createStore(
          builder_->createBinOp(
              spv::OpLogicalAnd, type_bool_,
              builder_->createBinOp(spv::OpFOrdEqual, type_bool_, operands_w[0],
                                    const_float_0_),
              builder_->createBinOp(op, type_bool_, operands_w[1],
                                    const_float_0_)),
          var_main_predicate_);
      predicate_written = true;
      if (!used_result_components) {
        return spv::NoResult;
      }
      // result = (src0.x == 0.0 && src1.x op 0.0) ? 0.0 : src0.x + 1.0
      // Or:
      // result = ((src0.x == 0.0 && src1.x op 0.0) ? -1.0 : src0.x) + 1.0
      spv::Id operands_x[2];
      for (uint32_t i = 0; i < 2; ++i) {
        operands_x[i] =
            builder_->createCompositeExtract(operands[i], type_float_, 0);
      }
      spv::Id condition = builder_->createBinOp(
          spv::OpLogicalAnd, type_bool_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, operands_x[0],
                                const_float_0_),
          builder_->createBinOp(op, type_bool_, operands_x[1], const_float_0_));
      spv::Id result = builder_->createBinOp(
          spv::OpFAdd, type_float_,
          builder_->createTriOp(spv::OpSelect, type_float_, condition,
                                builder_->makeFloatConstant(-1.0f),
                                operands_x[0]),
          const_float_1_);
      builder_->addDecoration(result, spv::DecorationNoContraction);
      return result;
    }

    case ucode::AluVectorOpcode::kKillEq:
    case ucode::AluVectorOpcode::kKillGt:
    case ucode::AluVectorOpcode::kKillGe:
    case ucode::AluVectorOpcode::kKillNe: {
      // Selection merge must be the penultimate instruction in the block, check
      // the condition before it.
      spv::Id condition = builder_->createUnaryOp(
          spv::OpAny, type_bool_,
          builder_->createBinOp(
              spv::Op(kOps[size_t(instr.vector_opcode)]), type_bool4_,
              GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                                   0b1111),
              GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                                   0b1111)));
      spv::Block& kill_block = builder_->makeNewBlock();
      spv::Block& merge_block = builder_->makeNewBlock();
      {
        std::unique_ptr<spv::Instruction> selection_merge_op =
            std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
        selection_merge_op->addIdOperand(merge_block.getId());
        selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
        builder_->getBuildPoint()->addInstruction(
            std::move(selection_merge_op));
      }
      builder_->createConditionalBranch(condition, &kill_block, &merge_block);
      builder_->setBuildPoint(&kill_block);
      // TODO(Triang3l): Demote to helper invocation to keep derivatives if
      // needed (and return const_float4_1_ if killed in this case).
      builder_->createNoResultOp(spv::OpKill);
      builder_->setBuildPoint(&merge_block);
      return const_float4_0_;
    }

    case ucode::AluVectorOpcode::kDst: {
      spv::Id operands[2] = {};
      if (used_result_components & 0b0110) {
        // result.yz is needed: [0] = y, [1] = z.
        // resuly.y is needed: scalar = y.
        // resuly.z is needed: scalar = z.
        operands[0] =
            GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                                 used_result_components & 0b0110);
      }
      if (used_result_components & 0b1010) {
        // result.yw is needed: [0] = y, [1] = w.
        // resuly.y is needed: scalar = y.
        // resuly.w is needed: scalar = w.
        operands[1] =
            GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                                 used_result_components & 0b1010);
      }
      // y = src0.y * src1.y
      spv::Id result_y = spv::NoResult;
      if (used_result_components & 0b0010) {
        spv::Id operands_y[2];
        operands_y[0] =
            (used_result_components & 0b0100)
                ? builder_->createCompositeExtract(operands[0], type_float_, 0)
                : operands[0];
        operands_y[1] =
            (used_result_components & 0b1000)
                ? builder_->createCompositeExtract(operands[1], type_float_, 0)
                : operands[1];
        result_y = builder_->createBinOp(spv::OpFMul, type_float_,
                                         operands_y[0], operands_y[1]);
        builder_->addDecoration(result_y, spv::DecorationNoContraction);
        if (!(instr.vector_operands[0].GetIdenticalComponents(
                  instr.vector_operands[1]) &
              0b0010)) {
          for (uint32_t i = 0; i < 2; ++i) {
            if (instr.vector_operands[i].is_absolute_value &&
                !instr.vector_operands[i].is_negated) {
              continue;
            }
            id_vector_temp_.clear();
            id_vector_temp_.push_back(operands_y[i]);
            operands_y[i] =
                builder_->createBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                            GLSLstd450FAbs, id_vector_temp_);
          }
          id_vector_temp_.clear();
          id_vector_temp_.reserve(2);
          id_vector_temp_.push_back(operands_y[0]);
          id_vector_temp_.push_back(operands_y[1]);
          result_y = builder_->createTriOp(
              spv::OpSelect, type_float_,
              builder_->createBinOp(spv::OpFOrdEqual, type_bool_,
                                    builder_->createBuiltinCall(
                                        type_float_, ext_inst_glsl_std_450_,
                                        GLSLstd450NMin, id_vector_temp_),
                                    const_float_0_),
              const_float_0_, result_y);
        }
      }
      id_vector_temp_.clear();
      id_vector_temp_.reserve(used_result_component_count);
      if (used_result_components & 0b0001) {
        // x = 1.0
        id_vector_temp_.push_back(const_float_1_);
      }
      if (used_result_components & 0b0010) {
        // y = src0.y * src1.y
        id_vector_temp_.push_back(result_y);
      }
      if (used_result_components & 0b0100) {
        // z = src0.z
        id_vector_temp_.push_back(
            (used_result_components & 0b0010)
                ? builder_->createCompositeExtract(operands[0], type_float_, 1)
                : operands[0]);
      }
      if (used_result_components & 0b1000) {
        // w = src1.w
        id_vector_temp_.push_back(
            (used_result_components & 0b0010)
                ? builder_->createCompositeExtract(operands[1], type_float_, 1)
                : operands[1]);
      }
      assert_true(id_vector_temp_.size() == used_result_component_count);
      if (used_result_component_count == 1) {
        // Only one component - not composite.
        return id_vector_temp_[0];
      }
      return builder_->createCompositeConstruct(
          type_float_vectors_[used_result_component_count - 1],
          id_vector_temp_);
    }
  }

  assert_unhandled_case(instr.vector_opcode);
  EmitTranslationError("Unknown ALU vector operation");
  return spv::NoResult;
}

}  // namespace gpu
}  // namespace xe
