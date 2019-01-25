#include "shader_translator.h"
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

#include "xenia/base/logging.h"
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
// and dumps of d3dcompiler and games: https://pastebin.com/i4kAv7bB

constexpr uint32_t ShaderTranslator::kMaxMemExports;

ShaderTranslator::ShaderTranslator() = default;

ShaderTranslator::~ShaderTranslator() = default;

void ShaderTranslator::Reset() {
  errors_.clear();
  ucode_disasm_buffer_.Reset();
  ucode_disasm_line_number_ = 0;
  previous_ucode_disasm_scan_offset_ = 0;
  register_count_ = 64;
  total_attrib_count_ = 0;
  vertex_bindings_.clear();
  unique_vertex_bindings_ = 0;
  texture_bindings_.clear();
  unique_texture_bindings_ = 0;
  std::memset(&constant_register_map_, 0, sizeof(constant_register_map_));
  uses_register_dynamic_addressing_ = false;
  for (size_t i = 0; i < xe::countof(writes_color_targets_); ++i) {
    writes_color_targets_[i] = false;
  }
  writes_depth_ = false;
  early_z_allowed_ = true;
  memexport_alloc_count_ = 0;
  memexport_eA_written_ = 0;
  std::memset(&memexport_eM_written_, 0, sizeof(memexport_eM_written_));
  memexport_stream_constants_.clear();
}

bool ShaderTranslator::GatherAllBindingInformation(Shader* shader) {
  // DEPRECATED: remove this codepath when GL4 goes away.
  Reset();

  shader_type_ = shader->type();
  ucode_dwords_ = shader->ucode_dwords();
  ucode_dword_count_ = shader->ucode_dword_count();

  uint32_t max_cf_dword_index = static_cast<uint32_t>(ucode_dword_count_);
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

    GatherInstructionInformation(cf_a);
    GatherInstructionInformation(cf_b);
  }

  shader->vertex_bindings_ = std::move(vertex_bindings_);
  shader->texture_bindings_ = std::move(texture_bindings_);
  for (size_t i = 0; i < xe::countof(writes_color_targets_); ++i) {
    shader->writes_color_targets_[i] = writes_color_targets_[i];
  }

  return true;
}

bool ShaderTranslator::Translate(Shader* shader,
                                 xenos::xe_gpu_program_cntl_t cntl) {
  Reset();
  register_count_ = shader->type() == ShaderType::kVertex ? cntl.vs_regs + 1
                                                          : cntl.ps_regs + 1;

  return TranslateInternal(shader);
}

bool ShaderTranslator::Translate(Shader* shader) {
  Reset();
  return TranslateInternal(shader);
}

bool ShaderTranslator::TranslateInternal(Shader* shader) {
  shader_type_ = shader->type();
  ucode_dwords_ = shader->ucode_dwords();
  ucode_dword_count_ = shader->ucode_dword_count();

  // Run through and gather all binding information and to check whether
  // registers are dynamically addressed.
  // Translators may need this before they start codegen.
  uint32_t max_cf_dword_index = static_cast<uint32_t>(ucode_dword_count_);
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

    GatherInstructionInformation(cf_a);
    GatherInstructionInformation(cf_b);
  }
  // Cleanup invalid/unneeded memexport allocs.
  for (uint32_t i = 0; i < kMaxMemExports; ++i) {
    if (!memexport_eM_written_[i]) {
      memexport_eA_written_ &= ~(1u << i);
    }
  }
  if (memexport_eA_written_ == 0) {
    memexport_stream_constants_.clear();
  }

  StartTranslation();

  TranslateBlocks();

  // Compute total number of float registers and total bytes used by the
  // register map. This saves us work later when we need to pack them.
  constant_register_map_.packed_byte_length = 0;
  constant_register_map_.float_count = 0;
  for (int i = 0; i < 4; ++i) {
    // Each bit indicates a vec4 (4 floats).
    constant_register_map_.float_count +=
        xe::bit_count(constant_register_map_.float_bitmap[i]);
  }
  constant_register_map_.packed_byte_length +=
      4 * 4 * constant_register_map_.float_count;
  // Each bit indicates a single word.
  constant_register_map_.packed_byte_length +=
      4 * xe::bit_count(constant_register_map_.int_bitmap);
  // Direct map between words and words we upload.
  for (int i = 0; i < 8; ++i) {
    if (constant_register_map_.bool_bitmap[i]) {
      constant_register_map_.packed_byte_length += 4;
    }
  }

  shader->errors_ = std::move(errors_);
  shader->translated_binary_ = CompleteTranslation();
  shader->ucode_disassembly_ = ucode_disasm_buffer_.to_string();
  shader->vertex_bindings_ = std::move(vertex_bindings_);
  shader->texture_bindings_ = std::move(texture_bindings_);
  shader->constant_register_map_ = std::move(constant_register_map_);
  for (size_t i = 0; i < xe::countof(writes_color_targets_); ++i) {
    shader->writes_color_targets_[i] = writes_color_targets_[i];
  }
  shader->early_z_allowed_ = early_z_allowed_;
  shader->memexport_stream_constants_.clear();
  for (uint32_t memexport_stream_constant : memexport_stream_constants_) {
    shader->memexport_stream_constants_.push_back(memexport_stream_constant);
  }

  shader->is_valid_ = true;
  shader->is_translated_ = true;
  for (const auto& error : shader->errors_) {
    if (error.is_fatal) {
      shader->is_valid_ = false;
      break;
    }
  }

  PostTranslation(shader);

  return shader->is_valid_;
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
  Shader::Error error;
  error.is_fatal = true;
  error.message = message;
  // TODO(benvanik): location information.
  errors_.push_back(std::move(error));
}

void ShaderTranslator::EmitUnimplementedTranslationError() {
  Shader::Error error;
  error.is_fatal = false;
  error.message = "Unimplemented translation";
  // TODO(benvanik): location information.
  errors_.push_back(std::move(error));
}

