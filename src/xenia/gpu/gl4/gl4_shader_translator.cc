/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl4_shader_translator.h>

#include <poly/math.h>
#include <xenia/gpu/gpu-private.h>

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::ucode;
using namespace xe::gpu::xenos;

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

GL4ShaderTranslator::GL4ShaderTranslator()
    : output_(kOutputCapacity), dwords_(nullptr) {}

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
  assert_true(program_cntl.vs_export_mode == 0);

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

  const auto& alloc_counts = vertex_shader->alloc_counts();

  // Vertex shader main() header.
  Append("void processVertex() {\n");

  // Add temporaries for any registers we may use.
  uint32_t temp_regs = program_cntl.vs_regs + program_cntl.ps_regs;
  for (uint32_t n = 0; n <= temp_regs; n++) {
    Append("  vec4 r%d = state.float_consts[%d];\n", n, n);
  }
  Append("  vec4 t;\n");

  // Execute blocks.
  const auto& execs = vertex_shader->execs();
  for (auto it = execs.begin(); it != execs.end(); ++it) {
    const instr_cf_exec_t& cf = *it;
    // TODO(benvanik): figure out how sequences/jmps/loops/etc work.
    if (!TranslateExec(cf)) {
      return "";
    }
  }

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
  Append("void processFragment() {\n");

  // Add temporary registers.
  uint32_t temp_regs = program_cntl.vs_regs + program_cntl.ps_regs;
  for (uint32_t n = 0; n <= std::max(15u, temp_regs); n++) {
    Append("  vec4 r%d = state.float_consts[%d];\n", n, n + 256);
  }
  Append("  vec4 t;\n");
  Append("  float s;\n");  // scalar result (used for RETAIN_PREV)

  // Bring registers local.
  for (uint32_t n = 0; n < kMaxInterpolators; n++) {
    Append("  r%d = vtx.o[%d];\n", n, n);
  }

  // Execute blocks.
  const auto& execs = pixel_shader->execs();
  for (auto it = execs.begin(); it != execs.end(); ++it) {
    const instr_cf_exec_t& cf = *it;
    // TODO(benvanik): figure out how sequences/jmps/loops/etc work.
    if (!TranslateExec(cf)) {
      return "";
    }
  }

  Append("}\n");
  return output_.to_string();
}

