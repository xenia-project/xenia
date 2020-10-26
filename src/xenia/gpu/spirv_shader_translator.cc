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
#include <utility>
#include <vector>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"

namespace xe {
namespace gpu {

SpirvShaderTranslator::SpirvShaderTranslator(bool supports_clip_distance,
                                             bool supports_cull_distance)
    : supports_clip_distance_(supports_clip_distance),
      supports_cull_distance_(supports_cull_distance) {}

void SpirvShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  builder_.reset();

  main_switch_op_.reset();
  main_switch_next_pc_phi_operands_.clear();

  cf_exec_conditional_merge_ = nullptr;
  cf_instruction_predicate_merge_ = nullptr;
}

void SpirvShaderTranslator::StartTranslation() {
  // Tool ID 26 "Xenia Emulator Microcode Translator".
  // https://github.com/KhronosGroup/SPIRV-Headers/blob/c43a43c7cc3af55910b9bec2a71e3e8a622443cf/include/spirv/spir-v.xml#L79
  // TODO(Triang3l): Logger.
  builder_ = std::make_unique<spv::Builder>(1 << 16, (26 << 16) | 1, nullptr);

  builder_->addCapability(IsSpirvTessEvalShader() ? spv::CapabilityTessellation
                                                  : spv::CapabilityShader);
  ext_inst_glsl_std_450_ = builder_->import("GLSL.std.450");
  builder_->setMemoryModel(spv::AddressingModelLogical,
                           spv::MemoryModelGLSL450);
  builder_->setSource(spv::SourceLanguageUnknown, 0);

  type_void_ = builder_->makeVoidType();
  type_bool_ = builder_->makeBoolType();
  type_int_ = builder_->makeIntType(32);
  type_int4_ = builder_->makeVectorType(type_int_, 4);
  type_uint_ = builder_->makeUintType(32);
  type_uint3_ = builder_->makeVectorType(type_uint_, 3);
  type_uint4_ = builder_->makeVectorType(type_uint_, 4);
  type_float_ = builder_->makeFloatType(32);
  type_float2_ = builder_->makeVectorType(type_float_, 2);
  type_float3_ = builder_->makeVectorType(type_float_, 3);
  type_float4_ = builder_->makeVectorType(type_float_, 4);

  const_int_0_ = builder_->makeIntConstant(0);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  for (uint32_t i = 0; i < 4; ++i) {
    id_vector_temp_.push_back(const_int_0_);
  }
  const_int4_0_ = builder_->makeCompositeConstant(type_int4_, id_vector_temp_);
  const_uint_0_ = builder_->makeUintConstant(0);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  for (uint32_t i = 0; i < 4; ++i) {
    id_vector_temp_.push_back(const_uint_0_);
  }
  const_uint4_0_ =
      builder_->makeCompositeConstant(type_uint4_, id_vector_temp_);
  const_float_0_ = builder_->makeFloatConstant(0.0f);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  for (uint32_t i = 0; i < 4; ++i) {
    id_vector_temp_.push_back(const_float_0_);
  }
  const_float4_0_ =
      builder_->makeCompositeConstant(type_float4_, id_vector_temp_);

  // Common uniform buffer - bool and loop constants.
  id_vector_temp_.clear();
  id_vector_temp_.reserve(2);
  // 256 bool constants.
  id_vector_temp_.push_back(builder_->makeArrayType(
      type_uint4_, builder_->makeUintConstant(2), sizeof(uint32_t) * 4));
  // Currently (as of October 24, 2020) makeArrayType only uses the stride to
  // check if deduplication can be done - the array stride decoration needs to
  // be applied explicitly.
  builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                          sizeof(uint32_t) * 4);
  // 32 loop constants.
  id_vector_temp_.push_back(builder_->makeArrayType(
      type_uint4_, builder_->makeUintConstant(8), sizeof(uint32_t) * 4));
  builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                          sizeof(uint32_t) * 4);
  spv::Id type_bool_loop_constants =
      builder_->makeStructType(id_vector_temp_, "XeBoolLoopConstants");
  builder_->addMemberName(type_bool_loop_constants, 0, "bool_constants");
  builder_->addMemberDecoration(type_bool_loop_constants, 0,
                                spv::DecorationOffset, 0);
  builder_->addMemberName(type_bool_loop_constants, 1, "loop_constants");
  builder_->addMemberDecoration(type_bool_loop_constants, 1,
                                spv::DecorationOffset, sizeof(uint32_t) * 8);
  builder_->addDecoration(type_bool_loop_constants, spv::DecorationBlock);
  uniform_bool_loop_constants_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassUniform, type_bool_loop_constants,
      "xe_uniform_bool_loop_constants");
  builder_->addDecoration(uniform_bool_loop_constants_,
                          spv::DecorationDescriptorSet,
                          int(kDescriptorSetBoolLoopConstants));
  builder_->addDecoration(uniform_bool_loop_constants_, spv::DecorationBinding,
                          0);

  if (IsSpirvVertexOrTessEvalShader()) {
    StartVertexOrTessEvalShaderBeforeMain();
  }

  // Begin the main function.
  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* function_main_entry;
  function_main_ = builder_->makeFunctionEntry(
      spv::NoPrecision, type_void_, "main", main_param_types, main_precisions,
      &function_main_entry);

  // Begin ucode translation. Initialize everything, even without defined
  // defaults, for safety.
  var_main_predicate_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassFunction, type_bool_,
      "xe_var_predicate", builder_->makeBoolConstant(false));
  var_main_loop_count_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassFunction, type_uint4_,
      "xe_var_loop_count", const_uint4_0_);
  var_main_address_absolute_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassFunction, type_int_,
      "xe_var_address_absolute", const_int_0_);
  var_main_address_relative_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassFunction, type_int4_,
      "xe_var_address_relative", const_int4_0_);
  uint32_t register_array_size = register_count();
  if (register_array_size) {
    id_vector_temp_.clear();
    id_vector_temp_.reserve(register_array_size);
    // TODO(Triang3l): In PS, only initialize starting from the interpolators,
    // probably manually. But not very important.
    for (uint32_t i = 0; i < register_array_size; ++i) {
      id_vector_temp_.push_back(const_float4_0_);
    }
    spv::Id type_register_array = builder_->makeArrayType(
        type_float4_, builder_->makeUintConstant(register_array_size), 0);
    var_main_registers_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_register_array,
        "xe_var_registers",
        builder_->makeCompositeConstant(type_register_array, id_vector_temp_));
  }

  // Write the execution model-specific prologue with access to variables in the
  // main function.
  if (IsSpirvVertexOrTessEvalShader()) {
    StartVertexOrTessEvalShaderInMain();
  }

  // Open the main loop.
  spv::Block& main_loop_pre_header = *builder_->getBuildPoint();
  main_loop_header_ = &builder_->makeNewBlock();
  spv::Block& main_loop_body = builder_->makeNewBlock();
  // Added later because the body has nested control flow, but according to the
  // specification:
  // "The order of blocks in a function must satisfy the rule that blocks appear
  //  before all blocks they dominate."
  main_loop_continue_ =
      new spv::Block(builder_->getUniqueId(), *function_main_);
  main_loop_merge_ = new spv::Block(builder_->getUniqueId(), *function_main_);
  builder_->createBranch(main_loop_header_);

  // If no jumps, don't create a switch, but still create a loop so exece can
  // break.
  bool has_main_switch = !label_addresses().empty();

  // Main loop header - based on whether it's the first iteration (entered from
  // the function or from the continuation), choose the program counter.
  builder_->setBuildPoint(main_loop_header_);
  spv::Id main_loop_pc_current = 0;
  if (has_main_switch) {
    // OpPhi must be the first in the block.
    id_vector_temp_.clear();
    id_vector_temp_.reserve(4);
    id_vector_temp_.push_back(const_int_0_);
    id_vector_temp_.push_back(main_loop_pre_header.getId());
    main_loop_pc_next_ = builder_->getUniqueId();
    id_vector_temp_.push_back(main_loop_pc_next_);
    id_vector_temp_.push_back(main_loop_continue_->getId());
    main_loop_pc_current =
        builder_->createOp(spv::OpPhi, type_int_, id_vector_temp_);
  }
  uint_vector_temp_.clear();
  builder_->createLoopMerge(main_loop_merge_, main_loop_continue_,
                            spv::LoopControlDontUnrollMask, uint_vector_temp_);
  builder_->createBranch(&main_loop_body);

  // Main loop body.
  builder_->setBuildPoint(&main_loop_body);
  if (has_main_switch) {
    // Create the program counter switch with cases for every label and for
    // label 0.
    main_switch_header_ = builder_->getBuildPoint();
    main_switch_merge_ =
        new spv::Block(builder_->getUniqueId(), *function_main_);
    {
      std::unique_ptr<spv::Instruction> main_switch_selection_merge_op =
          std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
      main_switch_selection_merge_op->addIdOperand(main_switch_merge_->getId());
      main_switch_selection_merge_op->addImmediateOperand(
          spv::SelectionControlDontFlattenMask);
      builder_->getBuildPoint()->addInstruction(
          std::move(main_switch_selection_merge_op));
    }
    main_switch_op_ = std::make_unique<spv::Instruction>(spv::OpSwitch);
    main_switch_op_->addIdOperand(main_loop_pc_current);
    main_switch_op_->addIdOperand(main_switch_merge_->getId());
    // The default case (the merge here) must have the header as a predecessor.
    main_switch_merge_->addPredecessor(main_switch_header_);
    // The instruction will be inserted later, when all cases are filled.
    // Insert and enter case 0.
    spv::Block* main_switch_case_0_block =
        new spv::Block(builder_->getUniqueId(), *function_main_);
    main_switch_op_->addImmediateOperand(0);
    main_switch_op_->addIdOperand(main_switch_case_0_block->getId());
    // Every switch case must have the OpSelectionMerge/OpSwitch block as a
    // predecessor.
    main_switch_case_0_block->addPredecessor(main_switch_header_);
    function_main_->addBlock(main_switch_case_0_block);
    builder_->setBuildPoint(main_switch_case_0_block);
  }
}

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  // Close flow control within the last switch case.
  CloseExecConditionals();
  bool has_main_switch = !label_addresses().empty();
  // After the final exec (if it happened to be not exece, which would already
  // have a break branch), break from the switch if it exists, or from the
  // loop it doesn't.
  if (!builder_->getBuildPoint()->isTerminated()) {
    builder_->createBranch(has_main_switch ? main_switch_merge_
                                           : main_loop_merge_);
  }
  if (has_main_switch) {
    // Insert the switch instruction with all cases added as operands.
    builder_->setBuildPoint(main_switch_header_);
    builder_->getBuildPoint()->addInstruction(std::move(main_switch_op_));
    // Build the main switch merge, breaking out of the loop after falling
    // through the end or breaking from exece (only continuing if a jump - from
    // a guest loop or from jmp/call - was made).
    function_main_->addBlock(main_switch_merge_);
    builder_->setBuildPoint(main_switch_merge_);
    builder_->createBranch(main_loop_merge_);
  }

  // Main loop continuation - choose the program counter based on the path
  // taken (-1 if not from a jump as a safe fallback, which would result in not
  // hitting any switch case and reaching the final break in the body).
  function_main_->addBlock(main_loop_continue_);
  builder_->setBuildPoint(main_loop_continue_);
  if (has_main_switch) {
    // OpPhi, if added, must be the first in the block.
    // If labels were added, but not jumps (for example, due to the call
    // instruction not being implemented as of October 18, 2020), send an
    // impossible program counter value (-1) to the OpPhi at the next iteration.
    if (main_switch_next_pc_phi_operands_.empty()) {
      main_switch_next_pc_phi_operands_.push_back(
          builder_->makeIntConstant(-1));
    }
    std::unique_ptr<spv::Instruction> main_loop_pc_next_op =
        std::make_unique<spv::Instruction>(
            main_loop_pc_next_, type_int_,
            main_switch_next_pc_phi_operands_.size() >= 2 ? spv::OpPhi
                                                          : spv::OpCopyObject);
    for (spv::Id operand : main_switch_next_pc_phi_operands_) {
      main_loop_pc_next_op->addIdOperand(operand);
    }
    builder_->getBuildPoint()->addInstruction(std::move(main_loop_pc_next_op));
  }
  builder_->createBranch(main_loop_header_);

  // Add the main loop merge block and go back to the function.
  function_main_->addBlock(main_loop_merge_);
  builder_->setBuildPoint(main_loop_merge_);

  if (IsSpirvVertexOrTessEvalShader()) {
    CompleteVertexOrTessEvalShaderInMain();
  }

  // End the main function.
  builder_->leaveFunction();

  // Make the main function the entry point.
  spv::ExecutionModel execution_model;
  if (IsSpirvFragmentShader()) {
    execution_model = spv::ExecutionModelFragment;
    builder_->addExecutionMode(function_main_,
                               spv::ExecutionModeOriginUpperLeft);
  } else {
    assert_true(IsSpirvVertexOrTessEvalShader());
    execution_model = IsSpirvTessEvalShader()
                          ? spv::ExecutionModelTessellationEvaluation
                          : spv::ExecutionModelVertex;
  }
  spv::Instruction* entry_point =
      builder_->addEntryPoint(execution_model, function_main_, "main");

  if (IsSpirvVertexOrTessEvalShader()) {
    CompleteVertexOrTessEvalShaderAfterMain(entry_point);
  }

  // TODO(Triang3l): Avoid copy?
  std::vector<unsigned int> module_uints;
  builder_->dump(module_uints);
  std::vector<uint8_t> module_bytes;
  module_bytes.reserve(sizeof(unsigned int) * module_uints.size());
  module_bytes.insert(module_bytes.cend(),
                      reinterpret_cast<const uint8_t*>(module_uints.data()),
                      reinterpret_cast<const uint8_t*>(module_uints.data()) +
                          sizeof(unsigned int) * module_uints.size());
  return module_bytes;
}

void SpirvShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0) {
    // 0 already added in the beginning.
    return;
  }

  assert_false(label_addresses().empty());

  // Close flow control within the previous switch case.
  CloseExecConditionals();

  spv::Function& function = builder_->getBuildPoint()->getParent();
  // Create the next switch case and fallthrough to it.
  spv::Block* new_case = new spv::Block(builder_->getUniqueId(), function);
  main_switch_op_->addImmediateOperand(cf_index);
  main_switch_op_->addIdOperand(new_case->getId());
  // Every switch case must have the OpSelectionMerge/OpSwitch block as a
  // predecessor.
  new_case->addPredecessor(main_switch_header_);
  // The previous block may have already been terminated if was exece.
  if (!builder_->getBuildPoint()->isTerminated()) {
    builder_->createBranch(new_case);
  }
  function.addBlock(new_case);
  builder_->setBuildPoint(new_case);
}

void SpirvShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  UpdateExecConditionals(instr.type, instr.bool_constant_index,
                         instr.condition);
}

void SpirvShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  if (instr.is_end) {
    // Break out of the main switch (if exists) and the main loop.
    CloseInstructionPredication();
    if (!builder_->getBuildPoint()->isTerminated()) {
      builder_->createBranch(label_addresses().empty() ? main_loop_merge_
                                                       : main_switch_merge_);
    }
  }
  UpdateExecConditionals(instr.type, instr.bool_constant_index,
                         instr.condition);
}

void SpirvShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  // loop il<idx>, L<idx> - loop with loop data il<idx>, end @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  EnsureBuildPointAvailable();

  id_vector_temp_.clear();
  id_vector_temp_.reserve(3);
  // Loop constants (member 1).
  id_vector_temp_.push_back(builder_->makeIntConstant(1));
  // 4-component vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index >> 2)));
  // Scalar within the vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index & 3)));
  // Count (unsigned) in bits 0:7 of the loop constant (struct member 1),
  // initial aL (unsigned) in 8:15.
  spv::Id loop_constant =
      builder_->createLoad(builder_->createAccessChain(
                               spv::StorageClassUniform,
                               uniform_bool_loop_constants_, id_vector_temp_),
                           spv::NoPrecision);

  spv::Id const_int_8 = builder_->makeIntConstant(8);

  // Push the count to the loop count stack - move XYZ to YZW and set X to the
  // new iteration count (swizzling the way glslang does it for similar GLSL).
  spv::Id loop_count_stack_old =
      builder_->createLoad(var_main_loop_count_, spv::NoPrecision);
  spv::Id loop_count_new =
      builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_, loop_constant,
                            const_int_0_, const_int_8);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  id_vector_temp_.push_back(loop_count_new);
  for (unsigned int i = 0; i < 3; ++i) {
    id_vector_temp_.push_back(
        builder_->createCompositeExtract(loop_count_stack_old, type_uint_, i));
  }
  builder_->createStore(
      builder_->createCompositeConstruct(type_uint4_, id_vector_temp_),
      var_main_loop_count_);

  // Push aL - keep the same value as in the previous loop if repeating, or the
  // new one otherwise.
  spv::Id address_relative_stack_old =
      builder_->createLoad(var_main_address_relative_, spv::NoPrecision);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  if (instr.is_repeat) {
    id_vector_temp_.emplace_back();
  } else {
    id_vector_temp_.push_back(builder_->createUnaryOp(
        spv::OpBitcast, type_int_,
        builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_,
                              loop_constant, const_int_8, const_int_8)));
  }
  for (unsigned int i = 0; i < 3; ++i) {
    id_vector_temp_.push_back(builder_->createCompositeExtract(
        address_relative_stack_old, type_int_, i));
  }
  if (instr.is_repeat) {
    id_vector_temp_[0] = id_vector_temp_[1];
  }
  builder_->createStore(
      builder_->createCompositeConstruct(type_int4_, id_vector_temp_),
      var_main_address_relative_);

  // Break (jump to the skip label) if the loop counter is 0 (since the
  // condition is checked in the end).
  spv::Block& head_block = *builder_->getBuildPoint();
  spv::Id loop_count_zero = builder_->createBinOp(
      spv::OpIEqual, type_bool_, loop_count_new, const_uint_0_);
  spv::Block& skip_block = builder_->makeNewBlock();
  spv::Block& body_block = builder_->makeNewBlock();
  {
    std::unique_ptr<spv::Instruction> selection_merge_op =
        std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
    selection_merge_op->addIdOperand(body_block.getId());
    selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
    head_block.addInstruction(std::move(selection_merge_op));
  }
  {
    std::unique_ptr<spv::Instruction> branch_conditional_op =
        std::make_unique<spv::Instruction>(spv::OpBranchConditional);
    branch_conditional_op->addIdOperand(loop_count_zero);
    branch_conditional_op->addIdOperand(skip_block.getId());
    branch_conditional_op->addIdOperand(body_block.getId());
    // More likely to enter than to skip.
    branch_conditional_op->addImmediateOperand(1);
    branch_conditional_op->addImmediateOperand(2);
    head_block.addInstruction(std::move(branch_conditional_op));
  }
  skip_block.addPredecessor(&head_block);
  body_block.addPredecessor(&head_block);
  builder_->setBuildPoint(&skip_block);
  main_switch_next_pc_phi_operands_.push_back(
      builder_->makeIntConstant(int(instr.loop_skip_address)));
  main_switch_next_pc_phi_operands_.push_back(
      builder_->getBuildPoint()->getId());
  builder_->createBranch(main_loop_continue_);
  builder_->setBuildPoint(&body_block);
}

void SpirvShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  // endloop il<idx>, L<idx> - end loop w/ data il<idx>, head @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  EnsureBuildPointAvailable();

  // Subtract 1 from the loop counter (will store later).
  spv::Id loop_count_stack_old =
      builder_->createLoad(var_main_loop_count_, spv::NoPrecision);
  spv::Id loop_count = builder_->createBinOp(
      spv::OpISub, type_uint_,
      builder_->createCompositeExtract(loop_count_stack_old, type_uint_, 0),
      builder_->makeUintConstant(1));
  spv::Id address_relative_stack_old =
      builder_->createLoad(var_main_address_relative_, spv::NoPrecision);

  // Predicated break works like break if (loop_count == 0 || [!]p0).
  // Three options, due to logical operations usage (so OpLogicalNot is not
  // required):
  // - Continue if (loop_count != 0).
  // - Continue if (loop_count != 0 && p0), if breaking if !p0.
  // - Break if (loop_count == 0 || p0), if breaking if p0.
  bool break_is_true = instr.is_predicated_break && instr.predicate_condition;
  spv::Id condition =
      builder_->createBinOp(break_is_true ? spv::OpIEqual : spv::OpINotEqual,
                            type_bool_, loop_count, const_uint_0_);
  if (instr.is_predicated_break) {
    condition = builder_->createBinOp(
        instr.predicate_condition ? spv::OpLogicalOr : spv::OpLogicalAnd,
        type_bool_, condition,
        builder_->createLoad(var_main_predicate_, spv::NoPrecision));
  }

  spv::Block& body_block = *builder_->getBuildPoint();
  spv::Block& continue_block = builder_->makeNewBlock();
  spv::Block& break_block = builder_->makeNewBlock();
  {
    std::unique_ptr<spv::Instruction> selection_merge_op =
        std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
    selection_merge_op->addIdOperand(break_block.getId());
    selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
    body_block.addInstruction(std::move(selection_merge_op));
  }
  {
    std::unique_ptr<spv::Instruction> branch_conditional_op =
        std::make_unique<spv::Instruction>(spv::OpBranchConditional);
    branch_conditional_op->addIdOperand(condition);
    // More likely to continue than to break.
    if (break_is_true) {
      branch_conditional_op->addIdOperand(break_block.getId());
      branch_conditional_op->addIdOperand(continue_block.getId());
      branch_conditional_op->addImmediateOperand(1);
      branch_conditional_op->addImmediateOperand(2);
    } else {
      branch_conditional_op->addIdOperand(continue_block.getId());
      branch_conditional_op->addIdOperand(break_block.getId());
      branch_conditional_op->addImmediateOperand(2);
      branch_conditional_op->addImmediateOperand(1);
    }
    body_block.addInstruction(std::move(branch_conditional_op));
  }
  continue_block.addPredecessor(&body_block);
  break_block.addPredecessor(&body_block);

  // Continue case.
  builder_->setBuildPoint(&continue_block);
  // Store the loop count with 1 subtracted.
  builder_->createStore(builder_->createCompositeInsert(
                            loop_count, loop_count_stack_old, type_uint4_, 0),
                        var_main_loop_count_);
  // Extract the value to add to aL (signed, in bits 16:23 of the loop
  // constant).
  id_vector_temp_.clear();
  id_vector_temp_.reserve(3);
  // Loop constants (member 1).
  id_vector_temp_.push_back(builder_->makeIntConstant(1));
  // 4-component vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index >> 2)));
  // Scalar within the vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index & 3)));
  spv::Id loop_constant =
      builder_->createLoad(builder_->createAccessChain(
                               spv::StorageClassUniform,
                               uniform_bool_loop_constants_, id_vector_temp_),
                           spv::NoPrecision);
  spv::Id address_relative_old = builder_->createCompositeExtract(
      address_relative_stack_old, type_int_, 0);
  builder_->createStore(
      builder_->createCompositeInsert(
          builder_->createBinOp(
              spv::OpIAdd, type_int_, address_relative_old,
              builder_->createTriOp(
                  spv::OpBitFieldSExtract, type_int_,
                  builder_->createUnaryOp(spv::OpBitcast, type_int_,
                                          loop_constant),
                  builder_->makeIntConstant(16), builder_->makeIntConstant(8))),
          address_relative_stack_old, type_int4_, 0),
      var_main_address_relative_);
  // Jump back to the beginning of the loop body.
  main_switch_next_pc_phi_operands_.push_back(
      builder_->makeIntConstant(int(instr.loop_body_address)));
  main_switch_next_pc_phi_operands_.push_back(
      builder_->getBuildPoint()->getId());
  builder_->createBranch(main_loop_continue_);

  // Break case.
  builder_->setBuildPoint(&break_block);
  // Pop the current loop off the loop counter and the relative address stacks -
  // move YZW to XYZ and set W to 0.
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  for (unsigned int i = 1; i < 4; ++i) {
    id_vector_temp_.push_back(
        builder_->createCompositeExtract(loop_count_stack_old, type_uint_, i));
  }
  id_vector_temp_.push_back(const_uint_0_);
  builder_->createStore(
      builder_->createCompositeConstruct(type_uint4_, id_vector_temp_),
      var_main_loop_count_);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  for (unsigned int i = 1; i < 4; ++i) {
    id_vector_temp_.push_back(builder_->createCompositeExtract(
        address_relative_stack_old, type_int_, i));
  }
  id_vector_temp_.push_back(const_int_0_);
  builder_->createStore(
      builder_->createCompositeConstruct(type_int4_, id_vector_temp_),
      var_main_address_relative_);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  // Now going to fall through to the next control flow instruction.
}

void SpirvShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  // Treat like exec, merge with execs if possible, since it's an if too.
  ParsedExecInstruction::Type type;
  if (instr.type == ParsedJumpInstruction::Type::kConditional) {
    type = ParsedExecInstruction::Type::kConditional;
  } else if (instr.type == ParsedJumpInstruction::Type::kPredicated) {
    type = ParsedExecInstruction::Type::kPredicated;
  } else {
    type = ParsedExecInstruction::Type::kUnconditional;
  }
  UpdateExecConditionals(type, instr.bool_constant_index, instr.condition);

  // UpdateExecConditionals may not necessarily close the instruction-level
  // predicate check (it's not necessary if the execs are merged), but here the
  // instruction itself is on the control flow level, so the predicate check is
  // on the control flow level too.
  CloseInstructionPredication();

  if (builder_->getBuildPoint()->isTerminated()) {
    // Unreachable for some reason.
    return;
  }
  main_switch_next_pc_phi_operands_.push_back(
      builder_->makeIntConstant(int(instr.target_address)));
  main_switch_next_pc_phi_operands_.push_back(
      builder_->getBuildPoint()->getId());
  builder_->createBranch(main_loop_continue_);
}

void SpirvShaderTranslator::EnsureBuildPointAvailable() {
  if (!builder_->getBuildPoint()->isTerminated()) {
    return;
  }
  spv::Block& new_block = builder_->makeNewBlock();
  new_block.setUnreachable();
  builder_->setBuildPoint(&new_block);
}