void ShaderTranslator::GatherInstructionInformation(
    const ControlFlowInstruction& cf) {
  switch (cf.opcode()) {
    case ControlFlowOpcode::kExec:
    case ControlFlowOpcode::kExecEnd:
    case ControlFlowOpcode::kCondExec:
    case ControlFlowOpcode::kCondExecEnd:
    case ControlFlowOpcode::kCondExecPred:
    case ControlFlowOpcode::kCondExecPredEnd:
    case ControlFlowOpcode::kCondExecPredClean:
    case ControlFlowOpcode::kCondExecPredCleanEnd: {
      uint32_t sequence = cf.exec.sequence();
      for (uint32_t instr_offset = cf.exec.address();
           instr_offset < cf.exec.address() + cf.exec.count();
           ++instr_offset, sequence >>= 2) {
        bool is_fetch = (sequence & 0x1) == 0x1;
        if (is_fetch) {
          // Gather vertex and texture fetches.
          auto fetch_opcode =
              static_cast<FetchOpcode>(ucode_dwords_[instr_offset * 3] & 0x1F);
          if (fetch_opcode == FetchOpcode::kVertexFetch) {
            assert_true(is_vertex_shader());
            GatherVertexFetchInformation(
                *reinterpret_cast<const VertexFetchInstruction*>(
                    ucode_dwords_ + instr_offset * 3));
          } else {
            GatherTextureFetchInformation(
                *reinterpret_cast<const TextureFetchInstruction*>(
                    ucode_dwords_ + instr_offset * 3));
          }
        } else {
          // Gather up color targets written to, and check if using dynamic
          // register indices.
          auto& op = *reinterpret_cast<const AluInstruction*>(ucode_dwords_ +
                                                              instr_offset * 3);
          if (op.has_vector_op()) {
            const auto& opcode_info =
                alu_vector_opcode_infos_[static_cast<int>(op.vector_opcode())];
            early_z_allowed_ &= !opcode_info.disable_early_z;
            for (size_t i = 0; i < opcode_info.argument_count; ++i) {
              if (op.src_is_temp(i + 1) && (op.src_reg(i + 1) & 0x40)) {
                uses_register_dynamic_addressing_ = true;
              }
            }
            if (op.is_export()) {
              if (is_pixel_shader()) {
                if (op.vector_dest() <= 3) {
                  writes_color_targets_[op.vector_dest()] = true;
                } else if (op.vector_dest() == 61) {
                  writes_depth_ = true;
                  early_z_allowed_ = false;
                }
              }
              if (memexport_alloc_count_ > 0 &&
                  memexport_alloc_count_ <= kMaxMemExports) {
                // Store used memexport constants because CPU code needs
                // addresses and sizes, and also whether there have been writes
                // to eA and eM# for register allocation in shader translator
                // implementations.
                // eA is (hopefully) always written to using:
                // mad eA, r#, const0100, c#
                // (though there are some exceptions, shaders in Halo 3 for some
                // reason set eA to zeros, but the swizzle of the constant is
                // not .xyzw in this case, and they don't write to eM#).
                uint32_t memexport_alloc_index = memexport_alloc_count_ - 1;
                if (op.vector_dest() == 32 &&
                    op.vector_opcode() == AluVectorOpcode::kMad &&
                    op.vector_write_mask() == 0b1111 && !op.src_is_temp(3) &&
                    op.src_swizzle(3) == 0) {
                  memexport_eA_written_ |= 1u << memexport_alloc_index;
                  memexport_stream_constants_.insert(op.src_reg(3));
                } else if (op.vector_dest() >= 33 && op.vector_dest() <= 37) {
                  if (memexport_eA_written_ & (1u << memexport_alloc_index)) {
                    memexport_eM_written_[memexport_alloc_index] |=
                        1 << (op.vector_dest() - 33);
                  }
                }
              }
            } else {
              if (op.is_vector_dest_relative()) {
                uses_register_dynamic_addressing_ = true;
              }
            }
          }
          if (op.has_scalar_op()) {
            const auto& opcode_info =
                alu_scalar_opcode_infos_[static_cast<int>(op.scalar_opcode())];
            early_z_allowed_ &= !opcode_info.disable_early_z;
            if (opcode_info.argument_count == 1 && op.src_is_temp(3) &&
                (op.src_reg(3) & 0x40)) {
              uses_register_dynamic_addressing_ = true;
            }
            if (op.is_export()) {
              if (is_pixel_shader()) {
                if (op.scalar_dest() <= 3) {
                  writes_color_targets_[op.scalar_dest()] = true;
                } else if (op.scalar_dest() == 61) {
                  writes_depth_ = true;
                  early_z_allowed_ = false;
                }
              }
              if (memexport_alloc_count_ > 0 &&
                  memexport_alloc_count_ <= kMaxMemExports &&
                  op.scalar_dest() >= 33 && op.scalar_dest() <= 37) {
                uint32_t memexport_alloc_index = memexport_alloc_count_ - 1;
                if (memexport_eA_written_ & (1u << memexport_alloc_index)) {
                  memexport_eM_written_[memexport_alloc_index] |=
                      1 << (op.scalar_dest() - 33);
                }
              }
            } else {
              if (op.is_scalar_dest_relative()) {
                uses_register_dynamic_addressing_ = true;
              }
            }
          }
        }
      }
    } break;
    case ControlFlowOpcode::kAlloc:
      if (cf.alloc.alloc_type() == AllocType::kMemory) {
        ++memexport_alloc_count_;
      }
      break;
    default:
      break;
  }
}

void ShaderTranslator::GatherVertexFetchInformation(
    const VertexFetchInstruction& op) {
  ParsedVertexFetchInstruction fetch_instr;
  ParseVertexFetchInstruction(op, &fetch_instr);

  // Don't bother setting up a binding for an instruction that fetches nothing.
  if (!op.fetches_any_data()) {
    return;
  }

  // Check if using dynamic register indices.
  if (op.is_dest_relative() || op.is_src_relative()) {
    uses_register_dynamic_addressing_ = true;
  }

  // Try to allocate an attribute on an existing binding.
  // If no binding for this fetch slot is found create it.
  using VertexBinding = Shader::VertexBinding;
  VertexBinding::Attribute* attrib = nullptr;
  for (auto& vertex_binding : vertex_bindings_) {
    if (vertex_binding.fetch_constant == op.fetch_constant_index()) {
      // It may not hold that all strides are equal, but I hope it does.
      assert_true(!fetch_instr.attributes.stride ||
                  vertex_binding.stride_words == fetch_instr.attributes.stride);
      vertex_binding.attributes.push_back({});
      attrib = &vertex_binding.attributes.back();
      break;
    }
  }
  if (!attrib) {
    assert_not_zero(fetch_instr.attributes.stride);
    VertexBinding vertex_binding;
    vertex_binding.binding_index = int(vertex_bindings_.size());
    vertex_binding.fetch_constant = op.fetch_constant_index();
    vertex_binding.stride_words = fetch_instr.attributes.stride;
    vertex_binding.attributes.push_back({});
    vertex_bindings_.emplace_back(std::move(vertex_binding));
    attrib = &vertex_bindings_.back().attributes.back();
  }

  // Populate attribute.
  attrib->attrib_index = total_attrib_count_++;
  attrib->fetch_instr = fetch_instr;
  attrib->size_words =
      GetVertexFormatSizeInWords(attrib->fetch_instr.attributes.data_format);
}

