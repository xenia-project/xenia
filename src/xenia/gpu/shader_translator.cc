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

#include "xenia/base/assert.h"
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

ShaderTranslator::ShaderTranslator() = default;

ShaderTranslator::~ShaderTranslator() = default;

void ShaderTranslator::Reset(xenos::ShaderType shader_type) {
  shader_type_ = shader_type;
  modification_ = GetDefaultModification(shader_type);
  errors_.clear();
  ucode_disasm_buffer_.Reset();
  ucode_disasm_line_number_ = 0;
  previous_ucode_disasm_scan_offset_ = 0;
  register_count_ = 64;
  label_addresses_.clear();
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
  kills_pixels_ = false;
  memexport_alloc_count_ = 0;
  memexport_eA_written_ = 0;
  std::memset(&memexport_eM_written_, 0, sizeof(memexport_eM_written_));
  memexport_stream_constants_.clear();
}

bool ShaderTranslator::Translate(Shader::Translation& translation,
                                 reg::SQ_PROGRAM_CNTL cntl) {
  xenos::ShaderType shader_type = translation.shader().type();
  Reset(shader_type);
  uint32_t cntl_num_reg = shader_type == xenos::ShaderType::kVertex
                              ? cntl.vs_num_reg
                              : cntl.ps_num_reg;
  register_count_ = (cntl_num_reg & 0x80) ? 0 : (cntl_num_reg + 1);

  return TranslateInternal(translation);
}

bool ShaderTranslator::Translate(Shader::Translation& translation) {
  Reset(translation.shader().type());
  return TranslateInternal(translation);
}

