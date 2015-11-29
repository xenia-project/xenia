/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader_translator.h"

#include <cstdarg>
#include <set>
#include <string>

#include "xenia/base/math.h"

namespace xe {
namespace gpu {

using namespace ucode;

// The Xbox 360 GPU is effectively an Adreno A200:
// https://github.com/freedreno/freedreno/wiki/A2XX-Shader-Instruction-Set-Architecture
//
// A lot of this information is derived from the freedreno drivers, AMD's
// documentation, publicly available Xbox presentations (from GDC/etc), and
// other reverse engineering.
//
// Naming has been matched as closely as possible to the real thing by using the
// publicly available XNA Game Studio shader assembler.
// You can find a tool for exploring this under tools/shader-playground/,
// allowing interative assembling/disassembling of shader code.
//
// Though the 360's GPU is similar to the Adreno r200, the microcode format is
// slightly different. Though this is a great guide it cannot be assumed it
// matches the 360 in all areas:
// https://github.com/freedreno/freedreno/blob/master/util/disasm-a2xx.c
//
// Lots of naming comes from the disassembly spit out by the XNA GS compiler
// and dumps of d3dcompiler and games: http://pastebin.com/i4kAv7bB

TranslatedShader::TranslatedShader(ShaderType shader_type,
                                   uint64_t ucode_data_hash,
                                   const uint32_t* ucode_dwords,
                                   size_t ucode_dword_count,
                                   std::vector<Error> errors)
    : shader_type_(shader_type),
      ucode_data_hash_(ucode_data_hash_),
      errors_(std::move(errors)) {
  ucode_data_.resize(ucode_dword_count);
  std::memcpy(ucode_data_.data(), ucode_dwords,
              ucode_dword_count * sizeof(uint32_t));

  is_valid_ = true;
  for (const auto& error : errors_) {
    if (error.is_fatal) {
      is_valid_ = false;
      break;
    }
  }
}

TranslatedShader::~TranslatedShader() = default;

ShaderTranslator::ShaderTranslator() = default;

ShaderTranslator::~ShaderTranslator() = default;

std::unique_ptr<TranslatedShader> ShaderTranslator::Translate(
    ShaderType shader_type, uint64_t ucode_data_hash,
    const uint32_t* ucode_dwords, size_t ucode_dword_count) {
  shader_type_ = shader_type;
  ucode_dwords_ = ucode_dwords;
  ucode_dword_count_ = ucode_dword_count;

  TranslateBlocks();

  std::unique_ptr<TranslatedShader> translated_shader(
      new TranslatedShader(shader_type, ucode_data_hash, ucode_dwords,
                           ucode_dword_count, std::move(errors_)));
  translated_shader->binary_ = CompleteTranslation();
  translated_shader->vertex_bindings_ = std::move(vertex_bindings_);
  translated_shader->texture_bindings_ = std::move(texture_bindings_);
  return translated_shader;
}

void ShaderTranslator::MarkUcodeInstruction(uint32_t dword_offset) {
  auto disasm = ucode_disasm_buffer_.GetString();
  size_t current_offset = ucode_disasm_buffer_.length();
  for (size_t i = previous_ucode_disasm_scan_offset_; i < current_offset; ++i) {
    if (disasm[i] == '\n') {
      ++ucode_disasm_line_number_;
    }
  }
  previous_ucode_disasm_scan_offset_ = current_offset;
}

void ShaderTranslator::AppendUcodeDisasm(char c) {
  ucode_disasm_buffer_.Append(c);
}

void ShaderTranslator::AppendUcodeDisasm(const char* value) {
  ucode_disasm_buffer_.Append(value);
}

void ShaderTranslator::AppendUcodeDisasmFormat(const char* format, ...) {
  va_list va;
  va_start(va, format);
  ucode_disasm_buffer_.AppendVarargs(format, va);
  va_end(va);
}

void ShaderTranslator::EmitTranslationError(const char* message) {
  TranslatedShader::Error error;
  error.is_fatal = true;
  error.message = message;
  // TODO(benvanik): location information.
  errors_.push_back(std::move(error));
}

void AddControlFlowTargetLabel(const ControlFlowInstruction& cf,
                               std::set<uint32_t>* label_addresses) {
  switch (cf.opcode()) {
    case ControlFlowOpcode::kLoopStart:
      label_addresses->insert(cf.loop_start.address());
      break;
    case ControlFlowOpcode::kLoopEnd:
      label_addresses->insert(cf.loop_end.address());
      break;
    case ControlFlowOpcode::kCondCall:
      label_addresses->insert(cf.cond_call.address());
      break;
    case ControlFlowOpcode::kCondJmp:
      label_addresses->insert(cf.cond_jmp.address());
      break;
  }
}

bool ShaderTranslator::TranslateBlocks() {
  // Control flow instructions come paired in blocks of 3 dwords and all are
  // listed at the top of the ucode.
  // Each control flow instruction is executed sequentially until the final
  // ending instruction.

  // Guess how long the control flow program is by scanning for the first
  // kExec-ish and instruction and using its address as the upper bound.
  // This is what freedreno does.
  uint32_t max_cf_dword_index = static_cast<uint32_t>(ucode_dword_count_);
  std::set<uint32_t> label_addresses;
  for (uint32_t i = 0; i < max_cf_dword_index; i += 3) {
    ControlFlowInstruction cf_a;
    ControlFlowInstruction cf_b;
    UnpackControlFlowInstructions(ucode_dwords_ + i, &cf_a, &cf_b);
    if (IsControlFlowOpcodeExec(cf_a.opcode())) {
      max_cf_dword_index =
          std::min(max_cf_dword_index, cf_a.exec.address() * 3);
    }
    if (IsControlFlowOpcodeExec(cf_b.opcode())) {
      max_cf_dword_index =
          std::min(max_cf_dword_index, cf_b.exec.address() * 3);
    }
    AddControlFlowTargetLabel(cf_a, &label_addresses);
    AddControlFlowTargetLabel(cf_b, &label_addresses);
  }

  // Run through and gather all binding information.
  // Translators may need this before they start codegen.
  for (uint32_t i = 0, cf_index = 0; i < max_cf_dword_index; i += 3) {
    ControlFlowInstruction cf_a;
    ControlFlowInstruction cf_b;
    UnpackControlFlowInstructions(ucode_dwords_ + i, &cf_a, &cf_b);
    GatherBindingInformation(cf_a);
    GatherBindingInformation(cf_b);
  }

  // Translate all instructions.
  for (uint32_t i = 0, cf_index = 0; i < max_cf_dword_index; i += 3) {
    ControlFlowInstruction cf_a;
    ControlFlowInstruction cf_b;
    UnpackControlFlowInstructions(ucode_dwords_ + i, &cf_a, &cf_b);

    MarkUcodeInstruction(i);
    if (label_addresses.count(cf_index)) {
      AppendUcodeDisasmFormat("                label L%u\n", cf_index);
    }
    AppendUcodeDisasmFormat("/* %4u.0 */ ", cf_index / 2);
    TranslateControlFlowInstruction(cf_a);
    ++cf_index;

    MarkUcodeInstruction(i);
    if (label_addresses.count(cf_index)) {
      AppendUcodeDisasmFormat("                label L%u\n", cf_index);
    }
    AppendUcodeDisasmFormat("/* %4u.1 */ ", cf_index / 2);
    TranslateControlFlowInstruction(cf_b);
    ++cf_index;
  }

  return true;
}

void ShaderTranslator::GatherBindingInformation(
    const ControlFlowInstruction& cf) {
  switch (cf.opcode()) {
    case ControlFlowOpcode::kExec:
    case ControlFlowOpcode::kExecEnd:
    case ControlFlowOpcode::kCondExec:
    case ControlFlowOpcode::kCondExecEnd:
    case ControlFlowOpcode::kCondExecPred:
    case ControlFlowOpcode::kCondExecPredEnd:
    case ControlFlowOpcode::kCondExecPredClean:
    case ControlFlowOpcode::kCondExecPredCleanEnd:
      uint32_t sequence = cf.exec.sequence();
      for (uint32_t instr_offset = cf.exec.address();
           instr_offset < cf.exec.address() + cf.exec.count();
           ++instr_offset, sequence >>= 2) {
        bool is_fetch = (sequence & 0x1) == 0x1;
        if (is_fetch) {
          auto fetch_opcode =
              static_cast<FetchOpcode>(ucode_dwords_[instr_offset * 3] & 0x1F);
          if (fetch_opcode == FetchOpcode::kVertexFetch) {
            GatherVertexBindingInformation(
                *reinterpret_cast<const VertexFetchInstruction*>(
                    ucode_dwords_ + instr_offset * 3));
          } else {
            GatherTextureBindingInformation(
                *reinterpret_cast<const TextureFetchInstruction*>(
                    ucode_dwords_ + instr_offset * 3));
          }
        }
      }
      break;
  }
}

void ShaderTranslator::GatherVertexBindingInformation(
    const VertexFetchInstruction& op) {
  TranslatedShader::VertexBinding binding;
  binding.binding_index = vertex_bindings_.size();
  binding.op = op;
  if (op.is_mini_fetch()) {
    // Reuses an existing fetch constant.
    binding.op.AssignFromFull(previous_vfetch_full_);
  } else {
    previous_vfetch_full_ = op;
  }
  binding.fetch_constant = binding.op.fetch_constant_index();
  vertex_bindings_.emplace_back(std::move(binding));
}

void ShaderTranslator::GatherTextureBindingInformation(
    const TextureFetchInstruction& op) {
  switch (op.opcode()) {
    case FetchOpcode::kSetTextureLod:
    case FetchOpcode::kSetTextureGradientsHorz:
    case FetchOpcode::kSetTextureGradientsVert:
      // Doesn't use bindings.
      return;
  }
  TranslatedShader::TextureBinding binding;
  binding.binding_index = texture_bindings_.size();
  binding.fetch_constant = op.fetch_constant_index();
  binding.op = op;
  texture_bindings_.emplace_back(std::move(binding));
}

void ShaderTranslator::TranslateControlFlowInstruction(
    const ControlFlowInstruction& cf) {
  bool ends_shader = false;
  switch (cf.opcode()) {
    case ControlFlowOpcode::kNop:
      TranslateControlFlowNop(cf);
      break;
    case ControlFlowOpcode::kExec:
      TranslateControlFlowExec(cf.exec);
      break;
    case ControlFlowOpcode::kExecEnd:
      TranslateControlFlowExec(cf.exec);
      ends_shader = true;
      break;
    case ControlFlowOpcode::kCondExec:
      TranslateControlFlowCondExec(cf.cond_exec);
      break;
    case ControlFlowOpcode::kCondExecEnd:
      TranslateControlFlowCondExec(cf.cond_exec);
      ends_shader = true;
      break;
    case ControlFlowOpcode::kCondExecPred:
      TranslateControlFlowCondExecPred(cf.cond_exec_pred);
      break;
    case ControlFlowOpcode::kCondExecPredEnd:
      TranslateControlFlowCondExecPred(cf.cond_exec_pred);
      ends_shader = true;
      break;
    case ControlFlowOpcode::kCondExecPredClean:
      TranslateControlFlowCondExec(cf.cond_exec);
      break;
    case ControlFlowOpcode::kCondExecPredCleanEnd:
      TranslateControlFlowCondExec(cf.cond_exec);
      ends_shader = true;
      break;
    case ControlFlowOpcode::kLoopStart:
      TranslateControlFlowLoopStart(cf.loop_start);
      break;
    case ControlFlowOpcode::kLoopEnd:
      TranslateControlFlowLoopEnd(cf.loop_end);
      break;
    case ControlFlowOpcode::kCondCall:
      TranslateControlFlowCondCall(cf.cond_call);
      break;
    case ControlFlowOpcode::kReturn:
      TranslateControlFlowReturn(cf.ret);
      break;
    case ControlFlowOpcode::kCondJmp:
      TranslateControlFlowCondJmp(cf.cond_jmp);
      break;
    case ControlFlowOpcode::kAlloc:
      TranslateControlFlowAlloc(cf.alloc);
      break;
    case ControlFlowOpcode::kMarkVsFetchDone:
      break;
    default:
      assert_unhandled_case(cf.opcode);
      break;
  }
  if (ends_shader) {
    // TODO(benvanik): return?
  }
}

void ShaderTranslator::TranslateControlFlowNop(
    const ControlFlowInstruction& cf) {
  AppendUcodeDisasm("      cnop\n");

  ProcessControlFlowNop(cf);
}

void ShaderTranslator::TranslateControlFlowExec(
    const ControlFlowExecInstruction& cf) {
  AppendUcodeDisasm("      ");
  AppendUcodeDisasm(cf.opcode() == ControlFlowOpcode::kExecEnd ? "exece"
                                                               : "exec");
  if (cf.is_yield()) {
    AppendUcodeDisasm(" Yield=true");
  }
  if (!cf.clean()) {
    AppendUcodeDisasm("  // PredicateClean=false");
  }
  AppendUcodeDisasm('\n');

  ProcessControlFlowExec(cf);

  TranslateExecInstructions(cf.address(), cf.count(), cf.sequence());
}

void ShaderTranslator::TranslateControlFlowCondExec(
    const ControlFlowCondExecInstruction& cf) {
  const char* opcode_name = "cexec";
  switch (cf.opcode()) {
    case ControlFlowOpcode::kCondExecEnd:
    case ControlFlowOpcode::kCondExecPredCleanEnd:
      opcode_name = "cexece";
      break;
  }
  AppendUcodeDisasm("      ");
  AppendUcodeDisasm(opcode_name);
  AppendUcodeDisasm(' ');
  if (!cf.condition()) {
    AppendUcodeDisasm('!');
  }
  AppendUcodeDisasmFormat("b%u", cf.bool_address());
  if (cf.is_yield()) {
    AppendUcodeDisasm(", Yield=true");
  }
  switch (cf.opcode()) {
    case ControlFlowOpcode::kCondExec:
    case ControlFlowOpcode::kCondExecEnd:
      AppendUcodeDisasm("  // PredicateClean=false");
      break;
  }
  AppendUcodeDisasm('\n');

  ProcessControlFlowCondExec(cf);

  TranslateExecInstructions(cf.address(), cf.count(), cf.sequence());
}

void ShaderTranslator::TranslateControlFlowCondExecPred(
    const ControlFlowCondExecPredInstruction& cf) {
  AppendUcodeDisasm(cf.condition() ? " (p0) " : "(!p0) ");
  AppendUcodeDisasm(
      cf.opcode() == ControlFlowOpcode::kCondExecPredEnd ? "exece" : "exec");
  if (cf.is_yield()) {
    AppendUcodeDisasm(" Yield=true");
  }
  if (!cf.clean()) {
    AppendUcodeDisasm("  // PredicateClean=false");
  }
  AppendUcodeDisasm('\n');

  ProcessControlFlowCondExecPred(cf);

  TranslateExecInstructions(cf.address(), cf.count(), cf.sequence());
}

void ShaderTranslator::TranslateControlFlowLoopStart(
    const ControlFlowLoopStartInstruction& cf) {
  AppendUcodeDisasm("      loop ");
  AppendUcodeDisasmFormat("i%u, L%u", cf.loop_id(), cf.address());
  if (cf.is_repeat()) {
    AppendUcodeDisasm(", Repeat=true");
  }
  AppendUcodeDisasm('\n');

  ProcessControlFlowLoopStart(cf);
}

void ShaderTranslator::TranslateControlFlowLoopEnd(
    const ControlFlowLoopEndInstruction& cf) {
  AppendUcodeDisasm(cf.condition() ? " (p0) " : "(!p0) ");
  AppendUcodeDisasmFormat("endloop i%u, L%u", cf.loop_id(), cf.address());
  AppendUcodeDisasm('\n');

  ProcessControlFlowLoopEnd(cf);
}

void ShaderTranslator::TranslateControlFlowCondCall(
    const ControlFlowCondCallInstruction& cf) {
  if (cf.is_unconditional()) {
    AppendUcodeDisasm("      call ");
  } else {
    if (cf.is_predicated()) {
      AppendUcodeDisasm(cf.condition() ? " (p0) " : "(!p0) ");
      AppendUcodeDisasm("call ");
    } else {
      AppendUcodeDisasm("      ccall ");
      if (!cf.condition()) {
        AppendUcodeDisasm('!');
      }
      AppendUcodeDisasmFormat("b%u, ", cf.bool_address());
    }
  }
  AppendUcodeDisasmFormat("L%u", cf.address());
  AppendUcodeDisasm('\n');

  ProcessControlFlowCondCall(cf);
}

void ShaderTranslator::TranslateControlFlowReturn(
    const ControlFlowReturnInstruction& cf) {
  AppendUcodeDisasm("      ret\n");

  ProcessControlFlowReturn(cf);
}

void ShaderTranslator::TranslateControlFlowCondJmp(
    const ControlFlowCondJmpInstruction& cf) {
  if (cf.is_unconditional()) {
    AppendUcodeDisasm("      jmp ");
  } else {
    if (cf.is_predicated()) {
      AppendUcodeDisasm(cf.condition() ? " (p0) " : "(!p0) ");
      AppendUcodeDisasm("jmp ");
    } else {
      AppendUcodeDisasm("      cjmp ");
      if (!cf.condition()) {
        AppendUcodeDisasm('!');
      }
      AppendUcodeDisasmFormat("b%u, ", cf.bool_address());
    }
  }
  AppendUcodeDisasmFormat("L%u", cf.address());
  AppendUcodeDisasm('\n');

  ProcessControlFlowCondJmp(cf);
}

void ShaderTranslator::TranslateControlFlowAlloc(
    const ControlFlowAllocInstruction& cf) {
  AppendUcodeDisasm("      alloc ");
  switch (cf.alloc_type()) {
    case AllocType::kNoAlloc:
      break;
    case AllocType::kVsPosition:
      AppendUcodeDisasmFormat("position");
      break;
    case AllocType::kVsInterpolators:  // or AllocType::kPsColors
      if (shader_type_ == ShaderType::kVertex) {
        AppendUcodeDisasm("interpolators");
      } else {
        AppendUcodeDisasm("colors");
      }
      break;
    case AllocType::kMemory:
      AppendUcodeDisasmFormat("export = %d", cf.size());
      break;
  }
  AppendUcodeDisasm('\n');

  ProcessControlFlowAlloc(cf);
}

void ShaderTranslator::TranslateExecInstructions(uint32_t address,
                                                 uint32_t count,
                                                 uint32_t sequence) {
  for (uint32_t instr_offset = address; instr_offset < address + count;
       ++instr_offset, sequence >>= 2) {
    MarkUcodeInstruction(instr_offset);
    AppendUcodeDisasmFormat("/* %4u   */ ", instr_offset);
    bool is_sync = (sequence & 0x2) == 0x2;
    bool is_fetch = (sequence & 0x1) == 0x1;
    if (is_sync) {
      AppendUcodeDisasm("         serialize\n             ");
    }
    if (is_fetch) {
      auto fetch_opcode =
          static_cast<FetchOpcode>(ucode_dwords_[instr_offset * 3] & 0x1F);
      if (fetch_opcode == FetchOpcode::kVertexFetch) {
        TranslateVertexFetchInstruction(
            *reinterpret_cast<const VertexFetchInstruction*>(ucode_dwords_ +
                                                             instr_offset * 3));
      } else {
        TranslateTextureFetchInstruction(
            *reinterpret_cast<const TextureFetchInstruction*>(
                ucode_dwords_ + instr_offset * 3));
      }
    } else {
      TranslateAluInstruction(*reinterpret_cast<const AluInstruction*>(
          ucode_dwords_ + instr_offset * 3));
    }
  }
}

const char kXyzwChannelNames[] = {'x', 'y', 'z', 'w'};
const char kFetchChannelNames[] = {'x', 'y', 'z', 'w', '0', '1', '?', '_'};
const char* kTemporaryNames[32] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9",  "r10",
    "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21",
    "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};
const char* kInterpolantNames[16] = {
    "o0", "o1", "o2",  "o3",  "o4",  "o5",  "o6",  "o7",
    "o8", "o9", "o10", "o11", "o12", "o13", "o14", "o15",
};
const char* kDimensionNames[] = {
    "1D", "2D", "3D", "Cube",
};
const char* kTextureFilterNames[] = {
    "point", "linear", "BASEMAP", "keep",
};
const char* kAnisoFilterNames[] = {
    "disabled", "max1to1", "max2to1", "max4to1", "max8to1", "max16to1", "keep",
};

int GetComponentCount(TextureDimension dimension) {
  switch (dimension) {
    case TextureDimension::k1D:
      return 1;
    case TextureDimension::k2D:
      return 2;
    case TextureDimension::k3D:
    case TextureDimension::kCube:
      return 3;
    default:
      assert_unhandled_case(dimension);
      return 1;
  }
}

void ShaderTranslator::DisasmFetchDestReg(uint32_t dest, uint32_t swizzle,
                                          bool is_relative) {
  if (is_relative) {
    AppendUcodeDisasmFormat("r[%u+aL]", dest);
  } else {
    AppendUcodeDisasmFormat("r%u", dest);
  }
  if (swizzle != 0x688) {
    AppendUcodeDisasm('.');
    for (int i = 0; i < 4; ++i) {
      if ((swizzle & 0x7) == 4) {
        AppendUcodeDisasm('0');
      } else if ((swizzle & 0x7) == 5) {
        AppendUcodeDisasm('1');
      } else if ((swizzle & 0x7) == 6) {
        AppendUcodeDisasm('?');
      } else if ((swizzle & 0x7) == 7) {
        AppendUcodeDisasm('_');
      } else {
        AppendUcodeDisasm(kXyzwChannelNames[swizzle & 0x3]);
      }
      swizzle >>= 3;
    }
  }
}

void ShaderTranslator::DisasmFetchSourceReg(uint32_t src, uint32_t swizzle,
                                            bool is_relative,
                                            int component_count) {
  if (is_relative) {
    AppendUcodeDisasmFormat("r[%u+aL]", src);
  } else {
    AppendUcodeDisasmFormat("r%u", src);
  }
  if (swizzle != 0xFF) {
    AppendUcodeDisasm('.');
    for (int i = 0; i < component_count; ++i) {
      AppendUcodeDisasm(kXyzwChannelNames[swizzle & 0x3]);
      swizzle >>= 2;
    }
  }
}

static const struct {
  const char* name;
} kVertexFetchDataFormats[0xff] = {
#define TYPE(id) \
  { #id }
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_8_8_8_8),     // 6
    TYPE(FMT_2_10_10_10),  // 7
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_10_11_11),  // 16
    TYPE(FMT_11_11_10),  // 17
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_16_16),        // 25
    TYPE(FMT_16_16_16_16),  // 26
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_16_16_FLOAT),        // 31
    TYPE(FMT_16_16_16_16_FLOAT),  // 32
    TYPE(FMT_32),                 // 33
    TYPE(FMT_32_32),              // 34
    TYPE(FMT_32_32_32_32),        // 35
    TYPE(FMT_32_FLOAT),           // 36
    TYPE(FMT_32_32_FLOAT),        // 37
    TYPE(FMT_32_32_32_32_FLOAT),  // 38
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_32_32_32_FLOAT),  // 57
#undef TYPE
};