void ShaderTranslator::GatherTextureFetchInformation(
    const TextureFetchInstruction& op) {
  // Check if using dynamic register indices.
  if (op.is_dest_relative() || op.is_src_relative()) {
    uses_register_dynamic_addressing_ = true;
  }

  switch (op.opcode()) {
    case FetchOpcode::kSetTextureLod:
    case FetchOpcode::kSetTextureGradientsHorz:
    case FetchOpcode::kSetTextureGradientsVert:
      // Doesn't use bindings.
      return;
    default:
      // Continue.
      break;
  }
  Shader::TextureBinding binding;
  binding.binding_index = -1;
  ParseTextureFetchInstruction(op, &binding.fetch_instr);
  binding.fetch_constant = binding.fetch_instr.operands[1].storage_index;

  // Check and see if this fetch constant was previously used...
  for (auto& tex_binding : texture_bindings_) {
    if (tex_binding.fetch_constant == binding.fetch_constant) {
      binding.binding_index = tex_binding.binding_index;
      break;
    }
  }

  if (binding.binding_index == -1) {
    // Assign a unique binding index.
    binding.binding_index = unique_texture_bindings_++;
  }

  texture_bindings_.emplace_back(std::move(binding));
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
    default:
      // Ignored.
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
  std::vector<ControlFlowInstruction> cf_instructions;
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

    cf_instructions.push_back(cf_a);
    cf_instructions.push_back(cf_b);
  }

  PreProcessControlFlowInstructions(cf_instructions);

  // Translate all instructions.
  for (uint32_t i = 0, cf_index = 0; i < max_cf_dword_index; i += 3) {
    ControlFlowInstruction cf_a;
    ControlFlowInstruction cf_b;
    UnpackControlFlowInstructions(ucode_dwords_ + i, &cf_a, &cf_b);

    cf_index_ = cf_index;
    MarkUcodeInstruction(i);
    if (label_addresses.count(cf_index)) {
      AppendUcodeDisasmFormat("                label L%u\n", cf_index);
      ProcessLabel(cf_index);
    }
    AppendUcodeDisasmFormat("/* %4u.0 */ ", cf_index / 2);
    ProcessControlFlowInstructionBegin(cf_index);
    TranslateControlFlowInstruction(cf_a);
    ProcessControlFlowInstructionEnd(cf_index);
    ++cf_index;

    cf_index_ = cf_index;
    MarkUcodeInstruction(i);
    if (label_addresses.count(cf_index)) {
      AppendUcodeDisasmFormat("                label L%u\n", cf_index);
      ProcessLabel(cf_index);
    }
    AppendUcodeDisasmFormat("/* %4u.1 */ ", cf_index / 2);
    ProcessControlFlowInstructionBegin(cf_index);
    TranslateControlFlowInstruction(cf_b);
    ProcessControlFlowInstructionEnd(cf_index);
    ++cf_index;
  }

  return true;
}

std::vector<uint8_t> UcodeShaderTranslator::CompleteTranslation() {
  return ucode_disasm_buffer().ToBytes();
}