void SpirvShaderTranslator::StartVertexOrTessEvalShaderBeforeMain() {
  // Create the inputs.
  if (IsSpirvTessEvalShader()) {
    input_primitive_id_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_int_, "gl_PrimitiveID");
    builder_->addDecoration(input_primitive_id_, spv::DecorationBuiltIn,
                            spv::BuiltInPrimitiveId);
  } else {
    input_vertex_index_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_int_, "gl_VertexIndex");
    builder_->addDecoration(input_vertex_index_, spv::DecorationBuiltIn,
                            spv::BuiltInVertexIndex);
  }

  // Create the entire GLSL 4.50 gl_PerVertex output similar to what glslang
  // does. Members (like gl_PointSize) don't need to be used, and also
  // ClipDistance and CullDistance may exist even if the device doesn't support
  // them, as long as the capabilities aren't enabled, and nothing is stored to
  // them.
  if (supports_clip_distance_) {
    builder_->addCapability(spv::CapabilityClipDistance);
  }
  if (supports_cull_distance_) {
    builder_->addCapability(spv::CapabilityCullDistance);
  }
  std::vector<spv::Id> struct_per_vertex_members;
  struct_per_vertex_members.reserve(kOutputPerVertexMemberCount);
  struct_per_vertex_members.push_back(type_float4_);
  struct_per_vertex_members.push_back(type_float_);
  // TODO(Triang3l): Specialization constant for ucp_cull_only_ena, for 6 + 1
  // or 1 + 7 array sizes.
  struct_per_vertex_members.push_back(builder_->makeArrayType(
      type_float_, builder_->makeUintConstant(supports_clip_distance_ ? 6 : 1),
      0));
  struct_per_vertex_members.push_back(
      builder_->makeArrayType(type_float_, builder_->makeUintConstant(1), 0));
  spv::Id type_struct_per_vertex =
      builder_->makeStructType(struct_per_vertex_members, "gl_PerVertex");
  builder_->addMemberDecoration(type_struct_per_vertex,
                                kOutputPerVertexMemberPosition,
                                spv::DecorationBuiltIn, spv::BuiltInPosition);
  builder_->addMemberDecoration(type_struct_per_vertex,
                                kOutputPerVertexMemberPointSize,
                                spv::DecorationBuiltIn, spv::BuiltInPointSize);
  builder_->addMemberDecoration(
      type_struct_per_vertex, kOutputPerVertexMemberClipDistance,
      spv::DecorationBuiltIn, spv::BuiltInClipDistance);
  builder_->addMemberDecoration(
      type_struct_per_vertex, kOutputPerVertexMemberCullDistance,
      spv::DecorationBuiltIn, spv::BuiltInCullDistance);
  builder_->addDecoration(type_struct_per_vertex, spv::DecorationBlock);
  output_per_vertex_ =
      builder_->createVariable(spv::NoPrecision, spv::StorageClassOutput,
                               type_struct_per_vertex, "xe_out_gl_PerVertex");
}

