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
  const_float_0_ = builder_->makeFloatConstant(0.0f);
  id_vector_temp_.clear();
  id_vector_temp_.reserve(4);
  for (uint32_t i = 0; i < 4; ++i) {
    id_vector_temp_.push_back(const_float_0_);
  }
  const_float4_0_ =
      builder_->makeCompositeConstant(type_float4_, id_vector_temp_);

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
  spv::Block* main_loop_pre_header = builder_->getBuildPoint();
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
    id_vector_temp_.clear();
    id_vector_temp_.reserve(4);
    id_vector_temp_.push_back(const_int_0_);
    id_vector_temp_.push_back(main_loop_pre_header->getId());
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

}  // namespace gpu
}  // namespace xe