bool ShaderTranslator::TranslateInternal(Shader::Translation& translation) {
  Shader& shader = translation.shader();
  assert_true(shader_type_ == shader.type());
  shader_type_ = shader.type();
  ucode_dwords_ = shader.ucode_dwords();
  ucode_dword_count_ = shader.ucode_dword_count();
  modification_ = translation.modification();

  // Control flow instructions come paired in blocks of 3 dwords and all are
  // listed at the top of the ucode.
  // Each control flow instruction is executed sequentially until the final
  // ending instruction.
  uint32_t max_cf_dword_index = static_cast<uint32_t>(ucode_dword_count_);
  std::vector<ControlFlowInstruction> cf_instructions;
  for (uint32_t i = 0; i < max_cf_dword_index; i += 3) {
    ControlFlowInstruction cf_a;
    ControlFlowInstruction cf_b;
    UnpackControlFlowInstructions(ucode_dwords_ + i, &cf_a, &cf_b);
    // Guess how long the control flow program is by scanning for the first
    // kExec-ish and instruction and using its address as the upper bound.
    // This is what freedreno does.
    if (IsControlFlowOpcodeExec(cf_a.opcode())) {
      max_cf_dword_index =
          std::min(max_cf_dword_index, cf_a.exec.address() * 3);
    }
    if (IsControlFlowOpcodeExec(cf_b.opcode())) {
      max_cf_dword_index =
          std::min(max_cf_dword_index, cf_b.exec.address() * 3);
    }
    // Gather all labels, binding, operand addressing and export information.
    // Translators may need this before they start codegen.
    GatherInstructionInformation(cf_a);
    GatherInstructionInformation(cf_b);
    cf_instructions.push_back(cf_a);
    cf_instructions.push_back(cf_b);
  }

  if (constant_register_map_.float_dynamic_addressing) {
    // All potentially can be referenced.
    constant_register_map_.float_count = 256;
    memset(constant_register_map_.float_bitmap, UINT8_MAX,
           sizeof(constant_register_map_.float_bitmap));
  } else {
    constant_register_map_.float_count = 0;
    for (int i = 0; i < 4; ++i) {
      // Each bit indicates a vec4 (4 floats).
      constant_register_map_.float_count +=
          xe::bit_count(constant_register_map_.float_bitmap[i]);
    }
  }

  // Cleanup invalid/unneeded memexport allocs.
  for (uint32_t i = 0; i < kMaxMemExports; ++i) {
    if (!(memexport_eA_written_ & (uint32_t(1) << i))) {
      memexport_eM_written_[i] = 0;
    } else if (!memexport_eM_written_[i]) {
      memexport_eA_written_ &= ~(uint32_t(1) << i);
    }
  }
  if (memexport_eA_written_ == 0) {
    memexport_stream_constants_.clear();
  }

  StartTranslation();

  PreProcessControlFlowInstructions(cf_instructions);

  // Translate all instructions.
  for (uint32_t i = 0, cf_index = 0; i < max_cf_dword_index; i += 3) {
    ControlFlowInstruction cf_a;
    ControlFlowInstruction cf_b;
    UnpackControlFlowInstructions(ucode_dwords_ + i, &cf_a, &cf_b);

    cf_index_ = cf_index;
    MarkUcodeInstruction(i);
    if (label_addresses_.find(cf_index) != label_addresses_.end()) {
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
    if (label_addresses_.find(cf_index) != label_addresses_.end()) {
      AppendUcodeDisasmFormat("                label L%u\n", cf_index);
      ProcessLabel(cf_index);
    }
    AppendUcodeDisasmFormat("/* %4u.1 */ ", cf_index / 2);
    ProcessControlFlowInstructionBegin(cf_index);
    TranslateControlFlowInstruction(cf_b);
    ProcessControlFlowInstructionEnd(cf_index);
    ++cf_index;
  }

  translation.errors_ = std::move(errors_);
  translation.translated_binary_ = CompleteTranslation();
  translation.is_translated_ = true;

  bool is_valid = true;
  for (const auto& error : translation.errors_) {
    if (error.is_fatal) {
      is_valid = false;
      break;
    }
  }
  translation.is_valid_ = is_valid;

  // Setup info that doesn't depend on the modification only once.
  bool setup_shader_post_translation_info =
      is_valid && !shader.post_translation_info_set_up_.test_and_set();
  if (setup_shader_post_translation_info) {
    shader.ucode_disassembly_ = ucode_disasm_buffer_.to_string();
    shader.vertex_bindings_ = std::move(vertex_bindings_);
    shader.texture_bindings_ = std::move(texture_bindings_);
    shader.constant_register_map_ = std::move(constant_register_map_);
    for (size_t i = 0; i < xe::countof(writes_color_targets_); ++i) {
      shader.writes_color_targets_[i] = writes_color_targets_[i];
    }
    shader.writes_depth_ = writes_depth_;
    shader.kills_pixels_ = kills_pixels_;
    shader.memexport_stream_constants_.clear();
    shader.memexport_stream_constants_.reserve(
        memexport_stream_constants_.size());
    shader.memexport_stream_constants_.insert(
        shader.memexport_stream_constants_.cend(),
        memexport_stream_constants_.cbegin(),
        memexport_stream_constants_.cend());
  }
  PostTranslation(translation, setup_shader_post_translation_info);

  // In case is_valid_ is modified by PostTranslation, reload.
  return translation.is_valid_;
}

void ShaderTranslator::MarkUcodeInstruction(uint32_t dword_offset) {
  auto disasm = ucode_disasm_buffer_.buffer();
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

void ShaderTranslator::EmitTranslationError(const char* message,
                                            bool is_fatal) {
  Shader::Error error;
  error.is_fatal = is_fatal;
  error.message = message;
  // TODO(benvanik): location information.
  errors_.push_back(std::move(error));
  XELOGE("Shader translation {}error: {}", is_fatal ? "fatal " : "", message);
}

void ShaderTranslator::GatherInstructionInformation(
    const ControlFlowInstruction& cf) {
  uint32_t bool_constant_index = UINT32_MAX;
  switch (cf.opcode()) {
    case ControlFlowOpcode::kCondExec:
    case ControlFlowOpcode::kCondExecEnd:
    case ControlFlowOpcode::kCondExecPredClean:
    case ControlFlowOpcode::kCondExecPredCleanEnd:
      bool_constant_index = cf.cond_exec.bool_address();
      break;
    case ControlFlowOpcode::kCondCall:
      label_addresses_.insert(cf.cond_call.address());
      if (!cf.cond_call.is_unconditional() && !cf.cond_call.is_predicated()) {
        bool_constant_index = cf.cond_call.bool_address();
      }
      break;
    case ControlFlowOpcode::kCondJmp:
      label_addresses_.insert(cf.cond_jmp.address());
      if (!cf.cond_jmp.is_unconditional() && !cf.cond_jmp.is_predicated()) {
        bool_constant_index = cf.cond_jmp.bool_address();
      }
      break;
    case ControlFlowOpcode::kLoopStart:
      label_addresses_.insert(cf.loop_start.address());
      constant_register_map_.loop_bitmap |= uint32_t(1)
                                            << cf.loop_start.loop_id();
      break;
    case ControlFlowOpcode::kLoopEnd:
      label_addresses_.insert(cf.loop_end.address());
      constant_register_map_.loop_bitmap |= uint32_t(1)
                                            << cf.loop_end.loop_id();
      break;
    case ControlFlowOpcode::kAlloc:
      if (cf.alloc.alloc_type() == AllocType::kMemory) {
        ++memexport_alloc_count_;
      }
      break;
    default:
      break;
  }
  if (bool_constant_index != UINT32_MAX) {
    constant_register_map_.bool_bitmap[bool_constant_index / 32] |=
        uint32_t(1) << (bool_constant_index % 32);
  }

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
          // Gather info needed for the translation pass because having such
          // state changed in the middle of translation may break things. Check
          // the comments for each specific variable set here to see usage
          // restrictions that can be assumed here (such as only marking exports
          // as written if the used write mask is non-empty).
          auto& op = *reinterpret_cast<const AluInstruction*>(ucode_dwords_ +
                                                              instr_offset * 3);
          ParsedAluInstruction instr;
          ParseAluInstruction(op, instr);

          kills_pixels_ = kills_pixels_ ||
                          ucode::AluVectorOpcodeIsKill(op.vector_opcode()) ||
                          ucode::AluScalarOpcodeIsKill(op.scalar_opcode());

          if (instr.vector_and_constant_result.storage_target !=
                  InstructionStorageTarget::kRegister ||
              instr.scalar_result.storage_target !=
                  InstructionStorageTarget::kRegister) {
            // Export is done to vector_dest of the ucode instruction for both
            // vector and scalar operations - no need to check separately.
            assert_true(instr.vector_and_constant_result.storage_target ==
                            instr.scalar_result.storage_target &&
                        instr.vector_and_constant_result.storage_index ==
                            instr.scalar_result.storage_index);
            if (instr.vector_and_constant_result.GetUsedWriteMask() ||
                instr.scalar_result.GetUsedWriteMask()) {
              InstructionStorageTarget export_target =
                  instr.vector_and_constant_result.storage_target;
              uint32_t export_index =
                  instr.vector_and_constant_result.storage_index;
              switch (export_target) {
                case InstructionStorageTarget::kExportAddress:
                  // Store used memexport constants because CPU code needs
                  // addresses and sizes, and also whether there have been
                  // writes to eA and eM# for register allocation in shader
                  // translator implementations.
                  // eA is (hopefully) always written to using:
                  // mad eA, r#, const0100, c#
                  // (though there are some exceptions, shaders in Halo 3 for
                  // some reason set eA to zeros, but the swizzle of the
                  // constant is not .xyzw in this case, and they don't write to
                  // eM#).
                  if (memexport_alloc_count_ > 0 &&
                      memexport_alloc_count_ <= kMaxMemExports) {
                    uint32_t memexport_stream_constant =
                        instr.GetMemExportStreamConstant();
                    if (memexport_stream_constant != UINT32_MAX) {
                      memexport_eA_written_ |= uint32_t(1)
                                               << (memexport_alloc_count_ - 1);
                      memexport_stream_constants_.insert(
                          memexport_stream_constant);
                    } else {
                      XELOGE(
                          "ShaderTranslator::GatherInstructionInformation: "
                          "Couldn't extract memexport stream constant index");
                    }
                  }
                  break;
                case InstructionStorageTarget::kExportData:
                  if (memexport_alloc_count_ > 0 &&
                      memexport_alloc_count_ <= kMaxMemExports) {
                    memexport_eM_written_[memexport_alloc_count_ - 1] |=
                        uint32_t(1) << export_index;
                  }
                  break;
                case InstructionStorageTarget::kColor:
                  writes_color_targets_[export_index] = true;
                  break;
                case InstructionStorageTarget::kDepth:
                  writes_depth_ = true;
                  break;
                default:
                  break;
              }
            }
          } else {
            if ((instr.vector_and_constant_result.GetUsedWriteMask() &&
                 instr.vector_and_constant_result.storage_addressing_mode !=
                     InstructionStorageAddressingMode::kStatic) ||
                (instr.scalar_result.GetUsedWriteMask() &&
                 instr.scalar_result.storage_addressing_mode !=
                     InstructionStorageAddressingMode::kStatic)) {
              uses_register_dynamic_addressing_ = true;
            }
          }

          uint32_t total_operand_count =
              instr.vector_operand_count + instr.scalar_operand_count;
          for (uint32_t i = 0; i < total_operand_count; ++i) {
            const InstructionOperand& operand =
                (i < instr.vector_operand_count)
                    ? instr.vector_operands[i]
                    : instr.scalar_operands[i - instr.vector_operand_count];
            if (operand.storage_source == InstructionStorageSource::kRegister) {
              if (operand.storage_addressing_mode !=
                  InstructionStorageAddressingMode::kStatic) {
                uses_register_dynamic_addressing_ = true;
              }
            } else if (operand.storage_source ==
                       InstructionStorageSource::kConstantFloat) {
              if (operand.storage_addressing_mode ==
                  InstructionStorageAddressingMode::kStatic) {
                // Store used float constants before translating so the
                // translator can use tightly packed indices if not dynamically
                // indexed.
                uint32_t constant_index = operand.storage_index;
                constant_register_map_.float_bitmap[constant_index / 64] |=
                    uint64_t(1) << (constant_index % 64);
              } else {
                constant_register_map_.float_dynamic_addressing = true;
              }
            }
          }
        }
      }
    } break;
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
  attrib->size_words = xenos::GetVertexFormatSizeInWords(
      attrib->fetch_instr.attributes.data_format);
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