void SpirvShaderTranslator::StartVertexOrTessEvalShaderInMain() {
  var_main_point_size_edge_flag_kill_vertex_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassFunction, type_float3_,
      "xe_var_point_size_edge_flag_kill_vertex");
}

void SpirvShaderTranslator::CompleteVertexOrTessEvalShaderInMain() {}

void SpirvShaderTranslator::CompleteVertexOrTessEvalShaderAfterMain(
    spv::Instruction* entry_point) {
  if (IsSpirvTessEvalShader()) {
    entry_point->addIdOperand(input_primitive_id_);
  } else {
    entry_point->addIdOperand(input_vertex_index_);
  }
  entry_point->addIdOperand(output_per_vertex_);
}

void SpirvShaderTranslator::UpdateExecConditionals(
    ParsedExecInstruction::Type type, uint32_t bool_constant_index,
    bool condition) {
  // Check if we can merge the new exec with the previous one, or the jump with
  // the previous exec. The instruction-level predicate check is also merged in
  // this case.
  if (type == ParsedExecInstruction::Type::kConditional) {
    // Can merge conditional with conditional, as long as the bool constant and
    // the expected values are the same.
    if (cf_exec_conditional_merge_ &&
        cf_exec_bool_constant_or_predicate_ == bool_constant_index &&
        cf_exec_condition_ == condition) {
      return;
    }
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    // Can merge predicated with predicated if the conditions are the same and
    // the previous exec hasn't modified the predicate register.
    if (!cf_exec_predicate_written_ && cf_exec_conditional_merge_ &&
        cf_exec_bool_constant_or_predicate_ == kCfExecBoolConstantPredicate &&
        cf_exec_condition_ == condition) {
      return;
    }
  } else {
    // Can merge unconditional with unconditional.
    assert_true(type == ParsedExecInstruction::Type::kUnconditional);
    if (!cf_exec_conditional_merge_) {
      return;
    }
  }

  CloseExecConditionals();

  if (type == ParsedExecInstruction::Type::kUnconditional) {
    return;
  }

  EnsureBuildPointAvailable();
  spv::Id condition_id;
  if (type == ParsedExecInstruction::Type::kConditional) {
    id_vector_temp_.clear();
    id_vector_temp_.reserve(3);
    // Bool constants (member 0).
    id_vector_temp_.push_back(const_int_0_);
    // 128-bit vector.
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int(bool_constant_index >> 7)));
    // 32-bit scalar of a 128-bit vector.
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int((bool_constant_index >> 5) & 3)));
    spv::Id bool_constant_scalar =
        builder_->createLoad(builder_->createAccessChain(
                                 spv::StorageClassUniform,
                                 uniform_bool_loop_constants_, id_vector_temp_),
                             spv::NoPrecision);
    condition_id = builder_->createBinOp(
        spv::OpINotEqual, type_bool_,
        builder_->createBinOp(
            spv::OpBitwiseAnd, type_uint_, bool_constant_scalar,
            builder_->makeUintConstant(uint32_t(1)
                                       << (bool_constant_index & 31))),
        const_uint_0_);
    cf_exec_bool_constant_or_predicate_ = bool_constant_index;
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    condition_id = builder_->createLoad(var_main_predicate_, spv::NoPrecision);
    cf_exec_bool_constant_or_predicate_ = kCfExecBoolConstantPredicate;
  } else {
    assert_unhandled_case(type);
    return;
  }
  cf_exec_condition_ = condition;
  spv::Function& function = builder_->getBuildPoint()->getParent();
  cf_exec_conditional_merge_ =
      new spv::Block(builder_->getUniqueId(), function);
  {
    std::unique_ptr<spv::Instruction> selection_merge_op =
        std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
    selection_merge_op->addIdOperand(cf_exec_conditional_merge_->getId());
    selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
    builder_->getBuildPoint()->addInstruction(std::move(selection_merge_op));
  }
  spv::Block& inner_block = builder_->makeNewBlock();
  builder_->createConditionalBranch(
      condition_id, condition ? &inner_block : cf_exec_conditional_merge_,
      condition ? cf_exec_conditional_merge_ : &inner_block);
  builder_->setBuildPoint(&inner_block);
}

