/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_

#include <memory>
#include <string>
#include <vector>

#include "third_party/glslang-spirv/SpvBuilder.h"
#include "third_party/spirv/GLSL.std.450.hpp11"
#include "xenia/gpu/shader_translator.h"
#include "xenia/ui/spirv/spirv_disassembler.h"

namespace xe {
namespace gpu {

class SpirvShaderTranslator : public ShaderTranslator {
 public:
  SpirvShaderTranslator();
  ~SpirvShaderTranslator() override;

 protected:
  void StartTranslation() override;
  std::vector<uint8_t> CompleteTranslation() override;
  void PostTranslation(Shader* shader) override;

  void PreProcessControlFlowInstruction(uint32_t cf_index) override;
  void ProcessLabel(uint32_t cf_index) override;
  void ProcessControlFlowInstructionBegin(uint32_t cf_index) override;
  void ProcessControlFlowInstructionEnd(uint32_t cf_index) override;
  void ProcessControlFlowNopInstruction() override;
  void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(
      const ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(
      const ParsedLoopEndInstruction& instr) override;
  void ProcessCallInstruction(const ParsedCallInstruction& instr) override;
  void ProcessReturnInstruction(const ParsedReturnInstruction& instr) override;
  void ProcessJumpInstruction(const ParsedJumpInstruction& instr) override;
  void ProcessAllocInstruction(const ParsedAllocInstruction& instr) override;
  void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) override;
  void ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  void ProcessVectorAluInstruction(const ParsedAluInstruction& instr);
  void ProcessScalarAluInstruction(const ParsedAluInstruction& instr);

  // Creates a call to the given GLSL intrinsic.
  spv::Id SpirvShaderTranslator::CreateGlslStd450InstructionCall(
      spv::Decoration precision, spv::Id result_type,
      spv::GLSLstd450 instruction_ordinal, std::vector<spv::Id> args);

  // Loads an operand into a value.
  // The value returned will be in the form described in the operand (number of
  // components, etc).
  spv::Id LoadFromOperand(const InstructionOperand& op);
  // Stores a value based on the specified result information.
  // The value will be transformed into the appropriate form for the result and
  // the proper components will be selected.
  void StoreToResult(spv::Id source_value_id, const InstructionResult& result,
                     spv::Id predicate_cond = 0);

  xe::ui::spirv::SpirvDisassembler disassembler_;

  // TODO(benvanik): replace with something better, make reusable, etc.
  std::unique_ptr<spv::Builder> builder_;
  spv::Id glsl_std_450_instruction_set_ = 0;

  // Types.
  spv::Id float_type_ = 0, bool_type_ = 0;
  spv::Id vec2_float_type_ = 0, vec3_float_type_ = 0, vec4_float_type_ = 0;
  spv::Id vec4_uint_type_ = 0;
  spv::Id vec4_bool_type_ = 0;

  // Constants.
  spv::Id vec4_float_zero_ = 0, vec4_float_one_ = 0;

  // Array of AMD registers.
  // These values are all pointers.
  spv::Id registers_ptr_ = 0, registers_type_ = 0;
  spv::Id consts_ = 0, a0_ = 0, aL_ = 0, p0_ = 0;
  spv::Id ps_ = 0, pv_ = 0;  // IDs of previous results
  spv::Id pos_ = 0;
  spv::Id interpolators_ = 0;
  spv::Id frag_outputs_ = 0;

  // Map of {binding -> {offset -> spv input}}
  std::map<uint32_t, std::map<uint32_t, spv::Id>> vertex_binding_map_;
  std::map<uint32_t, spv::Block*> cf_blocks_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
