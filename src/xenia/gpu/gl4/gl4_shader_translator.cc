/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_shader_translator.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/gpu_flags.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::ucode;
using namespace xe::gpu::xenos;

#define Append(...) output_.AppendFormat(__VA_ARGS__)

static const char chan_names[] = {
    'x', 'y', 'z', 'w',
    // these only apply to FETCH dst's, and we shouldn't be using them:
    '0', '1', '?', '_',
};

const char* GetVertexFormatTypeName(const GL4Shader::BufferDescElement& el) {
  switch (el.format) {
    case VertexFormat::k_32:
    case VertexFormat::k_32_FLOAT:
      return "float";
    case VertexFormat::k_16_16:
    case VertexFormat::k_32_32:
    case VertexFormat::k_16_16_FLOAT:
    case VertexFormat::k_32_32_FLOAT:
      return "vec2";
    case VertexFormat::k_10_11_11:
    case VertexFormat::k_11_11_10:
    case VertexFormat::k_32_32_32_FLOAT:
      return "vec3";
    case VertexFormat::k_8_8_8_8:
    case VertexFormat::k_2_10_10_10:
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_16_16_16_16_FLOAT:
    case VertexFormat::k_32_32_32_32_FLOAT:
      return "vec4";
    default:
      XELOGE("Unknown vertex format: %d", el.format);
      assert_always();
      return "vec4";
  }
}

GL4ShaderTranslator::GL4ShaderTranslator() : output_(kOutputCapacity) {}

GL4ShaderTranslator::~GL4ShaderTranslator() = default;

void GL4ShaderTranslator::Reset(GL4Shader* shader) {
  output_.Reset();
  shader_type_ = shader->type();
  dwords_ = shader->data();
}

std::string GL4ShaderTranslator::TranslateVertexShader(
    GL4Shader* vertex_shader, const xe_gpu_program_cntl_t& program_cntl) {
  Reset(vertex_shader);

  // Normal shaders only, for now.
  // TODO(benvanik): transform feedback/memexport.
  // 0 = normal
  // 2 = point size
  assert_true(program_cntl.vs_export_mode == 0 ||
              program_cntl.vs_export_mode == 2);

  // Add vertex shader input.
  uint32_t el_index = 0;
  const auto& buffer_inputs = vertex_shader->buffer_inputs();
  for (uint32_t n = 0; n < buffer_inputs.count; n++) {
    const auto& input = buffer_inputs.descs[n];
    for (uint32_t m = 0; m < input.element_count; m++) {
      const auto& el = input.elements[m];
      const char* type_name = GetVertexFormatTypeName(el);
      const auto& fetch = el.vtx_fetch;
      uint32_t fetch_slot = fetch.const_index * 3 + fetch.const_index_sel;
      Append("layout(location = %d) in %s vf%u_%d;\n", el_index, type_name,
             fetch_slot, fetch.offset);
      el_index++;
    }
  }

  // Vertex shader main() header.
  Append("void processVertex(const in StateData state) {\n");

  // Add temporaries for any registers we may use.
  uint32_t temp_regs = program_cntl.vs_regs + program_cntl.ps_regs;
  for (uint32_t n = 0; n <= temp_regs; n++) {
    Append("  vec4 r%d = state.float_consts[%d];\n", n, n);
  }

#if FLOW_CONTROL
  // Add temporary integer registers for loops that we may use.
  // Each loop uses an address, counter, and constant
  // TODO: Implement only for the used loops in the shader
  for (uint32_t n = 0; n < 32; n++) {
    Append("  int i%d_cnt = 0;\n", n);
    Append("  int i%d_addr = 0;\n", n);
  }
#endif

  Append("  vec4 t;\n");
  Append("  vec4 pv;\n");   // Previous Vector result.
  Append("  float ps;\n");  // Previous Scalar result (used for RETAIN_PREV).
  Append("  bool p = false;\n");  // Predicate temp, clause-local.
  Append("  int a0 = 0;\n");      // Address register.

  // Execute blocks.
  TranslateBlocks(vertex_shader);

  Append("}\n");
  return output_.to_string();
}

std::string GL4ShaderTranslator::TranslatePixelShader(
    GL4Shader* pixel_shader, const xe_gpu_program_cntl_t& program_cntl) {
  Reset(pixel_shader);

  // We need an input VS to make decisions here.
  // TODO(benvanik): do we need to pair VS/PS up and store the combination?
  // If the same PS is used with different VS that output different amounts
  // (and less than the number of required registers), things may die.

  // Pixel shader main() header.
  Append("void processFragment(const in StateData state) {\n");

  // Add temporary registers.
  uint32_t temp_regs = program_cntl.vs_regs + program_cntl.ps_regs;
  for (uint32_t n = 0; n <= std::max(15u, temp_regs); n++) {
    Append("  vec4 r%d = state.float_consts[%d];\n", n, n + 256);
  }
  Append("  vec4 t;\n");
  Append("  vec4 pv;\n");   // Previous Vector result.
  Append("  float ps;\n");  // Previous Scalar result (used for RETAIN_PREV).
  Append("  bool p = false;\n");  // Predicate temp, clause-local.
  Append("  int a0 = 0;\n");      // Address register.

  // Bring registers local.
  for (uint32_t n = 0; n < kMaxInterpolators; n++) {
    Append("  r%d = vtx.o[%d];\n", n, n);
  }

  // Execute blocks.
  TranslateBlocks(pixel_shader);

  Append("}\n");
  return output_.to_string();
}

void GL4ShaderTranslator::AppendSrcReg(const instr_alu_t& op, int i) {
  switch (i) {
    case 1: {
      int const_slot = 0;
      AppendSrcReg(op, op.src1_reg, op.src1_sel, op.src1_swiz,
                   op.src1_reg_negate, const_slot);
      break;
    }
    case 2: {
      int const_slot = op.src1_sel ? 1 : 0;
      AppendSrcReg(op, op.src2_reg, op.src2_sel, op.src2_swiz,
                   op.src2_reg_negate, const_slot);
      break;
    }
    case 3: {
      int const_slot = (op.src1_sel || op.src2_sel) ? 1 : 0;
      AppendSrcReg(op, op.src3_reg, op.src3_sel, op.src3_swiz,
                   op.src3_reg_negate, const_slot);
      break;
    }
  }
}

