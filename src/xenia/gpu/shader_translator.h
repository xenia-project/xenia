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
#include <set>
#include <string>
#include <vector>

#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/ucode.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

class ShaderTranslator {
 public:
  virtual ~ShaderTranslator();

  virtual uint32_t GetDefaultModification(
      xenos::ShaderType shader_type,
      Shader::HostVertexShaderType host_vertex_shader_type =
          Shader::HostVertexShaderType::kVertex) const {
    return 0;
  }

  bool Translate(Shader::Translation& translation, reg::SQ_PROGRAM_CNTL cntl);
  bool Translate(Shader::Translation& translation);

 protected:
  ShaderTranslator();

  // Resets translator state before beginning translation.
  // shader_type is passed here so translator implementations can generate
  // special fixed shaders for internal use, and set up the type for this
  // purpose.
  virtual void Reset(xenos::ShaderType shader_type);

  // Current host-side modification being generated.
  uint32_t modification() const { return modification_; }

  // Register count.
  uint32_t register_count() const { return register_count_; }
  // True if the current shader is a vertex shader.
  bool is_vertex_shader() const {
    return shader_type_ == xenos::ShaderType::kVertex;
  }
  // True if the current shader is a pixel shader.
  bool is_pixel_shader() const {
    return shader_type_ == xenos::ShaderType::kPixel;
  }
  // Labels that jumps (explicit or from loops) can be done to, gathered before
  // translation.
  const std::set<uint32_t>& label_addresses() const { return label_addresses_; }
  // Used constant register info, populated before translation.
  const Shader::ConstantRegisterMap& constant_register_map() const {
    return constant_register_map_;
  }
  // True if the current shader addresses general-purpose registers with dynamic
  // indices, set before translation. Doesn't include writes to r[#+a#] with an
  // empty used write mask.
  bool uses_register_dynamic_addressing() const {
    return uses_register_dynamic_addressing_;
  }
  // True if the current shader writes to a color target on any execution path,
  // set before translation. Doesn't include writes with an empty used write
  // mask.
  bool writes_color_target(int i) const { return writes_color_targets_[i]; }
  bool writes_any_color_target() const {
    for (size_t i = 0; i < xe::countof(writes_color_targets_); ++i) {
      if (writes_color_targets_[i]) {
        return true;
      }
    }
    return false;
  }
  // True if the current shader overrides the pixel depth, set before
  // translation. Doesn't include writes with an empty used write mask.
  bool writes_depth() const { return writes_depth_; }
  // True if the current shader has any `kill` instructions.
  bool kills_pixels() const { return kills_pixels_; }
  // A list of all vertex bindings, populated before translation occurs.
  const std::vector<Shader::VertexBinding>& vertex_bindings() const {
    return vertex_bindings_;
  }
  // A list of all texture bindings, populated before translation occurs.
  const std::vector<Shader::TextureBinding>& texture_bindings() const {
    return texture_bindings_;
  }

  // Based on the number of AS_VS/PS_EXPORT_STREAM_* enum sets found in a game
  // .pdb.
  static constexpr uint32_t kMaxMemExports = 16;
  // Bits indicating which eM# registers have been written to after each
  // `alloc export`, for up to kMaxMemExports exports. This will contain zero
  // for certain corrupt exports - that don't write to eA before writing to eM#,
  // or if the write was done any way other than MAD with a stream constant.
  const uint8_t* memexport_eM_written() const { return memexport_eM_written_; }
  // All c# registers used as the addend in MAD operations to eA, populated
  // before translation occurs.
  const std::set<uint32_t>& memexport_stream_constants() const {
    return memexport_stream_constants_;
  }

  // Whether the shader can have early depth and stencil writing enabled, unless
  // alpha test or alpha to coverage is enabled. Data gathered before
  // translation.
  bool CanWriteZEarly() const {
    // TODO(Triang3l): Investigate what happens to memexport when the pixel
    // fails the depth/stencil test, but in Direct3D 11 UAV writes disable early
    // depth/stencil.
    return !writes_depth_ && !kills_pixels_ &&
           memexport_stream_constants_.empty();
  }