void ShaderTranslator::DisasmVertexFetchAttributes(
    const VertexFetchInstruction& op) {
  if (op.is_index_rounded()) {
    AppendUcodeDisasm(", RoundIndex=true");
  }
  if (op.exp_adjust()) {
    AppendUcodeDisasmFormat(", ExpAdjust=%d", op.exp_adjust());
  }
  if (op.offset()) {
    AppendUcodeDisasmFormat(", Offset=%u", op.offset());
  }
  if (op.data_format() != VertexFormat::kUndefined) {
    AppendUcodeDisasmFormat(
        ", DataFormat=%s",
        kVertexFetchDataFormats[static_cast<int>(op.data_format())]);
  }
  if (op.stride()) {
    AppendUcodeDisasmFormat(", Stride=%u", op.stride());
  }
  if (op.is_signed()) {
    AppendUcodeDisasm(", Signed=true");
  }
  if (op.is_integer()) {
    AppendUcodeDisasm(", NumFormat=integer");
  }
  if (op.prefetch_count() != 0) {
    AppendUcodeDisasmFormat(", PrefetchCount=%u", op.prefetch_count() + 1);
  }
}

void ShaderTranslator::TranslateVertexFetchInstruction(
    const VertexFetchInstruction& op) {
  AppendUcodeDisasm("   ");
  if (op.is_predicated()) {
    AppendUcodeDisasm(op.predicate_condition() ? " (p0) " : "(!p0) ");
  } else {
    AppendUcodeDisasm("      ");
  }
  AppendUcodeDisasmFormat(op.is_mini_fetch() ? "vfetch_mini" : "vfetch_full");
  AppendUcodeDisasm(' ');
  DisasmFetchDestReg(op.dest(), op.dest_swizzle(), op.is_dest_relative());
  if (!op.is_mini_fetch()) {
    AppendUcodeDisasm(", ");
    DisasmFetchSourceReg(op.src(), op.src_swizzle(), op.is_src_relative(), 1);
    AppendUcodeDisasm(", ");
    AppendUcodeDisasmFormat("vf%u", 95 - op.fetch_constant_index());
  }
  DisasmVertexFetchAttributes(op);
  AppendUcodeDisasm('\n');

  if (!op.is_mini_fetch()) {
    previous_vfetch_full_ = op;
  }

  ProcessVertexFetchInstruction(op);
}