void GL4ShaderTranslator::AppendSrcReg(const instr_alu_t& op, uint32_t num,
                                       uint32_t type, uint32_t swiz,
                                       uint32_t negate, int const_slot) {
  if (negate) {
    Append("-");
  }
  if (type) {
    // Register.
    if (num & 0x80) {
      Append("abs(");
    }
    Append("r%u", num & 0x7F);
    if (num & 0x80) {
      Append(")");
    }
  } else {
    // Constant.
    if (op.abs_constants) {
      Append("abs(");
    }
    Append("state.float_consts[");
#if FLOW_CONTROL
    // NOTE(dariosamo): Some games don't seem to take into account the relative
    // a0
    // offset even when they should due to const_slot being a different value.
    if (op.const_0_rel_abs || op.const_1_rel_abs) {
#else
    if ((const_slot == 0 && op.const_0_rel_abs) ||
        (const_slot == 1 && op.const_1_rel_abs)) {
#endif
      if (op.relative_addr) {
        assert_true(num < 256);
        Append("a0 + %u", is_pixel_shader() ? num + 256 : num);
      } else {
        Append("a0");
      }
    } else {
      assert_true(num < 256);
      Append("%u", is_pixel_shader() ? num + 256 : num);
    }
    Append("]");
    if (op.abs_constants) {
      Append(")");
    }
  }
  if (swiz) {
    Append(".");
    for (int i = 0; i < 4; i++) {
      Append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
}

void GL4ShaderTranslator::PrintSrcReg(uint32_t num, uint32_t type,
                                      uint32_t swiz, uint32_t negate,
                                      uint32_t abs_constants) {
  if (negate) {
    Append("-");
  }
  if (type) {
    if (num & 0x80) {
      Append("|");
    }
    Append("R%u", num & 0x7F);
    if (num & 0x80) {
      Append("|");
    }
  } else {
    if (abs_constants) {
      Append("|");
    }
    num += is_pixel_shader() ? 256 : 0;
    Append("C%u", num);
    if (abs_constants) {
      Append("|");
    }
  }
  if (swiz) {
    Append(".");
    for (int i = 0; i < 4; i++) {
      Append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
}

void GL4ShaderTranslator::PrintVectorDstReg(const instr_alu_t& alu) {
  Append("%s%u", alu.export_data ? "export" : "R", alu.vector_dest);
  auto mask = alu.scalar_write_mask;
  if (mask != 0xf) {
    Append(".");
    for (int i = 0; i < 4; i++) {
      Append("%c", (mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void GL4ShaderTranslator::PrintScalarDstReg(const instr_alu_t& alu) {
  Append("%s%u", alu.export_data ? "export" : "R",
         alu.export_data ? alu.vector_dest : alu.scalar_dest);
  auto mask = alu.scalar_write_mask;
  if (mask != 0xf) {
    Append(".");
    for (int i = 0; i < 4; i++) {
      Append("%c", (mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void GL4ShaderTranslator::PrintExportComment(uint32_t num) {
  const char* name = nullptr;
  switch (shader_type_) {
    case ShaderType::kVertex:
      switch (num) {
        case 62:
          name = "gl_Position";
          break;
        case 63:
          name = "gl_PointSize";
          break;
        default:
          name = "??";
          break;
      }
      break;
    case ShaderType::kPixel:
      switch (num) {
        case 0:
          name = "gl_FragColor";
          break;
        default:
          name = "??";
          break;
      }
      break;
  }
  /* if we had a symbol table here, we could look
   * up the name of the varying..
   */
  if (name) {
    Append("\t; %s", name);
  }
}

void GL4ShaderTranslator::BeginAppendVectorOp(const ucode::instr_alu_t& op) {
  Append("  pv = (");
}

void GL4ShaderTranslator::AppendVectorOpSrcReg(const ucode::instr_alu_t& op,
                                               int i) {
  AppendSrcReg(op, i);
}

void GL4ShaderTranslator::EndAppendVectorOp(const ucode::instr_alu_t& op,
                                            uint32_t append_flags) {
  Append(");\n");
  if (op.vector_clamp) {
    Append("  pv = clamp(pv, 0.0, 1.0);\n");
  }

  // Special case exports.
  // TODO(benvanik): special write that only chooses one field -- what field? x?
  if (op.export_data) {
    switch (shader_type_) {
      case ShaderType::kVertex:
        switch (op.vector_dest) {
          case 63:
            // Append("  gl_PointSize = pv.x;\n");
            assert_zero(op.vector_write_mask);
            return;
        }
        break;
      case ShaderType::kPixel:
        switch (op.vector_dest) {
          case 61:
            // Append("  gl_FragDepth = pv.x;\n");
            assert_zero(op.vector_write_mask);
            return;
        }
        break;
    }
  }

  if (op.export_data) {
    // Export; this does some weird stuff to do special consts 0 and 1.
    uint32_t write_mask = op.vector_write_mask;
    uint32_t const_1_mask = op.scalar_write_mask;
    for (int i = 0; i < 4; ++i, write_mask >>= 1, const_1_mask >>= 1) {
      if (write_mask & 0x1) {
        Append("  ");
        AppendOpDestRegName(op, op.vector_dest);
        Append(".%c = ", chan_names[i]);
        if (const_1_mask & 0x1) {
          // Special export of constant 1.
          Append("1.0");
        } else {
          // Normal source from calculated pv.
          Append("pv.%c", chan_names[i]);
        }
        Append(";\n");
      } else if (op.scalar_dest_rel) {
        // Special export of constant value 0.
        Append("  ");
        AppendOpDestRegName(op, op.vector_dest);
        Append(".%c = 0.0;\n", chan_names[i]);
      }
    }
  } else {
    // Normal reg; just mask.
    uint32_t write_mask = op.vector_write_mask;
    for (int i = 0; i < 4; ++i, write_mask >>= 1) {
      if (write_mask & 0x1) {
        Append("  ");
        AppendOpDestRegName(op, op.vector_dest);
        Append(".%c = pv.%c;\n", chan_names[i], chan_names[i]);
      }
    }
  }
}

void GL4ShaderTranslator::BeginAppendScalarOp(const ucode::instr_alu_t& op) {
  Append("  ps = (");
}

void GL4ShaderTranslator::AppendScalarOpSrcReg(const ucode::instr_alu_t& op,
                                               int i) {
  AppendSrcReg(op, i);
}

void GL4ShaderTranslator::EndAppendScalarOp(const ucode::instr_alu_t& op,
                                            uint32_t append_flags) {
  Append(").x;\n");
  if (op.scalar_clamp) {
    Append("  ps = clamp(ps, 0.0, 1.0);\n");
  }

  uint32_t dest_num;
  uint32_t write_mask;
  if (op.export_data) {
    dest_num = op.vector_dest;
    write_mask = op.scalar_write_mask & ~op.vector_write_mask;
  } else {
    dest_num = op.scalar_dest;
    write_mask = op.scalar_write_mask;
  }

  // Mask out certain fields.
  for (int i = 0; i < 4; ++i, write_mask >>= 1) {
    if (write_mask & 0x1) {
      Append("  ");
      AppendOpDestRegName(op, dest_num);
      Append(".%c = ps;\n", chan_names[i]);
    }
  }
}

void GL4ShaderTranslator::AppendOpDestRegName(const ucode::instr_alu_t& op,
                                              uint32_t dest_num) {
  if (!op.export_data) {
    // Register.
    // TODO(benvanik): relative? abs? etc
    Append("r%u", dest_num);
  } else {
    // Export.
    switch (shader_type_) {
      case ShaderType::kVertex:
        switch (dest_num) {
          case 62:
            Append("gl_Position");
            break;
          case 63:
            Append("gl_PointSize");
            break;
          default:
            // Varying.
            Append("vtx.o[%u]", dest_num);
            break;
        }
        break;
      case ShaderType::kPixel:
        switch (dest_num) {
          case 0:
          case 63:  // ? masked?
            Append("oC[0]");
            break;
          case 1:
            Append("oC[1]");
            break;
          case 2:
            Append("oC[2]");
            break;
          case 3:
            Append("oC[3]");
            break;
          case 61:
            Append("gl_FragDepth");
            break;
          default:
            // TODO(benvanik): other render targets?
            assert_always();
            break;
        }
        break;
    }
  }
}

bool GL4ShaderTranslator::TranslateALU_ADDv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  AppendVectorOpSrcReg(alu, 1);
  Append(" + ");
  AppendVectorOpSrcReg(alu, 2);
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MULv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  AppendVectorOpSrcReg(alu, 1);
  Append(" * ");
  AppendVectorOpSrcReg(alu, 2);
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MAXv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  if (alu.src1_reg == alu.src2_reg && alu.src1_sel == alu.src2_sel &&
      alu.src1_swiz == alu.src2_swiz &&
      alu.src1_reg_negate == alu.src2_reg_negate) {
    // This is a mov.
    AppendVectorOpSrcReg(alu, 1);
  } else {
    Append("max(");
    AppendVectorOpSrcReg(alu, 1);
    Append(", ");
    AppendVectorOpSrcReg(alu, 2);
    Append(")");
  }
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MINv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("min(");
  AppendVectorOpSrcReg(alu, 1);
  Append(", ");
  AppendVectorOpSrcReg(alu, 2);
  Append(")");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SETXXv(const instr_alu_t& alu,
                                              const char* op) {
  BeginAppendVectorOp(alu);
  Append("vec4((");
  AppendVectorOpSrcReg(alu, 1);
  Append(").x %s (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").x ? 1.0 : 0.0, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(").y %s (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").y ? 1.0 : 0.0, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(").z %s (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").z ? 1.0 : 0.0, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(").w %s (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").w ? 1.0 : 0.0)");
  EndAppendVectorOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_SETEv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, "==");
}
bool GL4ShaderTranslator::TranslateALU_SETGTv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, ">");
}
bool GL4ShaderTranslator::TranslateALU_SETGTEv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, ">=");
}
bool GL4ShaderTranslator::TranslateALU_SETNEv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, "!=");
}

bool GL4ShaderTranslator::TranslateALU_FRACv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("fract(");
  AppendVectorOpSrcReg(alu, 1);
  Append(")");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_TRUNCv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("trunc(");
  AppendVectorOpSrcReg(alu, 1);
  Append(")");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_FLOORv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("floor(");
  AppendVectorOpSrcReg(alu, 1);
  Append(")");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MULADDv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("(");
  AppendVectorOpSrcReg(alu, 1);
  Append(" * ");
  AppendVectorOpSrcReg(alu, 2);
  Append(") + ");
  AppendVectorOpSrcReg(alu, 3);
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_CNDXXv(const instr_alu_t& alu,
                                              const char* op) {
  BeginAppendVectorOp(alu);
  // TODO(benvanik): check argument order - could be 3 as compare and 1 and 2 as
  // values.
  Append("vec4((");
  AppendVectorOpSrcReg(alu, 1);
  Append(").x %s 0.0 ? (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").x : (");
  AppendVectorOpSrcReg(alu, 3);
  Append(").x, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(").y %s 0.0 ? (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").y : (");
  AppendVectorOpSrcReg(alu, 3);
  Append(").y, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(").z %s 0.0 ? (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").z : (");
  AppendVectorOpSrcReg(alu, 3);
  Append(").z, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(").w %s 0.0 ? (", op);
  AppendVectorOpSrcReg(alu, 2);
  Append(").w : (");
  AppendVectorOpSrcReg(alu, 3);
  Append(").w)");
  EndAppendVectorOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_CNDEv(const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(alu, "==");
}
bool GL4ShaderTranslator::TranslateALU_CNDGTEv(const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(alu, ">=");
}
bool GL4ShaderTranslator::TranslateALU_CNDGTv(const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(alu, ">");
}

bool GL4ShaderTranslator::TranslateALU_DOT4v(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("dot(");
  AppendVectorOpSrcReg(alu, 1);
  Append(", ");
  AppendVectorOpSrcReg(alu, 2);
  Append(").xxxx");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_DOT3v(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("dot(vec4(");
  AppendVectorOpSrcReg(alu, 1);
  Append(").xyz, vec4(");
  AppendVectorOpSrcReg(alu, 2);
  Append(").xyz).xxxx");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_DOT2ADDv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("dot(vec4(");
  AppendVectorOpSrcReg(alu, 1);
  Append(").xy, vec4(");
  AppendVectorOpSrcReg(alu, 2);
  Append(").xy).xxxx + ");
  AppendVectorOpSrcReg(alu, 3);
  Append(".xxxx");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_CUBEv(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("cube(");
  AppendVectorOpSrcReg(alu, 1);
  Append(", ");
  AppendVectorOpSrcReg(alu, 2);
  Append(")");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MAX4v(const instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("max(");
  Append("max(");
  Append("max(");
  AppendVectorOpSrcReg(alu, 1);
  Append(".x, ");
  AppendVectorOpSrcReg(alu, 1);
  Append(".y), ");
  AppendVectorOpSrcReg(alu, 1);
  Append(".z), ");
  AppendVectorOpSrcReg(alu, 1);
  Append(".w).xxxx");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_PRED_SETXX_PUSHv(
    const ucode::instr_alu_t& alu, const char* op) {
  Append("  p = ((");
  AppendVectorOpSrcReg(alu, 1);
  Append(".w == 0.0) && (");
  AppendVectorOpSrcReg(alu, 2);
  Append(".w %s 0.0)) ? true : false;\n", op);
  BeginAppendVectorOp(alu);
  Append("((");
  AppendVectorOpSrcReg(alu, 1);
  Append(".x == 0.0) && (");
  AppendVectorOpSrcReg(alu, 2);
  Append(".x %s 0.0)) ? vec4(0.0) : ", op);
  AppendVectorOpSrcReg(alu, 1);
  Append(" + vec4(1.0)");
  EndAppendVectorOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETE_PUSHv(
    const ucode::instr_alu_t& alu) {
  return TranslateALU_PRED_SETXX_PUSHv(alu, "==");
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETNE_PUSHv(
    const ucode::instr_alu_t& alu) {
  return TranslateALU_PRED_SETXX_PUSHv(alu, "!=");
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETGT_PUSHv(
    const ucode::instr_alu_t& alu) {
  return TranslateALU_PRED_SETXX_PUSHv(alu, ">");
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETGTE_PUSHv(
    const ucode::instr_alu_t& alu) {
  return TranslateALU_PRED_SETXX_PUSHv(alu, ">=");
}

bool GL4ShaderTranslator::TranslateALU_DSTv(const ucode::instr_alu_t& alu) {
  BeginAppendVectorOp(alu);
  Append("vec4(1.0, (");
  AppendVectorOpSrcReg(alu, 1);
  Append(".y * ");
  AppendVectorOpSrcReg(alu, 1);
  Append(".y), ");
  AppendVectorOpSrcReg(alu, 1);
  Append(".z, ");
  AppendVectorOpSrcReg(alu, 2);
  Append(".w)");
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MOVAv(const ucode::instr_alu_t& alu) {
  Append("  a0 = clamp(int(floor(");
  AppendVectorOpSrcReg(alu, 1);
  Append(".w + 0.5)), -256, 255);\n");
  BeginAppendVectorOp(alu);
  if (alu.src1_reg == alu.src2_reg && alu.src1_sel == alu.src2_sel &&
      alu.src1_swiz == alu.src2_swiz &&
      alu.src1_reg_negate == alu.src2_reg_negate) {
    // This is a mov.
    AppendVectorOpSrcReg(alu, 1);
  } else {
    Append("max(");
    AppendVectorOpSrcReg(alu, 1);
    Append(", ");
    AppendVectorOpSrcReg(alu, 2);
    Append(")");
  }
  EndAppendVectorOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_ADDs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  AppendScalarOpSrcReg(alu, 3);
  Append(".x + ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".z");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_ADD_PREVs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  AppendSrcReg(alu, 3);
  Append(".x + ps");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MULs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  AppendScalarOpSrcReg(alu, 3);
  Append(".x * ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".z");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MUL_PREVs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  AppendSrcReg(alu, 3);
  Append(".x * ps");
  EndAppendScalarOp(alu);
  return true;
}

// ...

bool GL4ShaderTranslator::TranslateALU_MAXs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  if ((alu.src3_swiz & 0x3) == (((alu.src3_swiz >> 2) + 1) & 0x3)) {
    // This is a mov.
    AppendScalarOpSrcReg(alu, 3);
  } else {
    Append("max(");
    AppendScalarOpSrcReg(alu, 3);
    Append(".x, ");
    AppendScalarOpSrcReg(alu, 3);
    Append(".y)");
  }
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MINs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("min(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x, ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".y)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SETXXs(const instr_alu_t& alu,
                                              const char* op) {
  BeginAppendScalarOp(alu);
  Append("(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x %s 0.0) ? 1.0 : 0.0", op);
  EndAppendScalarOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_SETEs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, "==");
}
bool GL4ShaderTranslator::TranslateALU_SETGTs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, ">");
}
bool GL4ShaderTranslator::TranslateALU_SETGTEs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, ">=");
}
bool GL4ShaderTranslator::TranslateALU_SETNEs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, "!=");
}

bool GL4ShaderTranslator::TranslateALU_FRACs(const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("fract(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_TRUNCs(const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("trunc(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_FLOORs(const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("floor(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_EXP_IEEE(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("pow(2.0, ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_LOG_CLAMP(
    const ucode::instr_alu_t& alu) {
  Append("  ps = log2(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x);");
  BeginAppendScalarOp(alu);
  Append("isinf(ps) && ps < 0.0 ? -FLT_MAX : ps");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_LOG_IEEE(const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("log2(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIP_CLAMP(const instr_alu_t& alu) {
  // if result == -inf result = -flt_max
  // if result == +inf result = flt_max
  BeginAppendScalarOp(alu);
  Append("1.0 / ");
  AppendScalarOpSrcReg(alu, 3);
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIP_FF(const instr_alu_t& alu) {
  // if result == -inf result = -zero
  // if result == +inf result = zero
  BeginAppendScalarOp(alu);
  Append("1.0 / ");
  AppendScalarOpSrcReg(alu, 3);
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIP_IEEE(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("1.0 / ");
  AppendScalarOpSrcReg(alu, 3);
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIPSQ_CLAMP(
    const ucode::instr_alu_t& alu) {
  // if result == -inf result = -flt_max
  // if result == +inf result = flt_max
  BeginAppendScalarOp(alu);
  Append("inversesqrt(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIPSQ_FF(
    const ucode::instr_alu_t& alu) {
  // if result == -inf result = -zero
  // if result == +inf result = zero
  BeginAppendScalarOp(alu);
  Append("inversesqrt(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIPSQ_IEEE(
    const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("inversesqrt(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MOVAs(const ucode::instr_alu_t& alu) {
  Append("  a0 = clamp(int(floor(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x + 0.5)), -256, 255);\n");
  BeginAppendScalarOp(alu);
  if ((alu.src3_swiz & 0x3) == (((alu.src3_swiz >> 2) + 1) & 0x3)) {
    // This is a mov.
    AppendScalarOpSrcReg(alu, 3);
  } else {
    Append("max(");
    AppendScalarOpSrcReg(alu, 3);
    Append(".x, ");
    AppendScalarOpSrcReg(alu, 3);
    Append(".y)");
  }
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MOVA_FLOORs(
    const ucode::instr_alu_t& alu) {
  Append("  a0 = clamp(int(floor(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)), -256, 255);\n");
  BeginAppendScalarOp(alu);
  if ((alu.src3_swiz & 0x3) == (((alu.src3_swiz >> 2) + 1) & 0x3)) {
    // This is a mov.
    AppendScalarOpSrcReg(alu, 3);
  } else {
    Append("max(");
    AppendScalarOpSrcReg(alu, 3);
    Append(".x, ");
    AppendScalarOpSrcReg(alu, 3);
    Append(".y)");
  }
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SUBs(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  AppendScalarOpSrcReg(alu, 3);
  Append(".x - ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".z");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SUB_PREVs(
    const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  AppendScalarOpSrcReg(alu, 3);
  Append(".x - ps");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_PRED_SETXXs(const instr_alu_t& alu,
                                                   const char* op) {
  Append("  p = ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x %s 0.0;\n", op);
  BeginAppendScalarOp(alu);
  Append("(p ? 0.0 : 1.0).xxxx");
  EndAppendScalarOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETEs(const instr_alu_t& alu) {
  return TranslateALU_PRED_SETXXs(alu, "==");
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETNEs(const instr_alu_t& alu) {
  return TranslateALU_PRED_SETXXs(alu, "!=");
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETGTs(const instr_alu_t& alu) {
  return TranslateALU_PRED_SETXXs(alu, ">");
}
bool GL4ShaderTranslator::TranslateALU_PRED_SETGTEs(const instr_alu_t& alu) {
  return TranslateALU_PRED_SETXXs(alu, ">=");
}

bool GL4ShaderTranslator::TranslateALU_PRED_SET_INVs(
    const ucode::instr_alu_t& alu) {
  Append("  ps = ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x;\n");
  Append("  if (ps == 1.0) { p = true; ps = 0.0; }\n");
  Append("  else { p = false; ps = (ps == 0.0) ? 1.0 : ps; }\n");
  BeginAppendScalarOp(alu);
  Append("ps");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_PRED_SET_POPs(
    const ucode::instr_alu_t& alu) {
  Append("  ps = ");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x - 1.0;\n");
  Append("  if (ps <= 0.0) { p = true; ps = 0.0; }\n");
  Append("  else { p = false; }\n");
  BeginAppendScalarOp(alu);
  Append("ps");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SQRT_IEEE(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("sqrt(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MUL_CONST_0(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 =
      (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  // TODO(benvanik): const slot?
  int const_slot = (alu.src1_sel || alu.src2_sel) ? 1 : 0;
  AppendSrcReg(alu, alu.src3_reg, 0, 0, alu.src3_reg_negate, 0);
  Append(".%c * ", chan_names[swiz_a]);
  AppendSrcReg(alu, reg2, 1, 0, alu.src3_reg_negate, const_slot);
  Append(".%c", chan_names[swiz_b]);
  EndAppendScalarOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_MUL_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_MUL_CONST_0(alu);
}

bool GL4ShaderTranslator::TranslateALU_ADD_CONST_0(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 =
      (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  // TODO(benvanik): const slot?
  int const_slot = (alu.src1_sel || alu.src2_sel) ? 1 : 0;
  AppendSrcReg(alu, alu.src3_reg, 0, 0, alu.src3_reg_negate, 0);
  Append(".%c + ", chan_names[swiz_a]);
  AppendSrcReg(alu, reg2, 1, 0, alu.src3_reg_negate, const_slot);
  Append(".%c", chan_names[swiz_b]);
  EndAppendScalarOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_ADD_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_ADD_CONST_0(alu);
}

bool GL4ShaderTranslator::TranslateALU_SUB_CONST_0(const instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 =
      (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  // TODO(benvanik): const slot?
  int const_slot = (alu.src1_sel || alu.src2_sel) ? 1 : 0;
  AppendSrcReg(alu, alu.src3_reg, 0, 0, alu.src3_reg_negate, 0);
  Append(".%c - ", chan_names[swiz_a]);
  AppendSrcReg(alu, reg2, 1, 0, alu.src3_reg_negate, const_slot);
  Append(".%c", chan_names[swiz_b]);
  EndAppendScalarOp(alu);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_SUB_CONST_1(const instr_alu_t& alu) {
  // Handled as switch on scalar_opc.
  return TranslateALU_SUB_CONST_0(alu);
}

bool GL4ShaderTranslator::TranslateALU_SIN(const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("sin(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_COS(const ucode::instr_alu_t& alu) {
  BeginAppendScalarOp(alu);
  Append("cos(");
  AppendScalarOpSrcReg(alu, 3);
  Append(".x)");
  EndAppendScalarOp(alu);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RETAIN_PREV(const instr_alu_t& alu) {
  // TODO(benvanik): figure out how this is used.
  // It seems like vector writes to export regs will use this to write 1's to
  // components (like w in position).
  BeginAppendScalarOp(alu);
  Append("ps");
  EndAppendScalarOp(alu);
  return true;
}

typedef bool (GL4ShaderTranslator::*TranslateFn)(const instr_alu_t& alu);
typedef struct {
  uint32_t num_srcs;
  const char* name;
  TranslateFn fn;
} TranslateInfo;
#define ALU_INSTR(opc, num_srcs) \
  { num_srcs, #opc, nullptr }
#define ALU_INSTR_IMPL(opc, num_srcs) \
  { num_srcs, #opc, &GL4ShaderTranslator::TranslateALU_##opc }

bool GL4ShaderTranslator::TranslateALU(const instr_alu_t* alu, int sync) {
  static TranslateInfo vector_alu_instrs[0x20] = {
      ALU_INSTR_IMPL(ADDv, 2),               // 0
      ALU_INSTR_IMPL(MULv, 2),               // 1
      ALU_INSTR_IMPL(MAXv, 2),               // 2
      ALU_INSTR_IMPL(MINv, 2),               // 3
      ALU_INSTR_IMPL(SETEv, 2),              // 4
      ALU_INSTR_IMPL(SETGTv, 2),             // 5
      ALU_INSTR_IMPL(SETGTEv, 2),            // 6
      ALU_INSTR_IMPL(SETNEv, 2),             // 7
      ALU_INSTR_IMPL(FRACv, 1),              // 8
      ALU_INSTR_IMPL(TRUNCv, 1),             // 9
      ALU_INSTR_IMPL(FLOORv, 1),             // 10
      ALU_INSTR_IMPL(MULADDv, 3),            // 11
      ALU_INSTR_IMPL(CNDEv, 3),              // 12
      ALU_INSTR_IMPL(CNDGTEv, 3),            // 13
      ALU_INSTR_IMPL(CNDGTv, 3),             // 14
      ALU_INSTR_IMPL(DOT4v, 2),              // 15
      ALU_INSTR_IMPL(DOT3v, 2),              // 16
      ALU_INSTR_IMPL(DOT2ADDv, 3),           // 17 -- ???
      ALU_INSTR_IMPL(CUBEv, 2),              // 18
      ALU_INSTR_IMPL(MAX4v, 1),              // 19
      ALU_INSTR_IMPL(PRED_SETE_PUSHv, 2),    // 20
      ALU_INSTR_IMPL(PRED_SETNE_PUSHv, 2),   // 21
      ALU_INSTR_IMPL(PRED_SETGT_PUSHv, 2),   // 22
      ALU_INSTR_IMPL(PRED_SETGTE_PUSHv, 2),  // 23
      ALU_INSTR(KILLEv, 2),                  // 24
      ALU_INSTR(KILLGTv, 2),                 // 25
      ALU_INSTR(KILLGTEv, 2),                // 26
      ALU_INSTR(KILLNEv, 2),                 // 27
      ALU_INSTR_IMPL(DSTv, 2),               // 28
      ALU_INSTR_IMPL(MOVAv, 1),              // 29
  };
  static TranslateInfo scalar_alu_instrs[0x40] = {
      ALU_INSTR_IMPL(ADDs, 1),           // 0
      ALU_INSTR_IMPL(ADD_PREVs, 1),      // 1
      ALU_INSTR_IMPL(MULs, 1),           // 2
      ALU_INSTR_IMPL(MUL_PREVs, 1),      // 3
      ALU_INSTR(MUL_PREV2s, 1),          // 4
      ALU_INSTR_IMPL(MAXs, 1),           // 5
      ALU_INSTR_IMPL(MINs, 1),           // 6
      ALU_INSTR_IMPL(SETEs, 1),          // 7
      ALU_INSTR_IMPL(SETGTs, 1),         // 8
      ALU_INSTR_IMPL(SETGTEs, 1),        // 9
      ALU_INSTR_IMPL(SETNEs, 1),         // 10
      ALU_INSTR_IMPL(FRACs, 1),          // 11
      ALU_INSTR_IMPL(TRUNCs, 1),         // 12
      ALU_INSTR_IMPL(FLOORs, 1),         // 13
      ALU_INSTR_IMPL(EXP_IEEE, 1),       // 14
      ALU_INSTR_IMPL(LOG_CLAMP, 1),      // 15
      ALU_INSTR_IMPL(LOG_IEEE, 1),       // 16
      ALU_INSTR_IMPL(RECIP_CLAMP, 1),    // 17
      ALU_INSTR_IMPL(RECIP_FF, 1),       // 18
      ALU_INSTR_IMPL(RECIP_IEEE, 1),     // 19
      ALU_INSTR_IMPL(RECIPSQ_CLAMP, 1),  // 20
      ALU_INSTR_IMPL(RECIPSQ_FF, 1),     // 21
      ALU_INSTR_IMPL(RECIPSQ_IEEE, 1),   // 22
      ALU_INSTR_IMPL(MOVAs, 1),          // 23
      ALU_INSTR_IMPL(MOVA_FLOORs, 1),    // 24
      ALU_INSTR_IMPL(SUBs, 1),           // 25
      ALU_INSTR_IMPL(SUB_PREVs, 1),      // 26
      ALU_INSTR_IMPL(PRED_SETEs, 1),     // 27
      ALU_INSTR_IMPL(PRED_SETNEs, 1),    // 28
      ALU_INSTR_IMPL(PRED_SETGTs, 1),    // 29
      ALU_INSTR_IMPL(PRED_SETGTEs, 1),   // 30
      ALU_INSTR_IMPL(PRED_SET_INVs, 1),  // 31
      ALU_INSTR_IMPL(PRED_SET_POPs, 1),  // 32
      ALU_INSTR(PRED_SET_CLRs, 1),       // 33
      ALU_INSTR(PRED_SET_RESTOREs, 1),   // 34
      ALU_INSTR(KILLEs, 1),              // 35
      ALU_INSTR(KILLGTs, 1),             // 36
      ALU_INSTR(KILLGTEs, 1),            // 37
      ALU_INSTR(KILLNEs, 1),             // 38
      ALU_INSTR(KILLONEs, 1),            // 39
      ALU_INSTR_IMPL(SQRT_IEEE, 1),      // 40
      {0, 0, false},                     //
      ALU_INSTR_IMPL(MUL_CONST_0, 2),    // 42
      ALU_INSTR_IMPL(MUL_CONST_1, 2),    // 43
      ALU_INSTR_IMPL(ADD_CONST_0, 2),    // 44
      ALU_INSTR_IMPL(ADD_CONST_1, 2),    // 45
      ALU_INSTR_IMPL(SUB_CONST_0, 2),    // 46
      ALU_INSTR_IMPL(SUB_CONST_1, 2),    // 47
      ALU_INSTR_IMPL(SIN, 1),            // 48
      ALU_INSTR_IMPL(COS, 1),            // 49
      ALU_INSTR_IMPL(RETAIN_PREV, 1),    // 50
  };
#undef ALU_INSTR
#undef ALU_INSTR_IMPL

  // If not an export we can fast kill if there is no write mask.
  if (alu->vector_write_mask || (alu->export_data && alu->scalar_dest_rel)) {
    // Disassemble vector op.
    const auto& iv = vector_alu_instrs[alu->vector_opc];
    Append("  //   %sALU:\t", sync ? "(S)" : "   ");
    Append("%s", iv.name);
    if (alu->pred_select) {
      // seems to work similar to conditional execution in ARM instruction
      // set, so let's use a similar syntax for now:
      Append(alu->pred_condition ? "EQ" : "NE");
    }
    Append("\t");
    PrintVectorDstReg(*alu);
    Append(" = ");
    if (iv.num_srcs == 3) {
      PrintSrcReg(alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                  alu->src3_reg_negate, alu->abs_constants);
      Append(", ");
    }
    PrintSrcReg(alu->src1_reg, alu->src1_sel, alu->src1_swiz,
                alu->src1_reg_negate, alu->abs_constants);
    if (iv.num_srcs > 1) {
      Append(", ");
      PrintSrcReg(alu->src2_reg, alu->src2_sel, alu->src2_swiz,
                  alu->src2_reg_negate, alu->abs_constants);
    }
    if (alu->vector_clamp) {
      Append(" CLAMP");
    }
    if (alu->export_data) {
      PrintExportComment(alu->vector_dest);
    }
    Append("\n");

    // Translate vector op.
    if (iv.fn) {
      if (!(this->*iv.fn)(*alu)) {
        return false;
      }
    } else {
      assert_always();
      Append("  // <UNIMPLEMENTED>\n");
    }
  }

  // TODO(benvanik): see if there's a better way to no-op this.
  if (true) {  // alu->scalar_write_mask || alu->export_data) {
    // 2nd optional scalar op:

    // Disassemble scalar op.
    const auto& is = scalar_alu_instrs[alu->scalar_opc];
    Append("  //  ");
    Append("\t");
    if (is.name) {
      Append("\t    \t%s\t", is.name);
    } else {
      Append("\t    \tOP(%u)\t", alu->scalar_opc);
    }
    PrintScalarDstReg(*alu);
    Append(" = ");
    if (is.num_srcs == 2) {
      // ADD_CONST_0 dest, [const], [reg]
      uint32_t src3_swiz = alu->src3_swiz & ~0x3C;
      uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
      uint32_t swiz_b = (src3_swiz & 0x3);
      PrintSrcReg(alu->src3_reg, 0, 0, alu->src3_reg_negate,
                  alu->abs_constants);
      Append(".%c", chan_names[swiz_a]);
      Append(", ");
      uint32_t reg2 = (alu->scalar_opc & 1) | (alu->src3_swiz & 0x3C) |
                      (alu->src3_sel << 1);
      PrintSrcReg(reg2, 1, 0, alu->src3_reg_negate, alu->abs_constants);
      Append(".%c", chan_names[swiz_b]);
    } else {
      PrintSrcReg(alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                  alu->src3_reg_negate, alu->abs_constants);
    }
    if (alu->scalar_clamp) {
      Append(" CLAMP");
    }
    if (alu->export_data) {
      PrintExportComment(alu->scalar_dest);
    }
    Append("\n");

    // Translate scalar op.
    if (is.fn) {
      if (!(this->*is.fn)(*alu)) {
        return false;
      }
    } else {
      assert_always();
      Append("  // <UNIMPLEMENTED>\n");
    }
  }

  return true;
}

void GL4ShaderTranslator::PrintDestFetch(uint32_t dst_reg, uint32_t dst_swiz) {
  Append("\tR%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    Append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

void GL4ShaderTranslator::AppendFetchDest(uint32_t dst_reg, uint32_t dst_swiz) {
  Append("r%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    Append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

void GL4ShaderTranslator::AppendPredPre(bool is_cond_cf, uint32_t cf_condition,
                                        uint32_t pred_select,
                                        uint32_t condition) {
  if (pred_select && (!is_cond_cf || cf_condition != condition)) {
    Append("  if (%cp) {\n", condition ? ' ' : '!');
  }
}

void GL4ShaderTranslator::AppendPredPost(bool is_cond_cf, uint32_t cf_condition,
                                         uint32_t pred_select,
                                         uint32_t condition) {
  if (pred_select && (!is_cond_cf || cf_condition != condition)) {
    Append("  }\n");
  }
}

bool GL4ShaderTranslator::TranslateBlocks(GL4Shader* shader) {
  Append(" int pc = 0;\n");

#if FLOW_CONTROL
  Append(" while (pc != 0xFFFF) {\n");
  Append(" switch (pc) {\n");

  // Start here; fall through to begin.
  Append(" case 0:\n");
#endif  // FLOW_CONTROL

  // Process all execution blocks.
  instr_cf_t cfa;
  instr_cf_t cfb;
  auto data = shader->data();
  bool needs_break = false;
  for (uint32_t idx = 0; idx < shader->dword_count(); idx += 3) {
    uint32_t dword_0 = data[idx + 0];
    uint32_t dword_1 = data[idx + 1];
    uint32_t dword_2 = data[idx + 2];
    cfa.dword_0 = dword_0;
    cfa.dword_1 = dword_1 & 0xFFFF;
    cfb.dword_0 = (dword_1 >> 16) | (dword_2 << 16);
    cfb.dword_1 = dword_2 >> 16;
    if (cfa.opc == ALLOC) {
      // ?
    } else if (cfa.is_exec()) {
      if (needs_break) {
#if FLOW_CONTROL
        Append(" break;\n");
#endif  // FLOW_CONTROL
        needs_break = false;
      }
      TranslateExec(cfa.exec);
      needs_break = true;
    } else if (cfa.opc == COND_JMP) {
      TranslateJmp(cfa.jmp_call);
    }
#if FLOW_CONTROL
    else if (cfa.opc == LOOP_START) {
      TranslateLoopStart(cfa.loop);
    }
#endif  // FLOW_CONTROL

    if (cfb.opc == ALLOC) {
      // ?
    } else if (cfb.is_exec()) {
      if (needs_break) {
#if FLOW_CONTROL
        Append(" break;\n");
#endif  // FLOW_CONTROL
        needs_break = false;
      }
      needs_break = true;
      TranslateExec(cfb.exec);
    } else if (cfb.opc == COND_JMP) {
      TranslateJmp(cfb.jmp_call);
    }
#if FLOW_CONTROL
    else if (cfb.opc == LOOP_END) {
      TranslateLoopEnd(cfb.loop);
    }
#endif

    if (cfa.opc == EXEC_END || cfb.opc == EXEC_END) {
      break;
    }
  }

#if FLOW_CONTROL
  if (needs_break) {
    Append(" break;\n");
    needs_break = false;
  }

  // Fall-through and exit.
  Append(" default:\n");
  Append(" pc = 0xFFFF;\n");
  Append(" break;\n");

  Append("};\n");
  Append("}\n");
#endif  // FLOW_CONTROL

  return true;
}

static const struct {
  const char* name;
} cf_instructions[] = {
#define INSTR(opc, fxn) \
  { #opc }
    INSTR(NOP, print_cf_nop),                        //
    INSTR(EXEC, print_cf_exec),                      //
    INSTR(EXEC_END, print_cf_exec),                  //
    INSTR(COND_EXEC, print_cf_exec),                 //
    INSTR(COND_EXEC_END, print_cf_exec),             //
    INSTR(COND_PRED_EXEC, print_cf_exec),            //
    INSTR(COND_PRED_EXEC_END, print_cf_exec),        //
    INSTR(LOOP_START, print_cf_loop),                //
    INSTR(LOOP_END, print_cf_loop),                  //
    INSTR(COND_CALL, print_cf_jmp_call),             //
    INSTR(RETURN, print_cf_jmp_call),                //
    INSTR(COND_JMP, print_cf_jmp_call),              //
    INSTR(ALLOC, print_cf_alloc),                    //
    INSTR(COND_EXEC_PRED_CLEAN, print_cf_exec),      //
    INSTR(COND_EXEC_PRED_CLEAN_END, print_cf_exec),  //
    INSTR(MARK_VS_FETCH_DONE, print_cf_nop),         // ??
#undef INSTR
};

bool GL4ShaderTranslator::TranslateExec(const instr_cf_exec_t& cf) {
  Append("  // %s ADDR(0x%x) CNT(0x%x)", cf_instructions[cf.opc].name,
         cf.address, cf.count);
  if (cf.yeild) {
    Append(" YIELD");
  }
  uint8_t vc = cf.vc_hi | (cf.vc_lo << 2);
  if (vc) {
    Append(" VC(0x%x)", vc);
  }
  if (cf.is_cond_exec()) {
    Append(" BOOL_ADDR(0x%x)", cf.bool_addr);
  }
  if (cf.address_mode == ABSOLUTE_ADDR) {
    Append(" ABSOLUTE_ADDR");
  }
  if (cf.is_cond_exec()) {
    Append(" COND(%d)", cf.pred_condition);
  }
  Append("\n");

#if FLOW_CONTROL
  Append(" case 0x%x:\n", cf.address);
#endif  // FLOW_CONTROL

  if (cf.is_cond_exec()) {
    if (cf.opc == COND_EXEC_PRED_CLEAN || cf.opc == COND_EXEC_PRED_CLEAN_END) {
      Append("  p = (state.bool_consts[%d] & (1 << %d)) != 0;\n",
             cf.bool_addr / 32, cf.bool_addr % 32);
    }
    Append("  if(%cp) {\n", cf.pred_condition ? ' ' : '!');
  }

  uint32_t sequence = cf.serialize;
  for (uint32_t i = 0; i < cf.count; i++) {
    uint32_t alu_off = (cf.address + i);
    int sync = sequence & 0x2;
    if (sequence & 0x1) {
      const instr_fetch_t* fetch =
          (const instr_fetch_t*)(dwords_ + alu_off * 3);
      switch (fetch->opc) {
        case VTX_FETCH:
          AppendPredPre(cf.is_cond_exec(), cf.pred_condition,
                        fetch->vtx.pred_select, fetch->vtx.pred_condition);
          if (!TranslateVertexFetch(&fetch->vtx, sync)) {
            return false;
          }
          AppendPredPost(cf.is_cond_exec(), cf.pred_condition,
                         fetch->vtx.pred_select, fetch->vtx.pred_condition);
          break;
        case TEX_FETCH:
          AppendPredPre(cf.is_cond_exec(), cf.pred_condition,
                        fetch->tex.pred_select, fetch->tex.pred_condition);
          if (!TranslateTextureFetch(&fetch->tex, sync)) {
            return false;
          }
          AppendPredPost(cf.is_cond_exec(), cf.pred_condition,
                         fetch->tex.pred_select, fetch->tex.pred_condition);
          break;
        case TEX_GET_BORDER_COLOR_FRAC:
        case TEX_GET_COMP_TEX_LOD:
        case TEX_GET_GRADIENTS:
        case TEX_GET_WEIGHTS:
        case TEX_SET_TEX_LOD:
        case TEX_SET_GRADIENTS_H:
        case TEX_SET_GRADIENTS_V:
        default:
          assert_always();
          break;
      }
    } else {
      const instr_alu_t* alu = (const instr_alu_t*)(dwords_ + alu_off * 3);
      AppendPredPre(cf.is_cond_exec(), cf.pred_condition, alu->pred_select,
                    alu->pred_condition);
      if (!TranslateALU(alu, sync)) {
        return false;
      }
      AppendPredPost(cf.is_cond_exec(), cf.pred_condition, alu->pred_select,
                     alu->pred_condition);
    }
    sequence >>= 2;
  }

  if (cf.is_cond_exec()) {
    Append("  }\n");
  }

  if (cf.opc == EXEC_END) {
    Append(" pc = 0xFFFF;\n");
  } else {
    Append(" pc = 0x%x;\n", cf.address + cf.count);
  }

  return true;
}

bool GL4ShaderTranslator::TranslateJmp(const ucode::instr_cf_jmp_call_t& cf) {
  assert_true(cf.direction == 0);
  assert_true(cf.address_mode == 0);
  Append("  // %s", cf_instructions[cf.opc].name);
  Append(" ADDR(0x%x) DIR(%d)", cf.address, cf.direction);
  if (cf.address_mode == ABSOLUTE_ADDR) {
    Append(" ABSOLUTE_ADDR");
  }
  if (cf.force_call) {
    Append(" FORCE_CALL");
  } else {
    if (!cf.predicated_jmp) {
      Append(" BOOL_ADDR(0x%x)", cf.bool_addr);
    }
    Append(" COND(%d)", cf.condition);
  }
  Append("\n");

  if (!cf.force_call) {
    if (!cf.predicated_jmp) {
      Append(" p = (state.bool_consts[%d] & (1 << %d)) != 0;\n",
             cf.bool_addr / 32, cf.bool_addr % 32);
    }
    Append(" if(%cp) {\n", cf.condition ? ' ' : '!');
  }
  if (cf.address_mode == ABSOLUTE_ADDR) {
    Append(" pc = 0x%x;\n", cf.address);
  } else {
    Append(" pc = pc + 0x%x;\n", cf.address);
  }
  if (!cf.force_call) {
#if FLOW_CONTROL
    Append(" break;\n");
#endif  // FLOW_CONTROL
    Append(" }\n");
  }

  return true;
}

bool GL4ShaderTranslator::TranslateLoopStart(const ucode::instr_cf_loop_t& cf) {
  Append("  // %s", cf_instructions[cf.opc].name);
  Append(" ADDR(0x%x) LOOP ID(%d)", cf.address, cf.loop_id);
  if (cf.address_mode == ABSOLUTE_ADDR) {
    Append(" ABSOLUTE_ADDR");
  }
  Append("\n");
  Append(" i%d_addr = pc;\n", cf.loop_id);
  Append(" i%d_cnt = 0;\n", cf.loop_id);
  return true;
}

bool GL4ShaderTranslator::TranslateLoopEnd(const ucode::instr_cf_loop_t& cf) {
  Append("  // %s", cf_instructions[cf.opc].name);
  Append(" ADDR(0x%x) LOOP ID(%d)\n", cf.address, cf.loop_id);
  Append(" i%d_cnt = i%d_cnt + 1;\n", cf.loop_id, cf.loop_id);
  Append(" pc = (i%d_cnt < state.loop_consts[%d]) ? i%d_addr : pc;\n",
         cf.loop_id, cf.loop_id, cf.loop_id);
  return true;
}

bool GL4ShaderTranslator::TranslateVertexFetch(const instr_fetch_vtx_t* vtx,
                                               int sync) {
  static const struct {
    const char* name;
  } fetch_types[0xff] = {
#define TYPE(id) \
  { #id }
      TYPE(FMT_1_REVERSE),  // 0
      {0},
      TYPE(FMT_8),  // 2
      {0},
      {0},
      {0},
      TYPE(FMT_8_8_8_8),     // 6
      TYPE(FMT_2_10_10_10),  // 7
      {0},
      {0},
      TYPE(FMT_8_8),  // 10
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
      TYPE(FMT_16),           // 24
      TYPE(FMT_16_16),        // 25
      TYPE(FMT_16_16_16_16),  // 26
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
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

  // Disassemble.
  Append("  //   %sFETCH:\t", sync ? "(S)" : "   ");
  if (vtx->pred_select) {
    Append(vtx->pred_condition ? "EQ" : "NE");
  }
  PrintDestFetch(vtx->dst_reg, vtx->dst_swiz);
  Append(" = R%u.", vtx->src_reg);
  Append("%c", chan_names[vtx->src_swiz & 0x3]);
  if (fetch_types[vtx->format].name) {
    Append(" %s", fetch_types[vtx->format].name);
  } else {
    Append(" TYPE(0x%x)", vtx->format);
  }
  Append(" %s", vtx->format_comp_all ? "SIGNED" : "UNSIGNED");
  if (!vtx->num_format_all) {
    Append(" NORMALIZED");
  }
  Append(" STRIDE(%u)", vtx->stride);
  if (vtx->offset) {
    Append(" OFFSET(%u)", vtx->offset);
  }
  Append(" CONST(%u, %u)", vtx->const_index, vtx->const_index_sel);
  if (true) {
    // XXX
    Append(" src_reg_am=%u", vtx->src_reg_am);
    Append(" dst_reg_am=%u", vtx->dst_reg_am);
    Append(" num_format_all=%u", vtx->num_format_all);
    Append(" signed_rf_mode_all=%u", vtx->signed_rf_mode_all);
    Append(" exp_adjust_all=%u", vtx->exp_adjust_all);
  }
  Append("\n");

  // Translate.
  Append("  ");
  Append("r%u.xyzw", vtx->dst_reg);
  Append(" = vec4(");
  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  // TODO(benvanik): detect xyzw = xyzw, etc.
  // TODO(benvanik): detect and set as rN = vec4(samp.xyz, 1.0); / etc
  // uint32_t component_count =
  //     GetVertexFormatComponentCount(static_cast<VertexFormat>(vtx->format));
  uint32_t dst_swiz = vtx->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      Append("0.0");
    } else if ((dst_swiz & 0x7) == 5) {
      Append("1.0");
    } else if ((dst_swiz & 0x7) == 6) {
      // ?
      Append("?");
    } else if ((dst_swiz & 0x7) == 7) {
      Append("r%u.%c", vtx->dst_reg, chan_names[i]);
    } else {
      Append("vf%u_%d.%c", fetch_slot, vtx->offset, chan_names[dst_swiz & 0x3]);
    }
    if (i < 3) {
      Append(", ");
    }
    dst_swiz >>= 3;
  }
  Append(");\n");
  return true;
}

bool GL4ShaderTranslator::TranslateTextureFetch(const instr_fetch_tex_t* tex,
                                                int sync) {
  int src_component_count = 0;
  const char* sampler_type;
  switch (tex->dimension) {
    case DIMENSION_1D:
      src_component_count = 1;
      sampler_type = "sampler1D";
      break;
    case DIMENSION_2D:
      src_component_count = 2;
      sampler_type = "sampler2D";
      break;
    case DIMENSION_3D:
      src_component_count = 3;
      sampler_type = "sampler3D";
      break;
    case DIMENSION_CUBE:
      src_component_count = 3;
      sampler_type = "samplerCube";
      break;
    default:
      assert_unhandled_case(tex->dimension);
      return false;
  }

  // Disassemble.
  static const char* filter[] = {
      "POINT",    // TEX_FILTER_POINT
      "LINEAR",   // TEX_FILTER_LINEAR
      "BASEMAP",  // TEX_FILTER_BASEMAP
  };
  static const char* aniso_filter[] = {
      "DISABLED",  // ANISO_FILTER_DISABLED
      "MAX_1_1",   // ANISO_FILTER_MAX_1_1
      "MAX_2_1",   // ANISO_FILTER_MAX_2_1
      "MAX_4_1",   // ANISO_FILTER_MAX_4_1
      "MAX_8_1",   // ANISO_FILTER_MAX_8_1
      "MAX_16_1",  // ANISO_FILTER_MAX_16_1
  };
  static const char* arbitrary_filter[] = {
      "2x4_SYM",   // ARBITRARY_FILTER_2X4_SYM
      "2x4_ASYM",  // ARBITRARY_FILTER_2X4_ASYM
      "4x2_SYM",   // ARBITRARY_FILTER_4X2_SYM
      "4x2_ASYM",  // ARBITRARY_FILTER_4X2_ASYM
      "4x4_SYM",   // ARBITRARY_FILTER_4X4_SYM
      "4x4_ASYM",  // ARBITRARY_FILTER_4X4_ASYM
  };
  static const char* sample_loc[] = {
      "CENTROID",  // SAMPLE_CENTROID
      "CENTER",    // SAMPLE_CENTER
  };
  uint32_t src_swiz = tex->src_swiz;
  Append("  //   %sFETCH:\t", sync ? "(S)" : "   ");
  if (tex->pred_select) {
    Append(tex->pred_condition ? "EQ" : "NE");
  }
  PrintDestFetch(tex->dst_reg, tex->dst_swiz);
  Append(" = R%u.", tex->src_reg);
  for (int i = 0; i < src_component_count; i++) {
    Append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  Append(" CONST(%u)", tex->const_idx);
  if (tex->fetch_valid_only) {
    Append(" VALID_ONLY");
  }
  if (tex->tx_coord_denorm) {
    Append(" DENORM");
  }
  if (tex->mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    Append(" MAG(%s)", filter[tex->mag_filter]);
  }
  if (tex->min_filter != TEX_FILTER_USE_FETCH_CONST) {
    Append(" MIN(%s)", filter[tex->min_filter]);
  }
  if (tex->mip_filter != TEX_FILTER_USE_FETCH_CONST) {
    Append(" MIP(%s)", filter[tex->mip_filter]);
  }
  if (tex->aniso_filter != ANISO_FILTER_USE_FETCH_CONST) {
    Append(" ANISO(%s)", aniso_filter[tex->aniso_filter]);
  }
  if (tex->arbitrary_filter != ARBITRARY_FILTER_USE_FETCH_CONST) {
    Append(" ARBITRARY(%s)", arbitrary_filter[tex->arbitrary_filter]);
  }
  if (tex->vol_mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    Append(" VOL_MAG(%s)", filter[tex->vol_mag_filter]);
  }
  if (tex->vol_min_filter != TEX_FILTER_USE_FETCH_CONST) {
    Append(" VOL_MIN(%s)", filter[tex->vol_min_filter]);
  }
  if (!tex->use_comp_lod) {
    Append(" LOD(%u)", tex->use_comp_lod);
    Append(" LOD_BIAS(%u)", tex->lod_bias);
  }
  if (tex->use_reg_lod) {
    Append(" REG_LOD(%u)", tex->use_reg_lod);
  }
  if (tex->use_reg_gradients) {
    Append(" USE_REG_GRADIENTS");
  }
  Append(" LOCATION(%s)", sample_loc[tex->sample_location]);
  if (tex->offset_x || tex->offset_y || tex->offset_z) {
    Append(" OFFSET(%u,%u,%u)", tex->offset_x, tex->offset_y, tex->offset_z);
  }
  Append("\n");

  // Translate.
  // TODO(benvanik): if sampler == null, set to invalid color.
  Append("  if (state.texture_samplers[%d].x != 0) {\n", tex->const_idx & 0xF);
  if (tex->dimension == DIMENSION_CUBE) {
    Append("    t.xyz = r%u.", tex->src_reg);
    src_swiz = tex->src_swiz;
    for (int i = 0; i < src_component_count; i++) {
      Append("%c", chan_names[src_swiz & 0x3]);
      src_swiz >>= 2;
    }
    Append(";\n");
    // TODO(benvanik): undo CUBEv logic on t? (s,t,faceid)
    Append("    t = texture(%s(state.texture_samplers[%d]), t.xyz);\n",
           sampler_type, tex->const_idx & 0xF);
  } else {
    Append("    t = texture(");
    Append("%s(state.texture_samplers[%d])", sampler_type,
           tex->const_idx & 0xF);
    Append(", r%u.", tex->src_reg);
    src_swiz = tex->src_swiz;
    for (int i = 0; i < src_component_count; i++) {
      Append("%c", chan_names[src_swiz & 0x3]);
      src_swiz >>= 2;
    }
    Append(");\n");
  }
  Append("  } else {\n");
  Append("    t = vec4(r%u.", tex->src_reg);
  src_swiz = tex->src_swiz;
  for (int i = 0; i < src_component_count; i++) {
    Append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  switch (src_component_count) {
    case 1:
      Append(", 0.0, 0.0, 1.0);\n");
      break;
    case 2:
      Append(", 0.0, 1.0);\n");
      break;
    case 3:
      Append(", 1.0);\n");
      break;
  }
  Append("  }\n");

  Append("  r%u.xyzw = vec4(", tex->dst_reg);
  uint32_t dst_swiz = tex->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if (i) {
      Append(", ");
    }
    if ((dst_swiz & 0x7) == 4) {
      Append("0.0");
    } else if ((dst_swiz & 0x7) == 5) {
      Append("1.0");
    } else if ((dst_swiz & 0x7) == 6) {
      // ?
      Append("?");
      assert_always();
    } else if ((dst_swiz & 0x7) == 7) {
      Append("r%u.%c", tex->dst_reg, chan_names[i]);
    } else {
      Append("t.%c", chan_names[dst_swiz & 0x3]);
    }
    dst_swiz >>= 3;
  }
  Append(");\n");
  return true;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
