/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_shader_translator.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_geometry_shader.h>
#include <xenia/gpu/d3d11/d3d11_resource_cache.h>
#include <xenia/gpu/xenos/ucode.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


namespace {

const char* GetFormatTypeName(const VertexBufferResource::DeclElement& el) {
  switch (el.format) {
  case FMT_32:
    return el.is_signed ? "int" : "uint";
  case FMT_32_FLOAT:
    return "float";
  case FMT_16_16:
  case FMT_32_32:
    if (el.is_normalized) {
      return el.is_signed ? "snorm float2" : "unorm float2";
    } else {
      return el.is_signed ? "int2" : "uint2";
    }
  case FMT_16_16_FLOAT:
  case FMT_32_32_FLOAT:
    return "float2";
  case FMT_10_11_11:
  case FMT_11_11_10:
    return "int3"; // ?
  case FMT_32_32_32_FLOAT:
    return "float3";
  case FMT_8_8_8_8:
  case FMT_2_10_10_10:
  case FMT_16_16_16_16:
  case FMT_32_32_32_32:
    if (el.is_normalized) {
      return el.is_signed ? "snorm float4" : "unorm float4";
    } else {
      return el.is_signed ? "int4" : "uint4";
    }
  case FMT_16_16_16_16_FLOAT:
  case FMT_32_32_32_32_FLOAT:
    return "float4";
  default:
    XELOGE("Unknown vertex format: %d", el.format);
    assert_always();
    return "float4";
  }
}

}  // anonymous namespace

D3D11ShaderTranslator::D3D11ShaderTranslator()
    : capacity_(kCapacity), offset_(0) {
  buffer_[0] = 0;
}

int D3D11ShaderTranslator::TranslateVertexShader(
    VertexShaderResource* vertex_shader,
    const xe_gpu_program_cntl_t& program_cntl) {
  SCOPE_profile_cpu_f("gpu");

  type_ = XE_GPU_SHADER_TYPE_VERTEX;
  tex_fetch_index_ = 0;
  dwords_ = vertex_shader->dwords();

  // Add constants buffers.
  // We could optimize this by only including used buffers, but the compiler
  // seems to do a good job of doing this for us.
  // It also does read detection, so c[512] can end up c[4] in the asm -
  // instead of doing this optimization ourselves we could maybe just query
  // this from the compiler.
  append(
    "cbuffer float_consts : register(b0) {\n"
    "  float4 c[512];\n"
    "};\n");
  // TODO(benvanik): add bool/loop constants.

  AppendTextureHeader(vertex_shader->sampler_inputs());

  // Transform utilities. We adjust the output position in various ways
  // as we can't do this via D3D11 APIs.
  append(
    "cbuffer vs_consts : register(b3) {\n"
    "  float4 window;\n"              // x,y,w,h
    "  float4 viewport_z_enable;\n"   // min,(max - min),?,enabled
    "  float4 viewport_size;\n"       // x,y,w,h
    "};"
    "float4 applyViewport(float4 pos) {\n"
    "  if (viewport_z_enable.w) {\n"
    //"    pos.x = (pos.x + 1) * viewport_size.z * 0.5 + viewport_size.x;\n"
    //"    pos.y = (1 - pos.y) * viewport_size.w * 0.5 + viewport_size.y;\n"
    //"    pos.z = viewport_z_enable.x + pos.z * viewport_z_enable.y;\n"
    // w?
    "  } else {\n"
    "    pos.xy = pos.xy / float2(window.z / 2.0, -window.w / 2.0) + float2(-1.0, 1.0);\n"
    "    pos.zw = float2(0.0, 1.0);\n"
    "  }\n"
    "  pos.xy += window.xy;\n"
    "  return pos;\n"
    "}\n");

  // Add vertex shader input.
  append(
    "struct VS_INPUT {\n");
  uint32_t el_index = 0;
  const auto& buffer_inputs = vertex_shader->buffer_inputs();
  for (uint32_t n = 0; n < buffer_inputs.count; n++) {
    const auto& input = buffer_inputs.descs[n];
    for (uint32_t m = 0; m < input.info.element_count; m++) {
      const auto& el = input.info.elements[m];
      const char* type_name = GetFormatTypeName(el);
      const auto& fetch = el.vtx_fetch;
      uint32_t fetch_slot = fetch.const_index * 3 + fetch.const_index_sel;
      append(
        "  %s vf%u_%d : XE_VF%u;\n",
        type_name, fetch_slot, fetch.offset, el_index);
      el_index++;
    }
  }
  append(
    "};\n");

  // Add vertex shader output (pixel shader input).
  const auto& alloc_counts = vertex_shader->alloc_counts();
  append(
    "struct VS_OUTPUT {\n");
  if (alloc_counts.positions) {
    assert_true(alloc_counts.positions == 1);
    append(
      "  float4 oPos : SV_POSITION;\n");
  }
  if (alloc_counts.params) {
    append(
      "  float4 o[%d] : XE_O;\n",
      kMaxInterpolators);
  }
  if (alloc_counts.point_size) {
    append(
      "  float4 oPointSize : PSIZE;\n");
  }
  append(
    "};\n");

  // Vertex shader main() header.
  append(
    "VS_OUTPUT main(VS_INPUT i) {\n"
    "  VS_OUTPUT o;\n");

  // Always write position, as some shaders seem to only write certain values.
  if (alloc_counts.positions) {
    append(
      "  o.oPos = float4(0.0, 0.0, 0.0, 0.0);\n");
  }
  if (alloc_counts.point_size) {
    append(
      "  o.oPointSize = float4(1.0, 0.0, 0.0, 0.0);\n");
  }

  // TODO(benvanik): remove this, if possible (though the compiler may be smart
  //     enough to do it for us).
  if (alloc_counts.params) {
    for (uint32_t n = 0; n < kMaxInterpolators; n++) {
      append(
        "  o.o[%d] = float4(0.0, 0.0, 0.0, 0.0);\n", n);
    }
  }

  // Add temporaries for any registers we may use.
  uint32_t temp_regs = program_cntl.vs_regs + program_cntl.ps_regs;
  for (uint32_t n = 0; n <= temp_regs; n++) {
    append(
      "  float4 r%d = c[%d];\n", n, n);
  }
  append("  float4 t;\n");

  // Execute blocks.
  const auto& execs = vertex_shader->execs();
  for (auto it = execs.begin(); it != execs.end(); ++it) {
    const instr_cf_exec_t& cf = *it;
    // TODO(benvanik): figure out how sequences/jmps/loops/etc work.
    if (TranslateExec(cf)) {
      return 1;
    }
  }

  // main footer.
  if (alloc_counts.positions) {
    append(
      "  o.oPos = applyViewport(o.oPos);\n");
  }
  append(
    "  return o;\n"
    "};\n");

  return 0;
}