void ShaderTranslator::TranslateControlFlowInstruction(
    const ControlFlowInstruction& cf) {
  switch (cf.opcode()) {
    case ControlFlowOpcode::kNop:
      TranslateControlFlowNop(cf);
      break;
    case ControlFlowOpcode::kExec:
      TranslateControlFlowExec(cf.exec);
      break;
    case ControlFlowOpcode::kExecEnd:
      TranslateControlFlowExec(cf.exec);
      break;
    case ControlFlowOpcode::kCondExec:
      TranslateControlFlowCondExec(cf.cond_exec);
      break;
    case ControlFlowOpcode::kCondExecEnd:
      TranslateControlFlowCondExec(cf.cond_exec);
      break;
    case ControlFlowOpcode::kCondExecPred:
      TranslateControlFlowCondExecPred(cf.cond_exec_pred);
      break;
    case ControlFlowOpcode::kCondExecPredEnd:
      TranslateControlFlowCondExecPred(cf.cond_exec_pred);
      break;
    case ControlFlowOpcode::kCondExecPredClean:
      TranslateControlFlowCondExec(cf.cond_exec);
      break;
    case ControlFlowOpcode::kCondExecPredCleanEnd:
      TranslateControlFlowCondExec(cf.cond_exec);
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
  bool ends_shader = DoesControlFlowOpcodeEndShader(cf.opcode());
  if (ends_shader) {
    // TODO(benvanik): return?
  }
}

void ShaderTranslator::TranslateControlFlowNop(
    const ControlFlowInstruction& cf) {
  ucode_disasm_buffer_.Append("      cnop\n");

  ProcessControlFlowNopInstruction(cf_index_);
}

void ShaderTranslator::TranslateControlFlowExec(
    const ControlFlowExecInstruction& cf) {
  ParsedExecInstruction i;
  i.dword_index = cf_index_;
  i.opcode = cf.opcode();
  i.opcode_name = cf.opcode() == ControlFlowOpcode::kExecEnd ? "exece" : "exec";
  i.instruction_address = cf.address();
  i.instruction_count = cf.count();
  i.type = ParsedExecInstruction::Type::kUnconditional;
  i.is_end = cf.opcode() == ControlFlowOpcode::kExecEnd;
  i.clean = cf.clean();
  i.is_yield = cf.is_yield();
  i.sequence = cf.sequence();

  TranslateExecInstructions(i);
}

void ShaderTranslator::TranslateControlFlowCondExec(
    const ControlFlowCondExecInstruction& cf) {
  ParsedExecInstruction i;
  i.dword_index = cf_index_;
  i.opcode = cf.opcode();
  i.opcode_name = "cexec";
  switch (cf.opcode()) {
    case ControlFlowOpcode::kCondExecEnd:
    case ControlFlowOpcode::kCondExecPredCleanEnd:
      i.opcode_name = "cexece";
      i.is_end = true;
      break;
    default:
      break;
  }
  i.instruction_address = cf.address();
  i.instruction_count = cf.count();
  i.type = ParsedExecInstruction::Type::kConditional;
  i.bool_constant_index = cf.bool_address();
  constant_register_map_.bool_bitmap[i.bool_constant_index / 32] |=
      1 << (i.bool_constant_index % 32);
  i.condition = cf.condition();
  switch (cf.opcode()) {
    case ControlFlowOpcode::kCondExec:
    case ControlFlowOpcode::kCondExecEnd:
      i.clean = false;
      break;
    default:
      break;
  }
  i.is_yield = cf.is_yield();
  i.sequence = cf.sequence();

  TranslateExecInstructions(i);
}

void ShaderTranslator::TranslateControlFlowCondExecPred(
    const ControlFlowCondExecPredInstruction& cf) {
  ParsedExecInstruction i;
  i.dword_index = cf_index_;
  i.opcode = cf.opcode();
  i.opcode_name =
      cf.opcode() == ControlFlowOpcode::kCondExecPredEnd ? "exece" : "exec";
  i.instruction_address = cf.address();
  i.instruction_count = cf.count();
  i.type = ParsedExecInstruction::Type::kPredicated;
  i.condition = cf.condition();
  i.is_end = cf.opcode() == ControlFlowOpcode::kCondExecPredEnd;
  i.clean = cf.clean();
  i.is_yield = cf.is_yield();
  i.sequence = cf.sequence();

  TranslateExecInstructions(i);
}

void ShaderTranslator::TranslateControlFlowLoopStart(
    const ControlFlowLoopStartInstruction& cf) {
  ParsedLoopStartInstruction i;
  i.dword_index = cf_index_;
  i.loop_constant_index = cf.loop_id();
  constant_register_map_.int_bitmap |= 1 << i.loop_constant_index;
  i.is_repeat = cf.is_repeat();
  i.loop_skip_address = cf.address();

  i.Disassemble(&ucode_disasm_buffer_);

  ProcessLoopStartInstruction(i);
}

void ShaderTranslator::TranslateControlFlowLoopEnd(
    const ControlFlowLoopEndInstruction& cf) {
  ParsedLoopEndInstruction i;
  i.dword_index = cf_index_;
  i.is_predicated_break = cf.is_predicated_break();
  i.predicate_condition = cf.condition();
  i.loop_constant_index = cf.loop_id();
  constant_register_map_.int_bitmap |= 1 << i.loop_constant_index;
  i.loop_body_address = cf.address();

  i.Disassemble(&ucode_disasm_buffer_);

  ProcessLoopEndInstruction(i);
}

void ShaderTranslator::TranslateControlFlowCondCall(
    const ControlFlowCondCallInstruction& cf) {
  ParsedCallInstruction i;
  i.dword_index = cf_index_;
  i.target_address = cf.address();
  if (cf.is_unconditional()) {
    i.type = ParsedCallInstruction::Type::kUnconditional;
  } else if (cf.is_predicated()) {
    i.type = ParsedCallInstruction::Type::kPredicated;
    i.condition = cf.condition();
  } else {
    i.type = ParsedCallInstruction::Type::kConditional;
    i.bool_constant_index = cf.bool_address();
    constant_register_map_.bool_bitmap[i.bool_constant_index / 32] |=
        1 << (i.bool_constant_index % 32);
    i.condition = cf.condition();
  }

  i.Disassemble(&ucode_disasm_buffer_);

  ProcessCallInstruction(i);
}

void ShaderTranslator::TranslateControlFlowReturn(
    const ControlFlowReturnInstruction& cf) {
  ParsedReturnInstruction i;
  i.dword_index = cf_index_;

  i.Disassemble(&ucode_disasm_buffer_);

  ProcessReturnInstruction(i);
}

void ShaderTranslator::TranslateControlFlowCondJmp(
    const ControlFlowCondJmpInstruction& cf) {
  ParsedJumpInstruction i;
  i.dword_index = cf_index_;
  i.target_address = cf.address();
  if (cf.is_unconditional()) {
    i.type = ParsedJumpInstruction::Type::kUnconditional;
  } else if (cf.is_predicated()) {
    i.type = ParsedJumpInstruction::Type::kPredicated;
    i.condition = cf.condition();
  } else {
    i.type = ParsedJumpInstruction::Type::kConditional;
    i.bool_constant_index = cf.bool_address();
    constant_register_map_.bool_bitmap[i.bool_constant_index / 32] |=
        1 << (i.bool_constant_index % 32);
    i.condition = cf.condition();
  }

  i.Disassemble(&ucode_disasm_buffer_);

  ProcessJumpInstruction(i);
}

void ShaderTranslator::TranslateControlFlowAlloc(
    const ControlFlowAllocInstruction& cf) {
  ParsedAllocInstruction i;
  i.dword_index = cf_index_;
  i.type = cf.alloc_type();
  i.count = cf.size();
  i.is_vertex_shader = is_vertex_shader();

  i.Disassemble(&ucode_disasm_buffer_);

  ProcessAllocInstruction(i);
}

void ShaderTranslator::TranslateExecInstructions(
    const ParsedExecInstruction& instr) {
  instr.Disassemble(&ucode_disasm_buffer_);

  ProcessExecInstructionBegin(instr);

  uint32_t sequence = instr.sequence;
  for (uint32_t instr_offset = instr.instruction_address;
       instr_offset < instr.instruction_address + instr.instruction_count;
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
        auto& op = *reinterpret_cast<const VertexFetchInstruction*>(
            ucode_dwords_ + instr_offset * 3);
        TranslateVertexFetchInstruction(op);
      } else {
        auto& op = *reinterpret_cast<const TextureFetchInstruction*>(
            ucode_dwords_ + instr_offset * 3);
        TranslateTextureFetchInstruction(op);
      }
    } else {
      auto& op = *reinterpret_cast<const AluInstruction*>(ucode_dwords_ +
                                                          instr_offset * 3);
      TranslateAluInstruction(op);
    }
  }

  ProcessExecInstructionEnd(instr);
}

void ParseFetchInstructionResult(uint32_t dest, uint32_t swizzle,
                                 bool is_relative,
                                 InstructionResult* out_result) {
  out_result->storage_target = InstructionStorageTarget::kRegister;
  out_result->storage_index = dest;
  out_result->is_export = false;
  out_result->is_clamped = false;
  out_result->storage_addressing_mode =
      is_relative ? InstructionStorageAddressingMode::kAddressRelative
                  : InstructionStorageAddressingMode::kStatic;
  for (int i = 0; i < 4; ++i) {
    out_result->write_mask[i] = true;
    if ((swizzle & 0x7) == 4) {
      out_result->components[i] = SwizzleSource::k0;
    } else if ((swizzle & 0x7) == 5) {
      out_result->components[i] = SwizzleSource::k1;
    } else if ((swizzle & 0x7) == 6) {
      out_result->components[i] = SwizzleSource::k0;
    } else if ((swizzle & 0x7) == 7) {
      out_result->write_mask[i] = false;
    } else {
      out_result->components[i] = GetSwizzleFromComponentIndex(swizzle & 0x3);
    }
    swizzle >>= 3;
  }
}

