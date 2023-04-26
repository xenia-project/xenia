/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <memory>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {

spv::Id SpirvShaderTranslator::ZeroIfAnyOperandIsZero(spv::Id value,
                                                      spv::Id operand_0_abs,
                                                      spv::Id operand_1_abs) {
  EnsureBuildPointAvailable();
  int num_components = builder_->getNumComponents(value);
  assert_true(builder_->getNumComponents(operand_0_abs) == num_components);
  assert_true(builder_->getNumComponents(operand_1_abs) == num_components);
  return builder_->createTriOp(
      spv::OpSelect, type_float_,
      builder_->createBinOp(
          spv::OpFOrdEqual, type_bool_vectors_[num_components - 1],
          builder_->createBinBuiltinCall(
              type_float_vectors_[num_components - 1], ext_inst_glsl_std_450_,
              GLSLstd450NMin, operand_0_abs, operand_1_abs),
          const_float_vectors_0_[num_components - 1]),
      const_float_vectors_0_[num_components - 1], value);
}

void SpirvShaderTranslator::KillPixel(spv::Id condition) {
  // Same calls as in spv::Builder::If.
  spv::Function& function = builder_->getBuildPoint()->getParent();
  spv::Block* kill_block = new spv::Block(builder_->getUniqueId(), function);
  spv::Block* merge_block = new spv::Block(builder_->getUniqueId(), function);
  spv::Block& header_block = *builder_->getBuildPoint();

  function.addBlock(kill_block);
  builder_->setBuildPoint(kill_block);
  // Kill without influencing the control flow in the translated shader.
  if (var_main_kill_pixel_ != spv::NoResult) {
    builder_->createStore(builder_->makeBoolConstant(true),
                          var_main_kill_pixel_);
  }
  if (features_.demote_to_helper_invocation) {
    builder_->createNoResultOp(spv::OpDemoteToHelperInvocationEXT);
  }
  builder_->createBranch(merge_block);

  builder_->setBuildPoint(&header_block);
  builder_->createSelectionMerge(merge_block, spv::SelectionControlMaskNone);
  builder_->createConditionalBranch(condition, kill_block, merge_block);

  function.addBlock(merge_block);
  builder_->setBuildPoint(merge_block);
}

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

  bool predicate_written_scalar = false;
  spv::Id scalar_result =
      ProcessScalarAluOperation(instr, predicate_written_scalar);
  if (scalar_result != spv::NoResult) {
    EnsureBuildPointAvailable();
    builder_->createStore(scalar_result, var_main_previous_scalar_);
  } else {
    // Special retain_prev case - load ps only if needed and don't store the
    // same value back to ps.
    if (instr.scalar_result.GetUsedWriteMask()) {
      EnsureBuildPointAvailable();
      scalar_result =
          builder_->createLoad(var_main_previous_scalar_, spv::NoPrecision);
    }
  }

  StoreResult(instr.vector_and_constant_result, vector_result);
  StoreResult(instr.scalar_result, scalar_result);

  if (predicate_written_vector || predicate_written_scalar) {
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
      !ucode::GetAluVectorOpcodeInfo(instr.vector_opcode).changed_state) {
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
  // block.
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
      return builder_->createNoContractionBinOp(
          spv::OpFAdd, result_type,
          GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                               used_result_components),
          GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                               used_result_components));
    }
    case ucode::AluVectorOpcode::kMul:
    case ucode::AluVectorOpcode::kMad: {
      spv::Id multiplicands[2];
      for (uint32_t i = 0; i < 2; ++i) {
        multiplicands[i] =
            GetOperandComponents(operand_storage[i], instr.vector_operands[i],
                                 used_result_components);
      }
      spv::Id result = builder_->createNoContractionBinOp(
          spv::OpFMul, result_type, multiplicands[0], multiplicands[1]);
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
          different_operands[i] = GetAbsoluteOperand(different_operands[i],
                                                     instr.vector_operands[i]);
        }
        spv::Id different_zero = builder_->createBinOp(
            spv::OpFOrdEqual, type_bool_vectors_[different_count - 1],
            builder_->createBinBuiltinCall(
                different_type, ext_inst_glsl_std_450_, GLSLstd450NMin,
                different_operands[0], different_operands[1]),
            const_float_vectors_0_[different_count - 1]);
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
        result = builder_->createNoContractionBinOp(
            spv::OpFAdd, result_type, result,
            GetOperandComponents(operand_storage[2], instr.vector_operands[2],
                                 used_result_components));
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
        builder_->createStore(
            builder_->createUnaryOp(
                spv::OpConvertFToS, type_int_,
                builder_->createTriBuiltinCall(
                    type_float_, ext_inst_glsl_std_450_, GLSLstd450NClamp,
                    builder_->createUnaryBuiltinCall(
                        type_float_, ext_inst_glsl_std_450_, GLSLstd450Floor,
                        builder_->createNoContractionBinOp(
                            spv::OpFAdd, type_float_, maxa_operand_0_w,
                            builder_->makeFloatConstant(0.5f))),
                    builder_->makeFloatConstant(-256.0f),
                    builder_->makeFloatConstant(255.0f))),
            var_main_address_register_);
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
      return builder_->createUnaryBuiltinCall(
          result_type, ext_inst_glsl_std_450_,
          GLSLstd450(kOps[size_t(instr.vector_opcode)]),
          GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                               used_result_components));

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
        spv::Id product = builder_->createNoContractionBinOp(
            spv::OpFMul, type_float_, operand_components[0],
            operand_components[1]);
        if (different & (1 << i)) {
          // Shader Model 3: +0 or denormal * anything = +-0.
          product = ZeroIfAnyOperandIsZero(
              product,
              GetAbsoluteOperand(operand_components[0],
                                 instr.vector_operands[0]),
              GetAbsoluteOperand(operand_components[1],
                                 instr.vector_operands[1]));
        }
        if (!i) {
          result = product;
          continue;
        }
        result = builder_->createNoContractionBinOp(spv::OpFAdd, type_float_,
                                                    result, product);
      }
      if (instr.vector_opcode == ucode::AluVectorOpcode::kDp2Add) {
        result = builder_->createNoContractionBinOp(
            spv::OpFAdd, type_float_, result,
            GetOperandComponents(operand_storage[2], instr.vector_operands[2],
                                 0b0001));
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
          operand_abs[i] = builder_->createUnaryBuiltinCall(
              type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs, operand[i]);
        }
      } else {
        for (unsigned int i = 0; i < 3; ++i) {
          operand_abs[i] = operand[i];
        }
      }
      spv::Id operand_neg[3] = {};
      if (used_result_components & 0b0001) {
        operand_neg[1] = builder_->createNoContractionUnaryOp(
            spv::OpFNegate, type_float_, operand[1]);
      }
      if (used_result_components & 0b0010) {
        operand_neg[0] = builder_->createNoContractionUnaryOp(
            spv::OpFNegate, type_float_, operand[0]);
        operand_neg[2] = builder_->createNoContractionUnaryOp(
            spv::OpFNegate, type_float_, operand[2]);
      }

      spv::Id ma_z_result[4] = {}, ma_yx_result[4] = {};

      // Check if the major axis is Z (abs(z) >= abs(x) && abs(z) >= abs(y)).
      spv::Builder::If ma_z_if(
          builder_->createBinOp(
              spv::OpLogicalAnd, type_bool_,
              builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                    operand_abs[2], operand_abs[0]),
              builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                    operand_abs[2], operand_abs[1])),
          spv::SelectionControlMaskNone, *builder_);
      {
        // The major axis is Z.
        // tc = -y
        ma_z_result[0] = operand_neg[1];
        // ma/2 = z
        ma_z_result[2] = operand[2];
        if (used_result_components & 0b1010) {
          spv::Id z_is_neg = builder_->createBinOp(
              spv::OpFOrdLessThan, type_bool_, operand[2], const_float_0_);
          if (used_result_components & 0b0010) {
            // sc = z < 0.0 ? -x : x
            ma_z_result[1] =
                builder_->createTriOp(spv::OpSelect, type_float_, z_is_neg,
                                      operand_neg[0], operand[0]);
          }
          if (used_result_components & 0b1000) {
            // id = z < 0.0 ? 5.0 : 4.0
            ma_z_result[3] =
                builder_->createTriOp(spv::OpSelect, type_float_, z_is_neg,
                                      builder_->makeFloatConstant(5.0f),
                                      builder_->makeFloatConstant(4.0f));
          }
        }
      }
      spv::Block& ma_z_end_block = *builder_->getBuildPoint();
      ma_z_if.makeBeginElse();
      {
        spv::Id ma_y_result[4] = {}, ma_x_result[4] = {};

        // The major axis is not Z - create an inner conditional to check if the
        // major axis is Y (abs(y) >= abs(x)).
        spv::Builder::If ma_y_if(
            builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                  operand_abs[1], operand_abs[0]),
            spv::SelectionControlMaskNone, *builder_);
        {
          // The major axis is Y.
          // sc = x
          ma_y_result[1] = operand[0];
          // ma/2 = y
          ma_y_result[2] = operand[1];
          if (used_result_components & 0b1001) {
            spv::Id y_is_neg = builder_->createBinOp(
                spv::OpFOrdLessThan, type_bool_, operand[1], const_float_0_);
            if (used_result_components & 0b0001) {
              // tc = y < 0.0 ? -z : z
              ma_y_result[0] =
                  builder_->createTriOp(spv::OpSelect, type_float_, y_is_neg,
                                        operand_neg[2], operand[2]);
              // id = y < 0.0 ? 3.0 : 2.0
              ma_y_result[3] =
                  builder_->createTriOp(spv::OpSelect, type_float_, y_is_neg,
                                        builder_->makeFloatConstant(3.0f),
                                        builder_->makeFloatConstant(2.0f));
            }
          }
        }
        spv::Block& ma_y_end_block = *builder_->getBuildPoint();
        ma_y_if.makeBeginElse();
        {
          // The major axis is X.
          // tc = -y
          ma_x_result[0] = operand_neg[1];
          // ma/2 = x
          ma_x_result[2] = operand[0];
          if (used_result_components & 0b1010) {
            spv::Id x_is_neg = builder_->createBinOp(
                spv::OpFOrdLessThan, type_bool_, operand[0], const_float_0_);
            if (used_result_components & 0b0010) {
              // sc = x < 0.0 ? z : -z
              ma_x_result[1] =
                  builder_->createTriOp(spv::OpSelect, type_float_, x_is_neg,
                                        operand[2], operand_neg[2]);
            }
            if (used_result_components & 0b1000) {
              // id = x < 0.0 ? 1.0 : 0.0
              ma_x_result[3] =
                  builder_->createTriOp(spv::OpSelect, type_float_, x_is_neg,
                                        const_float_1_, const_float_0_);
            }
          }
        }
        spv::Block& ma_x_end_block = *builder_->getBuildPoint();
        ma_y_if.makeEndIf();

        // The major axis is Y or X - choose the options of the result from Y
        // and X.
        for (uint32_t i = 0; i < 4; ++i) {
          if (!(used_result_components & (1 << i))) {
            continue;
          }
          std::unique_ptr<spv::Instruction> phi_op =
              std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                 type_float_, spv::OpPhi);
          phi_op->addIdOperand(ma_y_result[i]);
          phi_op->addIdOperand(ma_y_end_block.getId());
          phi_op->addIdOperand(ma_x_result[i]);
          phi_op->addIdOperand(ma_x_end_block.getId());
          ma_yx_result[i] = phi_op->getResultId();
          builder_->getBuildPoint()->addInstruction(std::move(phi_op));
        }
      }
      spv::Block& ma_yx_end_block = *builder_->getBuildPoint();
      ma_z_if.makeEndIf();

      // Choose the result options from Z and YX cases.
      id_vector_temp_.clear();
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(used_result_components & (1 << i))) {
          continue;
        }
        std::unique_ptr<spv::Instruction> phi_op =
            std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                               type_float_, spv::OpPhi);
        phi_op->addIdOperand(ma_z_result[i]);
        phi_op->addIdOperand(ma_z_end_block.getId());
        phi_op->addIdOperand(ma_yx_result[i]);
        phi_op->addIdOperand(ma_yx_end_block.getId());
        id_vector_temp_.push_back(phi_op->getResultId());
        builder_->getBuildPoint()->addInstruction(std::move(phi_op));
      }
      assert_true(id_vector_temp_.size() == used_result_component_count);
      if (used_result_components & 0b0100) {
        // Multiply the major axis by 2.
        spv::Id& ma2 = id_vector_temp_[xe::bit_count(used_result_components &
                                                     ((1 << 2) - 1))];
        ma2 = builder_->createNoContractionBinOp(
            spv::OpFMul, type_float_, builder_->makeFloatConstant(2.0f), ma2);
      }
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
        result = builder_->createBinBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450NMax, result,
            builder_->createCompositeExtract(
                operand, type_float_, static_cast<unsigned int>(component)));
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
      return builder_->createNoContractionBinOp(
          spv::OpFAdd, type_float_,
          builder_->createTriOp(spv::OpSelect, type_float_, condition,
                                builder_->makeFloatConstant(-1.0f),
                                operands_x[0]),
          const_float_1_);
    }

    case ucode::AluVectorOpcode::kKillEq:
    case ucode::AluVectorOpcode::kKillGt:
    case ucode::AluVectorOpcode::kKillGe:
    case ucode::AluVectorOpcode::kKillNe: {
      KillPixel(builder_->createUnaryOp(
          spv::OpAny, type_bool_,
          builder_->createBinOp(
              spv::Op(kOps[size_t(instr.vector_opcode)]), type_bool4_,
              GetOperandComponents(operand_storage[0], instr.vector_operands[0],
                                   0b1111),
              GetOperandComponents(operand_storage[1], instr.vector_operands[1],
                                   0b1111))));
      return const_float_0_;
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
        result_y = builder_->createNoContractionBinOp(
            spv::OpFMul, type_float_, operands_y[0], operands_y[1]);
        if (!(instr.vector_operands[0].GetIdenticalComponents(
                  instr.vector_operands[1]) &
              0b0010)) {
          // Shader Model 3: +0 or denormal * anything = +-0.
          result_y = ZeroIfAnyOperandIsZero(
              result_y,
              GetAbsoluteOperand(operands_y[0], instr.vector_operands[0]),
              GetAbsoluteOperand(operands_y[1], instr.vector_operands[1]));
        }
      }
      id_vector_temp_.clear();
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

