/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <cstring>

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {

using spv::GLSLstd450;
using spv::Id;
using spv::Op;

SpirvShaderTranslator::SpirvShaderTranslator() = default;

SpirvShaderTranslator::~SpirvShaderTranslator() = default;

void SpirvShaderTranslator::StartTranslation() {
  // Create a new builder.
  builder_ = std::make_unique<spv::Builder>(0xFFFFFFFF);
  auto& b = *builder_;

  // Import required modules.
  glsl_std_450_instruction_set_ = b.import("GLSL.std.450");

  // Configure environment.
  b.setSource(spv::SourceLanguage::SourceLanguageUnknown, 0);
  b.setMemoryModel(spv::AddressingModel::AddressingModelLogical,
                   spv::MemoryModel::MemoryModelGLSL450);
  b.addCapability(spv::Capability::CapabilityShader);
  b.addCapability(spv::Capability::CapabilityGenericPointer);
  if (is_vertex_shader()) {
    b.addCapability(spv::Capability::CapabilityClipDistance);
    b.addCapability(spv::Capability::CapabilityCullDistance);
  }
  if (is_pixel_shader()) {
    b.addCapability(spv::Capability::CapabilityDerivativeControl);
  }

  // main() entry point.
  auto mainFn = b.makeMain();
  if (is_vertex_shader()) {
    b.addEntryPoint(spv::ExecutionModel::ExecutionModelVertex, mainFn, "main");
  } else {
    b.addEntryPoint(spv::ExecutionModel::ExecutionModelFragment, mainFn,
                    "main");
    b.addExecutionMode(mainFn, spv::ExecutionModeOriginUpperLeft);
  }

  // TODO(benvanik): transform feedback.
  if (false) {
    b.addCapability(spv::Capability::CapabilityTransformFeedback);
    b.addExecutionMode(mainFn, spv::ExecutionMode::ExecutionModeXfb);
  }

  auto float_1_0 = b.makeFloatConstant(2.0f);
  auto acos = CreateGlslStd450InstructionCall(
      spv::Decoration::DecorationInvariant, b.makeFloatType(32),
      GLSLstd450::kAcos, {float_1_0});
}

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  auto& b = *builder_;

  b.makeReturn(false);

  std::vector<uint32_t> spirv_words;
  b.dump(spirv_words);

  // Cleanup builder.
  builder_.reset();

  // Copy bytes out.
  // TODO(benvanik): avoid copy?
  std::vector<uint8_t> spirv_bytes;
  spirv_bytes.resize(spirv_words.size() * 4);
  std::memcpy(spirv_bytes.data(), spirv_words.data(), spirv_bytes.size());
  return spirv_bytes;
}

void SpirvShaderTranslator::PostTranslation(Shader* shader) {
  // TODO(benvanik): only if needed? could be slowish.
  auto disasm = disassembler_.Disassemble(
      reinterpret_cast<const uint32_t*>(shader->translated_binary().data()),
      shader->translated_binary().size() / 4);
  if (disasm->has_error()) {
    XELOGE("Failed to disassemble SPIRV - invalid?");
    return;
  }
  set_host_disassembly(shader, disasm->to_string());
}

void SpirvShaderTranslator::ProcessLabel(uint32_t cf_index) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessControlFlowNopInstruction() {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessCallInstruction(
    const ParsedCallInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessReturnInstruction(
    const ParsedReturnInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& b = *builder_;
  switch (instr.type) {
    case ParsedAluInstruction::Type::kNop:
      b.createNoResultOp(spv::Op::OpNop);
      break;
    case ParsedAluInstruction::Type::kVector:
      ProcessVectorAluInstruction(instr);
      break;
    case ParsedAluInstruction::Type::kScalar:
      ProcessScalarAluInstruction(instr);
      break;
  }
}

void SpirvShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& b = *builder_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& b = *builder_;

  spv::Id value_id = LoadFromOperand(instr.operands[0]);

  StoreToResult(value_id, instr.result);

  EmitUnimplementedTranslationError();
}

Id SpirvShaderTranslator::CreateGlslStd450InstructionCall(
    spv::Decoration precision, Id result_type, GLSLstd450 instruction_ordinal,
    std::vector<Id> args) {
  return builder_->createBuiltinCall(result_type, glsl_std_450_instruction_set_,
                                     static_cast<int>(instruction_ordinal),
                                     args);
}

spv::Id SpirvShaderTranslator::LoadFromOperand(const InstructionOperand& op) {
  auto& b = *builder_;

  spv::Id current_type_id = b.makeFloatType(32);
  spv::Id current_value_id = b.createUndefined(current_type_id);

  // storage_addressing_mode
  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      // TODO(benvanik): op.storage_index
      break;
    case InstructionStorageSource::kConstantFloat:
      // TODO(benvanik): op.storage_index
      break;
    case InstructionStorageSource::kConstantInt:
      // TODO(benvanik): op.storage_index
      break;
    case InstructionStorageSource::kConstantBool:
      // TODO(benvanik): op.storage_index
      break;
    case InstructionStorageSource::kVertexFetchConstant:
      // TODO(benvanik): op.storage_index
      break;
    case InstructionStorageSource::kTextureFetchConstant:
      // TODO(benvanik): op.storage_index
      break;
  }

  if (op.is_absolute_value) {
    current_value_id = CreateGlslStd450InstructionCall(
        spv::Decoration::DecorationRelaxedPrecision, current_type_id,
        GLSLstd450::kFAbs, {current_value_id});
  }
  if (op.is_negated) {
    current_value_id =
        b.createUnaryOp(spv::Op::OpFNegate, current_type_id, current_value_id);
  }

  // swizzle

  return current_value_id;
}

void SpirvShaderTranslator::StoreToResult(spv::Id source_value_id,
                                          const InstructionResult& result) {
  auto& b = *builder_;

  if (result.storage_target == InstructionStorageTarget::kNone) {
    // No-op?
    return;
  }

  spv::Id storage_pointer = 0;
  // storage_addressing_mode
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      // TODO(benvanik): result.storage_index
      break;
    case InstructionStorageTarget::kInterpolant:
      // TODO(benvanik): result.storage_index
      break;
    case InstructionStorageTarget::kPosition:
      // TODO(benvanik): result.storage_index
      break;
    case InstructionStorageTarget::kPointSize:
      // TODO(benvanik): result.storage_index
      break;
    case InstructionStorageTarget::kColorTarget:
      // TODO(benvanik): result.storage_index
      break;
    case InstructionStorageTarget::kDepth:
      // TODO(benvanik): result.storage_index
      break;
    case InstructionStorageTarget::kNone:
      assert_unhandled_case(result.storage_target);
      break;
  }

  spv::Id current_value_id = source_value_id;
  spv::Id current_type_id = b.getTypeId(source_value_id);

  // Clamp the input value.
  if (result.is_clamped) {
    //
  }

  // write mask

  // swizzle

  // Convert to the appropriate type, if needed.
  spv::Id desired_type_id = b.makeFloatType(32);
  if (current_value_id != desired_type_id) {
    EmitTranslationError("Type conversion on storage not yet implemented");
  }

  // Perform store into the pointer.
}

}  // namespace gpu
}  // namespace xe