  // Current line number in the ucode disassembly.
  size_t ucode_disasm_line_number() const { return ucode_disasm_line_number_; }
  // Ucode disassembly buffer accumulated during translation.
  StringBuffer& ucode_disasm_buffer() { return ucode_disasm_buffer_; }
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
  // setup_shader_post_translation_info if non-modification-specific parameters
  // of the Shader object behind the Translation can be set by this invocation.
  virtual void PostTranslation(Shader::Translation& translation,
                               bool setup_shader_post_translation_info) {}
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
  struct AluOpcodeInfo {
    const char* name;
    uint32_t argument_count;
    uint32_t src_swizzle_component_count;
  };

  bool TranslateInternal(Shader::Translation& translation);

  void MarkUcodeInstruction(uint32_t dword_offset);
  void AppendUcodeDisasm(char c);
  void AppendUcodeDisasm(const char* value);
  void AppendUcodeDisasmFormat(const char* format, ...);

  void GatherInstructionInformation(const ucode::ControlFlowInstruction& cf);
  void GatherVertexFetchInformation(const ucode::VertexFetchInstruction& op);
  void GatherTextureFetchInformation(const ucode::TextureFetchInstruction& op);
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

  void TranslateExecInstructions(const ParsedExecInstruction& instr);

  void TranslateVertexFetchInstruction(const ucode::VertexFetchInstruction& op);
  void ParseVertexFetchInstruction(const ucode::VertexFetchInstruction& op,
                                   ParsedVertexFetchInstruction* out_instr);

  void TranslateTextureFetchInstruction(
      const ucode::TextureFetchInstruction& op);
  void ParseTextureFetchInstruction(const ucode::TextureFetchInstruction& op,
                                    ParsedTextureFetchInstruction* out_instr);

  void TranslateAluInstruction(const ucode::AluInstruction& op);
  void ParseAluInstruction(const ucode::AluInstruction& op,
                           ParsedAluInstruction& out_instr) const;
  static void ParseAluInstructionOperand(const ucode::AluInstruction& op,
                                         uint32_t i,
                                         uint32_t swizzle_component_count,
                                         InstructionOperand& out_op);
  static void ParseAluInstructionOperandSpecial(
      const ucode::AluInstruction& op, InstructionStorageSource storage_source,
      uint32_t reg, bool negate, int const_slot, uint32_t component_index,
      InstructionOperand& out_op);

  // Input shader metadata and microcode.
  xenos::ShaderType shader_type_;
  const uint32_t* ucode_dwords_;
  size_t ucode_dword_count_;
  uint32_t register_count_;

  // Current host-side modification being generated.
  uint32_t modification_ = 0;

  // Accumulated translation errors.
  std::vector<Shader::Error> errors_;

  // Current control flow dword index.
  uint32_t cf_index_ = 0;

  // Microcode disassembly buffer, accumulated throughout the translation.
  StringBuffer ucode_disasm_buffer_;
  // Current line number in the disasm, which can be used for source annotation.
  size_t ucode_disasm_line_number_ = 0;
  // Last offset used when scanning for line numbers.
  size_t previous_ucode_disasm_scan_offset_ = 0;

  // Kept for supporting vfetch_mini.
  ucode::VertexFetchInstruction previous_vfetch_full_;

  // Labels that jumps (explicit or from loops) can be done to, gathered before
  // translation.
  std::set<uint32_t> label_addresses_;

  // Detected binding information gathered before translation. Must not be
  // affected by the modification index.
  int total_attrib_count_ = 0;
  std::vector<Shader::VertexBinding> vertex_bindings_;
  std::vector<Shader::TextureBinding> texture_bindings_;
  uint32_t unique_vertex_bindings_ = 0;
  uint32_t unique_texture_bindings_ = 0;

  // These all are gathered before translation.
  // uses_register_dynamic_addressing_ for writes, writes_color_targets_,
  // writes_depth_ don't include empty used write masks.
  // Must not be affected by the modification index.
  Shader::ConstantRegisterMap constant_register_map_ = {0};
  bool uses_register_dynamic_addressing_ = false;
  bool writes_color_targets_[4] = {false, false, false, false};
  bool writes_depth_ = false;
  bool kills_pixels_ = false;

  // Memexport info is gathered before translation.
  // Must not be affected by the modification index.
  uint32_t memexport_alloc_count_ = 0;
  // For register allocation in implementations - what was used after each
  // `alloc export`.
  uint32_t memexport_eA_written_ = 0;
  uint8_t memexport_eM_written_[kMaxMemExports] = {0};
  std::set<uint32_t> memexport_stream_constants_;

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
