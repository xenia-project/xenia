/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/hlsl_shader_translator.h"

#include "xenia/base/assert.h"

namespace xe {
namespace gpu {
using namespace ucode;

constexpr uint32_t kMaxInterpolators = 16;

#define EmitSource(...) source_.AppendFormat(__VA_ARGS__)
#define EmitSourceDepth(...)     \
  source_.Append("  ");          \
  source_.Append(depth_prefix_); \
  source_.AppendFormat(__VA_ARGS__)

HlslShaderTranslator::HlslShaderTranslator() {}
HlslShaderTranslator::~HlslShaderTranslator() = default;

void HlslShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  source_.Reset();
  depth_ = 0;
  depth_prefix[0] = 0;

  cf_wrote_pc_ = false;
  cf_exec_pred_ = false;
  cf_exec_pred_cond_ = false;

  writes_depth_ = false;

  srv_bindings_.clear();

  sampler_count_ = 0;

  cube_used_ = false;
}

void HlslShaderTranslator::EmitTranslationError(const char* message) {
  ShaderTranslator::EmitTranslationError(message);
  EmitSourceDepth("// TRANSLATION ERROR: %s\n", message);
}

void HlslShaderTranslator::EmitUnimplementedTranslationError() {
  ShaderTranslator::EmitUnimplementedTranslationError();
  EmitSourceDepth("// UNIMPLEMENTED TRANSLATION\n");
}

void HlslShaderTranslator::Indent() {
  depth_prefix_[depth_] = ' ';
  depth_prefix_[depth_ + 1] = ' ';
  depth_prefix_[depth_ + 2] = 0;
  depth_ += 2;
}

void HlslShaderTranslator::Unindent() {
  depth_ -= 2;
  depth_prefix_[depth_] = 0;
}

void HlslShaderTranslator::StartTranslation() {
  // Common things.
  EmitSource(
     "cbuffer xe_system_constants : register(b0) {\n"
     "  float2 xe_viewport_inv_scale;\n"
     "  uint xe_vertex_index_endian;\n"
     "  uint xe_textures_are_3d;\n"
     "}\n"
     "\n"
     "struct XeFloatConstantPage {\n"
     "  float4 c[16];\n"
     "}\n"
     "ConstantBuffer<XeFloatConstantPage> "
     "xe_float_constants[16] : register(b1);\n"
     "\n"
     "cbuffer xe_loop_bool_constants : register(b17) {\n"
     "  uint xe_bool_constants[8];\n"
     "  uint xe_loop_constants[32];\n"
     "}\n"
     "\n");

  if (is_vertex_shader()) {
    // Vertex fetching, output and prologue.
    // Endian register (2nd word of the fetch constant) is 00 for no swap, 01
    // for 8-in-16, 10 for 8-in-32 (a combination of 8-in-16 and 16-in-32), and
    // 11 for 16-in-32. This means we can check bits 0 ^ 1 to see if we need to
    // do a 8-in-16 swap, and bit 1 to see if a 16-in-32 swap is needed.
    // Vertex element is a temporary integer value for fetches.
    EmitSource(
        "cbuffer xe_vertex_fetch_constants : register(b18) {\n"
        "  uint2 xe_vertex_fetch[96];\n"
        "}\n"
        "\n"
        "ByteAddressBuffer xe_virtual_memory : register(t0, space1);\n"
        "\n"
        "#define XE_SWAP_OVERLOAD(XeSwapType) \\\n"
        "XeSwapType XeSwap(XeSwapType v, uint endian) { \\\n"
        "  [flatten] if (((endian ^ (endian >> 1u)) & 1u) != 0u) { \\\n"
        "    v = ((v & 0x00FF00FFu) << 8u) | ((v & 0xFF00FF00u) >> 8u); \\\n"
        "  } \\\n"
        "  [flatten] if ((endian & 2u) != 0u) { \\\n"
        "    v = (v << 16u) | (v >> 16u); \\\n"
        "  } \\\n"
        "  return v; \\\n"
        "}\n"
        "XE_SWAP_OVERLOAD(uint)\n"
        "XE_SWAP_OVERLOAD(uint2)\n"
        "XE_SWAP_OVERLOAD(uint3)\n"
        "XE_SWAP_OVERLOAD(uint4)\n"
        "\n"
        "struct XeVertexShaderOutput {\n"
        "  float4 position : SV_Position;\n"
        "  float4 interpolators[%u] : TEXCOORD;\n"
        "  float4 point_size : PSIZE;\n"
        "}\n"
        "\n"
        "XeVertexShaderOutput main(uint xe_vertex_index_be : SV_VertexID) {\n"
        "  float4 xe_r[%u];\n"
        "  uint xe_vertex_index =\n"
        "      XeSwap(xe_vertex_index_be, xe_vertex_index_endian);\n"
        "  uint4 xe_vertex_element;\n"
        "  xe_r[0].r = float(xe_vertex_index);\n"
        "  XeVertexShaderOutput xe_output;\n",
        kMaxInterpolators, register_count());
    // TODO(Triang3l): Reset interpolators to zero if really needed.
  } else if (is_pixel_shader()) {
    // Pixel shader inputs, outputs and prologue.
    // If the shader writes to depth, it needs to define
    // XE_PIXEL_SHADER_WRITES_DEPTH in the beginning of the final output.
    EmitSource(
        "struct XePixelShaderInput {\n"
        "  float4 position : SV_Position;\n"
        "  float4 interpolators[%u] : TEXCOORD;\n"
        "}\n"
        "\n"
        "struct XePixelShaderOutput {\n"
        "  float4 colors[4] : SV_Target;\n"
        "  #ifdef XE_PIXEL_SHADER_WRITES_DEPTH\n"
        "  float depth : SV_Depth;\n"
        "  #endif\n"
        "}\n"
        "\n"
        "XePixelShaderOutput main(XePixelShaderInput xe_input) {\n"
        "  float4 xe_r[%u];\n"
        "  XePixelShaderOutput xe_output;\n",
        kMaxInterpolators, register_count());
    // Copy interpolators to the first registers.
    uint32_t interpolator_register_count =
        std::min(register_count(), kMaxInterpolators);
    for (uint32_t i = 0; i < interpolator_register_count; ++i) {
      EmitSource("  xe_r[%u] = xe_input.interpolators[%u];\n", i, i);
    }
    // No need to write zero to every output because in case an output is
    // completely unused, writing to that render target will be disabled in the
    // blending state (in Halo 3, one important render target is destroyed by a
    // shader not writing to one of the outputs otherwise).
    // TODO(Triang3l): ps_param_gen.
  }

  EmitSource(
      // Dynamic index for source operands (mainly for float and bool constants
      // since they are indexed in two parts).
      "  uint xe_src_index;\n"
      // Sources for instructions.
      "  float4 xe_src0, xe_src1, xe_src2;\n"
      // Previous vector result (used as a scratch).
      "  float4 xe_pv;\n"
      // Previous scalar result (used for RETAIN_PREV).
      "  float xe_ps;\n"
      // Predicate temp, clause-local. Initially false like cf_exec_pred_cond_.
      "  bool xe_p0 = false;\n"
      // Address register when using absolute addressing.
      "  int xe_a0 = 0;\n"
      // Loop index stack - .x is the active loop, shifted right to yzw on push.
      "  int4 xe_aL = int4(0, 0, 0, 0);\n"
      // Loop counter stack, .x is the active loop.
      // Represents number of times remaining to loop.
      "  uint4 xe_loop_count = uint4(0u, 0u, 0u, 0u);\n"
      // Master loop and switch for flow control.
      "  uint xe_pc = 0u;\n"
      "\n"
      "  do {\n";
      "    switch (xe_pc) {\n"
      "    case 0u:\n");

  // Main function level (1).
  Indent();
  // Do level (2)
  Indent();
}

void HlslShaderTranslator::ProcessLabel(uint32_t cf_index) {
  // 0 is always added in the beginning.
  if (cf_index != 0) {
    EmitSourceDepth("case %u:\n", cf_index);
  }
}

void HlslShaderTranslator::ProcessControlFlowNopInstruction(uint32_t cf_index) {
  EmitSourceDepth("// cnop\n");
}

void HlslShaderTranslator::ProcessControlFlowInstructionBegin(
    uint32_t cf_index) {
  cf_wrote_pc_ = false;
  Indent();
}

void HlslShaderTranslator::ProcessControlFlowInstructionEnd(uint32_t cf_index) {
  if (!cf_wrote_pc_) {
    EmitSourceDepth("// Falling through to L%u\n", cf_index + 1);
  }
  Unindent();
}

void HlslShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  cf_exec_pred_ = false;
  switch (instr.type) {
    case ParsedExecInstruction::Type::kUnconditional:
      EmitSourceDepth("{\n");
      break;
    case ParsedExecInstruction::Type::kConditional:
      EmitSourceDepth("if ((xe_bool_constants[%u] & (1u << %uu)) %c= 0u) {\n",
                      instr.bool_constant_index >> 5,
                      instr.bool_constant_index & 31,
                      instr.condition ? '!' : '=');
      break;
    case ParsedExecInstruction::Type::kPredicated:
      cf_exec_pred_ = true;
      cf_exec_pred_cond_ = instr.condition;
      EmitSourceDepth("if (%cxe_p0) {\n", instr.condition ? ' ' : '!');
      break;
  }
  Indent();
}

void HlslShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  if (instr.is_end) {
    EmitSourceDepth("xe_pc = 0xFFFFu;\n");
    EmitSourceDepth("break;\n");
    cf_wrote_pc_ = true;
  }
  Unindent();
  EmitSourceDepth("}\n");
}

void HlslShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  // Setup counter.
  EmitSourceDepth("xe_loop_count.yzw = xe_loop_count.xyz;\n");
  EmitSourceDepth("xe_loop_count.x = xe_loop_constants[%u] & 0xFFu;\n");

  // Setup relative indexing.
  EmitSourceDepth("xe_aL = xe_aL.xxyz;\n");
  if (!instr.is_repeat) {
    // Push new loop starting index if not reusing the current one.
    EmitSourceDepth("xe_aL.x = int((xe_loop_constants[%u] >> 8u) & 0xFFu);\n");
  }

  // Quick skip loop if zero count.
  EmitSourceDepth("if (xe_loop_count.x == 0u) {\n");
  EmitSourceDepth("  xe_pc = %u;  // Skip loop to L%u\n",
                  instr.loop_skip_address, instr.loop_skip_address);
  EmitSourceDepth("} else {\n");
  EmitSourceDepth("  xe_pc = %u;  // Fallthrough to loop body L%u\n",
                  instr.dword_index + 1, instr.dword_index + 1);
  EmitSourceDepth("}\n");
  EmitSourceDepth("break;\n");
  cf_wrote_pc_ = true;
}

void HlslShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  // Decrement loop counter, and if we are done break out.
  EmitSourceDepth("if (--xe_loop_count.x == 0u");
  if (instr.is_predicated_break) {
    // If the predicate condition is met we 'break;' out of the loop.
    // Need to restore stack and fall through to the next cf.
    EmitSource(" || %cxe_p0) {\n", instr.predicate_condition ? ' ' : '!');
  } else {
    EmitSource(") {\n");
  }
  Indent();

  // Loop completed - pop and fall through to next cf.
  EmitSourceDepth("xe_loop_count.xyz = xe_loop_count.yzw;\n");
  EmitSourceDepth("xe_loop_count.w = 0u;\n");
  EmitSourceDepth("xe_aL.xyz = xe_aL.yzw;\n");
  EmitSourceDepth("xe_aL.w = 0;\n");
  EmitSourceDepth("xe_pc = %u;  // Exit loop to L%u\n", instr.dword_index + 1,
                  instr.dword_index + 1);

  Unindent();
  EmitSourceDepth("} else {\n");
  Indent();

  // Still looping. Adjust index and jump back to body.
  EmitSourceDepth("xe_aL.x += int(xe_loop_constants[%u] << 8u) >> 24;\n",
                  instr.loop_constant_index);
  EmitSourceDepth("xe_pc = %u;  // Loop back to body L%u\n",
                  instr.loop_body_address, instr.loop_body_address);

  Unindent();
  EmitSourceDepth("}\n");
  EmitSourceDepth("break;\n");
  cf_wrote_pc_ = true;
}

void HlslShaderTranslator::ProcessCallInstruction(
    const ParsedCallInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  EmitUnimplementedTranslationError();
}

void HlslShaderTranslator::ProcessReturnInstruction(
    const ParsedReturnInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  EmitUnimplementedTranslationError();
}

void HlslShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  bool needs_fallthrough = false;
  switch (instr.type) {
    case ParsedJumpInstruction::Type::kUnconditional:
      EmitSourceDepth("{\n");
      break;
    case ParsedJumpInstruction::Type::kConditional:
      EmitSourceDepth("if ((xe_bool_constants[%u] & (1u << %uu)) %c= 0u) {\n",
                      instr.bool_constant_index >> 5,
                      instr.bool_constant_index & 31,
                      instr.condition ? '!' : '=');
      needs_fallthrough = true;
      break;
    case ParsedJumpInstruction::Type::kPredicated:
      EmitSourceDepth("if (%cxe_p0) {\n", instr.condition ? ' ' : '!');
      needs_fallthrough = true;
      break;
  }
  Indent();

  EmitSourceDepth("xe_pc = %u;  // L%u\n", instr.target_address,
                  instr.target_address);
  EmitSourceDepth("break;\n");

  Unindent();
  if (needs_fallthrough) {
    uint32_t next_address = instr.dword_index + 1;
    EmitSourceDepth("} else {\n");
    EmitSourceDepth("  xe_pc = %u;  // Fallthrough to L%u\n", next_address,
                    next_address);
  }
  EmitSourceDepth("}\n");
}

void HlslShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);
}

bool HlslShaderTranslator::BeginPredicatedInstruction(
    bool is_predicated, bool predicate_condition) {
  if (is_predicated &&
      (!cf_exec_pred_ || cf_exec_pred_cond_ != predicate_condition)) {
    EmitSourceDepth("if (%cxe_p0) {\n", instr.predicate_condition ? ' ' : '!');
    Indent();
    return true;
  }
  return false;
}

void HlslShaderTranslator::EndPredicatedInstruction(bool conditional_emitted) {
  if (conditional_emitted) {
    Unindent();
    EmitSourceDepth("}\n");
  }
}

void HlslShaderTranslator::EmitLoadOperand(uint32_t src_index,
                                           const InstructionOperand& op) {
  // If indexing dynamically, emit the index because float and bool constants
  // need to be indexed in two parts.
  // Also verify we are not using vertex/texture fetch constants here.
  uint32_t storage_index_max;
  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      index_max = 127;
      break;
    case InstructionStorageSource::kConstantFloat:
    case InstructionStorageSource::kConstantBool:
      index_max = 255;
      break;
    case InstructionStorageSource::kConstantInt:
      index_max = 31;
      break;
    default:
      assert_always();
      return;
  }
  if (op.storage_addressing_mode ==
      InstructionStorageAddressingMode::kAddressAbsolute) {
    EmitSourceDepth("xe_src_index = uint(%u + xe_a0) & %uu;\n",
                    op.storage_index, storage_index_max);
  } else if (op.storage_addressing_mode ==
             InstructionStorageAddressingMode::kAddressRelative) {
    EmitSourceDepth("xe_src_index = uint(%u + xe_aL.x) & %uu;\n",
                    op.storage_index, storage_index_max);
  }

  // Negation and abs are store modifiers, so they're applied after swizzling.
  EmitSourceDepth("xe_src%u = ", src_index);
  if (op.is_negated) {
    EmitSource("-");
  }
  if (op.is_absolute_value) {
    EmitSource("abs");
  }
  EmitSource("(");

  if (op.storage_addressing_mode == InstructionStorageAddressingMode::kStatic) {
    switch (op.storage_source) {
      case InstructionStorageSource::kRegister:
        EmitSource("xe_r[%u]", op.storage_index);
        break;
      case InstructionStorageSource::kConstantFloat:
        EmitSource("xe_float_constants[%u].c[%u]", op.storage_index >> 4,
                   op.storage_index & 15);
        break;
      case InstructionStorageSource::kConstantInt:
        EmitSource("xe_loop_constants[%u]", op.storage_index);
        break;
      case InstructionStorageSource::kConstantBool:
        EmitSource("float((xe_bool_constants[%u] >> %uu) & 1u)",
                   op.storage_index >> 5, op.storage_index & 31);
        break;
      default:
        assert_always();
        break;
    }
  } else {
    switch (op.storage_source) {
      case InstructionStorageSource::kRegister:
        EmitSource("xe_r[xe_src_index]");
        break;
      case InstructionStorageSource::kConstantFloat:
        EmitSource(
            "xe_float_constants[xe_src_index >> 4u].c[xe_src_index & 15u]");
        break;
      case InstructionStorageSource::kConstantInt:
        EmitSource("xe_loop_constants[xe_src_index]");
        break;
      case InstructionStorageSource::kConstantBool:
        EmitSource("float((xe_bool_constants[xe_src_index >> 5u] >> "
                   "(xe_src_index & 31u)) & 1u)");
        break;
      default:
        assert_always();
        break;
    }
  }

  EmitSource(")");
  // Integer and bool constants are scalar, can't swizzle them.
  if (op.storage_source == InstructionStorageSource::kConstantInt ||
      op.storage_source == InstructionStorageSource::kConstantBool) {
    EmitSource(".xxxx");
  } else {
    if (!op.is_standard_swizzle()) {
      EmitSource(".");
      // For 1 component stores it will be .aaaa, for 2 components it's .abbb.
      for (int i = 0; i < op.component_count; ++i) {
        EmitSource("%c", GetCharForSwizzle(op.components[i]));
      }
      for (int i = op.component_count; i < 4; ++i) {
        EmitSource("%c",
                   GetCharForSwizzle(op.components[op.component_count - 1]));
      }
    }
  }
  EmitSource(";\n");
}

