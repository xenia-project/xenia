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

  struct SystemConstants {
    // vec4 0
    float mul_rcp_w[3];
    uint32_t vertex_base_index;
    // vec4 1
    float ndc_scale[3];
    uint32_t vertex_index_endian;
    // vec4 2
    float ndc_offset[3];
    float pixel_half_pixel_offset;
    // vec4 3
    float ssaa_inv_scale[2];
    uint32_t pixel_pos_reg;
  };

  struct TextureSRV {
    uint32_t fetch_constant;
    TextureDimension dimension;
  };
  const TextureSRV* GetTextureSRVs(uint32_t& count_out) const {
    count_out = texture_srv_count_;
    return texture_srvs_;
  }
  const uint32_t* GetSamplerFetchConstants(uint32_t& count_out) const {
    count_out = sampler_count_;
    return sampler_fetch_constants_;
  }

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

  // Returns whether a new conditional was emitted.
  bool BeginPredicatedInstruction(bool is_predicated, bool predicate_condition);
  void EndPredicatedInstruction(bool conditional_emitted);

  void EmitLoadOperand(size_t src_index, const InstructionOperand& op);
  void EmitStoreResult(const InstructionResult& result, bool source_is_scalar);

  StringBuffer source_inner_;
  uint32_t depth_ = 0;
  char depth_prefix_[32] = {0};

  bool cf_wrote_pc_ = false;
  bool cf_exec_pred_ = false;
  bool cf_exec_pred_cond_ = false;

  bool writes_depth_ = false;

  // Up to 32 2D array textures, 32 3D textures and 32 cube textures.
  TextureSRV texture_srvs_[96];
  uint32_t texture_srv_count_ = 0;
  // Finds or adds a texture binding to the shader's SRV list, returns t# index.
  uint32_t AddTextureSRV(uint32_t fetch_constant, TextureDimension dimension);

  // Sampler index -> fetch constant index mapping.
  // TODO(Triang3l): On binding tier 1 (Nvidia Fermi), there can't be more than
  // 16 samplers in a shader. Hopefully no game uses so many.
  uint32_t sampler_fetch_constants_[32];
  uint32_t sampler_count_ = 0;
  uint32_t AddSampler(uint32_t fetch_constant);

  void ProcessVectorAluInstruction(const ParsedAluInstruction& instr);
  void ProcessScalarAluInstruction(const ParsedAluInstruction& instr);

  // Whether the cube instruction has been used and conversion functions need to
  // be emitted.
  bool cube_used_ = false;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_HLSL_SHADER_TRANSLATOR_H_
