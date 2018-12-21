/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_H_
#define XENIA_GPU_SHADER_H_

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
  // Result is stored as memexport destination address
  // (see xenos::xe_gpu_memexport_stream_t).
  kExportAddress,
  // Result is stored to memexport destination data.
  kExportData,
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
inline char GetCharForComponentIndex(int i) {
  const static char kChars[] = {'x', 'y', 'z', 'w'};
  return kChars[i];
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
  // Returns number of components written
  uint32_t num_writes() const {
    uint32_t total = 0;
    for (int i = 0; i < 4; i++) {
      if (write_mask[i]) {
        total++;
      }
    }

    return total;
  }
  // Returns true if any non-constant components are written.
  bool stores_non_constants() const {
    for (int i = 0; i < 4; ++i) {
      if (write_mask[i] && components[i] != SwizzleSource::k0 &&
          components[i] != SwizzleSource::k1) {
        return true;
      }
    }
    return false;
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

  // Whether absolute values of two operands are identical (useful for emulating
  // Shader Model 3 0*anything=0 multiplication behavior).
  bool EqualsAbsolute(const InstructionOperand& other) const {
    if (storage_source != other.storage_source ||
        storage_index != other.storage_index ||
        storage_addressing_mode != other.storage_addressing_mode ||
        component_count != other.component_count) {
      return false;
    }
    for (int i = 0; i < component_count; ++i) {
      if (components[i] != other.components[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator==(const InstructionOperand& other) const {
    return EqualsAbsolute(other) && is_negated == other.is_negated &&
           is_absolute_value == other.is_absolute_value;
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

  // Whether this exec ends the shader.
  bool is_end = false;
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
    int stride = 0;  // In dwords.
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
    float lod_bias = 0.0f;
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
  ucode::AluVectorOpcode vector_opcode = ucode::AluVectorOpcode::kAdd;
  // Opcode for the instruction if it is a scalar type.
  ucode::AluScalarOpcode scalar_opcode = ucode::AluScalarOpcode::kAdds;
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

  // If this is a valid eA write (MAD with a stream constant), returns the index
  // of the stream float constant, otherwise returns UINT32_MAX.
  uint32_t GetMemExportStreamConstant() const {
    if (result.storage_target == InstructionStorageTarget::kExportAddress &&
        is_vector_type() && vector_opcode == ucode::AluVectorOpcode::kMad &&
        result.has_all_writes() &&
        operands[2].storage_source ==
            InstructionStorageSource::kConstantFloat &&
        operands[2].storage_addressing_mode ==
            InstructionStorageAddressingMode::kStatic &&
        operands[2].is_standard_swizzle()) {
      return operands[2].storage_index;
    }
    return UINT32_MAX;
  }

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

class Shader {
 public:
  struct Error {
    bool is_fatal = false;
    std::string message;
  };

  struct VertexBinding {
    struct Attribute {
      // Attribute index, 0-based in the entire shader.
      int attrib_index;
      // Fetch instruction with all parameters.
      ParsedVertexFetchInstruction fetch_instr;
      // Size of the attribute, in words.
      uint32_t size_words;
    };

    // Index within the vertex binding listing.
    int binding_index;
    // Fetch constant index [0-95].
    uint32_t fetch_constant;
    // Stride of the entire binding, in words.
    uint32_t stride_words;
    // Packed attributes within the binding buffer.
    std::vector<Attribute> attributes;
  };

  struct TextureBinding {
    // Index within the texture binding listing.
    size_t binding_index;
    // Fetch constant index [0-31].
    uint32_t fetch_constant;
    // Fetch instruction with all parameters.
    ParsedTextureFetchInstruction fetch_instr;
  };

  struct ConstantRegisterMap {
    // Bitmap of all kConstantFloat registers read by the shader.
    // Any shader can only read up to 256 of the 512, and the base is dependent
    // on the shader type. Each bit corresponds to a storage index from the type
    // base, so bit 0 in a vertex shader is register 0, and bit 0 in a fragment
    // shader is register 256.
    uint64_t float_bitmap[256 / 64];
    // Bitmap of all kConstantInt registers read by the shader.
    // Each bit corresponds to a storage index [0-31].
    uint32_t int_bitmap;
    // Bitmap of all kConstantBool registers read by the shader.
    // Each bit corresponds to a storage index [0-255].
    uint32_t bool_bitmap[256 / 32];

    // Total number of kConstantFloat registers read by the shader.
    uint32_t float_count;

    // Computed byte count of all registers required when packed.
    uint32_t packed_byte_length;
  };

  Shader(ShaderType shader_type, uint64_t ucode_data_hash,
         const uint32_t* ucode_dwords, size_t ucode_dword_count);
  virtual ~Shader();

  // Whether the shader is identified as a vertex or pixel shader.
  ShaderType type() const { return shader_type_; }

  // Microcode dwords in host endianness.
  const std::vector<uint32_t>& ucode_data() const { return ucode_data_; }
  uint64_t ucode_data_hash() const { return ucode_data_hash_; }
  const uint32_t* ucode_dwords() const { return ucode_data_.data(); }
  size_t ucode_dword_count() const { return ucode_data_.size(); }

  // All vertex bindings used in the shader.
  // Valid for vertex shaders only.
  const std::vector<VertexBinding>& vertex_bindings() const {
    return vertex_bindings_;
  }

  // All texture bindings used in the shader.
  // Valid for both vertex and pixel shaders.
  const std::vector<TextureBinding>& texture_bindings() const {
    return texture_bindings_;
  }

  // Bitmaps of all constant registers accessed by the shader.
  const ConstantRegisterMap& constant_register_map() const {
    return constant_register_map_;
  }

  // All c# registers used as the addend in MAD operations to eA.
  const std::vector<uint32_t>& memexport_stream_constants() const {
    return memexport_stream_constants_;
  }

  // Returns true if the given color target index [0-3].
  bool writes_color_target(int i) const { return writes_color_targets_[i]; }

  // True if the shader was translated and prepared without error.
  bool is_valid() const { return is_valid_; }

  // True if the shader has already been translated.
  bool is_translated() const { return is_translated_; }

  // Errors that occurred during translation.
  const std::vector<Error>& errors() const { return errors_; }

  // Microcode disassembly in D3D format.
  const std::string& ucode_disassembly() const { return ucode_disassembly_; }

  // Translated shader binary (or text).
  const std::vector<uint8_t>& translated_binary() const {
    return translated_binary_;
  }

  // Gets the translated shader binary as a string.
  // This is only valid if it is actually text.
  std::string GetTranslatedBinaryString() const;

  // Disassembly of the translated from the host graphics layer.
  // May be empty if the host does not support disassembly.
  const std::string& host_disassembly() const { return host_disassembly_; }
  // A lot of errors that occurred during preparation of the host shader.
  const std::string& host_error_log() const { return host_error_log_; }
  // Host binary that can be saved and reused across runs.
  // May be empty if the host does not support saving binaries.
  const std::vector<uint8_t>& host_binary() const { return host_binary_; }

  // Dumps the shader to a file in the given path based on ucode hash.
  // Both the ucode binary and disassembled and translated shader will be
  // written.
  // Returns the filename of the shader and the binary.
  std::pair<std::string, std::string> Dump(const std::string& base_path,
                                           const char* path_prefix);

 protected:
  friend class ShaderTranslator;

  ShaderType shader_type_;
  std::vector<uint32_t> ucode_data_;
  uint64_t ucode_data_hash_;

  std::vector<VertexBinding> vertex_bindings_;
  std::vector<TextureBinding> texture_bindings_;
  ConstantRegisterMap constant_register_map_ = {0};
  bool writes_color_targets_[4] = {false, false, false, false};
  std::vector<uint32_t> memexport_stream_constants_;

  bool is_valid_ = false;
  bool is_translated_ = false;
  std::vector<Error> errors_;

  std::string ucode_disassembly_;
  std::vector<uint8_t> translated_binary_;
  std::string host_disassembly_;
  std::string host_error_log_;
  std::vector<uint8_t> host_binary_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_H_