void HlslShaderTranslator::EmitStoreResult(const InstructionResult& result,
                                           bool source_is_scalar) {
  bool storage_is_scalar =
      result.storage_target == InstructionStorageTarget::kPointSize ||
      result.storage_target == InstructionStorageTarget::kDepth;
  if (storage_is_scalar) {
    if (!result.write_mask[0]) {
      return;
    }
  } else {
    if (!result.has_any_writes()) {
      return;
    }
  }

  bool storage_is_array = false;
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      EmitSourceDepth("xe_r");
      storage_is_array = true;
      break;
    case InstructionStorageTarget::kInterpolant:
      EmitSourceDepth("xe_output.interpolators");
      storage_is_array = true;
      break;
    case InstructionStorageTarget::kPosition:
      EmitSourceDepth("xe_output.position");
      break;
    case InstructionStorageTarget::kPointSize:
      EmitSourceDepth("xe_output.point_size");
      break;
    case InstructionStorageTarget::kColorTarget:
      EmitSourceDepth("xe_output.colors");
      storage_is_array = true;
      break;
    case InstructionStorageTarget::kDepth:
      EmitSourceDepth("xe_output.depth");
      writes_depth_ = true;
      break;
    default:
    case InstructionStorageTarget::kNone:
      return;
  }
  if (storage_is_array) {
    switch (result.storage_addressing_mode) {
      case InstructionStorageAddressingMode::kStatic:
        EmitSource("[%u]", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressAbsolute:
        EmitSource("[%u + xe_a0]", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressRelative:
        EmitSource("[%u + xe_aL.x]", result.storage_index);
        break;
    }
  }
  if (storage_is_scalar) {
    EmitSource(" = ");
    switch (result.components[0]) {
      case SwizzleSource::k0:
        EmitSource("0.0");
        break;
      case SwizzleSource::k1:
        EmitSource("1.0");
        break;
      default:
        if (result.is_clamped) {
          EmitSource("saturate(");
        }
        if (source_is_scalar) {
          EmitSource("xe_ps");
        } else {
          EmitSource("xe_pv.%c", GetCharForSwizzle(result.components[0]));
        }
        if (result.is_clamped) {
          EmitSource(")");
        }
        break;
    }
  } else {
    if (result.is_clamped) {
      EmitSource("saturate(");
    }
    bool has_const_writes = false;
    uint32_t component_write_count = 0;
    EmitSource(".");
    for (uint32_t i = 0; i < 4; ++i) {
      if (result.write_mask[i]) {
        if (result.components[i] == SwizzleSource::k0 ||
            result.components[i] == SwizzleSource::k1) {
          has_const_writes = true;
        }
        ++component_write_count;
        EmitSource("%c", GetCharForSwizzle(GetSwizzleFromComponentIndex(i)));
      }
    }
    EmitSource(" = ");
    if (has_const_writes) {
      if (component_write_count > 1) {
        EmitSource("float%u(", component_write_count);
      }
      bool has_written = false;
      for (uint32_t i = 0; i < 4; ++i) {
        if (result.write_mask[i]) {
          if (has_written) {
            EmitSource(", ");
          }
          has_written = true;
          switch (result.components[i]) {
            case SwizzleSource::k0:
              EmitSource("0.0");
              break;
            case SwizzleSource::k1:
              EmitSource("1.0");
              break;
            default:
              if (source_is_scalar) {
                EmitSource("xe_ps");
              } else {
                EmitSource("xe_pv.%c", GetCharForSwizzle(result.components[i]));
              }
              break;
          }
        }
      }
      if (component_write_count > 1) {
        EmitSource(")");
      }
    } else {
      if (source_is_scalar) {
        EmitSource("xe_ps");
        if (component_write_count > 1) {
          EmitSource(".x");
          if (component_write_count > 2) {
            EmitSource("x");
            if (component_write_count > 3) {
              EmitSource("x");
            }
          }
        }
      } else {
        EmitSource("xe_pv.");
        for (uint32_t i = 0; i < 4; ++i) {
          if (result.write_mask[i]) {
            EmitSource("%c", GetCharForSwizzle(result.components[i]));
          }
        }
      }
    }
    if (result.is_clamped) {
      EmitSource(")");
    }
  }
  EmitSource(";\n");
}

void HlslShaderTranslator::ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_);

  if (instr.operand_count < 2 ||
      instr.operand[1].storage_source !=
      InstructionStorageSource::kVertexFetchConstant) {
    assert_always();
    return;
  }

  bool conditional_emitted = BeginPredicatedInstruction(
      instr.is_predicated, instr.predicate_condition);

  // Load the element from the virtual memory as uints and swap.
  EmitLoadOperand(0, instr.operands[0]);
  const char* load_swizzle;
  const char* load_function_suffix;
  switch (instr.attributes.data_format) {
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_16_16_16_16_FLOAT:
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_FLOAT:
      load_swizzle = ".xy";
      load_function_suffix = "2";
      break;
    case VertexFormat::k_32_32_32_FLOAT:
      load_swizzle = ".xyz";
      load_function_suffix = "3";
      break;
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_32_32_32_32_FLOAT:
      load_swizzle = "";
      load_function_suffix = "4";
      break;
    default:
      load_swizzle = ".x";
      load_function_suffix = "";
      break;
  }
  EmitSourceDepth("xe_vertex_element%s = XeSwap(xe_virtual_memory.Load%s(\n",
                  load_swizzle, load_function_suffix);
  EmitSourceDepth("    (xe_vertex_fetch[%u].x & 0x1FFFFFFCu) + "
                  "uint(xe_src0.x) * %u + %u),\n",
      instr.operands[1].storage_index, instr.attributes.stride * 4,
      instr.attributes.offset * 4);
  EmitSourceDepth("    xe_vertex_fetch[%u].y);\n",
                  instr.operands[1].storage_index);

  // Convert to the target format.
  switch (instr.attributes.data_format) {
    case VertexFormat::k_8_8_8_8:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxxx >>\n");
      EmitSourceDepth("    uint4(0u, 8u, 16u, 24u)) & 255u;\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv = float4(int4(xe_vertex_element << 24u) >> 24);\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv = max(xe_pv * (1.0 / 127.0), (-1.0).xxxx);\n");
        } else {
          EmitSourceDepth("xe_pv *= 1.0 / 255.0;\n");
        }
      }
      break;
    case VertexFormat::k_2_10_10_10:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxxx >>\n"
      EmitSourceDepth(
          "    uint4(0u, 10u, 20u, 30u)) & uint4((1023u).xxx, 3u);\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv = float4(int4(xe_vertex_element << uint4((22u).xxx, 3u))\n");
        EmitSourceDepth("    >> int4((22).xxx, 3));\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv = max(xe_pv * float4((1.0 / 511.0).xxx, 1.0), "
                          "(-1.0).xxxx);\n");
        } else {
          EmitSourceDepth("xe_pv *= float4((1.0 / 1023.0).xxx, 1.0 / 3.0);\n");
        }
      }
      break;
    case VertexFormat::k_10_11_11:
      EmitSourceDepth("xe_vertex_element.xyz = (xe_vertex_element.xxx >>\n"
      EmitSourceDepth(
          "    uint3(0u, 11u, 22u)) & uint3(2047u, 2047u, 1023u);\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.xyz = float3(int3(xe_vertex_element.xyz <<\n");
        EmitSourceDepth("    uint3(21u, 21u, 22u)) >> int3(21, 21, 22));\n");
      } else {
        EmitSourceDepth("xe_pv.xyz = float3(xe_vertex_element.xyz);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv.xyz = max(xe_pv.xyz *\n");
          EmitSourceDepth(
              "    float3((1.0 / 1023.0).xx, 1.0 / 511.0), (-1.0).xxx);\n");
        } else {
          EmitSourceDepth(
              "xe_pv.xyz *= float3((1.0 / 2047.0).xx, 1.0 / 1023.0);\n");
        }
      }
      EmitSourceDepth("xe_pv.w = 1.0;\n");
      break;
    case VertexFormat::k_11_11_10:
      EmitSourceDepth("xe_vertex_element.xyz = (xe_vertex_element.xxx >>\n"
      EmitSourceDepth(
          "    uint3(0u, 10u, 21u)) & uint3(1023u, 2047u, 2047u);\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.xyz = float3(int3(xe_vertex_element.xyz <<\n");
        EmitSourceDepth("    uint3(22u, 21u, 21u)) >> int3(22, 21, 21));\n");
      } else {
        EmitSourceDepth("xe_pv.xyz = float3(xe_vertex_element.xyz);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv.xyz = max(xe_pv.xyz *\n");
          EmitSourceDepth(
              "    float3(1.0 / 511.0, (1.0 / 1023.0).xx), (-1.0).xxx);\n");
        } else {
          EmitSourceDepth(
              "xe_pv.xyz *= float3(1.0 / 1023.0, (1.0 / 2047.0).xx);\n");
        }
      }
      EmitSourceDepth("xe_pv.w = 1.0;\n");
      break;
    case VertexFormat::k_16_16:
      EmitSourceDepth("xe_vertex_element.xy = (xe_vertex_element.xx >>\n");
      EmitSourceDepth("    uint2(0u, 16u)) & 65535u;\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv.xy = float2(int2(xe_vertex_element.xy << 16u) >> 16);\n");
      } else {
        EmitSourceDepth("xe_pv.xy = float2(xe_vertex_element.xy);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth(
              "xe_pv.xy = max(xe_pv.xy * (1.0 / 32767.0), (-1.0).xx);\n");
        } else {
          EmitSourceDepth("xe_pv.xy *= 1.0 / 65535.0;\n");
        }
      }
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_16_16_16_16:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxxx >>\n");
      EmitSourceDepth("    uint4(0u, 16u, 0u, 16u)) & 65535u;\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv = float4(int4(xe_vertex_element << 16u) >> 16);\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth(
              "xe_pv = max(xe_pv * (1.0 / 32767.0), (-1.0).xxxx);\n");
        } else {
          EmitSourceDepth("xe_pv *= 1.0 / 65535.0;\n");
        }
      }
      break;
    case VertexFormat::k_16_16_FLOAT:
      EmitSourceDepth("xe_vertex_element.xy = (xe_vertex_element.xx >>\n");
      EmitSourceDepth("    uint2(0u, 16u)) & 65535u;\n");
      EmitSourceDepth("xe_pv.xy = f16tof32(xe_vertex_element.xy);\n");
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_16_16_16_16_FLOAT:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxxx >>\n");
      EmitSourceDepth("    uint4(0u, 16u, 0u, 16u)) & 65535u;\n");
      EmitSourceDepth("xe_pv = f16tof32(xe_vertex_element);\n");
      break;
    case VertexFormat::k_32:
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.x = float(int(xe_vertex_element.x));\n");
      } else {
        EmitSourceDepth("xe_pv.x = float(xe_vertex_element.x);\n");
      }
      if (!instr.attributes.is_integer) {
        EmitSourceDepth("xe_pv.x *= asfloat(0x%Xu);\n",
                        instr.attributes.is_signed ? 0x30000000 : 0x2F800000);
      }
      EmitSourceDepth("xe_pv.yzw = float3(0.0, 0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32:
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.xy = float2(int2(xe_vertex_element.xy));\n");
      } else {
        EmitSourceDepth("xe_pv.xy = float2(xe_vertex_element.xy);\n");
      }
      if (!instr.attributes.is_integer) {
        EmitSourceDepth("xe_pv.xy *= asfloat(0x%Xu);\n",
                        instr.attributes.is_signed ? 0x30000000 : 0x2F800000);
      }
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32_32_32:
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv = float4(int4(xe_vertex_element));\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        EmitSourceDepth("xe_pv *= asfloat(0x%Xu);\n",
                        instr.attributes.is_signed ? 0x30000000 : 0x2F800000);
      }
      break;
    case VertexFormat::k_32_FLOAT:
      EmitSourceDepth("xe_pv.x = asfloat(xe_vertex_element.x);\n");
      EmitSourceDepth("xe_pv.yzw = float3(0.0, 0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32_FLOAT:
      EmitSourceDepth("xe_pv.xy = asfloat(xe_vertex_element.xy);\n");
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32_32_32_FLOAT:
      EmitSourceDepth("xe_pv = asfloat(xe_vertex_element);\n");
      break;
    case VertexFormat::k_32_32_32_FLOAT:
      EmitSourceDepth("xe_pv.xyz = asfloat(xe_vertex_element.xyz);\n");
      EmitSourceDepth("xe_pv.w = 1.0;\n");
      break;
  }

  EmitStoreResult(instr.result, false);

  EndPredicatedInstruction(conditional_emitted);
}

