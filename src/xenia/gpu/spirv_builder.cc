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

#include "xenia/base/assert.h"

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

SpirvBuilder::IfBuilder::IfBuilder(spv::Id condition, unsigned int control,
                                   SpirvBuilder& builder,
                                   unsigned int thenWeight,
                                   unsigned int elseWeight)
    : builder(builder),
      condition(condition),
      control(control),
      thenWeight(thenWeight),
      elseWeight(elseWeight),
      function(builder.getBuildPoint()->getParent()) {
  // Make the blocks, but only put the then-block into the function, the
  // else-block and merge-block will be added later, in order, after earlier
  // code is emitted.
  thenBlock = new spv::Block(builder.getUniqueId(), function);
  elseBlock = nullptr;
  mergeBlock = new spv::Block(builder.getUniqueId(), function);

  // Save the current block, so that we can add in the flow control split when
  // makeEndIf is called.
  headerBlock = builder.getBuildPoint();

  spv::Id headerBlockId = headerBlock->getId();
  thenPhiParent = headerBlockId;
  elsePhiParent = headerBlockId;

  function.addBlock(thenBlock);
  builder.setBuildPoint(thenBlock);
}

void SpirvBuilder::IfBuilder::makeBeginElse(bool branchToMerge) {
#ifndef NDEBUG
  assert_true(currentBranch == Branch::kThen);
#endif

  if (branchToMerge) {
    // Close out the "then" by having it jump to the mergeBlock.
    thenPhiParent = builder.getBuildPoint()->getId();
    builder.createBranch(mergeBlock);
  }

  // Make the first else block and add it to the function.
  elseBlock = new spv::Block(builder.getUniqueId(), function);
  function.addBlock(elseBlock);

  // Start building the else block.
  builder.setBuildPoint(elseBlock);

#ifndef NDEBUG
  currentBranch = Branch::kElse;
#endif
}

void SpirvBuilder::IfBuilder::makeEndIf(bool branchToMerge) {
#ifndef NDEBUG
  assert_true(currentBranch == Branch::kThen || currentBranch == Branch::kElse);
#endif

  if (branchToMerge) {
    // Jump to the merge block.
    (elseBlock ? elsePhiParent : thenPhiParent) =
        builder.getBuildPoint()->getId();
    builder.createBranch(mergeBlock);
  }

  // Go back to the headerBlock and make the flow control split.
  builder.setBuildPoint(headerBlock);
  builder.createSelectionMerge(mergeBlock, control);
  {
    spv::Block* falseBlock = elseBlock ? elseBlock : mergeBlock;
    std::unique_ptr<spv::Instruction> branch =
        std::make_unique<spv::Instruction>(spv::OpBranchConditional);
    branch->addIdOperand(condition);
    branch->addIdOperand(thenBlock->getId());
    branch->addIdOperand(falseBlock->getId());
    if (thenWeight || elseWeight) {
      branch->addImmediateOperand(thenWeight);
      branch->addImmediateOperand(elseWeight);
    }
    builder.getBuildPoint()->addInstruction(std::move(branch));
    thenBlock->addPredecessor(builder.getBuildPoint());
    falseBlock->addPredecessor(builder.getBuildPoint());
  }

  // Add the merge block to the function.
  function.addBlock(mergeBlock);
  builder.setBuildPoint(mergeBlock);

#ifndef NDEBUG
  currentBranch = Branch::kMerge;
#endif
}

spv::Id SpirvBuilder::IfBuilder::createMergePhi(spv::Id then_variable,
                                                spv::Id else_variable) const {
  assert_true(builder.getBuildPoint() == mergeBlock);
  return builder.createQuadOp(spv::OpPhi, builder.getTypeId(then_variable),
                              then_variable, getThenPhiParent(), else_variable,
                              getElsePhiParent());
}

SpirvBuilder::SwitchBuilder::SwitchBuilder(spv::Id selector,
                                           unsigned int selection_control,
                                           SpirvBuilder& builder)
    : builder_(builder),
      selector_(selector),
      selection_control_(selection_control),
      function_(builder.getBuildPoint()->getParent()),
      header_block_(builder.getBuildPoint()),
      default_phi_parent_(builder.getBuildPoint()->getId()) {
  merge_block_ = new spv::Block(builder_.getUniqueId(), function_);
}

void SpirvBuilder::SwitchBuilder::makeBeginDefault() {
  assert_null(default_block_);

  endSegment();

  default_block_ = new spv::Block(builder_.getUniqueId(), function_);
  function_.addBlock(default_block_);
  default_block_->addPredecessor(header_block_);
  builder_.setBuildPoint(default_block_);

  current_branch_ = Branch::kDefault;
}

void SpirvBuilder::SwitchBuilder::makeBeginCase(unsigned int literal) {
  endSegment();

  auto case_block = new spv::Block(builder_.getUniqueId(), function_);
  function_.addBlock(case_block);
  cases_.emplace_back(literal, case_block->getId());
  case_block->addPredecessor(header_block_);
  builder_.setBuildPoint(case_block);

  current_branch_ = Branch::kCase;
}

void SpirvBuilder::SwitchBuilder::addCurrentCaseLiteral(unsigned int literal) {
  assert_true(current_branch_ == Branch::kCase);

  cases_.emplace_back(literal, cases_.back().second);
}

void SpirvBuilder::SwitchBuilder::makeEndSwitch() {
  endSegment();

  builder_.setBuildPoint(header_block_);

  builder_.createSelectionMerge(merge_block_, selection_control_);

  std::unique_ptr<spv::Instruction> switch_instruction =
      std::make_unique<spv::Instruction>(spv::OpSwitch);
  switch_instruction->addIdOperand(selector_);
  if (default_block_) {
    switch_instruction->addIdOperand(default_block_->getId());
  } else {
    switch_instruction->addIdOperand(merge_block_->getId());
    merge_block_->addPredecessor(header_block_);
  }
  for (const std::pair<unsigned int, spv::Id>& case_pair : cases_) {
    switch_instruction->addImmediateOperand(case_pair.first);
    switch_instruction->addIdOperand(case_pair.second);
  }
  builder_.getBuildPoint()->addInstruction(std::move(switch_instruction));

  function_.addBlock(merge_block_);
  builder_.setBuildPoint(merge_block_);

  current_branch_ = Branch::kMerge;
}

void SpirvBuilder::SwitchBuilder::endSegment() {
  assert_true(current_branch_ == Branch::kSelection ||
              current_branch_ == Branch::kDefault ||
              current_branch_ == Branch::kCase);

  if (current_branch_ == Branch::kSelection) {
    return;
  }

  if (!builder_.getBuildPoint()->isTerminated()) {
    builder_.createBranch(merge_block_);
    if (current_branch_ == Branch::kDefault) {
      default_phi_parent_ = builder_.getBuildPoint()->getId();
    }
  }

  current_branch_ = Branch::kSelection;
}

}  // namespace gpu
}  // namespace xe
