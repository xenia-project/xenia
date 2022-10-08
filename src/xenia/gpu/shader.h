/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_H_
#define XENIA_GPU_SHADER_H_

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/base/byte_order.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/ucode.h"

namespace xe {
namespace gpu {

// The structures here are used for both translation and disassembly.
//
// Because disassembly uses them too, to make sure "assemble -> disassemble ->
// reassemble" round trip is always successful with the XNA assembler (as it is
// the accuracy benchmark for translation), only generalization - not
// optimization like nop skipping/replacement - must be done while converting
// microcode to these structures (in other words, parsed shader code should be
// enough to accurately reconstruct the microcode for any shader that could be
// written by a human in assembly).
//
// During the "parsed -> host" part of the translation, however, translators are
// free to make any optimizations (as long as they don't affect the result, of
// course) they find appropriate.

enum class InstructionStorageTarget {
  // Result is not stored.
  kNone,
  // Result is stored to a temporary register indexed by storage_index [0-63].
  kRegister,
  // Result is stored into a vertex shader interpolator export [0-15].
  kInterpolator,
  // Result is stored to the position export (gl_Position).
  kPosition,
  // Result is stored to the vertex shader misc export register, see
  // ucode::ExportRegister::kVSPointSizeEdgeFlagKillVertex for description of
  // components.
  kPointSizeEdgeFlagKillVertex,
  // Result is stored as memexport destination address
  // (see xenos::xe_gpu_memexport_stream_t).
  kExportAddress,
  // Result is stored to memexport destination data.
  kExportData,
  // Result is stored to a color target export indexed by storage_index [0-3].
  kColor,
  // X of the result is stored to the depth export (gl_FragDepth).
  kDepth,
};

// Must be used only in translation to skip unused components, but not in
// disassembly (because oPts.x000 will be assembled, but oPts.x00_ has both
// skipped components and zeros, which cannot be encoded, and therefore it will
// not).
constexpr uint32_t GetInstructionStorageTargetUsedComponentCount(
    InstructionStorageTarget target) {
  switch (target) {
    case InstructionStorageTarget::kNone:
      return 0;
    case InstructionStorageTarget::kPointSizeEdgeFlagKillVertex:
      return 3;
    case InstructionStorageTarget::kDepth:
      return 1;
    default:
      return 4;
  }
}

enum class InstructionStorageAddressingMode {
  // The storage index is not dynamically addressed.
  kAbsolute,
  // The storage index is addressed by a0.
  // Float constants only.
  kAddressRegisterRelative,
  // The storage index is addressed by aL.
  // Float constants and temporary registers only.
  kLoopRelative,
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

constexpr SwizzleSource GetSwizzleFromComponentIndex(uint32_t i) {
  return static_cast<SwizzleSource>(i);
}
constexpr SwizzleSource GetSwizzledAluSourceComponent(
    uint32_t swizzle, uint32_t component_index) {
  return GetSwizzleFromComponentIndex(
      ucode::AluInstruction::GetSwizzledComponentIndex(swizzle,
                                                       component_index));
}
inline char GetCharForComponentIndex(uint32_t i) {
  const static char kChars[] = {'x', 'y', 'z', 'w'};
  return kChars[i];
}
inline char GetCharForSwizzle(SwizzleSource swizzle_source) {
  const static char kChars[] = {'x', 'y', 'z', 'w', '0', '1'};
  return kChars[static_cast<uint32_t>(swizzle_source)];
}

struct InstructionResult {
  // Where the result is going.
  InstructionStorageTarget storage_target = InstructionStorageTarget::kNone;
  // Index into the storage_target, if it is indexed.
  uint32_t storage_index = 0;
  // How the storage index is dynamically addressed, if it is.
  InstructionStorageAddressingMode storage_addressing_mode =
      InstructionStorageAddressingMode::kAbsolute;
  // True to clamp the result value to [0-1].
  bool is_clamped = false;
  // Defines whether each output component is written, though this is from the
  // original microcode, not taking into account whether such components
  // actually exist in the target.
  uint32_t original_write_mask = 0b0000;
  // Defines the source for each output component xyzw.
  SwizzleSource components[4] = {SwizzleSource::kX, SwizzleSource::kY,
                                 SwizzleSource::kZ, SwizzleSource::kW};
  // Returns the write mask containing only components actually present in the
  // target.
  uint32_t GetUsedWriteMask() const {
    uint32_t target_component_count =
        GetInstructionStorageTargetUsedComponentCount(storage_target);
    return original_write_mask & ((1 << target_component_count) - 1);
  }
  // True if the components are in their 'standard' swizzle arrangement (xyzw).
  bool IsStandardSwizzle() const {
    return (GetUsedWriteMask() == 0b1111) &&
           components[0] == SwizzleSource::kX &&
           components[1] == SwizzleSource::kY &&
           components[2] == SwizzleSource::kZ &&
           components[3] == SwizzleSource::kW;
  }
  // Returns the components of the result, before swizzling, that won't be
  // discarded or replaced with a constant.
  uint32_t GetUsedResultComponents() const {
    uint32_t used_write_mask = GetUsedWriteMask();
    uint32_t used_components = 0b0000;
    for (uint32_t i = 0; i < 4; ++i) {
      if ((used_write_mask & (1 << i)) && components[i] >= SwizzleSource::kX &&
          components[i] <= SwizzleSource::kW) {
        used_components |=
            1 << (uint32_t(components[i]) - uint32_t(SwizzleSource::kX));
      }
    }
    return used_components;
  }
  // Returns which components of the used write mask are constant, and what
  // values they have.
  uint32_t GetUsedConstantComponents(uint32_t& constant_values_out) const {
    uint32_t constant_components = 0;
    uint32_t constant_values = 0;
    uint32_t used_write_mask = GetUsedWriteMask();
    for (uint32_t i = 0; i < 4; ++i) {
      if (!(used_write_mask & (1 << i))) {
        continue;
      }
      SwizzleSource component = components[i];
      if (component >= SwizzleSource::kX && component <= SwizzleSource::kW) {
        continue;
      }
      constant_components |= 1 << i;
      if (component == SwizzleSource::k1) {
        constant_values |= 1 << i;
      }
    }
    constant_values_out = constant_values;
    return constant_components;
  }
};

enum class InstructionStorageSource {
  // Source is stored in a temporary register indexed by storage_index [0-63].
  kRegister,
  // Source is stored in a float constant indexed by storage_index [0-255].
  kConstantFloat,
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
  uint32_t storage_index = 0;
  // How the storage index is dynamically addressed, if it is.
  InstructionStorageAddressingMode storage_addressing_mode =
      InstructionStorageAddressingMode::kAbsolute;
  // True to negate the operand value.
  bool is_negated = false;
  // True to take the absolute value of the source (before any negation).
  bool is_absolute_value = false;
  // Number of components taken from the source operand.
  uint32_t component_count = 4;
  // Defines the source for each component xyzw (up to the given
  // component_count).
  SwizzleSource components[4] = {SwizzleSource::kX, SwizzleSource::kY,
                                 SwizzleSource::kZ, SwizzleSource::kW};
  // Returns the swizzle source for the component, replicating the rightmost
  // component if there are less than 4 components (similar to what the Xbox 360
  // shader compiler does as a general rule for unspecified components).
  SwizzleSource GetComponent(uint32_t index) const {
    return components[std::min(index, component_count - 1)];
  }
  // True if the components are in their 'standard' swizzle arrangement (xyzw).
  bool IsStandardSwizzle() const {
    switch (component_count) {
      case 4:
        return components[0] == SwizzleSource::kX &&
               components[1] == SwizzleSource::kY &&
               components[2] == SwizzleSource::kZ &&
               components[3] == SwizzleSource::kW;
    }
    return false;
  }