void ShaderTranslator::TranslateTextureFetchInstruction(
    const TextureFetchInstruction& op) {
  struct TextureFetchOpcodeInfo {
    const char* base_name;
    bool name_has_dimension;
    bool has_dest;
    bool has_src;
    bool has_const;
    bool has_attributes;
    int override_component_count;
  } opcode_info;
  switch (op.opcode()) {
    case FetchOpcode::kTextureFetch:
      opcode_info = {"tfetch", true, true, true, true, true, 0};
      break;
    case FetchOpcode::kGetTextureBorderColorFrac:
      opcode_info = {"getBCF", true, true, true, true, true, 0};
      break;
    case FetchOpcode::kGetTextureComputedLod:
      opcode_info = {"getCompTexLOD", true, true, true, true, true, 0};
      break;
    case FetchOpcode::kGetTextureGradients:
      opcode_info = {"getGradients", false, true, true, true, true, 2};
      break;
    case FetchOpcode::kGetTextureWeights:
      opcode_info = {"getWeights", true, true, true, true, true, 0};
      break;
    case FetchOpcode::kSetTextureLod:
      opcode_info = {"setTexLOD", false, false, true, false, false, 1};
      break;
    case FetchOpcode::kSetTextureGradientsHorz:
      opcode_info = {"setGradientH", false, false, true, false, false, 3};
      break;
    case FetchOpcode::kSetTextureGradientsVert:
      opcode_info = {"setGradientV", false, false, true, false, false, 3};
      break;
    default:
    case FetchOpcode::kUnknownTextureOp:
      assert_unhandled_case(fetch_opcode);
      return;
  }
  AppendUcodeDisasm("   ");
  if (op.is_predicated()) {
    AppendUcodeDisasm(op.predicate_condition() ? " (p0) " : "(!p0) ");
  } else {
    AppendUcodeDisasm("      ");
  }
  AppendUcodeDisasm(opcode_info.base_name);
  if (opcode_info.name_has_dimension) {
    AppendUcodeDisasmFormat(kDimensionNames[static_cast<int>(op.dimension())]);
  }
  AppendUcodeDisasm(' ');
  bool needs_comma = false;
  if (opcode_info.has_dest) {
    DisasmFetchDestReg(op.dest(), op.dest_swizzle(), op.is_dest_relative());
    needs_comma = true;
  }
  if (opcode_info.has_src) {
    if (needs_comma) {
      AppendUcodeDisasm(", ");
    }
    DisasmFetchSourceReg(op.src(), op.src_swizzle(), op.is_src_relative(),
                         opcode_info.override_component_count
                             ? opcode_info.override_component_count
                             : GetComponentCount(op.dimension()));
  }
  if (opcode_info.has_const) {
    if (needs_comma) {
      AppendUcodeDisasm(", ");
    }
    AppendUcodeDisasmFormat("tf%u", op.fetch_constant_index());
  }
  if (opcode_info.has_attributes) {
    DisasmTextureFetchAttributes(op);
  }
  AppendUcodeDisasm('\n');

  switch (op.opcode()) {
    case FetchOpcode::kTextureFetch:
      ProcessTextureFetchTextureFetch(op);
      return;
    case FetchOpcode::kGetTextureBorderColorFrac:
      ProcessTextureFetchGetTextureBorderColorFrac(op);
      return;
    case FetchOpcode::kGetTextureComputedLod:
      ProcessTextureFetchGetTextureComputedLod(op);
      return;
    case FetchOpcode::kGetTextureGradients:
      ProcessTextureFetchGetTextureGradients(op);
      return;
    case FetchOpcode::kGetTextureWeights:
      ProcessTextureFetchGetTextureWeights(op);
      return;
    case FetchOpcode::kSetTextureLod:
      ProcessTextureFetchSetTextureLod(op);
      return;
    case FetchOpcode::kSetTextureGradientsHorz:
      ProcessTextureFetchSetTextureGradientsHorz(op);
      return;
    case FetchOpcode::kSetTextureGradientsVert:
      ProcessTextureFetchSetTextureGradientsVert(op);
      return;
    default:
    case FetchOpcode::kUnknownTextureOp:
      assert_unhandled_case(fetch_opcode);
      return;
  }
}

