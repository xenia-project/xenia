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

enum class InstructionStorageTarget {
  // Result is not stored.
  kNone,
  // Result is stored to a temporary register indexed by storage_index [0-31].
  kRegister,
  // Result is stored into a vertex shader interpolant export [0-15].
  kInterpolant,
  // Result is stored to the position export (gl_Position).
  kPosition,
  // Result is stored to the point size export (gl_PointSize).
  kPointSize,
  // Result is stored to a color target export indexed by storage_index [0-3].
  kColorTarget,
  // Result is stored to the depth export (gl_FragDepth).
  kDepth,
};

enum class InstructionStorageAddressingMode {
  // The storage index is not dynamically addressed.
  kStatic,
  // The storage index is addressed by a0.
  kAddressAbsolute,
  // The storage index is addressed by aL.
  kAddressRelative,
};

// Describes the source value of a particular component.
enum class SwizzleSource {
  // Component receives the source X.
  kX,
  // Component receives the source Y.
  kY,
  // Component receives the source Z.
  kZ,
  // Component receives the source W.
  kW,
  // Component receives constant 0.
  k0,
  // Component receives constant 1.
  k1,
};

constexpr SwizzleSource GetSwizzleFromComponentIndex(int i) {
  return static_cast<SwizzleSource>(i);
}
inline char GetCharForSwizzle(SwizzleSource swizzle_source) {
  const static char kChars[] = {'x', 'y', 'z', 'w', '0', '1'};
  return kChars[static_cast<int>(swizzle_source)];
}

struct InstructionResult {
  // Where the result is going.
  InstructionStorageTarget storage_target = InstructionStorageTarget::kNone;
  // Index into the storage_target, if it is indexed.
  int storage_index = 0;
  // How the storage index is dynamically addressed, if it is.
  InstructionStorageAddressingMode storage_addressing_mode =
      InstructionStorageAddressingMode::kStatic;
  // True if the result is exporting from the shader.
  bool is_export = false;
  // True to clamp the result value to [0-1].
  bool is_clamped = false;
  // Defines whether each output component is written.
  bool write_mask[4] = {false, false, false, false};
  // Defines the source for each output component xyzw.
  SwizzleSource components[4] = {SwizzleSource::kX, SwizzleSource::kY,
                                 SwizzleSource::kZ, SwizzleSource::kW};
  // Returns true if any component is written to.
  bool has_any_writes() const {
    return write_mask[0] || write_mask[1] || write_mask[2] || write_mask[3];
  }
  // Returns true if all components are written to.
  bool has_all_writes() const {
    return write_mask[0] && write_mask[1] && write_mask[2] && write_mask[3];
  }
  // True if the components are in their 'standard' swizzle arrangement (xyzw).
  bool is_standard_swizzle() const {
    return has_all_writes() && components[0] == SwizzleSource::kX &&
           components[1] == SwizzleSource::kY &&
           components[2] == SwizzleSource::kZ &&
           components[3] == SwizzleSource::kW;
  }
};

enum class InstructionStorageSource {
  // Source is stored in a temporary register indexed by storage_index [0-31].
  kRegister,
  // Source is stored in a float constant indexed by storage_index [0-511].
  kConstantFloat,
  // Source is stored in a float constant indexed by storage_index [0-31].
  kConstantInt,
  // Source is stored in a float constant indexed by storage_index [0-255].
  kConstantBool,
  // Source is stored in a vertex fetch constant indexed by storage_index
  // [0-95].
  kVertexFetchConstant,
  // Source is stored in a texture fetch constant indexed by storage_index
  // [0-31].
  kTextureFetchConstant,
};

struct InstructionOperand {
  // Where the source comes from.
  InstructionStorageSource storage_source = InstructionStorageSource::kRegister;
  // Index into the storage_target, if it is indexed.
  int storage_index = 0;
  // How the storage index is dynamically addressed, if it is.
  InstructionStorageAddressingMode storage_addressing_mode =
      InstructionStorageAddressingMode::kStatic;
  // True to negate the operand value.
  bool is_negated = false;
  // True to take the absolute value of the source (before any negation).
  bool is_absolute_value = false;
  // Number of components taken from the source operand.
  int component_count = 0;
  // Defines the source for each component xyzw (up to the given
  // component_count).
  SwizzleSource components[4] = {SwizzleSource::kX, SwizzleSource::kY,
                                 SwizzleSource::kZ, SwizzleSource::kW};
  // True if the components are in their 'standard' swizzle arrangement (xyzw).
  bool is_standard_swizzle() const {
    switch (component_count) {
      case 4:
        return components[0] == SwizzleSource::kX &&
               components[1] == SwizzleSource::kY &&
               components[2] == SwizzleSource::kZ &&
               components[3] == SwizzleSource::kW;
    }
    return false;
  }
};