void GL4ShaderTranslator::AppendSrcReg(uint32_t num, uint32_t type,
                                       uint32_t swiz, uint32_t negate,
                                       uint32_t abs_constants) {
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
    if (abs_constants) {
      Append("abs(");
    }
    Append("state.float_consts[%u]", is_pixel_shader() ? num + 256 : num);
    if (abs_constants) {
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

void GL4ShaderTranslator::AppendDestRegName(uint32_t num, uint32_t dst_exp) {
  if (!dst_exp) {
    // Register.
    Append("r%u", num);
  } else {
    // Export.
    switch (shader_type_) {
      case ShaderType::kVertex:
        switch (num) {
          case 62:
            Append("gl_Position");
            break;
          case 63:
            // Write to t, as we need to splice just x out of it.
            Append("t");
            break;
          default:
            // Varying.
            Append("vtx.o[%u]", num);
            break;
        }
        break;
      case ShaderType::kPixel:
        switch (num) {
          case 0:
          case 63:  // ? masked?
            Append("oC[0]");
            break;
          case 61:
            // Write to t, as we need to splice just x out of it.
            Append("t");
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

void GL4ShaderTranslator::AppendDestReg(uint32_t num, uint32_t mask,
                                        uint32_t dst_exp) {
  if (mask != 0xF) {
    // If masking, store to a temporary variable and clean it up later.
    Append("t");
  } else {
    // Store directly to output.
    AppendDestRegName(num, dst_exp);
  }
}

void GL4ShaderTranslator::AppendDestRegPost(uint32_t num, uint32_t mask,
                                            uint32_t dst_exp) {
  switch (shader_type_) {
    case ShaderType::kVertex:
      switch (num) {
        case 63:
          Append("  gl_PointSize = t.x;\n");
          return;
      }
      break;
    case ShaderType::kPixel:
      switch (num) {
        case 61:
          // gl_FragDepth handling to just get x from the temp result.
          Append("  gl_FragDepth = t.x;\n");
          return;
      }
      break;
  }
  if (mask != 0xF) {
    // Masking.
    Append("  ");
    AppendDestRegName(num, dst_exp);
    Append(" = vec4(");
    for (int i = 0; i < 4; i++) {
      // TODO(benvanik): mask out values? mix in old value as temp?
      // Append("%c", (mask & 0x1) ? chan_names[i] : 'w');
      if (!(mask & 0x1)) {
        // Don't write - use existing value.
        AppendDestRegName(num, dst_exp);
        Append(".%c", chan_names[i]);
      } else {
        Append("t.%c", chan_names[i]);
      }
      mask >>= 1;
      if (i < 3) {
        Append(", ");
      }
    }
    Append(");\n");
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

void GL4ShaderTranslator::PrintDstReg(uint32_t num, uint32_t mask,
                                      uint32_t dst_exp) {
  Append("%s%u", dst_exp ? "export" : "R", num);
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

bool GL4ShaderTranslator::TranslateALU_ADDv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(" + ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MULv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(" * ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MAXv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  if (alu.src1_reg == alu.src2_reg && alu.src1_sel == alu.src2_sel &&
      alu.src1_swiz == alu.src2_swiz &&
      alu.src1_reg_negate == alu.src2_reg_negate) {
    // This is a mov.
    AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
                 alu.abs_constants);
  } else {
    Append("max(");
    AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
                 alu.abs_constants);
    Append(", ");
    AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
                 alu.abs_constants);
    Append(")");
  }
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MINv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("min(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(", ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SETXXv(const instr_alu_t& alu,
                                              const char* op) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("vec4((");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").x %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").x ? 1.0 : 0.0, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").y %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").y ? 1.0 : 0.0, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").z %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").z ? 1.0 : 0.0, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").w %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").w ? 1.0 : 0.0)");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("frac(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_TRUNCv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("trunc(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_FLOORv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("floor(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MULADDv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(" * ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(") + ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_CNDXXv(const instr_alu_t& alu,
                                              const char* op) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  // TODO(benvanik): check argument order - could be 3 as compare and 1 and 2 as
  // values.
  Append("vec4((");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").x %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").x : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(").x, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").y %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").y : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(").y, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").z %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").z : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(").z, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").w %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").w : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(").w)");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("dot(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(", ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(".xxxx;\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_DOT3v(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("dot(vec4(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").xyz, vec4(");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").xyz)");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(".xxxx;\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_DOT2ADDv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("dot(vec4(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(").xy, vec4(");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate,
               alu.abs_constants);
  Append(").xy) + ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(".x");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(".xxxx;\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

// CUBEv

bool GL4ShaderTranslator::TranslateALU_MAX4v(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  Append(" = ");
  if (alu.vector_clamp) {
    Append("clamp(");
  }
  Append("max(");
  Append("max(");
  Append("max(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(".x, ");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(".y), ");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(".z), ");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate,
               alu.abs_constants);
  Append(".w)");
  if (alu.vector_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return true;
}

// ...

bool GL4ShaderTranslator::TranslateALU_MAXs(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  if ((alu.src3_swiz & 0x3) == (((alu.src3_swiz >> 2) + 1) & 0x3)) {
    // This is a mov.
    AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
                 alu.abs_constants);
  } else {
    Append("max(");
    AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
                 alu.abs_constants);
    Append(".x, ");
    AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
                 alu.abs_constants);
    Append(".y).xxxx");
  }
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MINs(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  Append("min(");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(".x, ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(".y).xxxx");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SETXXs(const instr_alu_t& alu,
                                              const char* op) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  Append("((");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(".x %s 0.0) ? 1.0 : 0.0).xxxx", op);
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
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

bool GL4ShaderTranslator::TranslateALU_EXP_IEEE(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  Append("exp(");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_RECIP_IEEE(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  Append("(1.0 / ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_SQRT_IEEE(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  Append("sqrt(");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate,
               alu.abs_constants);
  Append(")");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}

bool GL4ShaderTranslator::TranslateALU_MUL_CONST_0(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 =
      (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  Append("(");
  AppendSrcReg(alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.abs_constants);
  Append(".%c * ", chan_names[swiz_a]);
  AppendSrcReg(reg2, 1, 0, alu.src3_reg_negate, alu.abs_constants);
  Append(".%c", chan_names[swiz_b]);
  Append(").xxxx");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_MUL_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_MUL_CONST_0(alu);
}

bool GL4ShaderTranslator::TranslateALU_ADD_CONST_0(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 =
      (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  Append("(");
  AppendSrcReg(alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.abs_constants);
  Append(".%c + ", chan_names[swiz_a]);
  AppendSrcReg(reg2, 1, 0, alu.src3_reg_negate, alu.abs_constants);
  Append(".%c", chan_names[swiz_b]);
  Append(").xxxx");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_ADD_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_ADD_CONST_0(alu);
}

bool GL4ShaderTranslator::TranslateALU_SUB_CONST_0(const instr_alu_t& alu) {
  AppendDestReg(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                alu.export_data);
  Append(" = ");
  if (alu.scalar_clamp) {
    Append("clamp(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 =
      (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  Append("(");
  AppendSrcReg(alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.abs_constants);
  Append(".%c - ", chan_names[swiz_a]);
  AppendSrcReg(reg2, 1, 0, alu.src3_reg_negate, alu.abs_constants);
  Append(".%c", chan_names[swiz_b]);
  Append(").xxxx");
  if (alu.scalar_clamp) {
    Append(", 0.0, 1.0)");
  }
  Append(";\n");
  AppendDestRegPost(get_alu_scalar_dest(alu), alu.scalar_write_mask,
                    alu.export_data);
  return true;
}
bool GL4ShaderTranslator::TranslateALU_SUB_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_SUB_CONST_0(alu);
}

bool GL4ShaderTranslator::TranslateALU_RETAIN_PREV(const instr_alu_t& alu) {
  // TODO(benvanik): figure out how this is used.
  // It seems like vector writes to export regs will use this to write 1's to
  // components (like w in position).
  assert_true(alu.export_data == 1);
  AppendDestReg(alu.vector_dest, alu.scalar_write_mask, alu.export_data);
  Append(" = ");
  Append("vec4(1.0, 1.0, 1.0, 1.0);\n");
  AppendDestRegPost(alu.vector_dest, alu.scalar_write_mask, alu.export_data);
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
      ALU_INSTR_IMPL(ADDv, 2),          // 0
      ALU_INSTR_IMPL(MULv, 2),          // 1
      ALU_INSTR_IMPL(MAXv, 2),          // 2
      ALU_INSTR_IMPL(MINv, 2),          // 3
      ALU_INSTR_IMPL(SETEv, 2),         // 4
      ALU_INSTR_IMPL(SETGTv, 2),        // 5
      ALU_INSTR_IMPL(SETGTEv, 2),       // 6
      ALU_INSTR_IMPL(SETNEv, 2),        // 7
      ALU_INSTR_IMPL(FRACv, 1),         // 8
      ALU_INSTR_IMPL(TRUNCv, 1),        // 9
      ALU_INSTR_IMPL(FLOORv, 1),        // 10
      ALU_INSTR_IMPL(MULADDv, 3),       // 11
      ALU_INSTR_IMPL(CNDEv, 3),         // 12
      ALU_INSTR_IMPL(CNDGTEv, 3),       // 13
      ALU_INSTR_IMPL(CNDGTv, 3),        // 14
      ALU_INSTR_IMPL(DOT4v, 2),         // 15
      ALU_INSTR_IMPL(DOT3v, 2),         // 16
      ALU_INSTR_IMPL(DOT2ADDv, 3),      // 17 -- ???
      ALU_INSTR(CUBEv, 2),              // 18
      ALU_INSTR_IMPL(MAX4v, 1),         // 19
      ALU_INSTR(PRED_SETE_PUSHv, 2),    // 20
      ALU_INSTR(PRED_SETNE_PUSHv, 2),   // 21
      ALU_INSTR(PRED_SETGT_PUSHv, 2),   // 22
      ALU_INSTR(PRED_SETGTE_PUSHv, 2),  // 23
      ALU_INSTR(KILLEv, 2),             // 24
      ALU_INSTR(KILLGTv, 2),            // 25
      ALU_INSTR(KILLGTEv, 2),           // 26
      ALU_INSTR(KILLNEv, 2),            // 27
      ALU_INSTR(DSTv, 2),               // 28
      ALU_INSTR(MOVAv, 1),              // 29
  };
  static TranslateInfo scalar_alu_instrs[0x40] = {
      ALU_INSTR(ADDs, 1),               // 0
      ALU_INSTR(ADD_PREVs, 1),          // 1
      ALU_INSTR(MULs, 1),               // 2
      ALU_INSTR(MUL_PREVs, 1),          // 3
      ALU_INSTR(MUL_PREV2s, 1),         // 4
      ALU_INSTR_IMPL(MAXs, 1),          // 5
      ALU_INSTR_IMPL(MINs, 1),          // 6
      ALU_INSTR_IMPL(SETEs, 1),         // 7
      ALU_INSTR_IMPL(SETGTs, 1),        // 8
      ALU_INSTR_IMPL(SETGTEs, 1),       // 9
      ALU_INSTR_IMPL(SETNEs, 1),        // 10
      ALU_INSTR(FRACs, 1),              // 11
      ALU_INSTR(TRUNCs, 1),             // 12
      ALU_INSTR(FLOORs, 1),             // 13
      ALU_INSTR_IMPL(EXP_IEEE, 1),      // 14
      ALU_INSTR(LOG_CLAMP, 1),          // 15
      ALU_INSTR(LOG_IEEE, 1),           // 16
      ALU_INSTR(RECIP_CLAMP, 1),        // 17
      ALU_INSTR(RECIP_FF, 1),           // 18
      ALU_INSTR_IMPL(RECIP_IEEE, 1),    // 19
      ALU_INSTR(RECIPSQ_CLAMP, 1),      // 20
      ALU_INSTR(RECIPSQ_FF, 1),         // 21
      ALU_INSTR(RECIPSQ_IEEE, 1),       // 22
      ALU_INSTR(MOVAs, 1),              // 23
      ALU_INSTR(MOVA_FLOORs, 1),        // 24
      ALU_INSTR(SUBs, 1),               // 25
      ALU_INSTR(SUB_PREVs, 1),          // 26
      ALU_INSTR(PRED_SETEs, 1),         // 27
      ALU_INSTR(PRED_SETNEs, 1),        // 28
      ALU_INSTR(PRED_SETGTs, 1),        // 29
      ALU_INSTR(PRED_SETGTEs, 1),       // 30
      ALU_INSTR(PRED_SET_INVs, 1),      // 31
      ALU_INSTR(PRED_SET_POPs, 1),      // 32
      ALU_INSTR(PRED_SET_CLRs, 1),      // 33
      ALU_INSTR(PRED_SET_RESTOREs, 1),  // 34
      ALU_INSTR(KILLEs, 1),             // 35
      ALU_INSTR(KILLGTs, 1),            // 36
      ALU_INSTR(KILLGTEs, 1),           // 37
      ALU_INSTR(KILLNEs, 1),            // 38
      ALU_INSTR(KILLONEs, 1),           // 39
      ALU_INSTR_IMPL(SQRT_IEEE, 1),     // 40
      {0, 0, false},                    //
      ALU_INSTR_IMPL(MUL_CONST_0, 2),   // 42
      ALU_INSTR_IMPL(MUL_CONST_1, 2),   // 43
      ALU_INSTR_IMPL(ADD_CONST_0, 2),   // 44
      ALU_INSTR_IMPL(ADD_CONST_1, 2),   // 45
      ALU_INSTR_IMPL(SUB_CONST_0, 2),   // 46
      ALU_INSTR_IMPL(SUB_CONST_1, 2),   // 47
      ALU_INSTR(SIN, 1),                // 48
      ALU_INSTR(COS, 1),                // 49
      ALU_INSTR_IMPL(RETAIN_PREV, 1),   // 50
  };
#undef ALU_INSTR
#undef ALU_INSTR_IMPL

  if (!alu->scalar_write_mask && !alu->vector_write_mask) {
    Append("  //   <nop>\n");
    return true;
  }

  if (alu->vector_write_mask) {
    // Disassemble vector op.
    const auto& iv = vector_alu_instrs[alu->vector_opc];
    Append("  //   %sALU:\t", sync ? "(S)" : "   ");
    Append("%s", iv.name);
    if (alu->pred_select & 0x2) {
      // seems to work similar to conditional execution in ARM instruction
      // set, so let's use a similar syntax for now:
      Append((alu->pred_select & 0x1) ? "EQ" : "NE");
    }
    Append("\t");
    PrintDstReg(alu->vector_dest, alu->vector_write_mask, alu->export_data);
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
      Append("  ");
      if (!(this->*iv.fn)(*alu)) {
        return false;
      }
    } else {
      Append("  // <UNIMPLEMENTED>\n");
    }
  }

  if (alu->scalar_write_mask || !alu->vector_write_mask) {
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
    PrintDstReg(get_alu_scalar_dest(*alu), alu->scalar_write_mask,
                alu->export_data);
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
      PrintExportComment(get_alu_scalar_dest(*alu));
    }
    Append("\n");

    // Translate scalar op.
    if (is.fn) {
      Append("  ");
      if (!(this->*is.fn)(*alu)) {
        return false;
      }
    } else {
      Append("  // <UNIMPLEMENTED>\n");
    }
  }

  return true;
}

void GL4ShaderTranslator::PrintDestFecth(uint32_t dst_reg, uint32_t dst_swiz) {
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

bool GL4ShaderTranslator::TranslateExec(const instr_cf_exec_t& cf) {
  static const struct {
    const char* name;
  } cf_instructions[] = {
#define INSTR(opc, fxn) \
  { #opc }
      INSTR(NOP, print_cf_nop), INSTR(EXEC, print_cf_exec),
      INSTR(EXEC_END, print_cf_exec), INSTR(COND_EXEC, print_cf_exec),
      INSTR(COND_EXEC_END, print_cf_exec), INSTR(COND_PRED_EXEC, print_cf_exec),
      INSTR(COND_PRED_EXEC_END, print_cf_exec),
      INSTR(LOOP_START, print_cf_loop), INSTR(LOOP_END, print_cf_loop),
      INSTR(COND_CALL, print_cf_jmp_call), INSTR(RETURN, print_cf_jmp_call),
      INSTR(COND_JMP, print_cf_jmp_call), INSTR(ALLOC, print_cf_alloc),
      INSTR(COND_EXEC_PRED_CLEAN, print_cf_exec),
      INSTR(COND_EXEC_PRED_CLEAN_END, print_cf_exec),
      INSTR(MARK_VS_FETCH_DONE, print_cf_nop),  // ??
#undef INSTR
  };

  Append("  // %s ADDR(0x%x) CNT(0x%x)", cf_instructions[cf.opc].name,
         cf.address, cf.count);
  if (cf.yeild) {
    Append(" YIELD");
  }
  uint8_t vc = cf.vc_hi | (cf.vc_lo << 2);
  if (vc) {
    Append(" VC(0x%x)", vc);
  }
  if (cf.bool_addr) {
    Append(" BOOL_ADDR(0x%x)", cf.bool_addr);
  }
  if (cf.address_mode == ABSOLUTE_ADDR) {
    Append(" ABSOLUTE_ADDR");
  }
  if (cf.is_cond_exec()) {
    Append(" COND(%d)", cf.condition);
  }
  Append("\n");

  uint32_t sequence = cf.serialize;
  for (uint32_t i = 0; i < cf.count; i++) {
    uint32_t alu_off = (cf.address + i);
    int sync = sequence & 0x2;
    if (sequence & 0x1) {
      const instr_fetch_t* fetch =
          (const instr_fetch_t*)(dwords_ + alu_off * 3);
      switch (fetch->opc) {
        case VTX_FETCH:
          if (!TranslateVertexFetch(&fetch->vtx, sync)) {
            return false;
          }
          break;
        case TEX_FETCH:
          if (!TranslateTextureFetch(&fetch->tex, sync)) {
            return false;
          }
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
      if (!TranslateALU(alu, sync)) {
        return false;
      }
    }
    sequence >>= 2;
  }

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
  PrintDestFecth(vtx->dst_reg, vtx->dst_swiz);
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
  uint32_t component_count =
      GetVertexFormatComponentCount(static_cast<VertexFormat>(vtx->format));
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
  PrintDestFecth(tex->dst_reg, tex->dst_swiz);
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
  Append("    t = texture(");
  Append("%s(state.texture_samplers[%d])", sampler_type, tex->const_idx & 0xF);
  Append(", r%u.", tex->src_reg);
  src_swiz = tex->src_swiz;
  for (int i = 0; i < src_component_count; i++) {
    Append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  Append(");\n");
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