void ShaderTranslator::DisasmTextureFetchAttributes(
    const TextureFetchInstruction& op) {
  if (!op.fetch_valid_only()) {
    AppendUcodeDisasm(", FetchValidOnly=false");
  }
  if (op.unnormalized_coordinates()) {
    AppendUcodeDisasm(", UnnormalizedTextureCoords=true");
  }
  if (op.has_mag_filter()) {
    AppendUcodeDisasmFormat(
        ", MagFilter=%s",
        kTextureFilterNames[static_cast<int>(op.mag_filter())]);
  }
  if (op.has_min_filter()) {
    AppendUcodeDisasmFormat(
        ", MinFilter=%s",
        kTextureFilterNames[static_cast<int>(op.min_filter())]);
  }
  if (op.has_mip_filter()) {
    AppendUcodeDisasmFormat(
        ", MipFilter=%s",
        kTextureFilterNames[static_cast<int>(op.mip_filter())]);
  }
  if (op.has_aniso_filter()) {
    AppendUcodeDisasmFormat(
        ", AnisoFilter=%s",
        kAnisoFilterNames[static_cast<int>(op.aniso_filter())]);
  }
  if (!op.use_computed_lod()) {
    AppendUcodeDisasm(", UseComputedLOD=false");
  }
  if (op.use_register_lod()) {
    AppendUcodeDisasm(", UseRegisterLOD=true");
  }
  if (op.use_register_gradients()) {
    AppendUcodeDisasm(", UseRegisterGradients=true");
  }
  int component_count = GetComponentCount(op.dimension());
  if (op.offset_x() != 0.0f) {
    AppendUcodeDisasmFormat(", OffsetX=%g", op.offset_x());
  }
  if (component_count > 1 && op.offset_y() != 0.0f) {
    AppendUcodeDisasmFormat(", OffsetY=%g", op.offset_y());
  }
  if (component_count > 2 && op.offset_z() != 0.0f) {
    AppendUcodeDisasmFormat(", OffsetZ=%g", op.offset_z());
  }
}