spv::Id SpirvShaderTranslator::ProcessScalarAluOperation(
    const ParsedAluInstruction& instr, bool& predicate_written) {
  predicate_written = false;

  spv::Id operand_storage[2] = {};
  for (uint32_t i = 0; i < instr.scalar_operand_count; ++i) {
    operand_storage[i] = LoadOperandStorage(instr.scalar_operands[i]);
  }

  // In case the paired vector instruction (if processed first) terminates the
  // block.
  EnsureBuildPointAvailable();

  // Lookup table for variants of instructions with similar structure.
  static const unsigned int kOps[] = {
      static_cast<unsigned int>(spv::OpFAdd),                  // kAdds
      static_cast<unsigned int>(spv::OpFAdd),                  // kAddsPrev
      static_cast<unsigned int>(spv::OpNop),                   // kMuls
      static_cast<unsigned int>(spv::OpNop),                   // kMulsPrev
      static_cast<unsigned int>(spv::OpNop),                   // kMulsPrev2
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kMaxs
      static_cast<unsigned int>(spv::OpFOrdLessThan),          // kMins
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kSeqs
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kSgts
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kSges
      static_cast<unsigned int>(spv::OpFUnordNotEqual),        // kSnes
      static_cast<unsigned int>(GLSLstd450Fract),              // kFrcs
      static_cast<unsigned int>(GLSLstd450Trunc),              // kTruncs
      static_cast<unsigned int>(GLSLstd450Floor),              // kFloors
      static_cast<unsigned int>(GLSLstd450Exp2),               // kExp
      static_cast<unsigned int>(spv::OpNop),                   // kLogc
      static_cast<unsigned int>(GLSLstd450Log2),               // kLog
      static_cast<unsigned int>(spv::OpNop),                   // kRcpc
      static_cast<unsigned int>(spv::OpNop),                   // kRcpf
      static_cast<unsigned int>(spv::OpNop),                   // kRcp
      static_cast<unsigned int>(spv::OpNop),                   // kRsqc
      static_cast<unsigned int>(spv::OpNop),                   // kRsqf
      static_cast<unsigned int>(GLSLstd450InverseSqrt),        // kRsq
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kMaxAs
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kMaxAsf
      static_cast<unsigned int>(spv::OpFSub),                  // kSubs
      static_cast<unsigned int>(spv::OpFSub),                  // kSubsPrev
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kSetpEq
      static_cast<unsigned int>(spv::OpFUnordNotEqual),        // kSetpNe
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kSetpGt
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kSetpGe
      static_cast<unsigned int>(spv::OpNop),                   // kSetpInv
      static_cast<unsigned int>(spv::OpNop),                   // kSetpPop
      static_cast<unsigned int>(spv::OpNop),                   // kSetpClr
      static_cast<unsigned int>(spv::OpNop),                   // kSetpRstr
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kKillsEq
      static_cast<unsigned int>(spv::OpFOrdGreaterThan),       // kKillsGt
      static_cast<unsigned int>(spv::OpFOrdGreaterThanEqual),  // kKillsGe
      static_cast<unsigned int>(spv::OpFUnordNotEqual),        // kKillsNe
      static_cast<unsigned int>(spv::OpFOrdEqual),             // kKillsOne
      static_cast<unsigned int>(GLSLstd450Sqrt),               // kSqrt
      static_cast<unsigned int>(spv::OpNop),                   // Invalid
      static_cast<unsigned int>(spv::OpNop),                   // kMulsc0
      static_cast<unsigned int>(spv::OpNop),                   // kMulsc1
      static_cast<unsigned int>(spv::OpFAdd),                  // kAddsc0
      static_cast<unsigned int>(spv::OpFAdd),                  // kAddsc1
      static_cast<unsigned int>(spv::OpFSub),                  // kSubsc0
      static_cast<unsigned int>(spv::OpFSub),                  // kSubsc1
      static_cast<unsigned int>(GLSLstd450Sin),                // kSin
      static_cast<unsigned int>(GLSLstd450Cos),                // kCos
      static_cast<unsigned int>(spv::OpNop),                   // kRetainPrev
  };

  switch (instr.scalar_opcode) {
    case ucode::AluScalarOpcode::kAdds:
    case ucode::AluScalarOpcode::kSubs: {
      spv::Id a, b;
      GetOperandScalarXY(operand_storage[0], instr.scalar_operands[0], a, b);
      return builder_->createNoContractionBinOp(
          spv::Op(kOps[size_t(instr.scalar_opcode)]), type_float_, a, b);
    }
    case ucode::AluScalarOpcode::kAddsPrev:
    case ucode::AluScalarOpcode::kSubsPrev: {
      return builder_->createNoContractionBinOp(
          spv::Op(kOps[size_t(instr.scalar_opcode)]), type_float_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001),
          builder_->createLoad(var_main_previous_scalar_, spv::NoPrecision));
    }
    case ucode::AluScalarOpcode::kMuls: {
      spv::Id a, b;
      GetOperandScalarXY(operand_storage[0], instr.scalar_operands[0], a, b);
      spv::Id result =
          builder_->createNoContractionBinOp(spv::OpFMul, type_float_, a, b);
      if (a != b) {
        // Shader Model 3: +0 or denormal * anything = +-0.
        result = ZeroIfAnyOperandIsZero(
            result, GetAbsoluteOperand(a, instr.scalar_operands[0]),
            GetAbsoluteOperand(b, instr.scalar_operands[0]));
      }
      return result;
    }
    case ucode::AluScalarOpcode::kMulsPrev: {
      spv::Id a = GetOperandComponents(operand_storage[0],
                                       instr.scalar_operands[0], 0b0001);
      spv::Id ps =
          builder_->createLoad(var_main_previous_scalar_, spv::NoPrecision);
      spv::Id result =
          builder_->createNoContractionBinOp(spv::OpFMul, type_float_, a, ps);
      // Shader Model 3: +0 or denormal * anything = +-0.
      return ZeroIfAnyOperandIsZero(
          result, GetAbsoluteOperand(a, instr.scalar_operands[0]),
          builder_->createUnaryBuiltinCall(type_float_, ext_inst_glsl_std_450_,
                                           GLSLstd450FAbs, ps));
    }
    case ucode::AluScalarOpcode::kMulsPrev2: {
      // Check if need to select the src0.a * ps case.
      // Selection merge must be the penultimate instruction in the block, check
      // the condition before it.
      spv::Id ps =
          builder_->createLoad(var_main_previous_scalar_, spv::NoPrecision);
      // ps != -FLT_MAX.
      spv::Id const_float_max_neg = builder_->makeFloatConstant(-FLT_MAX);
      spv::Id condition = builder_->createBinOp(
          spv::OpFUnordNotEqual, type_bool_, ps, const_float_max_neg);
      // isfinite(ps), or |ps| <= FLT_MAX, or -|ps| >= -FLT_MAX, since -FLT_MAX
      // is already loaded to an SGPR, this is also false if it's NaN.
      spv::Id ps_abs = builder_->createUnaryBuiltinCall(
          type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs, ps);
      spv::Id ps_abs_neg = builder_->createNoContractionUnaryOp(
          spv::OpFNegate, type_float_, ps_abs);
      condition = builder_->createBinOp(
          spv::OpLogicalAnd, type_bool_, condition,
          builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                ps_abs_neg, const_float_max_neg));
      // isfinite(src0.b), or -|src0.b| >= -FLT_MAX for the same reason.
      spv::Id b = GetOperandComponents(operand_storage[0],
                                       instr.scalar_operands[0], 0b0010);
      spv::Id b_abs_neg = b;
      if (!instr.scalar_operands[0].is_absolute_value) {
        b_abs_neg = builder_->createUnaryBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs, b_abs_neg);
      }
      if (!instr.scalar_operands[0].is_absolute_value ||
          !instr.scalar_operands[0].is_negated) {
        b_abs_neg = builder_->createNoContractionUnaryOp(
            spv::OpFNegate, type_float_, b_abs_neg);
      }
      condition = builder_->createBinOp(
          spv::OpLogicalAnd, type_bool_, condition,
          builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                b_abs_neg, const_float_max_neg));
      // src0.b > 0 (need !(src0.b <= 0), but src0.b has already been checked
      // for NaN).
      condition = builder_->createBinOp(
          spv::OpLogicalAnd, type_bool_, condition,
          builder_->createBinOp(spv::OpFOrdGreaterThan, type_bool_, b,
                                const_float_0_));
      spv::Block& pre_multiply_if_block = *builder_->getBuildPoint();
      spv::Id product;
      spv::Builder::If multiply_if(condition, spv::SelectionControlMaskNone,
                                   *builder_);
      {
        // Multiplication case.
        spv::Id a = instr.scalar_operands[0].GetComponent(0) !=
                            instr.scalar_operands[0].GetComponent(1)
                        ? GetOperandComponents(operand_storage[0],
                                               instr.scalar_operands[0], 0b0001)
                        : b;
        product =
            builder_->createNoContractionBinOp(spv::OpFMul, type_float_, a, ps);
        // Shader Model 3: +0 or denormal * anything = +-0.
        product = ZeroIfAnyOperandIsZero(
            product, GetAbsoluteOperand(a, instr.scalar_operands[0]), ps_abs);
      }
      spv::Block& multiply_end_block = *builder_->getBuildPoint();
      multiply_if.makeEndIf();
      // Merge - choose between the product and -FLT_MAX.
      {
        std::unique_ptr<spv::Instruction> phi_op =
            std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                               type_float_, spv::OpPhi);
        phi_op->addIdOperand(product);
        phi_op->addIdOperand(multiply_end_block.getId());
        phi_op->addIdOperand(const_float_max_neg);
        phi_op->addIdOperand(pre_multiply_if_block.getId());
        spv::Id phi_result = phi_op->getResultId();
        builder_->getBuildPoint()->addInstruction(std::move(phi_op));
        return phi_result;
      }
    }

    case ucode::AluScalarOpcode::kMaxs:
    case ucode::AluScalarOpcode::kMins:
    case ucode::AluScalarOpcode::kMaxAs:
    case ucode::AluScalarOpcode::kMaxAsf: {
      spv::Id a, b;
      GetOperandScalarXY(operand_storage[0], instr.scalar_operands[0], a, b);
      if (instr.scalar_opcode == ucode::AluScalarOpcode::kMaxAs ||
          instr.scalar_opcode == ucode::AluScalarOpcode::kMaxAsf) {
        // maxas: a0 = (int)clamp(floor(src0.a + 0.5), -256.0, 255.0)
        // maxasf: a0 = (int)clamp(floor(src0.a), -256.0, 255.0)
        spv::Id maxa_address;
        if (instr.scalar_opcode == ucode::AluScalarOpcode::kMaxAs) {
          maxa_address = builder_->createNoContractionBinOp(
              spv::OpFAdd, type_float_, a, builder_->makeFloatConstant(0.5f));
        } else {
          maxa_address = a;
        }
        builder_->createStore(
            builder_->createUnaryOp(
                spv::OpConvertFToS, type_int_,
                builder_->createTriBuiltinCall(
                    type_float_, ext_inst_glsl_std_450_, GLSLstd450NClamp,
                    builder_->createUnaryBuiltinCall(
                        type_float_, ext_inst_glsl_std_450_, GLSLstd450Floor,
                        maxa_address),
                    builder_->makeFloatConstant(-256.0f),
                    builder_->makeFloatConstant(255.0f))),
            var_main_address_register_);
      }
      if (a == b) {
        // max is commonly used as mov.
        return a;
      }
      // Shader Model 3 NaN behavior (a op b ? a : b, not SPIR-V FMax/FMin which
      // are undefined for NaN or NMax/NMin which return the non-NaN operand).
      return builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::Op(kOps[size_t(instr.scalar_opcode)]),
                                type_bool_, a, b),
          a, b);
    }

    case ucode::AluScalarOpcode::kSeqs:
    case ucode::AluScalarOpcode::kSgts:
    case ucode::AluScalarOpcode::kSges:
    case ucode::AluScalarOpcode::kSnes:
      return builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(
              spv::Op(kOps[size_t(instr.scalar_opcode)]), type_bool_,
              GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                                   0b0001),
              const_float_0_),
          const_float_1_, const_float_0_);

    case ucode::AluScalarOpcode::kFrcs:
    case ucode::AluScalarOpcode::kTruncs:
    case ucode::AluScalarOpcode::kFloors:
    case ucode::AluScalarOpcode::kExp:
    case ucode::AluScalarOpcode::kLog:
    case ucode::AluScalarOpcode::kRsq:
    case ucode::AluScalarOpcode::kSqrt:
    case ucode::AluScalarOpcode::kSin:
    case ucode::AluScalarOpcode::kCos:
      return builder_->createUnaryBuiltinCall(
          type_float_, ext_inst_glsl_std_450_,
          GLSLstd450(kOps[size_t(instr.scalar_opcode)]),
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
    case ucode::AluScalarOpcode::kLogc: {
      spv::Id result = builder_->createUnaryBuiltinCall(
          type_float_, ext_inst_glsl_std_450_, GLSLstd450Log2,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
      return builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(-INFINITY)),
          builder_->makeFloatConstant(-FLT_MAX), result);
    }
    case ucode::AluScalarOpcode::kRcpc: {
      spv::Id result = builder_->createNoContractionBinOp(
          spv::OpFDiv, type_float_, const_float_1_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
      result = builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(-INFINITY)),
          builder_->makeFloatConstant(-FLT_MAX), result);
      return builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(INFINITY)),
          builder_->makeFloatConstant(FLT_MAX), result);
    }
    case ucode::AluScalarOpcode::kRcpf: {
      spv::Id result = builder_->createNoContractionBinOp(
          spv::OpFDiv, type_float_, const_float_1_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
      result = builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(INFINITY)),
          const_float_0_, result);
      // Can't create -0.0f with makeFloatConstant due to float comparison
      // internally, cast to bit pattern.
      result = builder_->createTriOp(
          spv::OpSelect, type_uint_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(-INFINITY)),
          builder_->makeUintConstant(uint32_t(INT32_MIN)),
          builder_->createUnaryOp(spv::OpBitcast, type_uint_, result));
      return builder_->createUnaryOp(spv::OpBitcast, type_float_, result);
    }
    case ucode::AluScalarOpcode::kRcp: {
      return builder_->createNoContractionBinOp(
          spv::OpFDiv, type_float_, const_float_1_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
    }
    case ucode::AluScalarOpcode::kRsqc: {
      spv::Id result = builder_->createUnaryBuiltinCall(
          type_float_, ext_inst_glsl_std_450_, GLSLstd450InverseSqrt,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
      result = builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(-INFINITY)),
          builder_->makeFloatConstant(-FLT_MAX), result);
      return builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(INFINITY)),
          builder_->makeFloatConstant(FLT_MAX), result);
    }
    case ucode::AluScalarOpcode::kRsqf: {
      spv::Id result = builder_->createUnaryBuiltinCall(
          type_float_, ext_inst_glsl_std_450_, GLSLstd450InverseSqrt,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001));
      result = builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(INFINITY)),
          const_float_0_, result);
      // Can't create -0.0f with makeFloatConstant due to float comparison
      // internally, cast to bit pattern.
      result = builder_->createTriOp(
          spv::OpSelect, type_uint_,
          builder_->createBinOp(spv::OpFOrdEqual, type_bool_, result,
                                builder_->makeFloatConstant(-INFINITY)),
          builder_->makeUintConstant(uint32_t(INT32_MIN)),
          builder_->createUnaryOp(spv::OpBitcast, type_uint_, result));
      return builder_->createUnaryOp(spv::OpBitcast, type_float_, result);
    }

    case ucode::AluScalarOpcode::kSetpEq:
    case ucode::AluScalarOpcode::kSetpNe:
    case ucode::AluScalarOpcode::kSetpGt:
    case ucode::AluScalarOpcode::kSetpGe: {
      spv::Id predicate = builder_->createBinOp(
          spv::Op(kOps[size_t(instr.scalar_opcode)]), type_bool_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001),
          const_float_0_);
      builder_->createStore(predicate, var_main_predicate_);
      predicate_written = true;
      return builder_->createTriOp(spv::OpSelect, type_float_, predicate,
                                   const_float_0_, const_float_1_);
    }
    case ucode::AluScalarOpcode::kSetpInv: {
      spv::Id a = GetOperandComponents(operand_storage[0],
                                       instr.scalar_operands[0], 0b0001);
      spv::Id predicate = builder_->createBinOp(spv::OpFOrdEqual, type_bool_, a,
                                                const_float_1_);
      builder_->createStore(predicate, var_main_predicate_);
      predicate_written = true;
      return builder_->createTriOp(
          spv::OpSelect, type_float_, predicate, const_float_0_,
          builder_->createTriOp(
              spv::OpSelect, type_float_,
              builder_->createBinOp(spv::OpFOrdEqual, type_bool_, a,
                                    const_float_0_),
              const_float_1_, a));
    }
    case ucode::AluScalarOpcode::kSetpPop: {
      spv::Id a_minus_1 = builder_->createNoContractionBinOp(
          spv::OpFSub, type_float_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001),
          const_float_1_);
      spv::Id predicate = builder_->createBinOp(
          spv::OpFOrdLessThanEqual, type_bool_, a_minus_1, const_float_0_);
      builder_->createStore(predicate, var_main_predicate_);
      predicate_written = true;
      return builder_->createTriOp(spv::OpSelect, type_float_, predicate,
                                   const_float_0_, a_minus_1);
    }
    case ucode::AluScalarOpcode::kSetpClr:
      builder_->createStore(builder_->makeBoolConstant(false),
                            var_main_predicate_);
      return builder_->makeFloatConstant(FLT_MAX);
    case ucode::AluScalarOpcode::kSetpRstr: {
      spv::Id a = GetOperandComponents(operand_storage[0],
                                       instr.scalar_operands[0], 0b0001);
      spv::Id predicate = builder_->createBinOp(spv::OpFOrdEqual, type_bool_, a,
                                                const_float_0_);
      builder_->createStore(predicate, var_main_predicate_);
      predicate_written = true;
      return builder_->createTriOp(spv::OpSelect, type_float_, predicate,
                                   const_float_0_, a);
    }

    case ucode::AluScalarOpcode::kKillsEq:
    case ucode::AluScalarOpcode::kKillsGt:
    case ucode::AluScalarOpcode::kKillsGe:
    case ucode::AluScalarOpcode::kKillsNe:
    case ucode::AluScalarOpcode::kKillsOne: {
      KillPixel(builder_->createBinOp(
          spv::Op(kOps[size_t(instr.scalar_opcode)]), type_bool_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001),
          instr.scalar_opcode == ucode::AluScalarOpcode::kKillsOne
              ? const_float_1_
              : const_float_0_));
      return const_float_0_;
    }

    case ucode::AluScalarOpcode::kMulsc0:
    case ucode::AluScalarOpcode::kMulsc1: {
      spv::Id operand_0 = GetOperandComponents(
          operand_storage[0], instr.scalar_operands[0], 0b0001);
      spv::Id operand_1 = GetOperandComponents(
          operand_storage[1], instr.scalar_operands[1], 0b0001);
      spv::Id result = builder_->createNoContractionBinOp(
          spv::OpFMul, type_float_, operand_0, operand_1);
      if (!(instr.scalar_operands[0].GetIdenticalComponents(
                instr.scalar_operands[1]) &
            0b0001)) {
        // Shader Model 3: +0 or denormal * anything = +-0.
        result = ZeroIfAnyOperandIsZero(
            result, GetAbsoluteOperand(operand_0, instr.scalar_operands[0]),
            GetAbsoluteOperand(operand_1, instr.scalar_operands[1]));
      }
      return result;
    }
    case ucode::AluScalarOpcode::kAddsc0:
    case ucode::AluScalarOpcode::kAddsc1:
    case ucode::AluScalarOpcode::kSubsc0:
    case ucode::AluScalarOpcode::kSubsc1: {
      return builder_->createNoContractionBinOp(
          spv::Op(kOps[size_t(instr.scalar_opcode)]), type_float_,
          GetOperandComponents(operand_storage[0], instr.scalar_operands[0],
                               0b0001),
          GetOperandComponents(operand_storage[1], instr.scalar_operands[1],
                               0b0001));
    }

    case ucode::AluScalarOpcode::kRetainPrev:
      // Special case in ProcessAluInstruction - loading ps only if writing to
      // anywhere.
      return spv::NoResult;
  }

  assert_unhandled_case(instr.scalar_opcode);
  EmitTranslationError("Unknown ALU scalar operation");
  return spv::NoResult;
}

}  // namespace gpu
}  // namespace xe
