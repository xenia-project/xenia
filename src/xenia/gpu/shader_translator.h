/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SHADER_TRANSLATOR_H_

#include <memory>
#include "xenia/gpu/shader.h"

namespace xe {
namespace gpu {

class ShaderTranslator {
 public:
  virtual ~ShaderTranslator();

  virtual uint64_t GetDefaultVertexShaderModification(
      uint32_t dynamic_addressable_register_count,
      Shader::HostVertexShaderType host_vertex_shader_type =
          Shader::HostVertexShaderType::kVertex) const {
    return 0;
  }
  virtual uint64_t GetDefaultPixelShaderModification(
      uint32_t dynamic_addressable_register_count) const {
    return 0;
  }

  // AnalyzeUcode must be done on the shader before translating!
  bool TranslateAnalyzedShader(Shader::Translation& translation);

 protected:
  ShaderTranslator();

  // Resets translator state before beginning translation.
  virtual void Reset();

  // Shader and modification currently being translated.
  Shader::Translation& current_translation() const { return *translation_; }
  Shader& current_shader() const { return current_translation().shader(); }

  // Register count from SQ_PROGRAM_CNTL, stored by the implementation in its
  // modification bits.
  virtual uint32_t GetModificationRegisterCount() const {
    return xenos::kMaxShaderTempRegisters;
  }

  // True if the current shader is a vertex shader.
  bool is_vertex_shader() const {
    return current_shader().type() == xenos::ShaderType::kVertex;
  }
  // True if the current shader is a pixel shader.
  bool is_pixel_shader() const {
    return current_shader().type() == xenos::ShaderType::kPixel;
  }

  // Temporary register count, accessible via static and dynamic addressing.
  uint32_t register_count() const { return register_count_; }

  // Emits a translation error that will be passed back in the result.
  virtual void EmitTranslationError(const char* message, bool is_fatal = true);

  // Handles the start of translation.
  // At this point the vertex and texture bindings have been gathered.
  virtual void StartTranslation() {}

  // Handles the end of translation when all ucode has been processed.
  // Returns the translated shader binary.
  virtual std::vector<uint8_t> CompleteTranslation() {
    return std::vector<uint8_t>();
  }

  // Handles post-translation tasks when the shader has been fully translated.
  virtual void PostTranslation() {}
  // Sets the host disassembly on a shader.
  void set_host_disassembly(Shader::Translation& translation,
                            std::string value) {
    translation.host_disassembly_ = std::move(value);
  }

  // Pre-process a control-flow instruction before anything else.
  virtual void PreProcessControlFlowInstructions(
      std::vector<ucode::ControlFlowInstruction> instrs) {}

  // Handles translation for control flow label addresses.
  // This is triggered once for each label required (due to control flow
  // operations) before any of the instructions within the target exec.
  virtual void ProcessLabel(uint32_t cf_index) {}

  // Handles translation for control flow nop instructions.
  virtual void ProcessControlFlowNopInstruction(uint32_t cf_index) {}
  // Handles the start of a control flow instruction at the given address.
  virtual void ProcessControlFlowInstructionBegin(uint32_t cf_index) {}
  // Handles the end of a control flow instruction that began at the given
  // address.
  virtual void ProcessControlFlowInstructionEnd(uint32_t cf_index) {}
  // Handles translation for control flow exec instructions prior to their
  // contained ALU/fetch instructions.
  virtual void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) {
  }
  // Handles translation for control flow exec instructions after their
  // contained ALU/fetch instructions.
  virtual void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) {}
  // Handles translation for loop start instructions.
  virtual void ProcessLoopStartInstruction(
      const ParsedLoopStartInstruction& instr) {}
  // Handles translation for loop end instructions.
  virtual void ProcessLoopEndInstruction(
      const ParsedLoopEndInstruction& instr) {}
  // Handles translation for function call instructions.
  virtual void ProcessCallInstruction(const ParsedCallInstruction& instr) {}
  // Handles translation for function return instructions.
  virtual void ProcessReturnInstruction(const ParsedReturnInstruction& instr) {}
  // Handles translation for jump instructions.
  virtual void ProcessJumpInstruction(const ParsedJumpInstruction& instr) {}
  // Handles translation for alloc instructions.
  virtual void ProcessAllocInstruction(const ParsedAllocInstruction& instr) {}

  // Handles translation for vertex fetch instructions.
  virtual void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) {}
  // Handles translation for texture fetch instructions.
  virtual void ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) {}
  // Handles translation for ALU instructions.
  virtual void ProcessAluInstruction(const ParsedAluInstruction& instr) {}

 private:
  void TranslateControlFlowInstruction(const ucode::ControlFlowInstruction& cf);
  void TranslateExecInstructions(const ParsedExecInstruction& instr);

  // Current shader and modification being translated.
  Shader::Translation* translation_ = nullptr;

  // Accumulated translation errors.
  std::vector<Shader::Error> errors_;

  // Temporary register count, accessible via static and dynamic addressing.
  uint32_t register_count_ = 0;

  // Current control flow dword index.
  uint32_t cf_index_ = 0;

  // Kept for supporting vfetch_mini.
  ucode::VertexFetchInstruction previous_vfetch_full_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_TRANSLATOR_H_