const ShaderTranslator::AluOpcodeInfo
    ShaderTranslator::alu_vector_opcode_infos_[0x20] = {
        {"add", 2, 4, &ShaderTranslator::ProcessAluVectorAdd},        // 0
        {"mul", 2, 4, &ShaderTranslator::ProcessAluVectorMul},        // 1
        {"max", 2, 4, &ShaderTranslator::ProcessAluVectorMax},        // 2
        {"min", 2, 4, &ShaderTranslator::ProcessAluVectorMin},        // 3
        {"seq", 2, 4, &ShaderTranslator::ProcessAluVectorSetEQ},      // 4
        {"sgt", 2, 4, &ShaderTranslator::ProcessAluVectorSetGT},      // 5
        {"sge", 2, 4, &ShaderTranslator::ProcessAluVectorSetGE},      // 6
        {"sne", 2, 4, &ShaderTranslator::ProcessAluVectorSetNE},      // 7
        {"frc", 1, 4, &ShaderTranslator::ProcessAluVectorFrac},       // 8
        {"trunc", 1, 4, &ShaderTranslator::ProcessAluVectorTrunc},    // 9
        {"floor", 1, 4, &ShaderTranslator::ProcessAluVectorFloor},    // 10
        {"mad", 3, 4, &ShaderTranslator::ProcessAluVectorMad},        // 11
        {"cndeq", 3, 4, &ShaderTranslator::ProcessAluVectorCndEQ},    // 12
        {"cndge", 3, 4, &ShaderTranslator::ProcessAluVectorCndGE},    // 13
        {"cndgt", 3, 4, &ShaderTranslator::ProcessAluVectorCndGT},    // 14
        {"dp4", 2, 4, &ShaderTranslator::ProcessAluVectorDp4},        // 15
        {"dp3", 2, 4, &ShaderTranslator::ProcessAluVectorDp3},        // 16
        {"dp2add", 3, 4, &ShaderTranslator::ProcessAluVectorDp2Add},  // 17
        {"cube", 2, 4, &ShaderTranslator::ProcessAluVectorCube},      // 18
        {"max4", 1, 4, &ShaderTranslator::ProcessAluVectorMax4},      // 19
        {"setp_eq_push", 2, 4,
         &ShaderTranslator::ProcessAluVectorPredSetEQPush},  // 20
        {"setp_ne_push", 2, 4,
         &ShaderTranslator::ProcessAluVectorPredSetNEPush},  // 21
        {"setp_gt_push", 2, 4,
         &ShaderTranslator::ProcessAluVectorPredSetGTPush},  // 22
        {"setp_ge_push", 2, 4,
         &ShaderTranslator::ProcessAluVectorPredSetGEPush},             // 23
        {"kill_eq", 2, 4, &ShaderTranslator::ProcessAluVectorKillEQ},   // 24
        {"kill_gt", 2, 4, &ShaderTranslator::ProcessAluVectorKillGT},   // 25
        {"kill_ge", 2, 4, &ShaderTranslator::ProcessAluVectorKillLGE},  // 26
        {"kill_ne", 2, 4, &ShaderTranslator::ProcessAluVectorKillNE},   // 27
        {"dst", 2, 4, &ShaderTranslator::ProcessAluVectorDst},          // 28
        {"maxa", 2, 4, &ShaderTranslator::ProcessAluVectorMaxA},        // 29
};