void ShaderTranslator::TranslateVertexFetchInstruction(
    const VertexFetchInstruction& op) {
  ParsedVertexFetchInstruction instr;
  ParseVertexFetchInstruction(op, &instr);
  instr.Disassemble(&ucode_disasm_buffer_);
  ProcessVertexFetchInstruction(instr);
}

void ShaderTranslator::ParseVertexFetchInstruction(
    const VertexFetchInstruction& op, ParsedVertexFetchInstruction* out_instr) {
  auto& i = *out_instr;
  i.dword_index = 0;
  i.opcode = FetchOpcode::kVertexFetch;
  i.opcode_name = op.is_mini_fetch() ? "vfetch_mini" : "vfetch_full";
  i.is_mini_fetch = op.is_mini_fetch();
  i.is_predicated = op.is_predicated();
  i.predicate_condition = op.predicate_condition();

  ParseFetchInstructionResult(op.dest(), op.dest_swizzle(),
                              op.is_dest_relative(), &i.result);

  // Reuse previous vfetch_full if this is a mini.
  const auto& full_op = op.is_mini_fetch() ? previous_vfetch_full_ : op;
  auto& src_op = i.operands[i.operand_count++];
  src_op.storage_source = InstructionStorageSource::kRegister;
  src_op.storage_index = full_op.src();
  src_op.storage_addressing_mode =
      full_op.is_src_relative()
          ? InstructionStorageAddressingMode::kAddressRelative
          : InstructionStorageAddressingMode::kStatic;
  src_op.is_negated = false;
  src_op.is_absolute_value = false;
  src_op.component_count = 1;
  uint32_t swizzle = full_op.src_swizzle();
  for (int j = 0; j < src_op.component_count; ++j, swizzle >>= 2) {
    src_op.components[j] = GetSwizzleFromComponentIndex(swizzle & 0x3);
  }

  auto& const_op = i.operands[i.operand_count++];
  const_op.storage_source = InstructionStorageSource::kVertexFetchConstant;
  const_op.storage_index = full_op.fetch_constant_index();

  i.attributes.data_format = op.data_format();
  i.attributes.offset = op.offset();
  i.attributes.stride = full_op.stride();
  i.attributes.exp_adjust = op.exp_adjust();
  i.attributes.is_index_rounded = op.is_index_rounded();
  i.attributes.is_signed = op.is_signed();
  i.attributes.is_integer = !op.is_normalized();
  i.attributes.prefetch_count = op.prefetch_count();

  // Store for later use by mini fetches.
  if (!op.is_mini_fetch()) {
    previous_vfetch_full_ = op;
  }
}

void ShaderTranslator::TranslateTextureFetchInstruction(
    const TextureFetchInstruction& op) {
  ParsedTextureFetchInstruction instr;
  ParseTextureFetchInstruction(op, &instr);
  instr.Disassemble(&ucode_disasm_buffer_);
  ProcessTextureFetchInstruction(instr);
}

void ShaderTranslator::ParseTextureFetchInstruction(
    const TextureFetchInstruction& op,
    ParsedTextureFetchInstruction* out_instr) {
  struct TextureFetchOpcodeInfo {
    const char* name;
    bool has_dest;
    bool has_const;
    bool has_attributes;
    int override_component_count;
  } opcode_info;
  switch (op.opcode()) {
    case FetchOpcode::kTextureFetch: {
      static const char* kNames[] = {"tfetch1D", "tfetch2D", "tfetch3D",
                                     "tfetchCube"};
      opcode_info = {kNames[static_cast<int>(op.dimension())], true, true, true,
                     0};
    } break;
    case FetchOpcode::kGetTextureBorderColorFrac: {
      static const char* kNames[] = {"getBCF1D", "getBCF2D", "getBCF3D",
                                     "getBCFCube"};
      opcode_info = {kNames[static_cast<int>(op.dimension())], true, true, true,
                     0};
    } break;
    case FetchOpcode::kGetTextureComputedLod: {
      static const char* kNames[] = {"getCompTexLOD1D", "getCompTexLOD2D",
                                     "getCompTexLOD3D", "getCompTexLODCube"};
      opcode_info = {kNames[static_cast<int>(op.dimension())], true, true, true,
                     0};
    } break;
    case FetchOpcode::kGetTextureGradients:
      opcode_info = {"getGradients", true, true, true, 2};
      break;
    case FetchOpcode::kGetTextureWeights: {
      static const char* kNames[] = {"getWeights1D", "getWeights2D",
                                     "getWeights3D", "getWeightsCube"};
      opcode_info = {kNames[static_cast<int>(op.dimension())], true, true, true,
                     0};
    } break;
    case FetchOpcode::kSetTextureLod:
      opcode_info = {"setTexLOD", false, false, false, 1};
      break;
    case FetchOpcode::kSetTextureGradientsHorz:
      opcode_info = {"setGradientH", false, false, false, 3};
      break;
    case FetchOpcode::kSetTextureGradientsVert:
      opcode_info = {"setGradientV", false, false, false, 3};
      break;
    default:
    case FetchOpcode::kUnknownTextureOp:
      assert_unhandled_case(fetch_opcode);
      return;
  }

  auto& i = *out_instr;
  i.dword_index = 0;
  i.opcode = op.opcode();
  i.opcode_name = opcode_info.name;
  i.dimension = op.dimension();
  i.is_predicated = op.is_predicated();
  i.predicate_condition = op.predicate_condition();

  if (opcode_info.has_dest) {
    ParseFetchInstructionResult(op.dest(), op.dest_swizzle(),
                                op.is_dest_relative(), &i.result);
  } else {
    i.result.storage_target = InstructionStorageTarget::kNone;
  }

  auto& src_op = i.operands[i.operand_count++];
  src_op.storage_source = InstructionStorageSource::kRegister;
  src_op.storage_index = op.src();
  src_op.storage_addressing_mode =
      op.is_src_relative() ? InstructionStorageAddressingMode::kAddressRelative
                           : InstructionStorageAddressingMode::kStatic;
  src_op.is_negated = false;
  src_op.is_absolute_value = false;
  src_op.component_count =
      opcode_info.override_component_count
          ? opcode_info.override_component_count
          : GetTextureDimensionComponentCount(op.dimension());
  uint32_t swizzle = op.src_swizzle();
  for (int j = 0; j < src_op.component_count; ++j, swizzle >>= 2) {
    src_op.components[j] = GetSwizzleFromComponentIndex(swizzle & 0x3);
  }

  if (opcode_info.has_const) {
    auto& const_op = i.operands[i.operand_count++];
    const_op.storage_source = InstructionStorageSource::kTextureFetchConstant;
    const_op.storage_index = op.fetch_constant_index();
  }

  if (opcode_info.has_attributes) {
    i.attributes.fetch_valid_only = op.fetch_valid_only();
    i.attributes.unnormalized_coordinates = op.unnormalized_coordinates();
    i.attributes.mag_filter = op.mag_filter();
    i.attributes.min_filter = op.min_filter();
    i.attributes.mip_filter = op.mip_filter();
    i.attributes.aniso_filter = op.aniso_filter();
    i.attributes.use_computed_lod = op.use_computed_lod();
    i.attributes.use_register_lod = op.use_register_lod();
    i.attributes.use_register_gradients = op.use_register_gradients();
    i.attributes.lod_bias = op.lod_bias();
    i.attributes.offset_x = op.offset_x();
    i.attributes.offset_y = op.offset_y();
    i.attributes.offset_z = op.offset_z();
  }
}