struct ParsedExecInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Opcode for the instruction.
  ucode::ControlFlowOpcode opcode;
  // Friendly name of the instruction.
  const char* opcode_name = nullptr;

  // Instruction address where ALU/fetch instructions reside.
  uint32_t instruction_address = 0;
  // Number of instructions to execute.
  uint32_t instruction_count = 0;

  enum class Type {
    // Block is always executed.
    kUnconditional,
    // Execution is conditional on the value of the boolean constant.
    kConditional,
    // Execution is predicated.
    kPredicated,
  };
  // Condition required to execute the instructions.
  Type type = Type::kUnconditional;
  // Constant index used as the conditional if kConditional.
  uint32_t bool_constant_index = 0;
  // Required condition value of the comparision (true or false).
  bool condition = false;

  // Whether to reset the current predicate.
  bool clean = true;
  // ?
  bool is_yield = false;

  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence = 0;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedLoopStartInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Integer constant register that holds the loop parameters.
  // Byte-wise: [loop count, start, step [-128, 127], ?]
  uint32_t loop_constant_index = 0;
  // Whether to reuse the current aL instead of reset it to loop start.
  bool is_repeat = false;

  // Target address to jump to when skipping the loop.
  uint32_t loop_skip_address = 0;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedLoopEndInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Break from the loop if the predicate matches the expected value.
  bool is_predicated_break = false;
  // Required condition value of the comparision (true or false).
  bool predicate_condition = false;

  // Integer constant register that holds the loop parameters.
  // Byte-wise: [loop count, start, step [-128, 127], ?]
  uint32_t loop_constant_index = 0;

  // Target address of the start of the loop body.
  uint32_t loop_body_address = 0;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedCallInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Target address.
  uint32_t target_address = 0;

  enum class Type {
    // Call is always made.
    kUnconditional,
    // Call is conditional on the value of the boolean constant.
    kConditional,
    // Call is predicated.
    kPredicated,
  };
  // Condition required to make the call.
  Type type = Type::kUnconditional;
  // Constant index used as the conditional if kConditional.
  uint32_t bool_constant_index = 0;
  // Required condition value of the comparision (true or false).
  bool condition = false;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedReturnInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedJumpInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Target address.
  uint32_t target_address = 0;

  enum class Type {
    // Jump is always taken.
    kUnconditional,
    // Jump is conditional on the value of the boolean constant.
    kConditional,
    // Jump is predicated.
    kPredicated,
  };
  // Condition required to make the jump.
  Type type = Type::kUnconditional;
  // Constant index used as the conditional if kConditional.
  uint32_t bool_constant_index = 0;
  // Required condition value of the comparision (true or false).
  bool condition = false;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedAllocInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // The type of resource being allocated.
  ucode::AllocType type = ucode::AllocType::kNone;
  // Total count associated with the allocation.
  int count = 0;

  // True if this allocation is in a vertex shader.
  bool is_vertex_shader = false;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedVertexFetchInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Opcode for the instruction.
  ucode::FetchOpcode opcode;
  // Friendly name of the instruction.
  const char* opcode_name = nullptr;

  // True if the fetch is reusing a previous full fetch.
  // The previous fetch source and constant data will be populated.
  bool is_mini_fetch = false;

  // True if the instruction is predicated on the specified
  // predicate_condition.
  bool is_predicated = false;
  // Expected predication condition value if predicated.
  bool predicate_condition = false;

  // Describes how the instruction result is stored.
  InstructionResult result;

  // Number of source operands.
  size_t operand_count = 0;
  // Describes each source operand.
  InstructionOperand operands[2];

  struct Attributes {
    VertexFormat data_format = VertexFormat::kUndefined;
    int offset = 0;
    int stride = 0;
    int exp_adjust = 0;
    bool is_index_rounded = false;
    bool is_signed = false;
    bool is_integer = false;
    int prefetch_count = 0;
  };
  // Attributes describing the fetch operation.
  Attributes attributes;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedTextureFetchInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  // Opcode for the instruction.
  ucode::FetchOpcode opcode;
  // Friendly name of the instruction.
  const char* opcode_name = nullptr;
  // Texture dimension for opcodes that have multiple dimension forms.
  TextureDimension dimension = TextureDimension::k1D;

  // True if the instruction is predicated on the specified
  // predicate_condition.
  bool is_predicated = false;
  // Expected predication condition value if predicated.
  bool predicate_condition = false;

  // True if the instruction has a result.
  bool has_result() const {
    return result.storage_target != InstructionStorageTarget::kNone;
  }
  // Describes how the instruction result is stored.
  InstructionResult result;

  // Number of source operands.
  size_t operand_count = 0;
  // Describes each source operand.
  InstructionOperand operands[2];

  struct Attributes {
    bool fetch_valid_only = true;
    bool unnormalized_coordinates = false;
    TextureFilter mag_filter = TextureFilter::kUseFetchConst;
    TextureFilter min_filter = TextureFilter::kUseFetchConst;
    TextureFilter mip_filter = TextureFilter::kUseFetchConst;
    AnisoFilter aniso_filter = AnisoFilter::kUseFetchConst;
    bool use_computed_lod = true;
    bool use_register_lod = false;
    bool use_register_gradients = false;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;
  };
  // Attributes describing the fetch operation.
  Attributes attributes;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedAluInstruction {
  // Index into the ucode dword source.
  uint32_t dword_index = 0;

  enum class Type {
    kNop,
    kVector,
    kScalar,
  };
  // Type of the instruction.
  Type type = Type::kNop;
  bool is_nop() const { return type == Type::kNop; }
  bool is_vector_type() const { return type == Type::kVector; }
  bool is_scalar_type() const { return type == Type::kScalar; }
  // Opcode for the instruction if it is a vector type.
  ucode::AluVectorOpcode vector_opcode = ucode::AluVectorOpcode::kADDv;
  // Opcode for the instruction if it is a scalar type.
  ucode::AluScalarOpcode scalar_opcode = ucode::AluScalarOpcode::kADDs;
  // Friendly name of the instruction.
  const char* opcode_name = nullptr;

  // True if the instruction is paired with another instruction.
  bool is_paired = false;
  // True if the instruction is predicated on the specified
  // predicate_condition.
  bool is_predicated = false;
  // Expected predication condition value if predicated.
  bool predicate_condition = false;

  // Describes how the instruction result is stored.
  InstructionResult result;

  // Number of source operands.
  size_t operand_count = 0;
  // Describes each source operand.
  InstructionOperand operands[3];

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

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
    ParsedVertexFetchInstruction fetch_instr;
  };

  struct TextureBinding {
    // Index within the texture binding listing.
    size_t binding_index;
    // Fetch constant index [0-31].
    uint32_t fetch_constant;
    // Fetch instruction with all parameters.
    ParsedTextureFetchInstruction fetch_instr;
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
  const std::vector<Error>& errors() const { return errors_; }

  const std::vector<uint8_t>& binary() const { return binary_; }

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

  // True if the current shader is a vertex shader.
  bool is_vertex_shader() const { return shader_type_ == ShaderType::kVertex; }
  // True if the current shader is a pixel shader.
  bool is_pixel_shader() const { return shader_type_ == ShaderType::kPixel; }
  // A list of all vertex bindings, populated before translation occurs.
  const std::vector<TranslatedShader::VertexBinding>& vertex_bindings() const {
    return vertex_bindings_;
  }
  // A list of all texture bindings, populated before translation occurs.
  const std::vector<TranslatedShader::TextureBinding>& texture_bindings()
      const {
    return texture_bindings_;
  }

  // Current line number in the ucode disassembly.
  size_t ucode_disasm_line_number() const { return ucode_disasm_line_number_; }
  // Ucode disassembly buffer accumulated during translation.
  StringBuffer& ucode_disasm_buffer() { return ucode_disasm_buffer_; }
  // Emits a translation error that will be passed back in the result.
  void EmitTranslationError(const char* message);
  // Emits a translation error indicating that the current translation is not
  // implemented or supported.
  void EmitUnimplementedTranslationError();

  // Handles the start of translation.
  // At this point the vertex and texture bindings have been gathered.
  virtual void StartTranslation() {}

  // Handles the end of translation when all ucode has been processed.
  // Returns the translated shader binary.
  virtual std::vector<uint8_t> CompleteTranslation() {
    return std::vector<uint8_t>();
  }

  // Handles translation for control flow label addresses.
  // This is triggered once for each label required (due to control flow
  // operations) before any of the instructions within the target exec.
  virtual void ProcessLabel(uint32_t cf_index) {}

  // Handles translation for control flow nop instructions.
  virtual void ProcessControlFlowNopInstruction() {}
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
    size_t argument_count;
    int src_swizzle_component_count;
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

  void TranslateExecInstructions(const ParsedExecInstruction& instr);

  void TranslateVertexFetchInstruction(const ucode::VertexFetchInstruction& op);
  void ParseVertexFetchInstruction(const ucode::VertexFetchInstruction& op,
                                   ParsedVertexFetchInstruction* out_instr);

  void TranslateTextureFetchInstruction(
      const ucode::TextureFetchInstruction& op);
  void ParseTextureFetchInstruction(const ucode::TextureFetchInstruction& op,
                                    ParsedTextureFetchInstruction* out_instr);

  void TranslateAluInstruction(const ucode::AluInstruction& op);
  void ParseAluVectorInstruction(const ucode::AluInstruction& op,
                                 const AluOpcodeInfo& opcode_info);
  void ParseAluScalarInstruction(const ucode::AluInstruction& op,
                                 const AluOpcodeInfo& opcode_info);

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
