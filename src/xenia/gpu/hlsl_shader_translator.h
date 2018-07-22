/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_HLSL_SHADER_TRANSLATOR_H_
#define XENIA_GPU_HLSL_SHADER_TRANSLATOR_H_

#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

// For shader model 5_1 (Direct3D 12).
class HlslShaderTranslator : public ShaderTranslator {
 public:
  HlslShaderTranslator();
  ~HlslShaderTranslator() override;

  enum class SRVType : uint32_t {
    // 1D, 2D or stacked texture bound as a 2D array texture.
    Texture2DArray,
    // 3D texture (also has a 2D array view of the same fetch registers because
    // tfetch3D is used for both stacked and 3D textures.
    Texture3D,
    // Cube texture.
    TextureCube
  };

  struct SRVBinding {
    SRVType type : 2;
    // 0-31 for textures, 0-95 for vertex buffers.
    uint32_t fetch_constant : 7;
  };

 protected:
  void Reset() override;

  void EmitTranslationError(const char* message) override;
  void EmitUnimplementedTranslationError() override;

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

 private:
  void Indent();
  void Unindent();

  // Returns whether a new conditional was emitted.
  bool BeginPredicatedInstruction(bool is_predicated, bool predicate_condition);
  void EndPredicatedInstruction(bool conditional_emitted);

  void EmitLoadOperand(uint32_t src_index, const InstructionOperand& op);
  void EmitStoreResult(const InstructionResult& result, bool source_is_scalar);

  StringBuffer source_;
  uint32_t depth_ = 0;
  char depth_prefix_[16] = {0};

  bool cf_wrote_pc_ = false;
  bool cf_exec_pred_ = false;
  bool cf_exec_pred_cond_ = false;

  bool writes_depth_ = false;

  std::vector<SRVBinding> srv_bindings_;
  // Finds or adds an SRV binding to the shader's SRV list, returns t# index.
  uint32_t AddSRVBinding(SRVType type, uint32_t fetch_constant);

  // Sampler index -> fetch constant index mapping.
  // TODO(Triang3l): On binding tier 1 (Nvidia Fermi), there can't be more than
  // 16 samplers in a shader. Hopefully no game uses so many.
  uint32_t sampler_fetch_constants_[32];
  uint32_t sampler_count_ = 0;
  uint32_t AddSampler(uint32_t fetch_constant);

  // Whether the cube instruction has been used and conversion functions need to
  // be emitted.
  bool cube_used_ = false;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_HLSL_SHADER_TRANSLATOR_H_