const ShaderTranslator::AluOpcodeInfo
    ShaderTranslator::alu_vector_opcode_infos_[0x20] = {
        {"add", 2, 4, false},           // 0
        {"mul", 2, 4, false},           // 1
        {"max", 2, 4, false},           // 2
        {"min", 2, 4, false},           // 3
        {"seq", 2, 4, false},           // 4
        {"sgt", 2, 4, false},           // 5
        {"sge", 2, 4, false},           // 6
        {"sne", 2, 4, false},           // 7
        {"frc", 1, 4, false},           // 8
        {"trunc", 1, 4, false},         // 9
        {"floor", 1, 4, false},         // 10
        {"mad", 3, 4, false},           // 11
        {"cndeq", 3, 4, false},         // 12
        {"cndge", 3, 4, false},         // 13
        {"cndgt", 3, 4, false},         // 14
        {"dp4", 2, 4, false},           // 15
        {"dp3", 2, 4, false},           // 16
        {"dp2add", 3, 4, false},        // 17
        {"cube", 2, 4, false},          // 18
        {"max4", 1, 4, false},          // 19
        {"setp_eq_push", 2, 4, false},  // 20
        {"setp_ne_push", 2, 4, false},  // 21
        {"setp_gt_push", 2, 4, false},  // 22
        {"setp_ge_push", 2, 4, false},  // 23
        {"kill_eq", 2, 4, true},        // 24
        {"kill_gt", 2, 4, true},        // 25
        {"kill_ge", 2, 4, true},        // 26
        {"kill_ne", 2, 4, true},        // 27
        {"dst", 2, 4, false},           // 28
        {"maxa", 2, 4, false},          // 29
};

const ShaderTranslator::AluOpcodeInfo
    ShaderTranslator::alu_scalar_opcode_infos_[0x40] = {
        {"adds", 1, 2, false},         // 0
        {"adds_prev", 1, 1, false},    // 1
        {"muls", 1, 2, false},         // 2
        {"muls_prev", 1, 1, false},    // 3
        {"muls_prev2", 1, 2, false},   // 4
        {"maxs", 1, 2, false},         // 5
        {"mins", 1, 2, false},         // 6
        {"seqs", 1, 1, false},         // 7
        {"sgts", 1, 1, false},         // 8
        {"sges", 1, 1, false},         // 9
        {"snes", 1, 1, false},         // 10
        {"frcs", 1, 1, false},         // 11
        {"truncs", 1, 1, false},       // 12
        {"floors", 1, 1, false},       // 13
        {"exp", 1, 1, false},          // 14
        {"logc", 1, 1, false},         // 15
        {"log", 1, 1, false},          // 16
        {"rcpc", 1, 1, false},         // 17
        {"rcpf", 1, 1, false},         // 18
        {"rcp", 1, 1, false},          // 19
        {"rsqc", 1, 1, false},         // 20
        {"rsqf", 1, 1, false},         // 21
        {"rsq", 1, 1, false},          // 22
        {"maxas", 1, 2, false},        // 23
        {"maxasf", 1, 2, false},       // 24
        {"subs", 1, 2, false},         // 25
        {"subs_prev", 1, 1, false},    // 26
        {"setp_eq", 1, 1, false},      // 27
        {"setp_ne", 1, 1, false},      // 28
        {"setp_gt", 1, 1, false},      // 29
        {"setp_ge", 1, 1, false},      // 30
        {"setp_inv", 1, 1, false},     // 31
        {"setp_pop", 1, 1, false},     // 32
        {"setp_clr", 1, 1, false},     // 33
        {"setp_rstr", 1, 1, false},    // 34
        {"kills_eq", 1, 1, true},      // 35
        {"kills_gt", 1, 1, true},      // 36
        {"kills_ge", 1, 1, true},      // 37
        {"kills_ne", 1, 1, true},      // 38
        {"kills_one", 1, 1, true},     // 39
        {"sqrt", 1, 1, false},         // 40
        {"UNKNOWN", 0, 0, false},      // 41
        {"mulsc", 2, 1, false},        // 42
        {"mulsc", 2, 1, false},        // 43
        {"addsc", 2, 1, false},        // 44
        {"addsc", 2, 1, false},        // 45
        {"subsc", 2, 1, false},        // 46
        {"subsc", 2, 1, false},        // 47
        {"sin", 1, 1, false},          // 48
        {"cos", 1, 1, false},          // 49
        {"retain_prev", 1, 1, false},  // 50
};

void ShaderTranslator::TranslateAluInstruction(const AluInstruction& op) {
  if (!op.has_vector_op() && !op.has_scalar_op()) {
    ParsedAluInstruction instr;
    instr.type = ParsedAluInstruction::Type::kNop;
    instr.Disassemble(&ucode_disasm_buffer_);
    ProcessAluInstruction(instr);
    return;
  }

  ParsedAluInstruction instr;
  if (op.has_vector_op()) {
    const auto& opcode_info =
        alu_vector_opcode_infos_[static_cast<int>(op.vector_opcode())];
    ParseAluVectorInstruction(op, opcode_info, instr);
    ProcessAluInstruction(instr);
  }

  if (op.has_scalar_op()) {
    const auto& opcode_info =
        alu_scalar_opcode_infos_[static_cast<int>(op.scalar_opcode())];
    ParseAluScalarInstruction(op, opcode_info, instr);
    ProcessAluInstruction(instr);
  }
}