std::vector<uint8_t> UcodeShaderTranslator::CompleteTranslation() {
  return ucode_disasm_buffer().to_bytes();
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
  assert_not_zero(
      constant_register_map_.bool_bitmap[i.bool_constant_index / 32] &
      (uint32_t(1) << (i.bool_constant_index % 32)));
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
  assert_not_zero(constant_register_map_.loop_bitmap &
                  (uint32_t(1) << i.loop_constant_index));
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
  assert_not_zero(constant_register_map_.loop_bitmap &
                  (uint32_t(1) << i.loop_constant_index));
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
    assert_not_zero(
        constant_register_map_.bool_bitmap[i.bool_constant_index / 32] &
        (uint32_t(1) << (i.bool_constant_index % 32)));
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
    assert_not_zero(
        constant_register_map_.bool_bitmap[i.bool_constant_index / 32] &
        (uint32_t(1) << (i.bool_constant_index % 32)));
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
  out_result->is_clamped = false;
  out_result->storage_addressing_mode =
      is_relative ? InstructionStorageAddressingMode::kAddressRelative
                  : InstructionStorageAddressingMode::kStatic;
  out_result->original_write_mask = 0b1111;
  for (int i = 0; i < 4; ++i) {
    switch (swizzle & 0x7) {
      case 4:
      case 6:
        out_result->components[i] = SwizzleSource::k0;
        break;
      case 5:
        out_result->components[i] = SwizzleSource::k1;
        break;
      case 7:
        out_result->original_write_mask &= ~uint32_t(1 << i);
        break;
      default:
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
  for (uint32_t j = 0; j < src_op.component_count; ++j, swizzle >>= 2) {
    src_op.components[j] = GetSwizzleFromComponentIndex(swizzle & 0x3);
  }

  auto& const_op = i.operands[i.operand_count++];
  const_op.storage_source = InstructionStorageSource::kVertexFetchConstant;
  const_op.storage_index = full_op.fetch_constant_index();

  i.attributes.data_format = op.data_format();
  i.attributes.offset = op.offset();
  i.attributes.stride = full_op.stride();
  i.attributes.exp_adjust = op.exp_adjust();
  i.attributes.prefetch_count = op.prefetch_count();
  i.attributes.is_index_rounded = op.is_index_rounded();
  i.attributes.is_signed = op.is_signed();
  i.attributes.is_integer = !op.is_normalized();
  i.attributes.signed_rf_mode = op.signed_rf_mode();

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
    uint32_t override_component_count;
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
      assert_unhandled_case(fetch_opcode);
      return;
  }

  auto& i = *out_instr;
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
          : xenos::GetFetchOpDimensionComponentCount(op.dimension());
  uint32_t swizzle = op.src_swizzle();
  for (uint32_t j = 0; j < src_op.component_count; ++j, swizzle >>= 2) {
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
    i.attributes.vol_mag_filter = op.vol_mag_filter();
    i.attributes.vol_min_filter = op.vol_min_filter();
    i.attributes.use_computed_lod = op.use_computed_lod();
    i.attributes.use_register_lod = op.use_register_lod();
    i.attributes.use_register_gradients = op.use_register_gradients();
    i.attributes.lod_bias = op.lod_bias();
    i.attributes.offset_x = op.offset_x();
    i.attributes.offset_y = op.offset_y();
    i.attributes.offset_z = op.offset_z();
  }
}

uint32_t ParsedTextureFetchInstruction::GetNonZeroResultComponents() const {
  uint32_t components = 0b0000;
  switch (opcode) {
    case FetchOpcode::kTextureFetch:
    case FetchOpcode::kGetTextureGradients:
      components = 0b1111;
      break;
    case FetchOpcode::kGetTextureBorderColorFrac:
      components = 0b0001;
      break;
    case FetchOpcode::kGetTextureComputedLod:
      // Not checking if the MipFilter is basemap because XNA doesn't accept
      // MipFilter for getCompTexLOD.
      components = 0b0001;
      break;
    case FetchOpcode::kGetTextureWeights:
      // FIXME(Triang3l): Not caring about mag/min filters currently for
      // simplicity. It's very unlikely that this instruction is ever seriously
      // used to retrieve weights of zero though.
      switch (dimension) {
        case xenos::FetchOpDimension::k1D:
          components = 0b1001;
          break;
        case xenos::FetchOpDimension::k2D:
        case xenos::FetchOpDimension::kCube:
          // TODO(Triang3l): Is the depth lerp factor always 0 for cube maps?
          components = 0b1011;
          break;
        case xenos::FetchOpDimension::k3DOrStacked:
          components = 0b1111;
          break;
      }
      if (attributes.mip_filter == xenos::TextureFilter::kBaseMap ||
          attributes.mip_filter == xenos::TextureFilter::kPoint) {
        components &= ~uint32_t(0b1000);
      }
      break;
    case FetchOpcode::kSetTextureLod:
    case FetchOpcode::kSetTextureGradientsHorz:
    case FetchOpcode::kSetTextureGradientsVert:
      components = 0b0000;
      break;
    default:
      assert_unhandled_case(opcode);
  }
  return result.GetUsedResultComponents() & components;
}

const ShaderTranslator::AluOpcodeInfo
    ShaderTranslator::alu_vector_opcode_infos_[0x20] = {
        {"add", 2, 4},           // 0
        {"mul", 2, 4},           // 1
        {"max", 2, 4},           // 2
        {"min", 2, 4},           // 3
        {"seq", 2, 4},           // 4
        {"sgt", 2, 4},           // 5
        {"sge", 2, 4},           // 6
        {"sne", 2, 4},           // 7
        {"frc", 1, 4},           // 8
        {"trunc", 1, 4},         // 9
        {"floor", 1, 4},         // 10
        {"mad", 3, 4},           // 11
        {"cndeq", 3, 4},         // 12
        {"cndge", 3, 4},         // 13
        {"cndgt", 3, 4},         // 14
        {"dp4", 2, 4},           // 15
        {"dp3", 2, 4},           // 16
        {"dp2add", 3, 4},        // 17
        {"cube", 2, 4},          // 18
        {"max4", 1, 4},          // 19
        {"setp_eq_push", 2, 4},  // 20
        {"setp_ne_push", 2, 4},  // 21
        {"setp_gt_push", 2, 4},  // 22
        {"setp_ge_push", 2, 4},  // 23
        {"kill_eq", 2, 4},       // 24
        {"kill_gt", 2, 4},       // 25
        {"kill_ge", 2, 4},       // 26
        {"kill_ne", 2, 4},       // 27
        {"dst", 2, 4},           // 28
        {"maxa", 2, 4},          // 29
};

const ShaderTranslator::AluOpcodeInfo
    ShaderTranslator::alu_scalar_opcode_infos_[0x40] = {
        {"adds", 1, 2},         // 0
        {"adds_prev", 1, 1},    // 1
        {"muls", 1, 2},         // 2
        {"muls_prev", 1, 1},    // 3
        {"muls_prev2", 1, 2},   // 4
        {"maxs", 1, 2},         // 5
        {"mins", 1, 2},         // 6
        {"seqs", 1, 1},         // 7
        {"sgts", 1, 1},         // 8
        {"sges", 1, 1},         // 9
        {"snes", 1, 1},         // 10
        {"frcs", 1, 1},         // 11
        {"truncs", 1, 1},       // 12
        {"floors", 1, 1},       // 13
        {"exp", 1, 1},          // 14
        {"logc", 1, 1},         // 15
        {"log", 1, 1},          // 16
        {"rcpc", 1, 1},         // 17
        {"rcpf", 1, 1},         // 18
        {"rcp", 1, 1},          // 19
        {"rsqc", 1, 1},         // 20
        {"rsqf", 1, 1},         // 21
        {"rsq", 1, 1},          // 22
        {"maxas", 1, 2},        // 23
        {"maxasf", 1, 2},       // 24
        {"subs", 1, 2},         // 25
        {"subs_prev", 1, 1},    // 26
        {"setp_eq", 1, 1},      // 27
        {"setp_ne", 1, 1},      // 28
        {"setp_gt", 1, 1},      // 29
        {"setp_ge", 1, 1},      // 30
        {"setp_inv", 1, 1},     // 31
        {"setp_pop", 1, 1},     // 32
        {"setp_clr", 0, 0},     // 33
        {"setp_rstr", 1, 1},    // 34
        {"kills_eq", 1, 1},     // 35
        {"kills_gt", 1, 1},     // 36
        {"kills_ge", 1, 1},     // 37
        {"kills_ne", 1, 1},     // 38
        {"kills_one", 1, 1},    // 39
        {"sqrt", 1, 1},         // 40
        {"UNKNOWN", 0, 0},      // 41
        {"mulsc", 2, 1},        // 42
        {"mulsc", 2, 1},        // 43
        {"addsc", 2, 1},        // 44
        {"addsc", 2, 1},        // 45
        {"subsc", 2, 1},        // 46
        {"subsc", 2, 1},        // 47
        {"sin", 1, 1},          // 48
        {"cos", 1, 1},          // 49
        {"retain_prev", 0, 0},  // 50
};

void ShaderTranslator::TranslateAluInstruction(const AluInstruction& op) {
  ParsedAluInstruction instr;
  ParseAluInstruction(op, instr);
  instr.Disassemble(&ucode_disasm_buffer_);
  ProcessAluInstruction(instr);
}

void ShaderTranslator::ParseAluInstruction(const AluInstruction& op,
                                           ParsedAluInstruction& instr) const {
  instr.is_predicated = op.is_predicated();
  instr.predicate_condition = op.predicate_condition();

  bool is_export = op.is_export();

  InstructionStorageTarget storage_target = InstructionStorageTarget::kRegister;
  uint32_t storage_index_export = 0;
  if (is_export) {
    storage_target = InstructionStorageTarget::kNone;
    // Both vector and scalar operation export to vector_dest.
    ExportRegister export_register = ExportRegister(op.vector_dest());
    if (export_register == ExportRegister::kExportAddress) {
      storage_target = InstructionStorageTarget::kExportAddress;
    } else if (export_register >= ExportRegister::kExportData0 &&
               export_register <= ExportRegister::kExportData4) {
      storage_target = InstructionStorageTarget::kExportData;
      storage_index_export =
          uint32_t(export_register) - uint32_t(ExportRegister::kExportData0);
    } else if (is_vertex_shader()) {
      if (export_register >= ExportRegister::kVSInterpolator0 &&
          export_register <= ExportRegister::kVSInterpolator15) {
        storage_target = InstructionStorageTarget::kInterpolator;
        storage_index_export = uint32_t(export_register) -
                               uint32_t(ExportRegister::kVSInterpolator0);
      } else if (export_register == ExportRegister::kVSPosition) {
        storage_target = InstructionStorageTarget::kPosition;
      } else if (export_register ==
                 ExportRegister::kVSPointSizeEdgeFlagKillVertex) {
        storage_target = InstructionStorageTarget::kPointSizeEdgeFlagKillVertex;
      }
    } else if (is_pixel_shader()) {
      if (export_register >= ExportRegister::kPSColor0 &&
          export_register <= ExportRegister::kPSColor3) {
        storage_target = InstructionStorageTarget::kColor;
        storage_index_export =
            uint32_t(export_register) - uint32_t(ExportRegister::kPSColor0);
      } else if (export_register == ExportRegister::kPSDepth) {
        storage_target = InstructionStorageTarget::kDepth;
      }
    }
    if (storage_target == InstructionStorageTarget::kNone) {
      assert_always();
      XELOGE(
          "ShaderTranslator::ParseAluInstruction: Unsupported write to export "
          "{}",
          uint32_t(export_register));
    }
  }

  // Vector operation and constant 0/1 writes.

  instr.vector_opcode = op.vector_opcode();
  const auto& vector_opcode_info =
      alu_vector_opcode_infos_[uint32_t(instr.vector_opcode)];
  instr.vector_opcode_name = vector_opcode_info.name;

  instr.vector_and_constant_result.storage_target = storage_target;
  instr.vector_and_constant_result.storage_addressing_mode =
      InstructionStorageAddressingMode::kStatic;
  if (is_export) {
    instr.vector_and_constant_result.storage_index = storage_index_export;
  } else {
    instr.vector_and_constant_result.storage_index = op.vector_dest();
    assert_true(op.vector_dest() < register_count());
    if (op.is_vector_dest_relative()) {
      instr.vector_and_constant_result.storage_addressing_mode =
          InstructionStorageAddressingMode::kAddressRelative;
    }
  }
  instr.vector_and_constant_result.is_clamped = op.vector_clamp();
  uint32_t constant_0_mask = op.GetConstant0WriteMask();
  uint32_t constant_1_mask = op.GetConstant1WriteMask();
  instr.vector_and_constant_result.original_write_mask =
      op.GetVectorOpResultWriteMask() | constant_0_mask | constant_1_mask;
  for (uint32_t i = 0; i < 4; ++i) {
    SwizzleSource component = GetSwizzleFromComponentIndex(i);
    if (constant_0_mask & (1 << i)) {
      component = SwizzleSource::k0;
    } else if (constant_1_mask & (1 << i)) {
      component = SwizzleSource::k1;
    }
    instr.vector_and_constant_result.components[i] = component;
  }

  instr.vector_operand_count = vector_opcode_info.argument_count;
  for (uint32_t i = 0; i < instr.vector_operand_count; ++i) {
    InstructionOperand& vector_operand = instr.vector_operands[i];
    ParseAluInstructionOperand(op, i + 1,
                               vector_opcode_info.src_swizzle_component_count,
                               vector_operand);
  }

  // Scalar operation.

  instr.scalar_opcode = op.scalar_opcode();
  const auto& scalar_opcode_info =
      alu_scalar_opcode_infos_[uint32_t(instr.scalar_opcode)];
  instr.scalar_opcode_name = scalar_opcode_info.name;

  instr.scalar_result.storage_target = storage_target;
  instr.scalar_result.storage_addressing_mode =
      InstructionStorageAddressingMode::kStatic;
  if (is_export) {
    instr.scalar_result.storage_index = storage_index_export;
  } else {
    instr.scalar_result.storage_index = op.scalar_dest();
    assert_true(op.scalar_dest() < register_count());
    if (op.is_scalar_dest_relative()) {
      instr.scalar_result.storage_addressing_mode =
          InstructionStorageAddressingMode::kAddressRelative;
    }
  }
  instr.scalar_result.is_clamped = op.scalar_clamp();
  instr.scalar_result.original_write_mask = op.GetScalarOpResultWriteMask();
  for (uint32_t i = 0; i < 4; ++i) {
    instr.scalar_result.components[i] = GetSwizzleFromComponentIndex(i);
  }

  instr.scalar_operand_count = scalar_opcode_info.argument_count;
  if (instr.scalar_operand_count) {
    if (instr.scalar_operand_count == 1) {
      ParseAluInstructionOperand(op, 3,
                                 scalar_opcode_info.src_swizzle_component_count,
                                 instr.scalar_operands[0]);
    } else {
      uint32_t src3_swizzle = op.src_swizzle(3);
      uint32_t component_a = ((src3_swizzle >> 6) + 3) & 0x3;
      uint32_t component_b = ((src3_swizzle >> 0) + 0) & 0x3;
      uint32_t reg2 = (src3_swizzle & 0x3C) | (op.src_is_temp(3) << 1) |
                      (static_cast<int>(op.scalar_opcode()) & 1);
      int const_slot = (op.src_is_temp(1) || op.src_is_temp(2)) ? 1 : 0;

      ParseAluInstructionOperandSpecial(
          op, InstructionStorageSource::kConstantFloat, op.src_reg(3),
          op.src_negate(3), 0, component_a, instr.scalar_operands[0]);

      ParseAluInstructionOperandSpecial(op, InstructionStorageSource::kRegister,
                                        reg2, op.src_negate(3), const_slot,
                                        component_b, instr.scalar_operands[1]);
    }
  }
}

void ShaderTranslator::ParseAluInstructionOperand(
    const AluInstruction& op, uint32_t i, uint32_t swizzle_component_count,
    InstructionOperand& out_op) {
  int const_slot = 0;
  switch (i) {
    case 2:
      const_slot = op.src_is_temp(1) ? 0 : 1;
      break;
    case 3:
      const_slot = op.src_is_temp(1) && op.src_is_temp(2) ? 0 : 1;
      break;
  }
  out_op.is_negated = op.src_negate(i);
  uint32_t reg = op.src_reg(i);
  if (op.src_is_temp(i)) {
    out_op.storage_source = InstructionStorageSource::kRegister;
    out_op.storage_index = reg & 0x1F;
    out_op.is_absolute_value = (reg & 0x80) == 0x80;
    out_op.storage_addressing_mode =
        (reg & 0x40) ? InstructionStorageAddressingMode::kAddressRelative
                     : InstructionStorageAddressingMode::kStatic;
  } else {
    out_op.storage_source = InstructionStorageSource::kConstantFloat;
    out_op.storage_index = reg;
    if ((const_slot == 0 && op.is_const_0_addressed()) ||
        (const_slot == 1 && op.is_const_1_addressed())) {
      if (op.is_address_relative()) {
        out_op.storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressAbsolute;
      } else {
        out_op.storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressRelative;
      }
    } else {
      out_op.storage_addressing_mode =
          InstructionStorageAddressingMode::kStatic;
    }
    out_op.is_absolute_value = op.abs_constants();
  }
  out_op.component_count = swizzle_component_count;
  uint32_t swizzle = op.src_swizzle(i);
  if (swizzle_component_count == 1) {
    uint32_t a = ((swizzle >> 6) + 3) & 0x3;
    out_op.components[0] = GetSwizzleFromComponentIndex(a);
  } else if (swizzle_component_count == 2) {
    uint32_t a = ((swizzle >> 6) + 3) & 0x3;
    uint32_t b = ((swizzle >> 0) + 0) & 0x3;
    out_op.components[0] = GetSwizzleFromComponentIndex(a);
    out_op.components[1] = GetSwizzleFromComponentIndex(b);
  } else if (swizzle_component_count == 3) {
    assert_always();
  } else if (swizzle_component_count == 4) {
    for (uint32_t j = 0; j < swizzle_component_count; ++j, swizzle >>= 2) {
      out_op.components[j] = GetSwizzleFromComponentIndex((swizzle + j) & 0x3);
    }
  }
}

void ShaderTranslator::ParseAluInstructionOperandSpecial(
    const AluInstruction& op, InstructionStorageSource storage_source,
    uint32_t reg, bool negate, int const_slot, uint32_t component_index,
    InstructionOperand& out_op) {
  out_op.is_negated = negate;
  out_op.is_absolute_value = op.abs_constants();
  out_op.storage_source = storage_source;
  if (storage_source == InstructionStorageSource::kRegister) {
    out_op.storage_index = reg & 0x7F;
    out_op.storage_addressing_mode = InstructionStorageAddressingMode::kStatic;
  } else {
    out_op.storage_index = reg;
    if ((const_slot == 0 && op.is_const_0_addressed()) ||
        (const_slot == 1 && op.is_const_1_addressed())) {
      if (op.is_address_relative()) {
        out_op.storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressAbsolute;
      } else {
        out_op.storage_addressing_mode =
            InstructionStorageAddressingMode::kAddressRelative;
      }
    } else {
      out_op.storage_addressing_mode =
          InstructionStorageAddressingMode::kStatic;
    }
  }
  out_op.component_count = 1;
  out_op.components[0] = GetSwizzleFromComponentIndex(component_index);
}

bool ParsedAluInstruction::IsVectorOpDefaultNop() const {
  if (vector_opcode != ucode::AluVectorOpcode::kMax ||
      vector_and_constant_result.original_write_mask ||
      vector_and_constant_result.is_clamped ||
      vector_operands[0].storage_source !=
          InstructionStorageSource::kRegister ||
      vector_operands[0].storage_index != 0 ||
      vector_operands[0].storage_addressing_mode !=
          InstructionStorageAddressingMode::kStatic ||
      vector_operands[0].is_negated || vector_operands[0].is_absolute_value ||
      !vector_operands[0].IsStandardSwizzle() ||
      vector_operands[1].storage_source !=
          InstructionStorageSource::kRegister ||
      vector_operands[1].storage_index != 0 ||
      vector_operands[1].storage_addressing_mode !=
          InstructionStorageAddressingMode::kStatic ||
      vector_operands[1].is_negated || vector_operands[1].is_absolute_value ||
      !vector_operands[1].IsStandardSwizzle()) {
    return false;
  }
  if (vector_and_constant_result.storage_target ==
      InstructionStorageTarget::kRegister) {
    if (vector_and_constant_result.storage_index != 0 ||
        vector_and_constant_result.storage_addressing_mode !=
            InstructionStorageAddressingMode::kStatic) {
      return false;
    }
  } else {
    // In case both vector and scalar operations are nop, still need to write
    // somewhere that it's an export, not mov r0._, r0 + retain_prev r0._.
    // Accurate round trip is possible only if the target is o0 or oC0, because
    // if the total write mask is empty, the XNA assembler forces the
    // destination to be o0/oC0, but this doesn't really matter in this case.
    if (IsScalarOpDefaultNop()) {
      return false;
    }
  }
  return true;
}

bool ParsedAluInstruction::IsScalarOpDefaultNop() const {
  if (scalar_opcode != ucode::AluScalarOpcode::kRetainPrev ||
      scalar_result.original_write_mask || scalar_result.is_clamped) {
    return false;
  }
  if (scalar_result.storage_target == InstructionStorageTarget::kRegister) {
    if (scalar_result.storage_index != 0 ||
        scalar_result.storage_addressing_mode !=
            InstructionStorageAddressingMode::kStatic) {
      return false;
    }
  }
  // For exports, if both are nop, the vector operation will be kept to state in
  // the microcode that the destination in the microcode is an export.
  return true;
}

bool ParsedAluInstruction::IsNop() const {
  return scalar_opcode == ucode::AluScalarOpcode::kRetainPrev &&
         !scalar_result.GetUsedWriteMask() &&
         !vector_and_constant_result.GetUsedWriteMask() &&
         !ucode::AluVectorOpHasSideEffects(vector_opcode);
}

uint32_t ParsedAluInstruction::GetMemExportStreamConstant() const {
  if (vector_and_constant_result.storage_target ==
          InstructionStorageTarget::kExportAddress &&
      vector_opcode == ucode::AluVectorOpcode::kMad &&
      vector_and_constant_result.GetUsedResultComponents() == 0b1111 &&
      !vector_and_constant_result.is_clamped &&
      vector_operands[2].storage_source ==
          InstructionStorageSource::kConstantFloat &&
      vector_operands[2].storage_addressing_mode ==
          InstructionStorageAddressingMode::kStatic &&
      vector_operands[2].IsStandardSwizzle() &&
      !vector_operands[2].is_negated && !vector_operands[2].is_absolute_value) {
    return vector_operands[2].storage_index;
  }
  return UINT32_MAX;
}

}  // namespace gpu
}  // namespace xe
