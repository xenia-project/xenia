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
#include <string>
#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/gpu/ucode.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
class TranslatedShader {
 public:
  struct Error {
    bool is_fatal = false;
    std::string message;
  };

  struct VertexBinding {
    // Index within the vertex binding listing.
    size_t binding_index;
    // Fetch constant index [0-95].
    uint32_t fetch_constant;
    // Fetch instruction with all parameters.
    ucode::VertexFetchInstruction op;
  };
  struct TextureBinding {
    // Index within the texture binding listing.
    size_t binding_index;
    // Fetch constant index [0-31].
    uint32_t fetch_constant;
    // Fetch instruction with all parameters.
    ucode::TextureFetchInstruction op;
  };

  ~TranslatedShader();

  ShaderType type() const { return shader_type_; }
  const std::vector<uint32_t>& ucode_data() const { return ucode_data_; }
  const uint32_t* ucode_dwords() const { return ucode_data_.data(); }
  size_t ucode_dword_count() const { return ucode_data_.size(); }

  const std::vector<VertexBinding>& vertex_bindings() const {
    return vertex_bindings_;
  }
  const std::vector<TextureBinding>& texture_bindings() const {
    return texture_bindings_;
  }

  bool is_valid() const { return is_valid_; }
  const std::vector<Error> errors() const { return errors_; }

  const std::vector<uint8_t> binary() const { return binary_; }

 private:
  friend class ShaderTranslator;

  TranslatedShader(ShaderType shader_type, uint64_t ucode_data_hash,
                   const uint32_t* ucode_dwords, size_t ucode_dword_count,
                   std::vector<Error> errors);

  ShaderType shader_type_;
  std::vector<uint32_t> ucode_data_;
  uint64_t ucode_data_hash_;

  std::vector<VertexBinding> vertex_bindings_;
  std::vector<TextureBinding> texture_bindings_;

  bool is_valid_ = false;
  std::vector<Error> errors_;

  std::vector<uint8_t> binary_;
};

class ShaderTranslator {
 public:
  virtual ~ShaderTranslator();

  std::unique_ptr<TranslatedShader> Translate(ShaderType shader_type,
                                              uint64_t ucode_data_hash,
                                              const uint32_t* ucode_dwords,
                                              size_t ucode_dword_count);

 protected:
  ShaderTranslator();

  size_t ucode_disasm_line_number() const { return ucode_disasm_line_number_; }
  StringBuffer& ucode_disasm_buffer() { return ucode_disasm_buffer_; }
  void EmitTranslationError(const char* message);

  virtual std::vector<uint8_t> CompleteTranslation() {
    return std::vector<uint8_t>();
  }

  virtual void ProcessControlFlowNop(const ucode::ControlFlowInstruction& cf) {}
  virtual void ProcessControlFlowExec(
      const ucode::ControlFlowExecInstruction& cf) {}
  virtual void ProcessControlFlowCondExec(
      const ucode::ControlFlowCondExecInstruction& cf) {}
  virtual void ProcessControlFlowCondExecPred(
      const ucode::ControlFlowCondExecPredInstruction& cf) {}
  virtual void ProcessControlFlowLoopStart(
      const ucode::ControlFlowLoopStartInstruction& cf) {}
  virtual void ProcessControlFlowLoopEnd(
      const ucode::ControlFlowLoopEndInstruction& cf) {}
  virtual void ProcessControlFlowCondCall(
      const ucode::ControlFlowCondCallInstruction& cf) {}
  virtual void ProcessControlFlowReturn(
      const ucode::ControlFlowReturnInstruction& cf) {}
  virtual void ProcessControlFlowCondJmp(
      const ucode::ControlFlowCondJmpInstruction& cf) {}
  virtual void ProcessControlFlowAlloc(
      const ucode::ControlFlowAllocInstruction& cf) {}