void ParseAluInstructionOperand(const AluInstruction& op, int i,
                                int swizzle_component_count,
                                InstructionOperand* out_op) {
  int const_slot = 0;
  switch (i) {
    case 2:
      const_slot = op.src_is_temp(1) ? 0 : 1;
      break;
    case 3:
      const_slot = op.src_is_temp(1) && op.src_is_temp(2) ? 0 : 1;
      break;
  }
  out_op->is_negated = op.src_negate(i);
  uint32_t reg = op.src_reg(i);
  if (op.src_is_temp(i)) {
    out_op->storage_source = InstructionStorageSource::kRegister;
    out_op->storage_index = reg & 0x1F;
    out_op->is_absolute_value = (reg & 0x80) == 0x80;
    out_op->storage_addressing_mode =
        (reg & 0x40) ? InstructionStorageAddressingMode::kAddressRelative
                     : InstructionStorageAddressingMode::kStatic;
  } else {
    out_op->storage_source = InstructionStorageSource::kConstantFloat;
    out_op->storage_index = reg;
    if ((const_slot == 0 && op.is_const_0_addressed()) ||
        (const_slot == 1 && op.is_const_1_addressed())) {
      if (op.is_address_relative()) {
        out_op->storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressAbsolute;
      } else {
        out_op->storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressRelative;
      }
    } else {
      out_op->storage_addressing_mode =
          InstructionStorageAddressingMode::kStatic;
    }
    out_op->is_absolute_value = op.abs_constants();
  }
  out_op->component_count = swizzle_component_count;
  uint32_t swizzle = op.src_swizzle(i);
  if (swizzle_component_count == 1) {
    uint32_t a = ((swizzle >> 6) + 3) & 0x3;
    out_op->components[0] = GetSwizzleFromComponentIndex(a);
  } else if (swizzle_component_count == 2) {
    uint32_t a = ((swizzle >> 6) + 3) & 0x3;
    uint32_t b = ((swizzle >> 0) + 0) & 0x3;
    out_op->components[0] = GetSwizzleFromComponentIndex(a);
    out_op->components[1] = GetSwizzleFromComponentIndex(b);
  } else if (swizzle_component_count == 3) {
    assert_always();
  } else if (swizzle_component_count == 4) {
    for (int j = 0; j < swizzle_component_count; ++j, swizzle >>= 2) {
      out_op->components[j] = GetSwizzleFromComponentIndex((swizzle + j) & 0x3);
    }
  }
}

void ParseAluInstructionOperandSpecial(const AluInstruction& op,
                                       InstructionStorageSource storage_source,
                                       uint32_t reg, bool negate,
                                       int const_slot, uint32_t swizzle,
                                       InstructionOperand* out_op) {
  out_op->is_negated = negate;
  out_op->is_absolute_value = op.abs_constants();
  out_op->storage_source = storage_source;
  if (storage_source == InstructionStorageSource::kRegister) {
    out_op->storage_index = reg & 0x7F;
    out_op->storage_addressing_mode = InstructionStorageAddressingMode::kStatic;
  } else {
    out_op->storage_index = reg;
    if ((const_slot == 0 && op.is_const_0_addressed()) ||
        (const_slot == 1 && op.is_const_1_addressed())) {
      if (op.is_address_relative()) {
        out_op->storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressAbsolute;
      } else {
        out_op->storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressRelative;
      }
    } else {
      out_op->storage_addressing_mode =
          InstructionStorageAddressingMode::kStatic;
    }
  }
  out_op->component_count = 1;
  uint32_t a = swizzle & 0x3;
  out_op->components[0] = GetSwizzleFromComponentIndex(a);
}

void ShaderTranslator::ParseAluVectorInstruction(
    const AluInstruction& op, const AluOpcodeInfo& opcode_info,
    ParsedAluInstruction& i) {
  i.dword_index = 0;
  i.type = ParsedAluInstruction::Type::kVector;
  i.vector_opcode = op.vector_opcode();
  i.opcode_name = opcode_info.name;
  i.is_paired = op.has_scalar_op();
  i.is_predicated = op.is_predicated();
  i.predicate_condition = op.predicate_condition();

  i.result.is_export = op.is_export();
  i.result.is_clamped = op.vector_clamp();
  i.result.storage_target = InstructionStorageTarget::kRegister;
  i.result.storage_index = 0;
  uint32_t dest_num = op.vector_dest();
  if (!op.is_export()) {
    assert_true(dest_num < 32);
    i.result.storage_target = InstructionStorageTarget::kRegister;
    i.result.storage_index = dest_num;
    i.result.storage_addressing_mode =
        op.is_vector_dest_relative()
            ? InstructionStorageAddressingMode::kAddressRelative
            : InstructionStorageAddressingMode::kStatic;
  } else if (is_vertex_shader()) {
    switch (dest_num) {
      case 32:
        i.result.storage_target = InstructionStorageTarget::kExportAddress;
        break;
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        i.result.storage_index = dest_num - 33;
        i.result.storage_target = InstructionStorageTarget::kExportData;
        break;
      case 62:
        i.result.storage_target = InstructionStorageTarget::kPosition;
        break;
      case 63:
        i.result.storage_target = InstructionStorageTarget::kPointSize;
        break;
      default:
        if (dest_num < 16) {
          i.result.storage_target = InstructionStorageTarget::kInterpolant;
          i.result.storage_index = dest_num;
        } else {
          // Unimplemented.
          // assert_always();
          XELOGE(
              "ShaderTranslator::ParseAluVectorInstruction: Unsupported write "
              "to export %d",
              dest_num);
          i.result.storage_target = InstructionStorageTarget::kNone;
          i.result.storage_index = 0;
        }
        break;
    }
  } else if (is_pixel_shader()) {
    switch (dest_num) {
      case 0:
      case 63:  // ? masked?
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 0;
        break;
      case 1:
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 1;
        break;
      case 2:
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 2;
        break;
      case 3:
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 3;
        break;
      case 32:
        i.result.storage_target = InstructionStorageTarget::kExportAddress;
        break;
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        i.result.storage_index = dest_num - 33;
        i.result.storage_target = InstructionStorageTarget::kExportData;
        break;
      case 61:
        i.result.storage_target = InstructionStorageTarget::kDepth;
        break;
      default:
        XELOGE(
            "ShaderTranslator::ParseAluVectorInstruction: Unsupported write "
            "to export %d",
            dest_num);
        i.result.storage_target = InstructionStorageTarget::kNone;
        i.result.storage_index = 0;
    }
  }
  if (op.is_export()) {
    uint32_t write_mask = op.vector_write_mask();
    uint32_t const_1_mask = op.scalar_write_mask();
    if (!write_mask) {
      for (int j = 0; j < 4; ++j) {
        i.result.write_mask[j] = false;
      }
    } else {
      for (int j = 0; j < 4; ++j, write_mask >>= 1, const_1_mask >>= 1) {
        i.result.write_mask[j] = true;
        if (write_mask & 0x1) {
          if (const_1_mask & 0x1) {
            i.result.components[j] = SwizzleSource::k1;
          } else {
            i.result.components[j] = GetSwizzleFromComponentIndex(j);
          }
        } else {
          if (op.is_scalar_dest_relative()) {
            i.result.components[j] = SwizzleSource::k0;
          } else {
            i.result.write_mask[j] = false;
          }
        }
      }
    }
  } else {
    uint32_t write_mask = op.vector_write_mask();
    for (int j = 0; j < 4; ++j, write_mask >>= 1) {
      i.result.write_mask[j] = (write_mask & 0x1) == 0x1;
      i.result.components[j] = GetSwizzleFromComponentIndex(j);
    }
  }

  i.operand_count = opcode_info.argument_count;
  for (int j = 0; j < i.operand_count; ++j) {
    ParseAluInstructionOperand(
        op, j + 1, opcode_info.src_swizzle_component_count, &i.operands[j]);

    // Track constant float register loads.
    if (i.operands[j].storage_source ==
        InstructionStorageSource::kConstantFloat) {
      if (i.operands[j].storage_addressing_mode !=
          InstructionStorageAddressingMode::kStatic) {
        // Dynamic addressing makes all constants required.
        std::memset(constant_register_map_.float_bitmap, 0xFF,
                    sizeof(constant_register_map_.float_bitmap));
      } else {
        auto register_index = i.operands[j].storage_index;
        constant_register_map_.float_bitmap[register_index / 64] |=
            1ull << (register_index % 64);
      }
    }
  }

  i.Disassemble(&ucode_disasm_buffer_);
}

