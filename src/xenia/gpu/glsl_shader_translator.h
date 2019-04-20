/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GLSL_SHADER_TRANSLATOR_H_
#define XENIA_GPU_GLSL_SHADER_TRANSLATOR_H_

#include <memory>
#include <string>
#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

class GlslShaderTranslator : public ShaderTranslator {
 public:
  enum class Dialect {
    kGL45,
    kVulkan,
  };

  GlslShaderTranslator(Dialect dialect);
  ~GlslShaderTranslator() override;

 protected:
  void Reset() override;

  void EmitTranslationError(const char* message) override;
  void EmitUnimplementedTranslationError() override;

  void StartTranslation() override;
  std::vector<uint8_t> CompleteTranslation() override;

  void ProcessLabel(uint32_t cf_index) override;
  void ProcessControlFlowNopInstruction(uint32_t cf_index) override;
  void ProcessControlFlowInstructionBegin(uint32_t cf_index) override;
  void ProcessControlFlowInstructionEnd(uint32_t cf_index) override;
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
  void Indent();
  void Unindent();

  void EmitLoadOperand(size_t i, const InstructionOperand& op);
  void EmitStoreVectorResult(const InstructionResult& result);
  void EmitStoreScalarResult(const InstructionResult& result);
  void EmitStoreResult(const InstructionResult& result, const char* temp);

  Dialect dialect_;

  StringBuffer source_;
  int depth_ = 0;
  char depth_prefix_[16] = {0};
  bool cf_wrote_pc_ = false;
  bool cf_exec_pred_ = false;
  bool cf_exec_pred_cond_ = false;

  bool ProcessVectorAluOperation(const ParsedAluInstruction& instr);
  bool ProcessScalarAluOperation(const ParsedAluInstruction& instr);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GLSL_SHADER_TRANSLATOR_H_