int D3D11ShaderTranslator::TranslatePixelShader(
    PixelShaderResource* pixel_shader,
    const xe_gpu_program_cntl_t& program_cntl,
    const VertexShaderResource::AllocCounts& alloc_counts) {
  SCOPE_profile_cpu_f("gpu");

  // We need an input VS to make decisions here.
  // TODO(benvanik): do we need to pair VS/PS up and store the combination?
  // If the same PS is used with different VS that output different amounts
  // (and less than the number of required registers), things may die.

  type_ = XE_GPU_SHADER_TYPE_PIXEL;
  tex_fetch_index_ = 0;
  dwords_ = pixel_shader->dwords();

  // Add constants buffers.
  // We could optimize this by only including used buffers, but the compiler
  // seems to do a good job of doing this for us.
  // It also does read detection, so c[512] can end up c[4] in the asm -
  // instead of doing this optimization ourselves we could maybe just query
  // this from the compiler.
  append(
    "cbuffer float_consts : register(b0) {\n"
    "  float4 c[512];\n"
    "};\n");
  // TODO(benvanik): add bool/loop constants.

  AppendTextureHeader(pixel_shader->sampler_inputs());

  // Add vertex shader output (pixel shader input).
  append(
    "struct VS_OUTPUT {\n");
  if (alloc_counts.positions) {
    assert_true(alloc_counts.positions == 1);
    append(
      "  float4 oPos : SV_POSITION;\n");
  }
  if (alloc_counts.params) {
    append(
      "  float4 o[%d] : XE_O;\n",
      kMaxInterpolators);
  }
  append(
    "};\n");

  // Add pixel shader output.
  append(
    "struct PS_OUTPUT {\n");
  for (uint32_t n = 0; n < alloc_counts.params; n++) {
    append(
      "  float4 oC%d   : SV_TARGET%d;\n", n, n);
    if (program_cntl.ps_export_depth) {
      // Is this per render-target?
      append(
        "  float oD%d   : SV_DEPTH%d;\n", n, n);
    }
  }
  append(
    "};\n");

  // Pixel shader main() header.
  append(
    "PS_OUTPUT main(VS_OUTPUT i) {\n"
    "  PS_OUTPUT o;\n");
  for (uint32_t n = 0; n < alloc_counts.params; n++) {
    append(
      "  o.oC%d = float4(1.0, 0.0, 0.0, 1.0);\n", n);
  }

  // Add temporary registers.
  uint32_t temp_regs = program_cntl.vs_regs + program_cntl.ps_regs;
  for (uint32_t n = 0; n <= MAX(15, temp_regs); n++) {
    append(
      "  float4 r%d = c[%d];\n", n, n + 256);
  }
  append("  float4 t;\n");

  // Bring registers local.
  if (alloc_counts.params) {
    for (uint32_t n = 0; n < kMaxInterpolators; n++) {
      append(
        "  r%d = i.o[%d];\n", n, n);
    }
  }

  // Execute blocks.
  const auto& execs = pixel_shader->execs();
  for (auto it = execs.begin(); it != execs.end(); ++it) {
    const instr_cf_exec_t& cf = *it;
    // TODO(benvanik): figure out how sequences/jmps/loops/etc work.
    if (TranslateExec(cf)) {
      return 1;
    }
  }

  // main footer.
  append(
    "  return o;\n"
    "}\n");

  return 0;
}

void D3D11ShaderTranslator::AppendTextureHeader(
    const ShaderResource::SamplerInputs& sampler_inputs) {
  bool fetch_setup[32] = { false };

  // 1 texture per constant slot, 1 sampler per fetch.
  for (uint32_t n = 0; n < sampler_inputs.count; n++) {
    const auto& input = sampler_inputs.descs[n];
    const auto& fetch = input.tex_fetch;

    // Add texture, if needed.
    if (!fetch_setup[fetch.const_idx]) {
      fetch_setup[fetch.const_idx] = true;
      const char* texture_type = NULL;
      switch (fetch.dimension) {
      case DIMENSION_1D:
        texture_type = "Texture1D";
        break;
      default:
      case DIMENSION_2D:
        texture_type = "Texture2D";
        break;
      case DIMENSION_3D:
        texture_type = "Texture3D";
        break;
      case DIMENSION_CUBE:
        texture_type = "TextureCube";
        break;
      }
      append("%s x_texture_%d;\n", texture_type, fetch.const_idx);
    }

    // Add sampler.
    append("SamplerState x_sampler_%d;\n", n);
  }
}