void ShaderTranslator::ParseAluScalarInstruction(
    const AluInstruction& op, const AluOpcodeInfo& opcode_info,
    ParsedAluInstruction& i) {
  i.dword_index = 0;
  i.type = ParsedAluInstruction::Type::kScalar;
  i.scalar_opcode = op.scalar_opcode();
  i.opcode_name = opcode_info.name;
  i.is_paired = op.has_vector_op();
  i.is_predicated = op.is_predicated();
  i.predicate_condition = op.predicate_condition();

  uint32_t dest_num;
  uint32_t write_mask;
  if (op.is_export()) {
    dest_num = op.vector_dest();
    write_mask = op.scalar_write_mask() & ~op.vector_write_mask();
  } else {
    dest_num = op.scalar_dest();
    write_mask = op.scalar_write_mask();
  }
  i.result.is_export = op.is_export();
  i.result.is_clamped = op.scalar_clamp();
  i.result.storage_target = InstructionStorageTarget::kRegister;
  i.result.storage_index = 0;
  if (!op.is_export()) {
    assert_true(dest_num < 32);
    i.result.storage_target = InstructionStorageTarget::kRegister;
    i.result.storage_index = dest_num;
    i.result.storage_addressing_mode =
        op.is_scalar_dest_relative()
            ? InstructionStorageAddressingMode::kAddressRelative
            : InstructionStorageAddressingMode::kStatic;
  } else if (is_vertex_shader()) {
    switch (dest_num) {
      case 32:
        i.result.storage_target = InstructionStorageTarget::kExportAddress;
        break;
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        i.result.storage_index = dest_num - 33;
        i.result.storage_target = InstructionStorageTarget::kExportData;
        break;
      case 62:
        i.result.storage_target = InstructionStorageTarget::kPosition;
        break;
      case 63:
        i.result.storage_target = InstructionStorageTarget::kPointSize;
        break;
      default:
        if (dest_num < 16) {
          i.result.storage_target = InstructionStorageTarget::kInterpolant;
          i.result.storage_index = dest_num;
        } else {
          // Unimplemented.
          // assert_always();
          XELOGE(
              "ShaderTranslator::ParseAluScalarInstruction: Unsupported write "
              "to export %d",
              dest_num);
          i.result.storage_target = InstructionStorageTarget::kNone;
          i.result.storage_index = 0;
        }
        break;
    }
  } else if (is_pixel_shader()) {
    switch (dest_num) {
      case 0:
      case 63:  // ? masked?
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 0;
        break;
      case 1:
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 1;
        break;
      case 2:
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 2;
        break;
      case 3:
        i.result.storage_target = InstructionStorageTarget::kColorTarget;
        i.result.storage_index = 3;
        break;
      case 32:
        i.result.storage_target = InstructionStorageTarget::kExportAddress;
        break;
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        i.result.storage_index = dest_num - 33;
        i.result.storage_target = InstructionStorageTarget::kExportData;
        break;
      case 61:
        i.result.storage_target = InstructionStorageTarget::kDepth;
        break;
    }
  }
  for (int j = 0; j < 4; ++j, write_mask >>= 1) {
    i.result.write_mask[j] = (write_mask & 0x1) == 0x1;
    i.result.components[j] = GetSwizzleFromComponentIndex(j);
  }

  i.operand_count = opcode_info.argument_count;
  if (opcode_info.argument_count == 1) {
    ParseAluInstructionOperand(op, 3, opcode_info.src_swizzle_component_count,
                               &i.operands[0]);
  } else {
    uint32_t src3_swizzle = op.src_swizzle(3);
    uint32_t swiz_a = ((src3_swizzle >> 6) + 3) & 0x3;
    uint32_t swiz_b = ((src3_swizzle >> 0) + 0) & 0x3;
    uint32_t reg2 = (src3_swizzle & 0x3C) | (op.src_is_temp(3) << 1) |
                    (static_cast<int>(op.scalar_opcode()) & 1);

    int const_slot = (op.src_is_temp(1) || op.src_is_temp(2)) ? 1 : 0;

    ParseAluInstructionOperandSpecial(
        op, InstructionStorageSource::kConstantFloat, op.src_reg(3),
        op.src_negate(3), 0, swiz_a, &i.operands[0]);

    ParseAluInstructionOperandSpecial(op, InstructionStorageSource::kRegister,
                                      reg2, op.src_negate(3), const_slot,
                                      swiz_b, &i.operands[1]);
  }

  // Track constant float register loads - in either case, a float constant may
  // be used in operand 0.
  if (i.operands[0].storage_source ==
      InstructionStorageSource::kConstantFloat) {
    auto register_index = i.operands[0].storage_index;
    if (i.operands[0].storage_addressing_mode !=
        InstructionStorageAddressingMode::kStatic) {
      // Dynamic addressing makes all constants required.
      std::memset(constant_register_map_.float_bitmap, 0xFF,
                  sizeof(constant_register_map_.float_bitmap));
    } else {
      constant_register_map_.float_bitmap[register_index / 64] |=
          1ull << (register_index % 64);
    }
  }

  i.Disassemble(&ucode_disasm_buffer_);
}

}  // namespace gpu
}  // namespace xe