const ShaderTranslator::AluOpcodeInfo
    ShaderTranslator::alu_scalar_opcode_infos_[0x40] = {
        {"adds", 1, 2, &ShaderTranslator::ProcessAluScalarAdd},             // 0
        {"adds_prev", 1, 1, &ShaderTranslator::ProcessAluScalarAddPrev},    // 1
        {"muls", 1, 2, &ShaderTranslator::ProcessAluScalarMul},             // 2
        {"muls_prev", 1, 1, &ShaderTranslator::ProcessAluScalarMulPrev},    // 3
        {"muls_prev2", 1, 2, &ShaderTranslator::ProcessAluScalarMulPrev2},  // 4
        {"maxs", 1, 2, &ShaderTranslator::ProcessAluScalarMax},             // 5
        {"mins", 1, 2, &ShaderTranslator::ProcessAluScalarMin},             // 6
        {"seqs", 1, 1, &ShaderTranslator::ProcessAluScalarSetEQ},           // 7
        {"sgts", 1, 1, &ShaderTranslator::ProcessAluScalarSetGT},           // 8
        {"sges", 1, 1, &ShaderTranslator::ProcessAluScalarSetGE},           // 9
        {"snes", 1, 1, &ShaderTranslator::ProcessAluScalarSetNE},       // 10
        {"frcs", 1, 1, &ShaderTranslator::ProcessAluScalarFrac},        // 11
        {"truncs", 1, 1, &ShaderTranslator::ProcessAluScalarTrunc},     // 12
        {"floors", 1, 1, &ShaderTranslator::ProcessAluScalarFloor},     // 13
        {"exp", 1, 1, &ShaderTranslator::ProcessAluScalarExp},          // 14
        {"logc", 1, 1, &ShaderTranslator::ProcessAluScalarLogClamp},    // 15
        {"log", 1, 1, &ShaderTranslator::ProcessAluScalarLog},          // 16
        {"rcpc", 1, 1, &ShaderTranslator::ProcessAluScalarRecipClamp},  // 17
        {"rcpf", 1, 1,
         &ShaderTranslator::ProcessAluScalarRecipFixedFunc},            // 18
        {"rcp", 1, 1, &ShaderTranslator::ProcessAluScalarRecip},        // 19
        {"rsqc", 1, 1, &ShaderTranslator::ProcessAluScalarRSqrtClamp},  // 20
        {"rsqf", 1, 1,
         &ShaderTranslator::ProcessAluScalarRSqrtFixedFunc},              // 21
        {"rsq", 1, 1, &ShaderTranslator::ProcessAluScalarRSqrt},          // 22
        {"movas", 1, 1, &ShaderTranslator::ProcessAluScalarMovA},         // 23
        {"movasf", 1, 1, &ShaderTranslator::ProcessAluScalarMovAFloor},   // 24
        {"subs", 1, 2, &ShaderTranslator::ProcessAluScalarSub},           // 25
        {"subs_prev", 1, 1, &ShaderTranslator::ProcessAluScalarSubPrev},  // 26
        {"setp_eq", 1, 1, &ShaderTranslator::ProcessAluScalarPredSetEQ},  // 27
        {"setp_ne", 1, 1, &ShaderTranslator::ProcessAluScalarPredSetNE},  // 28
        {"setp_gt", 1, 1, &ShaderTranslator::ProcessAluScalarPredSetGT},  // 29
        {"setp_ge", 1, 1, &ShaderTranslator::ProcessAluScalarPredSetGE},  // 30
        {"setp_inv", 1, 1,
         &ShaderTranslator::ProcessAluScalarPredSetInv},  // 31
        {"setp_pop", 1, 1,
         &ShaderTranslator::ProcessAluScalarPredSetPop},  // 32
        {"setp_clr", 1, 1,
         &ShaderTranslator::ProcessAluScalarPredSetClear},  // 33
        {"setp_rstr", 1, 1,
         &ShaderTranslator::ProcessAluScalarPredSetRestore},              // 34
        {"kills_eq", 1, 1, &ShaderTranslator::ProcessAluScalarKillEQ},    // 35
        {"kills_gt", 1, 1, &ShaderTranslator::ProcessAluScalarKillGT},    // 36
        {"kills_ge", 1, 1, &ShaderTranslator::ProcessAluScalarKillGE},    // 37
        {"kills_ne", 1, 1, &ShaderTranslator::ProcessAluScalarKillNE},    // 38
        {"kills_one", 1, 1, &ShaderTranslator::ProcessAluScalarKillOne},  // 39
        {"sqrt", 1, 1, &ShaderTranslator::ProcessAluScalarSqrt},          // 40
        {"UNKNOWN", 0, 0, nullptr},                                       // 41
        {"mulsc", 2, 1, &ShaderTranslator::ProcessAluScalarMulConst0},    // 42
        {"mulsc", 2, 1, &ShaderTranslator::ProcessAluScalarMulConst1},    // 43
        {"addsc", 2, 1, &ShaderTranslator::ProcessAluScalarAddConst0},    // 44
        {"addsc", 2, 1, &ShaderTranslator::ProcessAluScalarAddConst1},    // 45
        {"subsc", 2, 1, &ShaderTranslator::ProcessAluScalarSubConst0},    // 46
        {"subsc", 2, 1, &ShaderTranslator::ProcessAluScalarSubConst1},    // 47
        {"sin", 1, 1, &ShaderTranslator::ProcessAluScalarSin},            // 48
        {"cos", 1, 1, &ShaderTranslator::ProcessAluScalarCos},            // 49
        {"retain_prev", 1, 1,
         &ShaderTranslator::ProcessAluScalarRetainPrev},  // 50
};