namespace {

static const char chan_names[] = {
  'x', 'y', 'z', 'w',
  // these only apply to FETCH dst's, and we shouldn't be using them:
  '0', '1', '?', '_',
};

}  // namespace

void D3D11ShaderTranslator::AppendSrcReg(uint32_t num, uint32_t type,
                                         uint32_t swiz, uint32_t negate,
                                         uint32_t abs) {
  if (negate) {
    append("-");
  }
  if (abs) {
    append("abs(");
  }
  if (type) {
    // Register.
    append("r%u", num);
  } else {
    // Constant.
    append("c[%u]", type_ == XE_GPU_SHADER_TYPE_PIXEL ? num + 256 : num);
  }
  if (swiz) {
    append(".");
    for (int i = 0; i < 4; i++) {
      append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
  if (abs) {
    append(")");
  }
}

void D3D11ShaderTranslator::AppendDestRegName(uint32_t num, uint32_t dst_exp) {
  if (!dst_exp) {
    // Register.
    append("r%u", num);
  } else {
    // Export.
    switch (type_) {
    case XE_GPU_SHADER_TYPE_VERTEX:
      switch (num) {
      case 62:
        append("o.oPos");
        break;
      case 63:
        append("o.oPointSize");
        break;
      default:
        // Varying.
        append("o.o[%u]", num);;
        break;
      }
      break;
    case XE_GPU_SHADER_TYPE_PIXEL:
      switch (num) {
      case 0:
        append("o.oC0");
        break;
      default:
        // TODO(benvanik): other render targets?
        // TODO(benvanik): depth?
        assert_always();
        break;
      }
      break;
    }
  }
}

void D3D11ShaderTranslator::AppendDestReg(uint32_t num, uint32_t mask,
                                          uint32_t dst_exp) {
  if (mask != 0xF) {
    // If masking, store to a temporary variable and clean it up later.
    append("t");
  } else {
    // Store directly to output.
    AppendDestRegName(num, dst_exp);
  }
}

void D3D11ShaderTranslator::AppendDestRegPost(uint32_t num, uint32_t mask,
                                              uint32_t dst_exp) {
  if (mask != 0xF) {
    // Masking.
    append("  ");
    AppendDestRegName(num, dst_exp);
    append(" = float4(");
    for (int i = 0; i < 4; i++) {
      // TODO(benvanik): mask out values? mix in old value as temp?
      // append("%c", (mask & 0x1) ? chan_names[i] : 'w');
      if (!(mask & 0x1)) {
        AppendDestRegName(num, dst_exp);
      } else {
        append("t");
      }
      append(".%c", chan_names[i]);
      mask >>= 1;
      if (i < 3) {
        append(", ");
      }
    }
    append(");\n");
  }
}

void D3D11ShaderTranslator::PrintSrcReg(uint32_t num, uint32_t type,
                                        uint32_t swiz, uint32_t negate,
                                        uint32_t abs) {
  if (negate) {
    append("-");
  }
  if (abs) {
    append("|");
  }
  append("%c%u", type ? 'R' : 'C', num);
  if (swiz) {
    append(".");
    for (int i = 0; i < 4; i++) {
      append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
  if (abs) {
    append("|");
  }
}

void D3D11ShaderTranslator::PrintDstReg(uint32_t num, uint32_t mask,
                                        uint32_t dst_exp) {
  append("%s%u", dst_exp ? "export" : "R", num);
  if (mask != 0xf) {
    append(".");
    for (int i = 0; i < 4; i++) {
      append("%c", (mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void D3D11ShaderTranslator::PrintExportComment(uint32_t num) {
  const char *name = NULL;
  switch (type_) {
  case XE_GPU_SHADER_TYPE_VERTEX:
    switch (num) {
    case 62: name = "gl_Position";  break;
    case 63: name = "gl_PointSize"; break;
    }
    break;
  case XE_GPU_SHADER_TYPE_PIXEL:
    switch (num) {
    case 0:  name = "gl_FragColor"; break;
    }
    break;
  }
  /* if we had a symbol table here, we could look
   * up the name of the varying..
   */
  if (name) {
    append("\t; %s", name);
  }
}

int D3D11ShaderTranslator::TranslateALU_ADDv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(" + ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_MULv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(" * ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_MAXv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  if (alu.src1_reg == alu.src2_reg &&
      alu.src1_sel == alu.src2_sel &&
      alu.src1_swiz == alu.src2_swiz &&
      alu.src1_reg_negate == alu.src2_reg_negate &&
      alu.src1_reg_abs == alu.src2_reg_abs) {
    // This is a mov.
    AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  } else {
    append("max(");
    AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
    append(", ");
    AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
    append(")");
  }
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_MINv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("min(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(", ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_SETXXv(const instr_alu_t& alu, const char* op) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("float4((");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").x %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").x ? 1.0 : 0.0, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").y %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").y ? 1.0 : 0.0, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").z %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").z ? 1.0 : 0.0, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").w %s (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").w ? 1.0 : 0.0)");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}
int D3D11ShaderTranslator::TranslateALU_SETEv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, "==");
}
int D3D11ShaderTranslator::TranslateALU_SETGTv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, ">");
}
int D3D11ShaderTranslator::TranslateALU_SETGTEv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, ">=");
}
int D3D11ShaderTranslator::TranslateALU_SETNEv(const instr_alu_t& alu) {
  return TranslateALU_SETXXv(alu, "!=");
}