void SpirvShaderTranslator::UpdateInstructionPredication(bool predicated,
                                                         bool condition) {
  if (!predicated) {
    CloseInstructionPredication();
    return;
  }

  if (cf_instruction_predicate_merge_) {
    if (cf_instruction_predicate_condition_ == condition) {
      // Already in the needed instruction-level conditional.
      return;
    }
    CloseInstructionPredication();
  }

  // If the instruction predicate condition is the same as the exec predicate
  // condition, no need to open a check. However, if there was a `setp` prior
  // to this instruction, the predicate value now may be different than it was
  // in the beginning of the exec.
  if (!cf_exec_predicate_written_ && cf_exec_conditional_merge_ &&
      cf_exec_bool_constant_or_predicate_ == kCfExecBoolConstantPredicate &&
      cf_exec_condition_ == condition) {
    return;
  }

  cf_instruction_predicate_condition_ = condition;
  EnsureBuildPointAvailable();
  spv::Id predicate_id =
      builder_->createLoad(var_main_predicate_, spv::NoPrecision);
  spv::Block& predicated_block = builder_->makeNewBlock();
  cf_instruction_predicate_merge_ = &builder_->makeNewBlock();
  {
    std::unique_ptr<spv::Instruction> selection_merge_op =
        std::make_unique<spv::Instruction>(spv::OpSelectionMerge);
    selection_merge_op->addIdOperand(cf_instruction_predicate_merge_->getId());
    selection_merge_op->addImmediateOperand(spv::SelectionControlMaskNone);
    builder_->getBuildPoint()->addInstruction(std::move(selection_merge_op));
  }
  builder_->createConditionalBranch(
      predicate_id,
      condition ? &predicated_block : cf_instruction_predicate_merge_,
      condition ? cf_instruction_predicate_merge_ : &predicated_block);
  builder_->setBuildPoint(&predicated_block);
}

void SpirvShaderTranslator::CloseInstructionPredication() {
  if (!cf_instruction_predicate_merge_) {
    return;
  }
  spv::Block& inner_block = *builder_->getBuildPoint();
  if (!inner_block.isTerminated()) {
    builder_->createBranch(cf_instruction_predicate_merge_);
  }
  inner_block.getParent().addBlock(cf_instruction_predicate_merge_);
  builder_->setBuildPoint(cf_instruction_predicate_merge_);
  cf_instruction_predicate_merge_ = nullptr;
}

void SpirvShaderTranslator::CloseExecConditionals() {
  // Within the exec - instruction-level predicate check.
  CloseInstructionPredication();
  // Exec level.
  if (cf_exec_conditional_merge_) {
    spv::Block& inner_block = *builder_->getBuildPoint();
    if (!inner_block.isTerminated()) {
      builder_->createBranch(cf_exec_conditional_merge_);
    }
    inner_block.getParent().addBlock(cf_exec_conditional_merge_);
    builder_->setBuildPoint(cf_exec_conditional_merge_);
    cf_exec_conditional_merge_ = nullptr;
  }
  // Nothing relies on the predicate value being unchanged now.
  cf_exec_predicate_written_ = false;
}

}  // namespace gpu
}  // namespace xe