const char* GetDestRegName(ShaderType shader_type, uint32_t dest_num,
                           bool is_export) {
  if (!is_export) {
    assert_true(dest_num < xe::countof(kTemporaryNames));
    return kTemporaryNames[dest_num];
  }
  if (shader_type == ShaderType::kVertex) {
    switch (dest_num) {
      case 62:
        return "oPos";
      case 63:
        return "oPts";
      default:
        assert_true(dest_num < xe::countof(kInterpolantNames));
        return kInterpolantNames[dest_num];
    }
  } else {
    switch (dest_num) {
      case 0:
      case 63:  // ? masked?
        return "oC0";
      case 1:
        return "oC1";
      case 2:
        return "oC2";
      case 3:
        return "oC3";
      case 61:
        return "oDepth";
    }
  }
  return "???";
}

void ShaderTranslator::DisasmAluSourceReg(const AluInstruction& op, int i,
                                          int swizzle_component_count) {
  int const_slot = 0;
  switch (i) {
    case 2:
      const_slot = op.src_is_temp(1) ? 0 : 1;
      break;
    case 3:
      const_slot = op.src_is_temp(1) && op.src_is_temp(2) ? 0 : 1;
      break;
  }
  if (op.src_negate(i)) {
    AppendUcodeDisasm('-');
  }
  uint32_t reg = op.src_reg(i);
  if (op.src_is_temp(i)) {
    AppendUcodeDisasm(kTemporaryNames[reg & 0x7F]);
    if (reg & 0x80) {
      AppendUcodeDisasm("_abs");
    }
  } else {
    if ((const_slot == 0 && op.is_const_0_addressed()) ||
        (const_slot == 1 && op.is_const_1_addressed())) {
      if (op.is_address_relative()) {
        AppendUcodeDisasmFormat("c[%u+a0]", reg);
      } else {
        AppendUcodeDisasmFormat("c[%u+aL]", reg);
      }
    } else {
      AppendUcodeDisasmFormat("c%u", reg);
    }
    if (op.abs_constants()) {
      AppendUcodeDisasm("_abs");
    }
  }
  uint32_t swizzle = op.src_swizzle(i);
  if (swizzle) {
    AppendUcodeDisasm('.');
    if (swizzle_component_count == 1) {
      uint32_t a = swizzle & 0x3;
      AppendUcodeDisasm(kXyzwChannelNames[a]);
    } else if (swizzle_component_count == 2) {
      swizzle >>= 4;
      uint32_t a = swizzle & 0x3;
      uint32_t b = ((swizzle >> 2) + 1) & 0x3;
      ;
      AppendUcodeDisasm(kXyzwChannelNames[a]);
      AppendUcodeDisasm(kXyzwChannelNames[b]);
    } else {
      for (int i = 0; i < swizzle_component_count; ++i, swizzle >>= 2) {
        AppendUcodeDisasm(kXyzwChannelNames[(swizzle + i) & 0x3]);
      }
    }
  }
}

void ShaderTranslator::DisasmAluSourceRegScalarSpecial(
    const AluInstruction& op, uint32_t reg, bool is_temp, bool negate,
    int const_slot, uint32_t swizzle) {
  if (negate) {
    AppendUcodeDisasm('-');
  }
  if (is_temp) {
    AppendUcodeDisasm(kTemporaryNames[reg & 0x7F]);
    if (reg & 0x80) {
      AppendUcodeDisasm("_abs");
    }
  } else {
    AppendUcodeDisasmFormat("c%u", reg);
    if (op.abs_constants()) {
      AppendUcodeDisasm("_abs");
    }
  }
  AppendUcodeDisasm('.');
  AppendUcodeDisasm(kXyzwChannelNames[swizzle]);
}