  // Returns which components of two operands will always be bitwise equal
  // (disregarding component_count for simplicity of usage with GetComponent,
  // treating the rightmost component as replicated). This, strictly with all
  // conditions, must be used when emulating Shader Model 3 +-0 * x = +0
  // multiplication behavior with IEEE-compliant multiplication (because
  // -0 * |-0|, or -0 * +0, is -0, while the result must be +0).
  uint32_t GetIdenticalComponents(const InstructionOperand& other) const {
    if (storage_source != other.storage_source ||
        storage_index != other.storage_index ||
        storage_addressing_mode != other.storage_addressing_mode ||
        is_negated != other.is_negated ||
        is_absolute_value != other.is_absolute_value) {
      return 0;
    }
    uint32_t identical_components = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      identical_components |= uint32_t(GetComponent(i) == other.GetComponent(i))
                              << i;
    }
    return identical_components;
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
  // Whether the hardware doesn't have to wait for the predicate to be updated
  // after this exec.
  bool is_predicate_clean = true;
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
  // 0:7 - uint8 loop count, 8:15 - uint8 start aL, 16:23 - int8 aL step.
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
  // 0:7 - uint8 loop count, 8:15 - uint8 start aL, 16:23 - int8 aL step.
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
  // Note that if the result doesn't have any components to write the fetched
  // value to, the address calculation in vfetch_full must still be performed
  // because such a vfetch_full may be used to setup addressing for vfetch_mini
  // (wires in the color pass of 5454082B do vfetch_full to r2.000_, and then a
  // true vfetch_mini).
  InstructionResult result;

