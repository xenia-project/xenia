/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

namespace xe {
namespace gpu {

SpirvShaderTranslator::SpirvShaderTranslator() = default;

SpirvShaderTranslator::~SpirvShaderTranslator() = default;

void SpirvShaderTranslator::StartTranslation() {
  auto& e = emitter_;

  auto fn = e.MakeMainEntry();
  auto float_1_0 = e.MakeFloatConstant(1.0f);
  auto acos = e.CreateGlslStd450InstructionCall(
      spv::Decoration::Invariant, e.MakeFloatType(32), spv::GLSLstd450::Acos,
      {{float_1_0}});
  e.MakeReturn(true);
}

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  auto& e = emitter_;

  std::vector<uint32_t> spirv_words;
  e.Serialize(spirv_words);

  std::vector<uint8_t> spirv_bytes;
  spirv_bytes.resize(spirv_words.size() * 4);
  std::memcpy(spirv_bytes.data(), spirv_words.data(), spirv_bytes.size());
  return spirv_bytes;
}

void SpirvShaderTranslator::ProcessLabel(uint32_t cf_index) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessControlFlowNopInstruction() {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessCallInstruction(
    const ParsedCallInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessReturnInstruction(
    const ParsedReturnInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& e = emitter_;
  switch (instr.type) {
    case ParsedAluInstruction::Type::kNop:
      e.CreateNop();
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
  auto& e = emitter_;

  EmitUnimplementedTranslationError();
}

void SpirvShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& e = emitter_;

  spv::Id value_id = LoadFromOperand(instr.operands[0]);

  StoreToResult(value_id, instr.result);

  EmitUnimplementedTranslationError();
}

spv::Id SpirvShaderTranslator::LoadFromOperand(const InstructionOperand& op) {
  auto& e = emitter_;

  spv::Id current_type_id = e.MakeFloatType(32);
  spv::Id current_value_id = e.CreateUndefined(current_type_id);

  // storage_addressing_mode
  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      //
      op.storage_index;
      break;
    case InstructionStorageSource::kConstantFloat:
      //
      op.storage_index;
      break;
    case InstructionStorageSource::kConstantInt:
      //
      op.storage_index;
      break;
    case InstructionStorageSource::kConstantBool:
      //
      op.storage_index;
      break;
    case InstructionStorageSource::kVertexFetchConstant:
      //
      op.storage_index;
      break;
    case InstructionStorageSource::kTextureFetchConstant:
      //
      op.storage_index;
      break;
  }

  if (op.is_absolute_value) {
    current_value_id = e.CreateGlslStd450InstructionCall(
        spv::Decoration::RelaxedPrecision, current_type_id,
        spv::GLSLstd450::FAbs, {current_value_id});
  }
  if (op.is_negated) {
    current_value_id =
        e.CreateUnaryOp(spv::Op::OpFNegate, current_type_id, current_value_id);
  }

  // swizzle

  return current_value_id;
}

void SpirvShaderTranslator::StoreToResult(spv::Id source_value_id,
                                          const InstructionResult& result) {
  auto& e = emitter_;

  if (result.storage_target == InstructionStorageTarget::kNone) {
    // No-op?
    return;
  }

  spv::Id storage_pointer = 0;
  // storage_addressing_mode
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      //
      result.storage_index;
      break;
    case InstructionStorageTarget::kInterpolant:
      //
      result.storage_index;
      break;
    case InstructionStorageTarget::kPosition:
      //
      break;
    case InstructionStorageTarget::kPointSize:
      //
      break;
    case InstructionStorageTarget::kColorTarget:
      //
      result.storage_index;
      break;
    case InstructionStorageTarget::kDepth:
      //
      break;
  }

  spv::Id current_value_id = source_value_id;
  spv::Id current_type_id = e.GetTypeId(source_value_id);

  // Clamp the input value.
  if (result.is_clamped) {
    //
  }

  // write mask

  // swizzle

  // Convert to the appropriate type, if needed.
  spv::Id desired_type_id = e.MakeFloatType(32);
  if (current_value_id != desired_type_id) {
    EmitTranslationError("Type conversion on storage not yet implemented");
  }

  // Perform store into the pointer.
}

}  // namespace gpu
}  // namespace xe
