/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_builder.h"

#include <memory>
#include <utility>
#include <vector>

namespace xe {
namespace gpu {

spv::Id SpirvBuilder::createQuadOp(spv::Op op_code, spv::Id type_id,
                                   spv::Id operand1, spv::Id operand2,
                                   spv::Id operand3, spv::Id operand4) {
  if (generatingOpCodeForSpecConst) {
    std::vector<spv::Id> operands(4);
    operands[0] = operand1;
    operands[1] = operand2;
    operands[2] = operand3;
    operands[3] = operand4;
    return createSpecConstantOp(op_code, type_id, operands,
                                std::vector<spv::Id>());
  }
  std::unique_ptr<spv::Instruction> op =
      std::make_unique<spv::Instruction>(getUniqueId(), type_id, op_code);
  op->addIdOperand(operand1);
  op->addIdOperand(operand2);
  op->addIdOperand(operand3);
  op->addIdOperand(operand4);
  spv::Id result = op->getResultId();
  buildPoint->addInstruction(std::move(op));
  return result;
}

spv::Id SpirvBuilder::createNoContractionUnaryOp(spv::Op op_code,
                                                 spv::Id type_id,
                                                 spv::Id operand) {
  spv::Id result = createUnaryOp(op_code, type_id, operand);
  addDecoration(result, spv::DecorationNoContraction);
  return result;
}

spv::Id SpirvBuilder::createNoContractionBinOp(spv::Op op_code, spv::Id type_id,
                                               spv::Id operand1,
                                               spv::Id operand2) {
  spv::Id result = createBinOp(op_code, type_id, operand1, operand2);
  addDecoration(result, spv::DecorationNoContraction);
  return result;
}

spv::Id SpirvBuilder::createUnaryBuiltinCall(spv::Id result_type,
                                             spv::Id builtins, int entry_point,
                                             spv::Id operand) {
  std::unique_ptr<spv::Instruction> instruction =
      std::make_unique<spv::Instruction>(getUniqueId(), result_type,
                                         spv::OpExtInst);
  instruction->addIdOperand(builtins);
  instruction->addImmediateOperand(entry_point);
  instruction->addIdOperand(operand);
  spv::Id result = instruction->getResultId();
  getBuildPoint()->addInstruction(std::move(instruction));
  return result;
}

spv::Id SpirvBuilder::createBinBuiltinCall(spv::Id result_type,
                                           spv::Id builtins, int entry_point,
                                           spv::Id operand1, spv::Id operand2) {
  std::unique_ptr<spv::Instruction> instruction =
      std::make_unique<spv::Instruction>(getUniqueId(), result_type,
                                         spv::OpExtInst);
  instruction->addIdOperand(builtins);
  instruction->addImmediateOperand(entry_point);
  instruction->addIdOperand(operand1);
  instruction->addIdOperand(operand2);
  spv::Id result = instruction->getResultId();
  getBuildPoint()->addInstruction(std::move(instruction));
  return result;
}

spv::Id SpirvBuilder::createTriBuiltinCall(spv::Id result_type,
                                           spv::Id builtins, int entry_point,
                                           spv::Id operand1, spv::Id operand2,
                                           spv::Id operand3) {
  std::unique_ptr<spv::Instruction> instruction =
      std::make_unique<spv::Instruction>(getUniqueId(), result_type,
                                         spv::OpExtInst);
  instruction->addIdOperand(builtins);
  instruction->addImmediateOperand(entry_point);
  instruction->addIdOperand(operand1);
  instruction->addIdOperand(operand2);
  instruction->addIdOperand(operand3);
  spv::Id result = instruction->getResultId();
  getBuildPoint()->addInstruction(std::move(instruction));
  return result;
}

}  // namespace gpu
}  // namespace xe