  virtual void ProcessVertexFetchInstruction(
      const ucode::VertexFetchInstruction& op) {}
  virtual void ProcessTextureFetchTextureFetch(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchGetTextureBorderColorFrac(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchGetTextureComputedLod(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchGetTextureGradients(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchGetTextureWeights(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchSetTextureLod(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchSetTextureGradientsHorz(
      const ucode::TextureFetchInstruction& op) {}
  virtual void ProcessTextureFetchSetTextureGradientsVert(
      const ucode::TextureFetchInstruction& op) {}

  virtual void ProcessAluNop(const ucode::AluInstruction& op) {}

  virtual void ProcessAluVectorAdd(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorMul(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorMax(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorMin(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorSetEQ(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorSetGT(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorSetGE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorSetNE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorFrac(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorTrunc(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorFloor(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorMad(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorCndEQ(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorCndGE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorCndGT(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorDp4(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorDp3(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorDp2Add(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorCube(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorMax4(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorPredSetEQPush(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorPredSetNEPush(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorPredSetGTPush(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorPredSetGEPush(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorKillEQ(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorKillGT(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorKillLGE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorKillNE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorDst(const ucode::AluInstruction& op) {}
  virtual void ProcessAluVectorMaxA(const ucode::AluInstruction& op) {}

  virtual void ProcessAluScalarAdd(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarAddPrev(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMul(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMulPrev(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMulPrev2(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMax(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMin(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSetEQ(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSetGT(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSetGE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSetNE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarFrac(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarTrunc(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarFloor(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarExp(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarLogClamp(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarLog(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarRecipClamp(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarRecipFixedFunc(const ucode::AluInstruction& op) {
  }
  virtual void ProcessAluScalarRecip(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarRSqrtClamp(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarRSqrtFixedFunc(const ucode::AluInstruction& op) {
  }
  virtual void ProcessAluScalarRSqrt(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMovA(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMovAFloor(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSub(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSubPrev(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetEQ(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetNE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetGT(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetGE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetInv(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetPop(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetClear(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarPredSetRestore(const ucode::AluInstruction& op) {
  }
  virtual void ProcessAluScalarKillEQ(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarKillGT(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarKillGE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarKillNE(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarKillOne(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSqrt(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMulConst0(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarMulConst1(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarAddConst0(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarAddConst1(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSubConst0(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSubConst1(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarSin(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarCos(const ucode::AluInstruction& op) {}
  virtual void ProcessAluScalarRetainPrev(const ucode::AluInstruction& op) {}

 private:
  struct AluOpcodeInfo {
    const char* name;
    size_t argument_count;
    int src_swizzle_component_count;
    void (ShaderTranslator::*fn)(const ucode::AluInstruction& op);
  };

  void MarkUcodeInstruction(uint32_t dword_offset);
  void AppendUcodeDisasm(char c);
  void AppendUcodeDisasm(const char* value);
  void AppendUcodeDisasmFormat(const char* format, ...);

  bool TranslateBlocks();
  void GatherBindingInformation(const ucode::ControlFlowInstruction& cf);
  void GatherVertexBindingInformation(const ucode::VertexFetchInstruction& op);
  void GatherTextureBindingInformation(
      const ucode::TextureFetchInstruction& op);
  void TranslateControlFlowInstruction(const ucode::ControlFlowInstruction& cf);
  void TranslateControlFlowNop(const ucode::ControlFlowInstruction& cf);
  void TranslateControlFlowExec(const ucode::ControlFlowExecInstruction& cf);
  void TranslateControlFlowCondExec(
      const ucode::ControlFlowCondExecInstruction& cf);
  void TranslateControlFlowCondExecPred(
      const ucode::ControlFlowCondExecPredInstruction& cf);
  void TranslateControlFlowLoopStart(
      const ucode::ControlFlowLoopStartInstruction& cf);
  void TranslateControlFlowLoopEnd(
      const ucode::ControlFlowLoopEndInstruction& cf);
  void TranslateControlFlowCondCall(
      const ucode::ControlFlowCondCallInstruction& cf);
  void TranslateControlFlowReturn(
      const ucode::ControlFlowReturnInstruction& cf);
  void TranslateControlFlowCondJmp(
      const ucode::ControlFlowCondJmpInstruction& cf);
  void TranslateControlFlowAlloc(const ucode::ControlFlowAllocInstruction& cf);

  void TranslateExecInstructions(uint32_t address, uint32_t count,
                                 uint32_t sequence);

  void DisasmFetchDestReg(uint32_t dest, uint32_t swizzle, bool is_relative);
  void DisasmFetchSourceReg(uint32_t src, uint32_t swizzle, bool is_relative,
                            int component_count);
  void TranslateVertexFetchInstruction(const ucode::VertexFetchInstruction& op);
  void TranslateTextureFetchInstruction(
      const ucode::TextureFetchInstruction& op);
  void DisasmVertexFetchAttributes(const ucode::VertexFetchInstruction& op);
  void DisasmTextureFetchAttributes(const ucode::TextureFetchInstruction& op);

  void TranslateAluInstruction(const ucode::AluInstruction& op);
  void DisasmAluVectorInstruction(const ucode::AluInstruction& op,
                                  const AluOpcodeInfo& opcode_info);
  void DisasmAluScalarInstruction(const ucode::AluInstruction& op,
                                  const AluOpcodeInfo& opcode_info);
  void DisasmAluSourceReg(const ucode::AluInstruction& op, int i,
                          int swizzle_component_count);
  void DisasmAluSourceRegScalarSpecial(const ucode::AluInstruction& op,
                                       uint32_t reg, bool is_temp, bool negate,
                                       int const_slot, uint32_t swizzle);

  // Input shader metadata and microcode.
  ShaderType shader_type_;
  const uint32_t* ucode_dwords_;
  size_t ucode_dword_count_;

  // Accumulated translation errors.
  std::vector<TranslatedShader::Error> errors_;

  // Microcode disassembly buffer, accumulated throughout the translation.
  StringBuffer ucode_disasm_buffer_;
  // Current line number in the disasm, which can be used for source annotation.
  size_t ucode_disasm_line_number_ = 0;
  // Last offset used when scanning for line numbers.
  size_t previous_ucode_disasm_scan_offset_ = 0;

  // Kept for supporting vfetch_mini.
  ucode::VertexFetchInstruction previous_vfetch_full_;

  // Detected binding information gathered before translation.
  std::vector<TranslatedShader::VertexBinding> vertex_bindings_;
  std::vector<TranslatedShader::TextureBinding> texture_bindings_;

  static const AluOpcodeInfo alu_vector_opcode_infos_[0x20];
  static const AluOpcodeInfo alu_scalar_opcode_infos_[0x40];
};

class UcodeShaderTranslator : public ShaderTranslator {
 public:
  UcodeShaderTranslator() = default;

 protected:
  std::vector<uint8_t> CompleteTranslation() override;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_TRANSLATOR_H_