int D3D11ShaderTranslator::TranslateALU_FRACv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("frac(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_TRUNCv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("trunc(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_FLOORv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("floor(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_MULADDv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("mad(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(", ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(", ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_CNDXXv(const instr_alu_t& alu, const char* op) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  // TODO(benvanik): check argument order - could be 3 as compare and 1 and 2 as values.
  append("float4((");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").x %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").x : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(").x, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").y %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").y : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(").y, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").z %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").z : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(").z, (");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").w %s 0.0 ? (", op);
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").w : (");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(").w)");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}
int D3D11ShaderTranslator::TranslateALU_CNDEv(const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(alu, "==");
}
int D3D11ShaderTranslator::TranslateALU_CNDGTEv(const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(alu, ">=");
}
int D3D11ShaderTranslator::TranslateALU_CNDGTv(const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(alu, ">");
}

int D3D11ShaderTranslator::TranslateALU_DOT4v(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("dot(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(", ");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(")");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_DOT3v(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("dot(float4(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").xyz, float4(");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").xyz)");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_DOT2ADDv(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("dot(float4(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(").xy, float4(");
  AppendSrcReg(alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  append(").xy) + ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".x");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

// CUBEv

int D3D11ShaderTranslator::TranslateALU_MAX4v(const instr_alu_t& alu) {
  AppendDestReg(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  append(" = ");
  if (alu.vector_clamp) {
    append("saturate(");
  }
  append("max(");
  append("max(");
  append("max(");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(".x, ");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(".y), ");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(".z), ");
  AppendSrcReg(alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  append(".w)");
  if (alu.vector_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

// ...

int D3D11ShaderTranslator::TranslateALU_MAXs(const instr_alu_t& alu) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  if ((alu.src3_swiz & 0x3) == (((alu.src3_swiz >> 2) + 1) & 0x3)) {
    // This is a mov.
    AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  } else {
    append("max(");
    AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
    append(".x, ");
    AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
    append(".y).xxxx");
  }
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_MINs(const instr_alu_t& alu) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  append("min(");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".x, ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".y).xxxx");
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_SETXXs(const instr_alu_t& alu, const char* op) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  append("((");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".x %s 0.0) ? 1.0 : 0.0).xxxx", op);
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int D3D11ShaderTranslator::TranslateALU_SETEs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, "==");
}
int D3D11ShaderTranslator::TranslateALU_SETGTs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, ">");
}
int D3D11ShaderTranslator::TranslateALU_SETGTEs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, ">=");
}
int D3D11ShaderTranslator::TranslateALU_SETNEs(const instr_alu_t& alu) {
  return TranslateALU_SETXXs(alu, "!=");
}

int D3D11ShaderTranslator::TranslateALU_RECIP_IEEE(const instr_alu_t& alu) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  append("(1.0 / ");
  AppendSrcReg(alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  append(")");
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}

int D3D11ShaderTranslator::TranslateALU_MUL_CONST_0(const instr_alu_t& alu) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 = (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  append("(");
  AppendSrcReg(alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".%c * ", chan_names[swiz_a]);
  AppendSrcReg(reg2, 1, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".%c", chan_names[swiz_b]);
  append(").xxxx");
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int D3D11ShaderTranslator::TranslateALU_MUL_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_MUL_CONST_0(alu);
}

int D3D11ShaderTranslator::TranslateALU_ADD_CONST_0(const instr_alu_t& alu) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 = (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  append("(");
  AppendSrcReg(alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".%c + ", chan_names[swiz_a]);
  AppendSrcReg(reg2, 1, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".%c", chan_names[swiz_b]);
  append(").xxxx");
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int D3D11ShaderTranslator::TranslateALU_ADD_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_ADD_CONST_0(alu);
}

int D3D11ShaderTranslator::TranslateALU_SUB_CONST_0(const instr_alu_t& alu) {
  AppendDestReg(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  append(" = ");
  if (alu.scalar_clamp) {
    append("saturate(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 = (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  append("(");
  AppendSrcReg(alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".%c - ", chan_names[swiz_a]);
  AppendSrcReg(reg2, 1, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  append(".%c", chan_names[swiz_b]);
  append(").xxxx");
  if (alu.scalar_clamp) {
    append(")");
  }
  append(";\n");
  AppendDestRegPost(alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int D3D11ShaderTranslator::TranslateALU_SUB_CONST_1(const instr_alu_t& alu) {
  return TranslateALU_SUB_CONST_0(alu);
}

namespace {

typedef int (D3D11ShaderTranslator::*TranslateFn)(const instr_alu_t& alu);
typedef struct {
  uint32_t num_srcs;
  const char* name;
  TranslateFn   fn;
} TranslateInfo;
#define ALU_INSTR(opc, num_srcs) \
    { num_srcs, #opc, nullptr }
#define ALU_INSTR_IMPL(opc, num_srcs) \
    { num_srcs, #opc, &D3D11ShaderTranslator::TranslateALU_##opc }

}  // namespace

int D3D11ShaderTranslator::TranslateALU(const instr_alu_t* alu, int sync) {
  static TranslateInfo vector_alu_instrs[0x20] = {
    ALU_INSTR_IMPL(ADDv,              2),  // 0
    ALU_INSTR_IMPL(MULv,              2),  // 1
    ALU_INSTR_IMPL(MAXv,              2),  // 2
    ALU_INSTR_IMPL(MINv,              2),  // 3
    ALU_INSTR_IMPL(SETEv,             2),  // 4
    ALU_INSTR_IMPL(SETGTv,            2),  // 5
    ALU_INSTR_IMPL(SETGTEv,           2),  // 6
    ALU_INSTR_IMPL(SETNEv,            2),  // 7
    ALU_INSTR_IMPL(FRACv,             1),  // 8
    ALU_INSTR_IMPL(TRUNCv,            1),  // 9
    ALU_INSTR_IMPL(FLOORv,            1),  // 10
    ALU_INSTR_IMPL(MULADDv,           3),  // 11
    ALU_INSTR_IMPL(CNDEv,             3),  // 12
    ALU_INSTR_IMPL(CNDGTEv,           3),  // 13
    ALU_INSTR_IMPL(CNDGTv,            3),  // 14
    ALU_INSTR_IMPL(DOT4v,             2),  // 15
    ALU_INSTR_IMPL(DOT3v,             2),  // 16
    ALU_INSTR_IMPL(DOT2ADDv,          3),  // 17 -- ???
    ALU_INSTR(CUBEv,                  2),  // 18
    ALU_INSTR_IMPL(MAX4v,             1),  // 19
    ALU_INSTR(PRED_SETE_PUSHv,        2),  // 20
    ALU_INSTR(PRED_SETNE_PUSHv,       2),  // 21
    ALU_INSTR(PRED_SETGT_PUSHv,       2),  // 22
    ALU_INSTR(PRED_SETGTE_PUSHv,      2),  // 23
    ALU_INSTR(KILLEv,                 2),  // 24
    ALU_INSTR(KILLGTv,                2),  // 25
    ALU_INSTR(KILLGTEv,               2),  // 26
    ALU_INSTR(KILLNEv,                2),  // 27
    ALU_INSTR(DSTv,                   2),  // 28
    ALU_INSTR(MOVAv,                  1),  // 29
  };
  static TranslateInfo scalar_alu_instrs[0x40] = {
    ALU_INSTR(ADDs,                   1),  // 0
    ALU_INSTR(ADD_PREVs,              1),  // 1
    ALU_INSTR(MULs,                   1),  // 2
    ALU_INSTR(MUL_PREVs,              1),  // 3
    ALU_INSTR(MUL_PREV2s,             1),  // 4
    ALU_INSTR_IMPL(MAXs,              1),  // 5
    ALU_INSTR_IMPL(MINs,              1),  // 6
    ALU_INSTR_IMPL(SETEs,             1),  // 7
    ALU_INSTR_IMPL(SETGTs,            1),  // 8
    ALU_INSTR_IMPL(SETGTEs,           1),  // 9
    ALU_INSTR_IMPL(SETNEs,            1),  // 10
    ALU_INSTR(FRACs,                  1),  // 11
    ALU_INSTR(TRUNCs,                 1),  // 12
    ALU_INSTR(FLOORs,                 1),  // 13
    ALU_INSTR(EXP_IEEE,               1),  // 14
    ALU_INSTR(LOG_CLAMP,              1),  // 15
    ALU_INSTR(LOG_IEEE,               1),  // 16
    ALU_INSTR(RECIP_CLAMP,            1),  // 17
    ALU_INSTR(RECIP_FF,               1),  // 18
    ALU_INSTR_IMPL(RECIP_IEEE,        1),  // 19
    ALU_INSTR(RECIPSQ_CLAMP,          1),  // 20
    ALU_INSTR(RECIPSQ_FF,             1),  // 21
    ALU_INSTR(RECIPSQ_IEEE,           1),  // 22
    ALU_INSTR(MOVAs,                  1),  // 23
    ALU_INSTR(MOVA_FLOORs,            1),  // 24
    ALU_INSTR(SUBs,                   1),  // 25
    ALU_INSTR(SUB_PREVs,              1),  // 26
    ALU_INSTR(PRED_SETEs,             1),  // 27
    ALU_INSTR(PRED_SETNEs,            1),  // 28
    ALU_INSTR(PRED_SETGTs,            1),  // 29
    ALU_INSTR(PRED_SETGTEs,           1),  // 30
    ALU_INSTR(PRED_SET_INVs,          1),  // 31
    ALU_INSTR(PRED_SET_POPs,          1),  // 32
    ALU_INSTR(PRED_SET_CLRs,          1),  // 33
    ALU_INSTR(PRED_SET_RESTOREs,      1),  // 34
    ALU_INSTR(KILLEs,                 1),  // 35
    ALU_INSTR(KILLGTs,                1),  // 36
    ALU_INSTR(KILLGTEs,               1),  // 37
    ALU_INSTR(KILLNEs,                1),  // 38
    ALU_INSTR(KILLONEs,               1),  // 39
    ALU_INSTR(SQRT_IEEE,              1),  // 40
    { 0, 0, false },
    ALU_INSTR_IMPL(MUL_CONST_0,       2),  // 42
    ALU_INSTR_IMPL(MUL_CONST_1,       2),  // 43
    ALU_INSTR_IMPL(ADD_CONST_0,       2),  // 44
    ALU_INSTR_IMPL(ADD_CONST_1,       2),  // 45
    ALU_INSTR_IMPL(SUB_CONST_0,       2),  // 46
    ALU_INSTR_IMPL(SUB_CONST_1,       2),  // 47
    ALU_INSTR(SIN,                    1),  // 48
    ALU_INSTR(COS,                    1),  // 49
    ALU_INSTR(RETAIN_PREV,            1),  // 50
  };
#undef ALU_INSTR
#undef ALU_INSTR_IMPL

  if (!alu->scalar_write_mask && !alu->vector_write_mask) {
    append("  //   <nop>\n");
    return 0;
  }

  if (alu->vector_write_mask) {
    // Disassemble vector op.
    const auto& iv = vector_alu_instrs[alu->vector_opc];
    append("  //   %sALU:\t", sync ? "(S)" : "   ");
    append("%s", iv.name);
    if (alu->pred_select & 0x2) {
      // seems to work similar to conditional execution in ARM instruction
      // set, so let's use a similar syntax for now:
      append((alu->pred_select & 0x1) ? "EQ" : "NE");
    }
    append("\t");
    PrintDstReg(alu->vector_dest, alu->vector_write_mask, alu->export_data);
    append(" = ");
    if (iv.num_srcs == 3) {
      PrintSrcReg(alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                  alu->src3_reg_negate, alu->src3_reg_abs);
      append(", ");
    }
    PrintSrcReg(alu->src1_reg, alu->src1_sel, alu->src1_swiz,
                alu->src1_reg_negate, alu->src1_reg_abs);
    if (iv.num_srcs > 1) {
      append(", ");
      PrintSrcReg(alu->src2_reg, alu->src2_sel, alu->src2_swiz,
                  alu->src2_reg_negate, alu->src2_reg_abs);
    }
    if (alu->vector_clamp) {
      append(" CLAMP");
    }
    if (alu->export_data) {
      PrintExportComment(alu->vector_dest);
    }
    append("\n");

    // Translate vector op.
    if (iv.fn) {
      append("  ");
      if ((this->*iv.fn)(*alu)) {
        return 1;
      }
    } else {
      append("  // <UNIMPLEMENTED>\n");
    }
  }

  if (alu->scalar_write_mask || !alu->vector_write_mask) {
    // 2nd optional scalar op:

    // Disassemble scalar op.
    const auto& is = scalar_alu_instrs[alu->scalar_opc];
    append("  //  ");
    append("\t");
    if (is.name) {
      append("\t    \t%s\t", is.name);
    } else {
      append("\t    \tOP(%u)\t", alu->scalar_opc);
    }
    PrintDstReg(alu->scalar_dest, alu->scalar_write_mask, alu->export_data);
    append(" = ");
    if (is.num_srcs == 2) {
      // ADD_CONST_0 dest, [const], [reg]
      uint32_t src3_swiz = alu->src3_swiz & ~0x3C;
      uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
      uint32_t swiz_b = (src3_swiz & 0x3);
      PrintSrcReg(alu->src3_reg, 0, 0,
                  alu->src3_reg_negate, alu->src3_reg_abs);
      append(".%c", chan_names[swiz_a]);
      append(", ");
      uint32_t reg2 = (alu->scalar_opc & 1) | (alu->src3_swiz & 0x3C) | (alu->src3_sel << 1);
      PrintSrcReg(reg2, 1, 0,
                  alu->src3_reg_negate, alu->src3_reg_abs);
      append(".%c", chan_names[swiz_b]);
    } else {
      PrintSrcReg(alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                  alu->src3_reg_negate, alu->src3_reg_abs);
    }
    if (alu->scalar_clamp) {
      append(" CLAMP");
    }
    if (alu->export_data) {
      PrintExportComment(alu->scalar_dest);
    }
    append("\n");

    // Translate scalar op.
    if (is.fn) {
      append("  ");
      if ((this->*is.fn)(*alu)) {
        return 1;
      }
    } else {
      append("  // <UNIMPLEMENTED>\n");
    }
  }

  return 0;
}

void D3D11ShaderTranslator::PrintDestFecth(uint32_t dst_reg,
                                           uint32_t dst_swiz) {
  append("\tR%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

void D3D11ShaderTranslator::AppendFetchDest(uint32_t dst_reg,
                                            uint32_t dst_swiz) {
  append("r%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

int D3D11ShaderTranslator::GetFormatComponentCount(uint32_t format) {
  switch (format) {
  case FMT_32:
  case FMT_32_FLOAT:
    return 1;
  case FMT_16_16:
  case FMT_16_16_FLOAT:
  case FMT_32_32:
  case FMT_32_32_FLOAT:
    return 2;
  case FMT_10_11_11:
  case FMT_11_11_10:
  case FMT_32_32_32_FLOAT:
    return 3;
  case FMT_8_8_8_8:
  case FMT_2_10_10_10:
  case FMT_16_16_16_16:
  case FMT_16_16_16_16_FLOAT:
  case FMT_32_32_32_32:
  case FMT_32_32_32_32_FLOAT:
    return 4;
  default:
    XELOGE("Unknown vertex format: %d", format);
    assert_always();
    return 4;
  }
}

int D3D11ShaderTranslator::TranslateExec(const instr_cf_exec_t& cf) {
  static const struct {
    const char *name;
  } cf_instructions[] = {
  #define INSTR(opc, fxn) { #opc }
      INSTR(NOP, print_cf_nop),
      INSTR(EXEC, print_cf_exec),
      INSTR(EXEC_END, print_cf_exec),
      INSTR(COND_EXEC, print_cf_exec),
      INSTR(COND_EXEC_END, print_cf_exec),
      INSTR(COND_PRED_EXEC, print_cf_exec),
      INSTR(COND_PRED_EXEC_END, print_cf_exec),
      INSTR(LOOP_START, print_cf_loop),
      INSTR(LOOP_END, print_cf_loop),
      INSTR(COND_CALL, print_cf_jmp_call),
      INSTR(RETURN, print_cf_jmp_call),
      INSTR(COND_JMP, print_cf_jmp_call),
      INSTR(ALLOC, print_cf_alloc),
      INSTR(COND_EXEC_PRED_CLEAN, print_cf_exec),
      INSTR(COND_EXEC_PRED_CLEAN_END, print_cf_exec),
      INSTR(MARK_VS_FETCH_DONE, print_cf_nop),  // ??
  #undef INSTR
  };

  append(
    "  // %s ADDR(0x%x) CNT(0x%x)",
    cf_instructions[cf.opc].name, cf.address, cf.count);
  if (cf.yeild) {
    append(" YIELD");
  }
  uint8_t vc = cf.vc_hi | (cf.vc_lo << 2);
  if (vc) {
    append(" VC(0x%x)", vc);
  }
  if (cf.bool_addr) {
    append(" BOOL_ADDR(0x%x)", cf.bool_addr);
  }
  if (cf.address_mode == ABSOLUTE_ADDR) {
    append(" ABSOLUTE_ADDR");
  }
  if (cf.is_cond_exec()) {
    append(" COND(%d)", cf.condition);
  }
  append("\n");

  uint32_t sequence = cf.serialize;
  for (uint32_t i = 0; i < cf.count; i++) {
    uint32_t alu_off = (cf.address + i);
    int sync = sequence & 0x2;
    if (sequence & 0x1) {
      const instr_fetch_t* fetch =
          (const instr_fetch_t*)(dwords_ + alu_off * 3);
      switch (fetch->opc) {
      case VTX_FETCH:
        if (TranslateVertexFetch(&fetch->vtx, sync)) {
          return 1;
        }
        break;
      case TEX_FETCH:
        if (TranslateTextureFetch(&fetch->tex, sync)) {
          return 1;
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
      const instr_alu_t* alu =
          (const instr_alu_t*)(dwords_ + alu_off * 3);
      if (TranslateALU(alu, sync)) {
        return 1;
      }
    }
    sequence >>= 2;
  }

  return 0;
}

int D3D11ShaderTranslator::TranslateVertexFetch(const instr_fetch_vtx_t* vtx,
                                                int sync) {
  static const struct {
    const char *name;
  } fetch_types[0xff] = {
  #define TYPE(id) { #id }
      TYPE(FMT_1_REVERSE), // 0
      {0},
      TYPE(FMT_8), // 2
      {0},
      {0},
      {0},
      TYPE(FMT_8_8_8_8), // 6
      TYPE(FMT_2_10_10_10), // 7
      {0},
      {0},
      TYPE(FMT_8_8), // 10
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
      TYPE(FMT_16), // 24
      TYPE(FMT_16_16), // 25
      TYPE(FMT_16_16_16_16), // 26
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      TYPE(FMT_32), // 33
      TYPE(FMT_32_32), // 34
      TYPE(FMT_32_32_32_32), // 35
      TYPE(FMT_32_FLOAT), // 36
      TYPE(FMT_32_32_FLOAT), // 37
      TYPE(FMT_32_32_32_32_FLOAT), // 38
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
      TYPE(FMT_32_32_32_FLOAT), // 57
  #undef TYPE
  };

  // Disassemble.
  append("  //   %sFETCH:\t", sync ? "(S)" : "   ");
  if (vtx->pred_select) {
    append(vtx->pred_condition ? "EQ" : "NE");
  }
  PrintDestFecth(vtx->dst_reg, vtx->dst_swiz);
  append(" = R%u.", vtx->src_reg);
  append("%c", chan_names[vtx->src_swiz & 0x3]);
  if (fetch_types[vtx->format].name) {
    append(" %s", fetch_types[vtx->format].name);
  } else  {
    append(" TYPE(0x%x)", vtx->format);
  }
  append(" %s", vtx->format_comp_all ? "SIGNED" : "UNSIGNED");
  if (!vtx->num_format_all) {
    append(" NORMALIZED");
  }
  append(" STRIDE(%u)", vtx->stride);
  if (vtx->offset) {
    append(" OFFSET(%u)", vtx->offset);
  }
  append(" CONST(%u, %u)", vtx->const_index, vtx->const_index_sel);
  if (1) {
    // XXX
    append(" src_reg_am=%u", vtx->src_reg_am);
    append(" dst_reg_am=%u", vtx->dst_reg_am);
    append(" num_format_all=%u", vtx->num_format_all);
    append(" signed_rf_mode_all=%u", vtx->signed_rf_mode_all);
    append(" exp_adjust_all=%u", vtx->exp_adjust_all);
  }
  append("\n");

  // Translate.
  append("  ");
  append("r%u.xyzw", vtx->dst_reg);
  append(" = float4(");
  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  // TODO(benvanik): detect xyzw = xyzw, etc.
  // TODO(benvanik): detect and set as rN = float4(samp.xyz, 1.0); / etc
  uint32_t component_count = GetFormatComponentCount(vtx->format);
  uint32_t dst_swiz = vtx->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      append("0.0");
    } else if ((dst_swiz & 0x7) == 5) {
      append("1.0");
    } else if ((dst_swiz & 0x7) == 6) {
      // ?
      append("?");
    } else if ((dst_swiz & 0x7) == 7) {
      append("r%u.%c", vtx->dst_reg, chan_names[i]);
    } else {
      append("i.vf%u_%d.%c",
                     fetch_slot, vtx->offset,
                     chan_names[dst_swiz & 0x3]);
    }
    if (i < 3) {
      append(", ");
    }
    dst_swiz >>= 3;
  }
  append(");\n");
  return 0;
}

int D3D11ShaderTranslator::TranslateTextureFetch(const instr_fetch_tex_t* tex,
                                                 int sync) {
  // Disassemble.
  static const char *filter[] = {
    "POINT",    // TEX_FILTER_POINT
    "LINEAR",   // TEX_FILTER_LINEAR
    "BASEMAP",  // TEX_FILTER_BASEMAP
  };
  static const char *aniso_filter[] = {
    "DISABLED", // ANISO_FILTER_DISABLED
    "MAX_1_1",  // ANISO_FILTER_MAX_1_1
    "MAX_2_1",  // ANISO_FILTER_MAX_2_1
    "MAX_4_1",  // ANISO_FILTER_MAX_4_1
    "MAX_8_1",  // ANISO_FILTER_MAX_8_1
    "MAX_16_1", // ANISO_FILTER_MAX_16_1
  };
  static const char *arbitrary_filter[] = {
    "2x4_SYM",  // ARBITRARY_FILTER_2X4_SYM
    "2x4_ASYM", // ARBITRARY_FILTER_2X4_ASYM
    "4x2_SYM",  // ARBITRARY_FILTER_4X2_SYM
    "4x2_ASYM", // ARBITRARY_FILTER_4X2_ASYM
    "4x4_SYM",  // ARBITRARY_FILTER_4X4_SYM
    "4x4_ASYM", // ARBITRARY_FILTER_4X4_ASYM
  };
  static const char *sample_loc[] = {
    "CENTROID", // SAMPLE_CENTROID
    "CENTER",   // SAMPLE_CENTER
  };
  uint32_t src_swiz = tex->src_swiz;
  append("  //   %sFETCH:\t", sync ? "(S)" : "   ");
  if (tex->pred_select) {
    append(tex->pred_condition ? "EQ" : "NE");
  }
  PrintDestFecth(tex->dst_reg, tex->dst_swiz);
  append(" = R%u.", tex->src_reg);
  for (int i = 0; i < 3; i++) {
    append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  append(" CONST(%u)", tex->const_idx);
  if (tex->fetch_valid_only) {
    append(" VALID_ONLY");
  }
  if (tex->tx_coord_denorm) {
    append(" DENORM");
  }
  if (tex->mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    append(" MAG(%s)", filter[tex->mag_filter]);
  }
  if (tex->min_filter != TEX_FILTER_USE_FETCH_CONST) {
    append(" MIN(%s)", filter[tex->min_filter]);
  }
  if (tex->mip_filter != TEX_FILTER_USE_FETCH_CONST) {
    append(" MIP(%s)", filter[tex->mip_filter]);
  }
  if (tex->aniso_filter != ANISO_FILTER_USE_FETCH_CONST) {
    append(" ANISO(%s)", aniso_filter[tex->aniso_filter]);
  }
  if (tex->arbitrary_filter != ARBITRARY_FILTER_USE_FETCH_CONST) {
    append(" ARBITRARY(%s)", arbitrary_filter[tex->arbitrary_filter]);
  }
  if (tex->vol_mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    append(" VOL_MAG(%s)", filter[tex->vol_mag_filter]);
  }
  if (tex->vol_min_filter != TEX_FILTER_USE_FETCH_CONST) {
    append(" VOL_MIN(%s)", filter[tex->vol_min_filter]);
  }
  if (!tex->use_comp_lod) {
    append(" LOD(%u)", tex->use_comp_lod);
    append(" LOD_BIAS(%u)", tex->lod_bias);
  }
  if (tex->use_reg_lod) {
    append(" REG_LOD(%u)", tex->use_reg_lod);
  }
  if (tex->use_reg_gradients) {
    append(" USE_REG_GRADIENTS");
  }
  append(" LOCATION(%s)", sample_loc[tex->sample_location]);
  if (tex->offset_x || tex->offset_y || tex->offset_z) {
    append(" OFFSET(%u,%u,%u)", tex->offset_x, tex->offset_y, tex->offset_z);
  }
  append("\n");

  int src_component_count = 0;
  switch (tex->dimension) {
  case DIMENSION_1D:
    src_component_count = 1;
    break;
  default:
  case DIMENSION_2D:
    src_component_count = 2;
    break;
  case DIMENSION_3D:
    src_component_count = 3;
    break;
  case DIMENSION_CUBE:
    src_component_count = 3;
    break;
  }

  // Translate.
  append("  ");
  append("r%u.xyzw", tex->dst_reg);
  append(" = ");
  append(
      "x_texture_%d.Sample(x_sampler_%d, r%u.",
      tex->const_idx,
      tex_fetch_index_++, // hacky way to line up to tex buffers
      tex->src_reg);
  src_swiz = tex->src_swiz;
  for (int i = 0; i < src_component_count; i++) {
    append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  append(").");

  // Pass one over dest does xyzw and fakes the special values.
  // TODO(benvanik): detect and set as rN = float4(samp.xyz, 1.0); / etc
  uint32_t dst_swiz = tex->dst_swiz;
  for (int i = 0; i < 4; i++) {
    append("%c", chan_names[dst_swiz & 0x3]);
    dst_swiz >>= 3;
  }
  append(";\n");
  // Do another pass to set constant values.
  dst_swiz = tex->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      append("  r%u.%c = 0.0;\n", tex->dst_reg, chan_names[i]);
    } else if ((dst_swiz & 0x7) == 5) {
      append("  r%u.%c = 1.0;\n", tex->dst_reg, chan_names[i]);
    }
    dst_swiz >>= 3;
  }
  return 0;
}