  // Number of source operands.
  size_t operand_count = 0;
  // Describes each source operand.
  // Note that for vfetch_mini, which inherits the operands from vfetch_full,
  // the index operand register may been overwritten between the vfetch_full and
  // the vfetch_mini (happens in 4D530910 for wheels), but that should have no
  // effect on the index actually used for fetching. A copy of the index
  // therefore must be stored by vfetch_full (the base address, stride and
  // rounding may be pre-applied to it since they will be the same in the
  // vfetch_full and all its vfetch_mini instructions).
  InstructionOperand operands[2];

  struct Attributes {
    xenos::VertexFormat data_format = xenos::VertexFormat::kUndefined;
    int32_t offset = 0;
    uint32_t stride = 0;  // In dwords.
    int32_t exp_adjust = 0;
    // Prefetch count minus 1.
    uint32_t prefetch_count = 0;
    xenos::SignedRepeatingFractionMode signed_rf_mode =
        xenos::SignedRepeatingFractionMode::kZeroClampMinusOne;
    bool is_index_rounded = false;
    bool is_signed = false;
    bool is_integer = false;
  };
  // Attributes describing the fetch operation.
  Attributes attributes;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedTextureFetchInstruction {
  // Opcode for the instruction.
  ucode::FetchOpcode opcode;
  // Friendly name of the instruction.
  const char* opcode_name = nullptr;
  // Texture dimension for opcodes that have multiple dimension forms.
  xenos::FetchOpDimension dimension = xenos::FetchOpDimension::k1D;

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
    xenos::TextureFilter mag_filter = xenos::TextureFilter::kUseFetchConst;
    xenos::TextureFilter min_filter = xenos::TextureFilter::kUseFetchConst;
    xenos::TextureFilter mip_filter = xenos::TextureFilter::kUseFetchConst;
    xenos::AnisoFilter aniso_filter = xenos::AnisoFilter::kUseFetchConst;
    xenos::TextureFilter vol_mag_filter = xenos::TextureFilter::kUseFetchConst;
    xenos::TextureFilter vol_min_filter = xenos::TextureFilter::kUseFetchConst;
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

  // Considering the operation, dimensions, filter overrides, and the result
  // components, returns which components of the result will have a value that
  // is not always zero.
  uint32_t GetNonZeroResultComponents() const;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

struct ParsedAluInstruction {
  // Opcode for the vector part of the instruction.
  ucode::AluVectorOpcode vector_opcode = ucode::AluVectorOpcode::kAdd;
  // Opcode for the scalar part of the instruction.
  ucode::AluScalarOpcode scalar_opcode = ucode::AluScalarOpcode::kAdds;
  // Friendly name of the vector instruction.
  const char* vector_opcode_name = nullptr;
  // Friendly name of the scalar instruction.
  const char* scalar_opcode_name = nullptr;

  // True if the instruction is predicated on the specified
  // predicate_condition.
  bool is_predicated = false;
  // Expected predication condition value if predicated.
  bool predicate_condition = false;

  // Describes how the vector operation result and, for exports, constant 0/1
  // are stored. For simplicity of translation and disassembly, treating
  // constant 0/1 writes as a part of the vector operation - they need to be
  // expressed somehow in the disassembly anyway with a properly disassembled
  // instruction even if only constants are being exported. The XNA disassembler
  // falls back to displaying the whole vector operation, even if only constant
  // components are written, if the scalar operation is a nop or if the vector
  // operation changes a0, p0 or kills pixels (but if the scalar operation isn't
  // nop, it outputs the entire constant mask in the scalar operation
  // destination). Normally the XNA disassembler outputs the constant mask in
  // both vector and scalar operations, but that's not required by assembler, so
  // it doesn't really matter whether it's specified in the vector operation, in
  // the scalar operation, or in both.
  InstructionResult vector_and_constant_result;
  // Describes how the scalar operation result is stored.
  InstructionResult scalar_result;
  // Both operations must be executed before any result is stored if vector and
  // scalar operations are paired. There are cases of vector result being used
  // as scalar operand or vice versa (the ring on Avalanche in 4D5307E6, for
  // example), in this case there must be no dependency between the two
  // operations.

  // Number of source operands of the vector operation.
  uint32_t vector_operand_count = 0;
  // Describes each source operand of the vector operation.
  InstructionOperand vector_operands[3];
  // Number of source operands of the scalar operation.
  uint32_t scalar_operand_count = 0;
  // Describes each source operand of the scalar operation.
  InstructionOperand scalar_operands[2];

  // Whether the vector part of the instruction is the same as if it was omitted
  // in the assembly (if compiled or assembled with the Xbox 360 shader
  // compiler), and thus reassembling the shader with this instruction omitted
  // will result in the same microcode (since instructions with just an empty
  // write mask may have different values in other fields).
  // This is for disassembly! Translators should use the write masks and
  // the changed state bits in the opcode info to skip operations, as this only
  // covers one very specific nop format!
  bool IsVectorOpDefaultNop() const;
  // Whether the scalar part of the instruction is the same as if it was omitted
  // in the assembly (if compiled or assembled with the Xbox 360 shader
  // compiler), and thus reassembling the shader with this instruction omitted
  // will result in the same microcode (since instructions with just an empty
  // write mask may have different values in other fields).
  bool IsScalarOpDefaultNop() const;

  // For translation (not disassembly) - whether this instruction has totally no
  // effect.
  bool IsNop() const;

  // If this is a "normal" eA write recognized by Xenia (MAD with a stream
  // constant), returns the index of the stream float constant, otherwise
  // returns UINT32_MAX.
  uint32_t GetMemExportStreamConstant() const;

  // Disassembles the instruction into ucode assembly text.
  void Disassemble(StringBuffer* out) const;
};

void ParseControlFlowExec(const ucode::ControlFlowExecInstruction& cf,
                          uint32_t cf_index, ParsedExecInstruction& instr);
void ParseControlFlowCondExec(const ucode::ControlFlowCondExecInstruction& cf,
                              uint32_t cf_index, ParsedExecInstruction& instr);
void ParseControlFlowCondExecPred(
    const ucode::ControlFlowCondExecPredInstruction& cf, uint32_t cf_index,
    ParsedExecInstruction& instr);
void ParseControlFlowLoopStart(const ucode::ControlFlowLoopStartInstruction& cf,
                               uint32_t cf_index,
                               ParsedLoopStartInstruction& instr);
void ParseControlFlowLoopEnd(const ucode::ControlFlowLoopEndInstruction& cf,
                             uint32_t cf_index,
                             ParsedLoopEndInstruction& instr);
void ParseControlFlowCondCall(const ucode::ControlFlowCondCallInstruction& cf,
                              uint32_t cf_index, ParsedCallInstruction& instr);
void ParseControlFlowReturn(const ucode::ControlFlowReturnInstruction& cf,
                            uint32_t cf_index, ParsedReturnInstruction& instr);
void ParseControlFlowCondJmp(const ucode::ControlFlowCondJmpInstruction& cf,
                             uint32_t cf_index, ParsedJumpInstruction& instr);
void ParseControlFlowAlloc(const ucode::ControlFlowAllocInstruction& cf,
                           uint32_t cf_index, bool is_vertex_shader,
                           ParsedAllocInstruction& instr);

// Returns whether the fetch is a full one, and the next parsed mini vertex
// fetch should inherit most of its parameters.
bool ParseVertexFetchInstruction(
    const ucode::VertexFetchInstruction& op,
    const ucode::VertexFetchInstruction& previous_full_op,
    ParsedVertexFetchInstruction& instr);
void ParseTextureFetchInstruction(const ucode::TextureFetchInstruction& op,
                                  ParsedTextureFetchInstruction& instr);
void ParseAluInstruction(const ucode::AluInstruction& op,
                         xenos::ShaderType shader_type,
                         ParsedAluInstruction& instr);

class Shader {
 public:
  // Type of the vertex shader on the host - shader interface depends on in, so
  // it must be known at translation time. If values are changed, INVALIDATE
  // SHADER STORAGES (increase their version constexpr) where those are stored!
  // And check bit count where this is packed. This is : uint32_t for simplicity
  // of packing in bit fields.
  enum class HostVertexShaderType : uint32_t {
    kVertex,

    kDomainStart,
    kLineDomainCPIndexed = kDomainStart,
    kLineDomainPatchIndexed,
    kTriangleDomainCPIndexed,
    kTriangleDomainPatchIndexed,
    kQuadDomainCPIndexed,
    kQuadDomainPatchIndexed,
    kDomainEnd,

    // For implementation without unconditional support for memory writes from
    // vertex shaders, vertex shader converted to a compute shader doing only
    // memory export.
    kMemexportCompute,

    // 4 host vertices for 1 guest vertex, for implementations without
    // unconditional geometry shader support.
    kPointListAsTriangleStrip,
    // 3 guest vertices processed by the host shader invocation to choose the
    // strip orientation, for implementations without unconditional geometry
    // shader support.
    kRectangleListAsTriangleStrip,
  };
  // For packing HostVertexShaderType in bit fields.
  static constexpr uint32_t kHostVertexShaderTypeBitCount = 4;

  static constexpr bool IsHostVertexShaderTypeDomain(
      HostVertexShaderType host_vertex_shader_type) {
    return host_vertex_shader_type >= HostVertexShaderType::kDomainStart &&
           host_vertex_shader_type < HostVertexShaderType::kDomainEnd;
  }

  struct Error {
    bool is_fatal = false;
    std::string message;
  };

  struct VertexBinding {
    struct Attribute {
      // Fetch instruction with all parameters.
      ParsedVertexFetchInstruction fetch_instr;
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
    // on the shader type and SQ_VS/PS_CONST registers. Each bit corresponds to
    // a storage index from the type base.
    uint64_t float_bitmap[256 / 64];
    // Bitmap of all loop constants read by the shader.
    // Each bit corresponds to a storage index [0-31].
    uint32_t loop_bitmap;
    // Bitmap of all bool constants read by the shader.
    // Each bit corresponds to a storage index [0-255].
    uint32_t bool_bitmap[256 / 32];
    // Bitmap of all vertex fetch constants read by the shader.
    // Each bit corresponds to a storage index [0-95].
    uint32_t vertex_fetch_bitmap[96 / 32];

    // Total number of kConstantFloat registers read by the shader.
    uint32_t float_count;

    // Whether kConstantFloat registers are indexed dynamically - in this case,
    // float_bitmap must be set to all 1, and tight packing must not be done.
    bool float_dynamic_addressing;

    // Returns the index of the float4 constant as if all float4 constant
    // registers actually referenced were tightly packed in a buffer, or
    // UINT32_MAX if not found.
    uint32_t GetPackedFloatConstantIndex(uint32_t float_constant) const {
      if (float_constant >= 256) {
        return UINT32_MAX;
      }
      if (float_dynamic_addressing) {
        // Any can potentially be read - not packing.
        return float_constant;
      }
      uint32_t block_index = float_constant / 64;
      uint32_t bit_index = float_constant % 64;
      if (!(float_bitmap[block_index] & (uint64_t(1) << bit_index))) {
        return UINT32_MAX;
      }
      uint32_t offset = 0;
      for (uint32_t i = 0; i < block_index; ++i) {
        offset += xe::bit_count(float_bitmap[i]);
      }
      return offset + xe::bit_count(float_bitmap[block_index] &
                                    ((uint64_t(1) << bit_index) - 1));
    }
  };

  // Based on the number of AS_VS/PS_EXPORT_STREAM_* enum sets found in a game
  // .pdb.
  static constexpr uint32_t kMaxMemExports = 16;

  class Translation {
   public:
    virtual ~Translation() {}

    Shader& shader() const { return shader_; }

    // Translator-specific modification bits.
    uint64_t modification() const { return modification_; }

    // True if the shader was translated and prepared without error.
    bool is_valid() const { return is_valid_; }

    // True if the shader has already been translated.
    bool is_translated() const { return is_translated_; }

    // Errors that occurred during translation.
    const std::vector<Error>& errors() const { return errors_; }

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

    // In case disassembly depends on the GPU backend, for setting it
    // externally.
    void set_host_disassembly(std::string disassembly) {
      host_disassembly_ = std::move(disassembly);
    }

    // For dumping after translation. Dumps the shader's translated code, and,
    // if available, translated disassembly, to files in the given directory
    // based on ucode hash. Returns {binary path, disassembly path if written}.
    std::pair<std::filesystem::path, std::filesystem::path> Dump(
        const std::filesystem::path& base_path, const char* path_prefix) const;

   protected:
    Translation(Shader& shader, uint64_t modification)
        : shader_(shader), modification_(modification) {}

    // If there was some failure during preparation on the implementation side.
    void MakeInvalid() { is_valid_ = false; }

   private:
    friend class Shader;
    friend class ShaderTranslator;

    Shader& shader_;
    uint64_t modification_;

    bool is_valid_ = false;
    bool is_translated_ = false;
    std::vector<Error> errors_;
    std::vector<uint8_t> translated_binary_;
    std::string host_disassembly_;
  };

  // ucode_source_endian specifies the endianness of the ucode_dwords argument -
  // inside the Shader, the ucode will be stored with the native byte order.
  Shader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
         const uint32_t* ucode_dwords, size_t ucode_dword_count,
         std::endian ucode_source_endian = std::endian::big);
  virtual ~Shader();

  // Whether the shader is identified as a vertex or pixel shader.
  xenos::ShaderType type() const { return shader_type_; }

  // Microcode dwords in host endianness.
  const std::vector<uint32_t>& ucode_data() const { return ucode_data_; }
  uint64_t ucode_data_hash() const { return ucode_data_hash_; }
  const uint32_t* ucode_dwords() const { return ucode_data_.data(); }
  size_t ucode_dword_count() const { return ucode_data_.size(); }

  bool is_ucode_analyzed() const { return is_ucode_analyzed_; }
  // ucode_disasm_buffer is temporary storage for disassembly (provided
  // externally so it won't need to be reallocated for every shader).
  void AnalyzeUcode(StringBuffer& ucode_disasm_buffer);

  // The following parameters, until the translation, are valid if ucode
  // information has been gathered.

  // Microcode disassembly in D3D format.
  const std::string& ucode_disassembly() const { return ucode_disassembly_; }

  // All vertex bindings used in the shader.
  const std::vector<VertexBinding>& vertex_bindings() const {
    return vertex_bindings_;
  }

  // All texture bindings used in the shader.
  const std::vector<TextureBinding>& texture_bindings() const {
    return texture_bindings_;
  }

  // Bitmaps of all constant registers accessed by the shader.
  const ConstantRegisterMap& constant_register_map() const {
    return constant_register_map_;
  }

  // uint5[Shader::kMaxMemExports] - bits indicating which eM# registers have
  // been written to after each `alloc export`, for up to Shader::kMaxMemExports
  // exports. This will contain zero for certain corrupt exports - for those to
  // which a valid eA was not written via a MAD with a stream constant.
  const uint8_t* memexport_eM_written() const { return memexport_eM_written_; }

  // All c# registers used as the addend in MAD operations to eA.
  const std::set<uint32_t>& memexport_stream_constants() const {
    return memexport_stream_constants_;
  }
  bool is_valid_memexport_used() const {
    return !memexport_stream_constants_.empty();
  }

  // Labels that jumps (explicit or from loops) can be done to.
  const std::set<uint32_t>& label_addresses() const { return label_addresses_; }

  // Exclusive upper bound of the indexes of paired control flow instructions
  // (each corresponds to 3 dwords).
  uint32_t cf_pair_index_bound() const { return cf_pair_index_bound_; }

  // Upper bound of temporary registers addressed statically by the shader -
  // highest static register address + 1, or 0 if no registers referenced this
  // way. SQ_PROGRAM_CNTL is not always reliable - some draws (like single point
  // draws with oPos = 0001 that are done by Xbox 360's Direct3D 9 sometimes;
  // can be reproduced by launching the intro mission in 4D5307E6 from the
  // campaign lobby) that aren't supposed to cover any pixels use an invalid
  // (zero) SQ_PROGRAM_CNTL, but with an outdated pixel shader loaded, in this
  // case SQ_PROGRAM_CNTL may contain a number smaller than actually needed by
  // the pixel shader - SQ_PROGRAM_CNTL should be used to go above this count if
  // uses_register_dynamic_addressing is true.
  uint32_t register_static_address_bound() const {
    return register_static_address_bound_;
  }

  // Whether the shader addresses temporary registers dynamically, thus
  // SQ_PROGRAM_CNTL should determine the number of registers to use, not only
  // register_static_address_bound.
  bool uses_register_dynamic_addressing() const {
    return uses_register_dynamic_addressing_;
  }

  // For building shader modification bits (and also for normalization of them),
  // returns the amount of temporary registers that need to be allocated
  // explicitly - if not using register dynamic addressing, the shader
  // translator will use register_static_address_bound directly.
  uint32_t GetDynamicAddressableRegisterCount(
      uint32_t program_cntl_num_reg) const {
    if (!uses_register_dynamic_addressing()) {
      return 0;
    }
    return std::max((program_cntl_num_reg & 0x80)
                        ? uint32_t(0)
                        : (program_cntl_num_reg + uint32_t(1)),
                    register_static_address_bound());
  }

  // True if the current shader has any `kill` instructions.
  bool kills_pixels() const { return kills_pixels_; }

  // True if the shader has any texture-related instructions (any fetch
  // instructions other than vertex fetch) writing any non-constant components.
  bool uses_texture_fetch_instruction_results() const {
    return uses_texture_fetch_instruction_results_;
  }

  // Whether each interpolator is written on any execution path.
  uint32_t writes_interpolators() const { return writes_interpolators_; }

  // Whether the system vertex shader exports are written on any execution path.
  uint32_t writes_point_size_edge_flag_kill_vertex() const {
    return writes_point_size_edge_flag_kill_vertex_;
  }

  // Returns the mask of the interpolators the pixel shader potentially requires
  // from the vertex shader, and also the PsParamGen destination register, or
  // UINT32_MAX if it's not needed.
  uint32_t GetInterpolatorInputMask(reg::SQ_PROGRAM_CNTL sq_program_cntl,
                                    reg::SQ_CONTEXT_MISC sq_context_misc,
                                    uint32_t& param_gen_pos_out) const;

  // True if the shader overrides the pixel depth.
  bool writes_depth() const { return writes_depth_; }

  // Whether the shader can have early depth and stencil writing enabled, unless
  // alpha test or alpha to coverage is enabled.
  bool implicit_early_z_write_allowed() const {
    // TODO(Triang3l): Investigate what happens to memexport when the pixel
    // fails the depth/stencil test, but in Direct3D 11 UAV writes disable early
    // depth/stencil.
    return !kills_pixels() && !writes_depth() && !is_valid_memexport_used();
  }

  // Whether each color render target is written to on any execution path.
  uint32_t writes_color_targets() const { return writes_color_targets_; }
  bool writes_color_target(uint32_t i) const {
    return (writes_color_targets() & (uint32_t(1) << i)) != 0;
  }

  // Host translations with the specified modification bits. Not thread-safe
  // with respect to translation creation/destruction.
  const std::unordered_map<uint64_t, Translation*>& translations() const {
    return translations_;
  }
  Translation* GetTranslation(uint64_t modification) const {
    auto it = translations_.find(modification);
    if (it != translations_.cend()) {
      return it->second;
    }
    return nullptr;
  }
  Translation* GetOrCreateTranslation(uint64_t modification,
                                      bool* is_new = nullptr);
  // For shader storage loading, to remove a modification in case of translation
  // failure. Not thread-safe.
  void DestroyTranslation(uint64_t modification);

  // An externally managed identifier of the shader storage the microcode of the
  // shader was last written to, or was loaded from, to only write the shader
  // microcode to the storage once. UINT32_MAX by default.
  uint32_t ucode_storage_index() const { return ucode_storage_index_; }
  void set_ucode_storage_index(uint32_t storage_index) {
    ucode_storage_index_ = storage_index;
  }

  // Dumps the shader's microcode binary and, if analyzed, disassembly, to files
  // in the given directory based on ucode hash. Returns the name of the written
  // file. Can be called at any time, doesn't require the shader to be
  // translated. Returns {binary path, disassembly path if written}.
  std::pair<std::filesystem::path, std::filesystem::path> DumpUcode(
      const std::filesystem::path& base_path) const;

 protected:
  friend class ShaderTranslator;

  virtual Translation* CreateTranslationInstance(uint64_t modification);

  xenos::ShaderType shader_type_;
  std::vector<uint32_t> ucode_data_;
  uint64_t ucode_data_hash_;

  // Whether info needed before translating has been gathered already - may be
  // needed to determine which modifications are actually needed and make sense
  // (for instance, there may be draws not covering anything and not allocating
  // any pixel shader registers in SQ_PROGRAM_CNTL, but still using the pixel
  // shader from the previous draw - in this case, every shader that happens to
  // be before such draw will need to be translated again with a different
  // dynamically addressed register count, which may cause compilation of
  // different random pipelines across many random frames, thus causing
  // stuttering - normally host pipeline states are deterministically only
  // compiled when a new material appears in the game, and having the order of
  // draws also matter in such unpredictable way would break this rule; limit
  // the effect to shaders with dynamic register addressing only, which are
  // extremely rare; however care should be taken regarding depth format-related
  // translation modifications in this case), also some info needed for drawing
  // is collected during the ucode analysis.
  bool is_ucode_analyzed_ = false;

  std::string ucode_disassembly_;
  std::vector<VertexBinding> vertex_bindings_;
  std::vector<TextureBinding> texture_bindings_;
  ConstantRegisterMap constant_register_map_ = {0};
  uint8_t memexport_eM_written_[kMaxMemExports] = {};
  std::set<uint32_t> memexport_stream_constants_;
  std::set<uint32_t> label_addresses_;
  uint32_t cf_pair_index_bound_ = 0;
  uint32_t register_static_address_bound_ = 0;
  uint32_t writes_interpolators_ = 0;
  uint32_t writes_point_size_edge_flag_kill_vertex_ = 0;
  uint32_t writes_color_targets_ = 0b0000;
  bool uses_register_dynamic_addressing_ = false;
  bool kills_pixels_ = false;
  bool uses_texture_fetch_instruction_results_ = false;
  bool writes_depth_ = false;

  // Modification bits -> translation.
  std::unordered_map<uint64_t, Translation*> translations_;

  uint32_t ucode_storage_index_ = UINT32_MAX;

 private:
  void GatherExecInformation(
      const ParsedExecInstruction& instr,
      ucode::VertexFetchInstruction& previous_vfetch_full,
      uint32_t& unique_texture_bindings, uint32_t memexport_alloc_current_count,
      uint32_t& memexport_eA_written, StringBuffer& ucode_disasm_buffer);
  void GatherVertexFetchInformation(
      const ucode::VertexFetchInstruction& op,
      ucode::VertexFetchInstruction& previous_vfetch_full,
      StringBuffer& ucode_disasm_buffer);
  void GatherTextureFetchInformation(const ucode::TextureFetchInstruction& op,
                                     uint32_t& unique_texture_bindings,
                                     StringBuffer& ucode_disasm_buffer);
  void GatherAluInstructionInformation(const ucode::AluInstruction& op,
                                       uint32_t memexport_alloc_current_count,
                                       uint32_t& memexport_eA_written,
                                       StringBuffer& ucode_disasm_buffer);
  void GatherOperandInformation(const InstructionOperand& operand);
  void GatherFetchResultInformation(const InstructionResult& result);
  void GatherAluResultInformation(const InstructionResult& result,
                                  uint32_t memexport_alloc_current_count);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_H_