uint32_t HlslShaderTranslator::AddSRVBinding(SRVType type,
                                             uint32_t fetch_constant) {
  for (uint32_t i = 0; i < srv_bindings_.size(); ++i) {
    const SRVBinding& binding = srv_bindings_[i];
    if (binding.type == type && binding.fetch_constant == fetch_constant) {
      return i;
    }
  }
  SRVBinding new_binding;
  new_binding.type = type;
  new_binding.fetch_constant = fetch_constant;
  srv_bindings_.push_back(new_binding);
  return uint32_t(srv_bindings_.size() - 1);
}

uint32_t HlslShaderTranslator::AddSampler(uint32_t fetch_constant) {
  for (uint32_t i = 0; i < sampler_count_; ++i) {
    if (sampler_fetch_constants_[i] == fetch_constant) {
      return i;
    }
  }
  sampler_fetch_constants_[sampler_count_] = fetch_constant;
  return sampler_count_++;
}

void HlslShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  bool conditional_emitted = BeginPredicatedInstruction(
      instr.is_predicated, instr.predicate_condition);

  for (size_t i = 0; i < instr.operand_count; ++i) {
    EmitLoadOperand(i, instr.operands[i]);
  }

  switch (instr.vector_opcode) {
    case AluVectorOpcode::kAdd:
      EmitSourceDepth("xe_pv = xe_src0 + xe_src1;\n");
      break;
    case AluVectorOpcode::kMul:
      EmitSourceDepth("xe_pv = xe_src0 * xe_src1;\n");
      break;
    case AluVectorOpcode::kMax:
      EmitSourceDepth("xe_pv = max(xe_src0, xe_src1);\n");
      break;
    case AluVectorOpcode::kSeq:
      EmitSourceDepth("xe_pv = float4(xe_src0 == xe_src1);\n");
      break;
    case AluVectorOpcode::kSgt:
      EmitSourceDepth("xe_pv = float4(xe_src0 > xe_src1);\n");
      break;
    case AluVectorOpcode::kSge:
      EmitSourceDepth("xe_pv = float4(xe_src0 >= xe_src1);\n");
      break;
    case AluVectorOpcode::kSne:
      EmitSourceDepth("xe_pv = float4(xe_src0 != xe_src1);\n");
      break;
    case AluVectorOpcode::kFrc:
      EmitSourceDepth("xe_pv = frac(xe_src0);\n");
      break;
    case AluVectorOpcode::kTrunc:
      EmitSourceDepth("xe_pv = trunc(xe_src0);\n");
      break;
    case AluVectorOpcode::kFloor:
      EmitSourceDepth("xe_pv = floor(xe_src0);\n");
      break;
    case AluVectorOpcode::kMad:
      EmitSourceDepth("xe_pv = xe_src0 * xe_src1 + xe_src2;\n");
      break;
    case AluVectorOpcode::kCndEq:
      EmitSourceDepth(
          "xe_pv = lerp(xe_src2, xe_src1, float4(xe_src0 == (0.0).xxxx));\n");
      break;
    case AluVectorOpcode::kCndGe:
      EmitSourceDepth(
          "xe_pv = lerp(xe_src2, xe_src1, float4(xe_src0 >= (0.0).xxxx));\n");
      break;
    case AluVectorOpcode::kCndGt:
      EmitSourceDepth(
          "xe_pv = lerp(xe_src2, xe_src1, float4(xe_src0 > (0.0).xxxx));\n");
      break;
    case AluVectorOpcode::kDp4:
      EmitSourceDepth("xe_pv = dot(xe_src0, xe_src1).xxxx;\n");
      break;
    case AluVectorOpcode::kDp3:
      EmitSourceDepth("xe_pv = dot(xe_src0.xyz, xe_src1.xyz).xxxx;\n");
      break;
    case AluVectorOpcode::kDp2Add:
      EmitSourceDepth(
          "xe_pv = (dot(xe_src0.xy, xe_src1.xy) + xe_src2.x).xxxx;\n");
      break;
    case AluVectorOpcode::kCube:
      EmitSourceDepth("xe_pv = XeCubeTo2D(xe_src0);\n");
      cube_used_ = true;
      break;
    case AluVectorOpcode::kMax4:
      EmitSourceDepth("xe_pv.xy = max(xe_src0.xy, xe_src0.zw);\n");
      EmitSourceDepth("xe_pv.xxxx = max(xe_pv.x, xe_pv.y);\n");
      break;
    case AluVectorOpcode::kSetpEqPush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w == 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x == 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kSetpNePush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w != 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x != 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kSetpGtPush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w > 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x > 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kSetpGePush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w >= 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x >= 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kKillEq:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 == xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kKillGt:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 > xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kKillGe:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 >= xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kKillNe:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 != xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kDst:
      EmitSourceDepth("xe_pv.x = 1.0;\n");
      EmitSourceDepth("xe_pv.y = xe_src0.y * xe_src1.y;\n");
      EmitSourceDepth("xe_pv.z = xe_src0.z;\n");
      EmitSourceDepth("xe_pv.w = xe_src1.w;\n");
      break;
    case AluVectorOpcode::kMaxA:
      EmitSourceDepth("xe_a0 = clamp(int(round(xe_src0.w)), -256, 255);\n");
      EmitSourceDepth("xe_pv = max(xe_src0, xe_src1);\n");
      break;
  }

  EmitStoreResult(instr.result, false);

  EndPredicatedInstruction(conditional_emitted);
}

}  // namespace gpu
}  // namespace xe