void ShaderTranslator::TranslateAluInstruction(const AluInstruction& op) {
  if (!op.has_vector_op() && !op.has_scalar_op()) {
    AppendUcodeDisasm("         nop\n");
    ProcessAluNop(op);
    return;
  }

  if (op.has_vector_op()) {
    const auto& opcode_info =
        alu_vector_opcode_infos_[static_cast<int>(op.vector_opcode())];
    DisasmAluVectorInstruction(op, opcode_info);
    (this->*(opcode_info.fn))(op);
  }

  if (op.has_scalar_op()) {
    const auto& opcode_info =
        alu_scalar_opcode_infos_[static_cast<int>(op.scalar_opcode())];
    DisasmAluScalarInstruction(op, opcode_info);
    (this->*(opcode_info.fn))(op);
  }
}

void ShaderTranslator::DisasmAluVectorInstruction(
    const AluInstruction& op, const AluOpcodeInfo& opcode_info) {
  AppendUcodeDisasm("   ");
  if (op.is_predicated()) {
    AppendUcodeDisasm(op.predicate_condition() ? " (p0) " : "(!p0) ");
  } else {
    AppendUcodeDisasm("      ");
  }
  if (!op.has_vector_op()) {
    AppendUcodeDisasmFormat("nop\n");
    return;
  }

  AppendUcodeDisasmFormat(opcode_info.name);
  if (op.vector_clamp()) {
    AppendUcodeDisasm("_sat");
  }
  AppendUcodeDisasm(' ');

  AppendUcodeDisasm(
      GetDestRegName(shader_type_, op.vector_dest(), op.is_export()));
  if (op.is_export()) {
    uint32_t write_mask = op.vector_write_mask();
    uint32_t const_1_mask = op.scalar_write_mask();
    if (!(write_mask == 0xF && const_1_mask == 0)) {
      AppendUcodeDisasm('.');
      for (int i = 0; i < 4; ++i, write_mask >>= 1, const_1_mask >>= 1) {
        if (write_mask & 0x1) {
          if (const_1_mask & 0x1) {
            AppendUcodeDisasm('1');
          } else {
            AppendUcodeDisasm(kXyzwChannelNames[i]);
          }
        } else if (op.is_scalar_dest_relative()) {
          AppendUcodeDisasm('0');
        }
      }
    }
  } else {
    if (!op.vector_write_mask()) {
      AppendUcodeDisasm("._");
    } else if (op.vector_write_mask() != 0xF) {
      uint32_t write_mask = op.vector_write_mask();
      AppendUcodeDisasm('.');
      for (int i = 0; i < 4; ++i, write_mask >>= 1) {
        if (write_mask & 0x1) {
          AppendUcodeDisasm(kXyzwChannelNames[i]);
        }
      }
    }
  }
  AppendUcodeDisasm(", ");

  DisasmAluSourceReg(op, 1, opcode_info.src_swizzle_component_count);
  if (opcode_info.argument_count > 1) {
    AppendUcodeDisasm(", ");
    DisasmAluSourceReg(op, 2, opcode_info.src_swizzle_component_count);
  }
  if (opcode_info.argument_count > 2) {
    AppendUcodeDisasm(", ");
    DisasmAluSourceReg(op, 3, opcode_info.src_swizzle_component_count);
  }

  AppendUcodeDisasm('\n');
}

void ShaderTranslator::DisasmAluScalarInstruction(
    const AluInstruction& op, const AluOpcodeInfo& opcode_info) {
  if (op.has_vector_op()) {
    AppendUcodeDisasm("              + ");
  } else {
    AppendUcodeDisasm("   ");
  }
  if (op.is_predicated()) {
    AppendUcodeDisasm(op.predicate_condition() ? " (p0) " : "(!p0) ");
  } else {
    AppendUcodeDisasm("      ");
  }
  AppendUcodeDisasmFormat(opcode_info.name);
  if (op.scalar_clamp()) {
    AppendUcodeDisasm("_sat");
  }
  AppendUcodeDisasm(' ');

  uint32_t dest_num;
  uint32_t write_mask;
  if (op.is_export()) {
    dest_num = op.vector_dest();
    write_mask = op.scalar_write_mask() & ~op.vector_write_mask();
  } else {
    dest_num = op.scalar_dest();
    write_mask = op.scalar_write_mask();
  }
  AppendUcodeDisasm(GetDestRegName(shader_type_, dest_num, op.is_export()));
  if (!write_mask) {
    AppendUcodeDisasm("._");
  } else if (write_mask != 0xF) {
    AppendUcodeDisasm('.');
    for (int i = 0; i < 4; ++i, write_mask >>= 1) {
      if (write_mask & 0x1) {
        AppendUcodeDisasm(kXyzwChannelNames[i]);
      }
    }
  }
  AppendUcodeDisasm(", ");

  if (opcode_info.argument_count == 1) {
    DisasmAluSourceReg(op, 3, opcode_info.src_swizzle_component_count);
  } else {
    uint32_t src3_swizzle = op.src_swizzle(3);
    uint32_t swiz_a = ((src3_swizzle >> 6) - 1) & 0x3;
    uint32_t swiz_b = (src3_swizzle & 0x3);
    uint32_t reg2 = (static_cast<int>(op.scalar_opcode()) & 1) |
                    (src3_swizzle & 0x3C) | (op.src_is_temp(3) << 1);
    int const_slot = (op.src_is_temp(1) || op.src_is_temp(2)) ? 1 : 0;
    DisasmAluSourceRegScalarSpecial(op, op.src_reg(3), false, op.src_negate(3),
                                    0, swiz_a);
    AppendUcodeDisasm(", ");
    DisasmAluSourceRegScalarSpecial(op, reg2, true, op.src_negate(3),
                                    const_slot, swiz_b);
  }

  AppendUcodeDisasm('\n');
}

std::vector<uint8_t> UcodeShaderTranslator::CompleteTranslation() {
  return ucode_disasm_buffer().ToBytes();
}

}  // namespace gpu
}  // namespace xe
