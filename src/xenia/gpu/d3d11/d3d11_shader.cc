/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_shader.h>

#include <xenia/gpu/xenos/ucode.h>

#include <d3dcompiler.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


namespace {

const int OUTPUT_CAPACITY = 64 * 1024;

}  // anonymous namespace


struct xe::gpu::d3d11::Output {
  char buffer[OUTPUT_CAPACITY];
  size_t capacity;
  size_t offset;
  Output() :
      capacity(OUTPUT_CAPACITY),
      offset(0) {
    buffer[0] = 0;
  }
  void append(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int len = xevsnprintfa(
        buffer + offset, capacity - offset, format, args);
    va_end(args);
    offset += len;
    buffer[offset] = 0;
  }
};


D3D11Shader::D3D11Shader(
    ID3D11Device* device,
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    translated_src_(NULL),
    Shader(type, src_ptr, length, hash) {
  device_ = device;
  device_->AddRef();
}

D3D11Shader::~D3D11Shader() {
  if (translated_src_) {
    xe_free(translated_src_);
  }
  XESAFERELEASE(device_);
}

void D3D11Shader::set_translated_src(char* value) {
  if (translated_src_) {
    xe_free(translated_src_);
  }
  translated_src_ = xestrdupa(value);
}

ID3D10Blob* D3D11Shader::Compile(const char* shader_source) {
  // TODO(benvanik): pick shared runtime mode defines.
  D3D10_SHADER_MACRO defines[] = {
    "TEST_DEFINE", "1",
    0, 0,
  };

  uint32_t flags1 = 0;
  flags1 |= D3D10_SHADER_DEBUG;
  flags1 |= D3D10_SHADER_ENABLE_STRICTNESS;
  uint32_t flags2 = 0;

  // Create a name.
  char file_name[64];
  xesnprintfa(file_name, XECOUNT(file_name),
      "gen_%.16XLL.%s",
      hash_,
      type_ == XE_GPU_SHADER_TYPE_VERTEX ? "vs" : "ps");

  // TODO(benvanik): dump to disk so tools can find it.

  // Compile shader to bytecode blob.
  ID3D10Blob* shader_blob = 0;
  ID3D10Blob* error_blob = 0;
  HRESULT hr = D3DCompile(
      shader_source, strlen(shader_source),
      file_name,
      defines, NULL,
      "main",
      type_ == XE_GPU_SHADER_TYPE_VERTEX ?
          "vs_5_0" : "ps_5_0",
      flags1, flags2,
      &shader_blob, &error_blob);
  if (error_blob) {
    char* msg = (char*)error_blob->GetBufferPointer();
    XELOGE("D3D11: shader compile failed with %s", msg);
  }
  XESAFERELEASE(error_blob);
  if (FAILED(hr)) {
    return NULL;
  }
  return shader_blob;
}


D3D11VertexShader::D3D11VertexShader(
    ID3D11Device* device,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    handle_(0), input_layout_(0),
    D3D11Shader(device, XE_GPU_SHADER_TYPE_VERTEX,
                src_ptr, length, hash) {
}

D3D11VertexShader::~D3D11VertexShader() {
  XESAFERELEASE(input_layout_);
  XESAFERELEASE(handle_);
}

int D3D11VertexShader::Prepare(xe_gpu_program_cntl_t* program_cntl) {
  if (handle_) {
    return 0;
  }

  // TODO(benvanik): look in file based on hash/etc.
  void* byte_code = NULL;
  size_t byte_code_length = 0;

  // Translate and compile source.
  const char* shader_source = Translate(program_cntl);
  if (!shader_source) {
    return 1;
  }
  ID3D10Blob* shader_blob = Compile(shader_source);
  if (!shader_blob) {
    return 1;
  }
  byte_code_length = shader_blob->GetBufferSize();
  byte_code = xe_malloc(byte_code_length);
  xe_copy_struct(
      byte_code, shader_blob->GetBufferPointer(), byte_code_length);
  XESAFERELEASE(shader_blob);

  // Create shader.
  HRESULT hr = device_->CreateVertexShader(
      byte_code, byte_code_length,
      NULL,
      &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader");
    xe_free(byte_code);
    return 1;
  }

  // Create input layout.
  size_t element_count = fetch_vtxs_.size();
  D3D11_INPUT_ELEMENT_DESC* element_descs =
      (D3D11_INPUT_ELEMENT_DESC*)xe_alloca(
          sizeof(D3D11_INPUT_ELEMENT_DESC) * element_count);
  int n = 0;
  for (std::vector<instr_fetch_vtx_t>::iterator it = fetch_vtxs_.begin();
       it != fetch_vtxs_.end(); ++it, ++n) {
    const instr_fetch_vtx_t& vtx = *it;
    DXGI_FORMAT vtx_format;
    switch (vtx.format) {
    case FMT_1_REVERSE:
      vtx_format = DXGI_FORMAT_R1_UNORM; // ?
      break;
    case FMT_8:
      if (!vtx.num_format_all) {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R8_SNORM : DXGI_FORMAT_R8_UNORM;
      } else {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R8_SINT : DXGI_FORMAT_R8_UINT;
      }
      break;
    case FMT_8_8_8_8:
      if (!vtx.num_format_all) {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
      } else {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R8G8B8A8_SINT : DXGI_FORMAT_R8G8B8A8_UINT;
      }
      break;
    case FMT_8_8:
      if (!vtx.num_format_all) {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R8G8_SNORM : DXGI_FORMAT_R8G8_UNORM;
      } else {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R8G8_SINT : DXGI_FORMAT_R8G8_UINT;
      }
      break;
    case FMT_16:
      if (!vtx.num_format_all) {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R16_SNORM : DXGI_FORMAT_R16_UNORM;
      } else {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R16_SINT : DXGI_FORMAT_R16_UINT;
      }
      break;
    case FMT_16_16:
      if (!vtx.num_format_all) {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM;
      } else {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R16G16_SINT : DXGI_FORMAT_R16G16_UINT;
      }
      break;
    case FMT_16_16_16_16:
      if (!vtx.num_format_all) {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R16G16B16A16_SNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
      } else {
        vtx_format = vtx.format_comp_all ?
            DXGI_FORMAT_R16G16B16A16_SINT : DXGI_FORMAT_R16G16B16A16_UINT;
      }
      break;
    case FMT_32:
      vtx_format = vtx.format_comp_all ?
          DXGI_FORMAT_R32_SINT : DXGI_FORMAT_R32_UINT;
      break;
    case FMT_32_32:
      vtx_format = vtx.format_comp_all ?
          DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32G32_UINT;
      break;
    case FMT_32_32_32_32:
      vtx_format = vtx.format_comp_all ?
          DXGI_FORMAT_R32G32B32A32_SINT : DXGI_FORMAT_R32G32B32A32_UINT;
      break;
    case FMT_32_FLOAT:
      vtx_format = DXGI_FORMAT_R32_FLOAT;
      break;
    case FMT_32_32_FLOAT:
      vtx_format = DXGI_FORMAT_R32G32_FLOAT;
      break;
    case FMT_32_32_32_32_FLOAT:
      vtx_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      break;
    case FMT_32_32_32_FLOAT:
      vtx_format = DXGI_FORMAT_R32G32B32_FLOAT;
      break;
    default:
      XEASSERTALWAYS();
      break;
    }
    element_descs[n].SemanticName         = "XE_VF";
    element_descs[n].SemanticIndex        = n;
    element_descs[n].Format               = vtx_format;
    // Pick slot in same way that driver does.
    // CONST(31, 2) = reg 31, index 2 = rf([31] * 6 + [2] * 2)
    uint32_t fetch_slot = vtx.const_index * 3 + vtx.const_index_sel;
    uint32_t vb_slot = 95 - fetch_slot;
    element_descs[n].InputSlot            = vb_slot;
    element_descs[n].AlignedByteOffset    = vtx.offset * 4;
    element_descs[n].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
    element_descs[n].InstanceDataStepRate = 0;
  }
  hr = device_->CreateInputLayout(
      element_descs,
      (UINT)element_count,
      byte_code, byte_code_length,
      &input_layout_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader input layout");
    xe_free(byte_code);
    return 1;
  }

  xe_free(byte_code);

  is_prepared_ = true;
  return 0;
}


const char* D3D11VertexShader::Translate(xe_gpu_program_cntl_t* program_cntl) {
  Output* output = new Output();
  xe_gpu_translate_ctx_t ctx;
  ctx.output  = output;
  ctx.type    = type_;

  // Add constants buffers.
  // We could optimize this by only including used buffers, but the compiler
  // seems to do a good job of doing this for us.
  // It also does read detection, so c[512] can end up c[4] in the asm -
  // instead of doing this optimization ourselves we could maybe just query
  // this from the compiler.
  output->append(
    "cbuffer float_consts : register(b0) {\n"
    "  float4 c[512];\n"
    "};\n");
  // TODO(benvanik): add bool/loop constants.

  // Add vertex shader input.
  output->append(
    "struct VS_INPUT {\n");
  int n = 0;
  for (std::vector<instr_fetch_vtx_t>::iterator it = fetch_vtxs_.begin();
       it != fetch_vtxs_.end(); ++it, ++n) {
    const instr_fetch_vtx_t& vtx = *it;
    uint32_t fetch_slot = vtx.const_index * 3 + vtx.const_index_sel;
    output->append(
      "  float4 vf%u_%d : XE_VF%u;\n", fetch_slot, vtx.offset, n);
  }
  output->append(
    "};\n");

  // Add vertex shader output (pixel shader input).
  output->append(
    "struct VS_OUTPUT {\n");
  if (alloc_counts_.positions) {
    XEASSERT(alloc_counts_.positions == 1);
    output->append(
      "  float4 oPos : SV_POSITION;\n");
  }
  if (alloc_counts_.params) {
    output->append(
      "  float4 o[%d] : XE_O;\n",
      alloc_counts_.params);
  }
  output->append(
    "};\n");

  // Vertex shader main() header.
  output->append(
    "VS_OUTPUT main(VS_INPUT i) {\n"
    "  VS_OUTPUT o;\n");

  // TODO(benvanik): remove this, if possible (though the compiler may be smart
  //     enough to do it for us).
  for (uint32_t n = 0; n < alloc_counts_.params; n++) {
    output->append(
      "  o.o[%d] = float4(0.0, 0.0, 0.0, 0.0);\n", n);
  }

  // Add temporaries for any registers we may use.
  for (uint32_t n = 0; n <= program_cntl->vs_regs; n++) {
    output->append(
      "  float4 r%d;\n", n);
  }

  // Execute blocks.
  for (std::vector<instr_cf_exec_t>::iterator it = execs_.begin();
       it != execs_.end(); ++it) {
    instr_cf_exec_t& cf = *it;
    // TODO(benvanik): figure out how sequences/jmps/loops/etc work.
    if (TranslateExec(ctx, cf)) {
      delete output;
      return NULL;
    }
  }

  // main footer.
  output->append(
    "  return o;\n"
    "};\n");

  set_translated_src(output->buffer);
  delete output;
  return translated_src_;
}


D3D11PixelShader::D3D11PixelShader(
    ID3D11Device* device,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    handle_(0),
    D3D11Shader(device, XE_GPU_SHADER_TYPE_PIXEL,
                src_ptr, length, hash) {
}

D3D11PixelShader::~D3D11PixelShader() {
  XESAFERELEASE(handle_);
}

int D3D11PixelShader::Prepare(xe_gpu_program_cntl_t* program_cntl,
                              D3D11VertexShader* input_shader) {
  if (handle_) {
    return 0;
  }

  // TODO(benvanik): look in file based on hash/etc.
  void* byte_code = NULL;
  size_t byte_code_length = 0;

  // Translate and compile source.
  const char* shader_source = Translate(program_cntl, input_shader);
  if (!shader_source) {
    return 1;
  }
  ID3D10Blob* shader_blob = Compile(shader_source);
  if (!shader_blob) {
    return 1;
  }
  byte_code_length = shader_blob->GetBufferSize();
  byte_code = xe_malloc(byte_code_length);
  xe_copy_struct(
      byte_code, shader_blob->GetBufferPointer(), byte_code_length);
  XESAFERELEASE(shader_blob);

  // Create shader.
  HRESULT hr = device_->CreatePixelShader(
      byte_code, byte_code_length,
      NULL,
      &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader");
    xe_free(byte_code);
    return 1;
  }

  xe_free(byte_code);

  is_prepared_ = true;
  return 0;
}

const char* D3D11PixelShader::Translate(
    xe_gpu_program_cntl_t* program_cntl, D3D11VertexShader* input_shader) {
  Output* output = new Output();
  xe_gpu_translate_ctx_t ctx;
  ctx.output  = output;
  ctx.type    = type_;

  // We need an input VS to make decisions here.
  // TODO(benvanik): do we need to pair VS/PS up and store the combination?
  // If the same PS is used with different VS that output different amounts
  // (and less than the number of required registers), things may die.
  XEASSERTNOTNULL(input_shader);
  const Shader::alloc_counts_t& input_alloc_counts =
      input_shader->alloc_counts();

  // Add constants buffers.
  // We could optimize this by only including used buffers, but the compiler
  // seems to do a good job of doing this for us.
  // It also does read detection, so c[512] can end up c[4] in the asm -
  // instead of doing this optimization ourselves we could maybe just query
  // this from the compiler.
  output->append(
    "cbuffer float_consts : register(b0) {\n"
    "  float4 c[512];\n"
    "};\n");
  // TODO(benvanik): add bool/loop constants.

  // Add vertex shader output (pixel shader input).
  output->append(
    "struct VS_OUTPUT {\n");
  if (input_alloc_counts.positions) {
    XEASSERT(input_alloc_counts.positions == 1);
    output->append(
      "  float4 oPos : SV_POSITION;\n");
  }
  if (input_alloc_counts.params) {
    output->append(
      "  float4 o[%d] : XE_O;\n",
      input_alloc_counts.params);
  }
  output->append(
    "};\n");

  // Add pixel shader output.
  output->append(
    "struct PS_OUTPUT {\n");
  for (uint32_t n = 0; n < alloc_counts_.params; n++) {
    output->append(
      "  float4 oC%d   : SV_TARGET%d;\n", n, n);
    if (program_cntl->ps_export_depth) {
      // Is this per render-target?
      output->append(
        "  float oD%d   : SV_DEPTH%d;\n", n, n);
    }
  }
  output->append(
    "};\n");

  // Pixel shader main() header.
  output->append(
    "PS_OUTPUT main(VS_OUTPUT i) {\n"
    "  PS_OUTPUT o;\n");

  // Add temporary registers.
  for (uint32_t n = 0; n <= program_cntl->ps_regs; n++) {
    output->append(
      "  float4 r%d;\n", n);
  }

  // Bring registers local.
  for (uint32_t n = 0; n < input_alloc_counts.params; n++) {
    output->append(
      "  r%d = i.o[%d];\n", n, n);
  }

  // Execute blocks.
  for (std::vector<instr_cf_exec_t>::iterator it = execs_.begin();
       it != execs_.end(); ++it) {
    instr_cf_exec_t& cf = *it;
    // TODO(benvanik): figure out how sequences/jmps/loops/etc work.
    if (TranslateExec(ctx, cf)) {
      delete output;
      return NULL;
    }
  }

  // main footer.
  output->append(
    "  return o;\n"
    "}\n");

  set_translated_src(output->buffer);
  delete output;
  return translated_src_;
}


namespace {

static const char chan_names[] = {
  'x', 'y', 'z', 'w'
};

void AppendSrcReg(
    xe_gpu_translate_ctx_t& ctx,
    uint32_t num, uint32_t type,
    uint32_t swiz, uint32_t negate, uint32_t abs) {
  if (negate) {
    ctx.output->append("-");
  }
  if (abs) {
    ctx.output->append("abs(");
  }
  if (type) {
    // Register.
    ctx.output->append("r%u", num);
  } else {
    // Constant.
    ctx.output->append("c[%u]", num);
  }
  if (swiz) {
    ctx.output->append(".");
    for (int i = 0; i < 4; i++) {
      ctx.output->append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
  if (abs) {
    ctx.output->append(")");
  }
}

void AppendDestReg(
    xe_gpu_translate_ctx_t& ctx,
    uint32_t num, uint32_t mask, uint32_t dst_exp) {
  if (!dst_exp) {
    // Register.
    ctx.output->append("r%u", num);
  } else {
    // Export.
    switch (ctx.type) {
    case XE_GPU_SHADER_TYPE_VERTEX:
      switch (num) {
      case 62:
        ctx.output->append("o.oPos");
        break;
      case 63:
        ctx.output->append("o.point_size");
        break;
      default:
        // Varying.
        ctx.output->append("o.o[%u]", num);;
        break;
      }
      break;
    case XE_GPU_SHADER_TYPE_PIXEL:
      switch (num) {
      case 0:
        ctx.output->append("o.oC0");
        break;
      default:
        // TODO(benvanik): other render targets?
        // TODO(benvanik): depth?
        XEASSERTALWAYS();
        break;
      }
      break;
    }
  }
  if (mask != 0xf) {
    ctx.output->append(".");
    for (int i = 0; i < 4; i++) {
      ctx.output->append("%c", (mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void print_srcreg(
    Output* output,
    uint32_t num, uint32_t type,
    uint32_t swiz, uint32_t negate, uint32_t abs) {
  if (negate) {
    output->append("-");
  }
  if (abs) {
    output->append("|");
  }
  output->append("%c%u", type ? 'R' : 'C', num);
  if (swiz) {
    output->append(".");
    for (int i = 0; i < 4; i++) {
      output->append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
  if (abs) {
    output->append("|");
  }
}

void print_dstreg(
    Output* output, uint32_t num, uint32_t mask, uint32_t dst_exp) {
  output->append("%s%u", dst_exp ? "export" : "R", num);
  if (mask != 0xf) {
    output->append(".");
    for (int i = 0; i < 4; i++) {
      output->append("%c", (mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void print_export_comment(
    Output* output, uint32_t num, XE_GPU_SHADER_TYPE type) {
  const char *name = NULL;
  switch (type) {
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
    output->append("\t; %s", name);
  }
}

int TranslateALU_ADDv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(" + ");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

int TranslateALU_MULv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(" * ");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

int TranslateALU_MAXv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  if (alu.src1_reg == alu.src2_reg &&
      alu.src1_sel == alu.src2_sel &&
      alu.src1_swiz == alu.src2_swiz &&
      alu.src1_reg_negate == alu.src2_reg_negate &&
      alu.src1_reg_abs == alu.src2_reg_abs) {
    // This is a mov.
    AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  } else {
    ctx.output->append("max(");
    AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
    ctx.output->append(", ");
    AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
    ctx.output->append(")");
  }
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

int TranslateALU_MINv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("min(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(", ");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

int TranslateALU_FRACv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("frac(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

int TranslateALU_TRUNCv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("trunc(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

int TranslateALU_FLOORv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("floor(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

// ...

int TranslateALU_MULADDv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("mad(");
  // TODO(benvanik): verify correct - may be 1,2,3 for (1*2+3)
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(", ");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(", ");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  return 0;
}

typedef int (*xe_gpu_translate_alu_fn)(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu);
typedef struct {
  uint32_t    num_srcs;
  const char* name;
  xe_gpu_translate_alu_fn   fn;
} xe_gpu_translate_alu_info_t;
#define ALU_INSTR(opc, num_srcs) \
    { num_srcs, #opc, 0 }
#define ALU_INSTR_IMPL(opc, num_srcs) \
    { num_srcs, #opc, TranslateALU_##opc }
static xe_gpu_translate_alu_info_t vector_alu_instrs[0x20] = {
  ALU_INSTR_IMPL(ADDv,               2),  // 0
  ALU_INSTR_IMPL(MULv,               2),  // 1
  ALU_INSTR_IMPL(MAXv,               2),  // 2
  ALU_INSTR_IMPL(MINv,               2),  // 3
  ALU_INSTR(SETEv,              2),  // 4
  ALU_INSTR(SETGTv,             2),  // 5
  ALU_INSTR(SETGTEv,            2),  // 6
  ALU_INSTR(SETNEv,             2),  // 7
  ALU_INSTR_IMPL(FRACv,              1),  // 8
  ALU_INSTR_IMPL(TRUNCv,             1),  // 9
  ALU_INSTR_IMPL(FLOORv,             1),  // 10
  ALU_INSTR_IMPL(MULADDv,            3),  // 11
  ALU_INSTR(CNDEv,              3),  // 12
  ALU_INSTR(CNDGTEv,            3),  // 13
  ALU_INSTR(CNDGTv,             3),  // 14
  ALU_INSTR(DOT4v,              2),  // 15
  ALU_INSTR(DOT3v,              2),  // 16
  ALU_INSTR(DOT2ADDv,           3),  // 17 -- ???
  ALU_INSTR(CUBEv,              2),  // 18
  ALU_INSTR(MAX4v,              1),  // 19
  ALU_INSTR(PRED_SETE_PUSHv,    2),  // 20
  ALU_INSTR(PRED_SETNE_PUSHv,   2),  // 21
  ALU_INSTR(PRED_SETGT_PUSHv,   2),  // 22
  ALU_INSTR(PRED_SETGTE_PUSHv,  2),  // 23
  ALU_INSTR(KILLEv,             2),  // 24
  ALU_INSTR(KILLGTv,            2),  // 25
  ALU_INSTR(KILLGTEv,           2),  // 26
  ALU_INSTR(KILLNEv,            2),  // 27
  ALU_INSTR(DSTv,               2),  // 28
  ALU_INSTR(MOVAv,              1),  // 29
};
static xe_gpu_translate_alu_info_t scalar_alu_instrs[0x40] = {
  ALU_INSTR(ADDs,               1),  // 0
  ALU_INSTR(ADD_PREVs,          1),  // 1
  ALU_INSTR(MULs,               1),  // 2
  ALU_INSTR(MUL_PREVs,          1),  // 3
  ALU_INSTR(MUL_PREV2s,         1),  // 4
  ALU_INSTR(MAXs,               1),  // 5
  ALU_INSTR(MINs,               1),  // 6
  ALU_INSTR(SETEs,              1),  // 7
  ALU_INSTR(SETGTs,             1),  // 8
  ALU_INSTR(SETGTEs,            1),  // 9
  ALU_INSTR(SETNEs,             1),  // 10
  ALU_INSTR(FRACs,              1),  // 11
  ALU_INSTR(TRUNCs,             1),  // 12
  ALU_INSTR(FLOORs,             1),  // 13
  ALU_INSTR(EXP_IEEE,           1),  // 14
  ALU_INSTR(LOG_CLAMP,          1),  // 15
  ALU_INSTR(LOG_IEEE,           1),  // 16
  ALU_INSTR(RECIP_CLAMP,        1),  // 17
  ALU_INSTR(RECIP_FF,           1),  // 18
  ALU_INSTR(RECIP_IEEE,         1),  // 19
  ALU_INSTR(RECIPSQ_CLAMP,      1),  // 20
  ALU_INSTR(RECIPSQ_FF,         1),  // 21
  ALU_INSTR(RECIPSQ_IEEE,       1),  // 22
  ALU_INSTR(MOVAs,              1),  // 23
  ALU_INSTR(MOVA_FLOORs,        1),  // 24
  ALU_INSTR(SUBs,               1),  // 25
  ALU_INSTR(SUB_PREVs,          1),  // 26
  ALU_INSTR(PRED_SETEs,         1),  // 27
  ALU_INSTR(PRED_SETNEs,        1),  // 28
  ALU_INSTR(PRED_SETGTs,        1),  // 29
  ALU_INSTR(PRED_SETGTEs,       1),  // 30
  ALU_INSTR(PRED_SET_INVs,      1),  // 31
  ALU_INSTR(PRED_SET_POPs,      1),  // 32
  ALU_INSTR(PRED_SET_CLRs,      1),  // 33
  ALU_INSTR(PRED_SET_RESTOREs,  1),  // 34
  ALU_INSTR(KILLEs,             1),  // 35
  ALU_INSTR(KILLGTs,            1),  // 36
  ALU_INSTR(KILLGTEs,           1),  // 37
  ALU_INSTR(KILLNEs,            1),  // 38
  ALU_INSTR(KILLONEs,           1),  // 39
  ALU_INSTR(SQRT_IEEE,          1),  // 40
  { 0, 0, false },
  ALU_INSTR(MUL_CONST_0,        1),  // 42
  ALU_INSTR(MUL_CONST_1,        1),  // 43
  ALU_INSTR(ADD_CONST_0,        1),  // 44
  ALU_INSTR(ADD_CONST_1,        1),  // 45
  ALU_INSTR(SUB_CONST_0,        1),  // 46
  ALU_INSTR(SUB_CONST_1,        1),  // 47
  ALU_INSTR(SIN,                1),  // 48
  ALU_INSTR(COS,                1),  // 49
  ALU_INSTR(RETAIN_PREV,        1),  // 50
};
#undef ALU_INSTR

int TranslateALU(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t* alu, int sync) {
  Output* output = ctx.output;

  if (!alu->scalar_write_mask && !alu->vector_write_mask) {
    output->append("  //   <nop>\n");
    return 0;
  }

  if (alu->vector_write_mask) {
    // Disassemble vector op.
    xe_gpu_translate_alu_info_t& iv = vector_alu_instrs[alu->vector_opc];
    output->append("  //   %sALU:\t", sync ? "(S)" : "   ");
    output->append("%s", iv.name);
    if (alu->pred_select & 0x2) {
      // seems to work similar to conditional execution in ARM instruction
      // set, so let's use a similar syntax for now:
      output->append((alu->pred_select & 0x1) ? "EQ" : "NE");
    }
    output->append("\t");
    print_dstreg(output,
                  alu->vector_dest, alu->vector_write_mask, alu->export_data);
    output->append(" = ");
    if (iv.num_srcs == 3) {
      print_srcreg(output,
                    alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                    alu->src3_reg_negate, alu->src3_reg_abs);
      output->append(", ");
    }
    print_srcreg(output,
                  alu->src1_reg, alu->src1_sel, alu->src1_swiz,
                  alu->src1_reg_negate, alu->src1_reg_abs);
    if (iv.num_srcs > 1) {
      output->append(", ");
      print_srcreg(output,
                    alu->src2_reg, alu->src2_sel, alu->src2_swiz,
                    alu->src2_reg_negate, alu->src2_reg_abs);
    }
    if (alu->vector_clamp) {
      output->append(" CLAMP");
    }
    if (alu->export_data) {
      print_export_comment(output, alu->vector_dest, ctx.type);
    }
    output->append("\n");

    // Translate vector op.
    if (iv.fn) {
      output->append("  ");
      if (iv.fn(ctx, *alu)) {
        return 1;
      }
    } else {
      output->append("  // <UNIMPLEMENTED>\n");
    }
  }

  if (alu->scalar_write_mask || !alu->vector_write_mask) {
    // 2nd optional scalar op:

    // Disassemble scalar op.
    xe_gpu_translate_alu_info_t& is = scalar_alu_instrs[alu->scalar_opc];
    output->append("  //  ");
    output->append("                          \t");
    if (is.name) {
      output->append("\t    \t%s\t", is.name);
    } else {
      output->append("\t    \tOP(%u)\t", alu->scalar_opc);
    }
    print_dstreg(output,
                 alu->scalar_dest, alu->scalar_write_mask, alu->export_data);
    output->append(" = ");
    print_srcreg(output,
                 alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                 alu->src3_reg_negate, alu->src3_reg_abs);
    // TODO ADD/MUL must have another src?!?
    if (alu->scalar_clamp) {
      output->append(" CLAMP");
    }
    if (alu->export_data) {
      print_export_comment(output, alu->scalar_dest, ctx.type);
    }
    output->append("\n");

    // Translate scalar op.
    if (is.fn) {
      output->append("  ");
      if (is.fn(ctx, *alu)) {
        return 1;
      }
    } else {
      output->append("  // <UNIMPLEMENTED>\n");
    }
  }

  return 0;
}

struct {
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
    {0},
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

void print_fetch_dst(Output* output, uint32_t dst_reg, uint32_t dst_swiz) {
  output->append("\tR%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    output->append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

void AppendFetchDest(Output* output, uint32_t dst_reg, uint32_t dst_swiz) {
  output->append("r%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    output->append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

int TranslateVertexFetch(
    xe_gpu_translate_ctx_t& ctx, const instr_fetch_vtx_t* vtx, int sync) {
  Output* output = ctx.output;

  // Disassemble.
  output->append("  //   %sFETCH:\t", sync ? "(S)" : "   ");
  if (vtx->pred_select) {
    output->append(vtx->pred_condition ? "EQ" : "NE");
  }
  print_fetch_dst(output, vtx->dst_reg, vtx->dst_swiz);
  output->append(" = R%u.", vtx->src_reg);
  output->append("%c", chan_names[vtx->src_swiz & 0x3]);
  if (fetch_types[vtx->format].name) {
    output->append(" %s", fetch_types[vtx->format].name);
  } else  {
    output->append(" TYPE(0x%x)", vtx->format);
  }
  output->append(" %s", vtx->format_comp_all ? "SIGNED" : "UNSIGNED");
  if (!vtx->num_format_all) {
    output->append(" NORMALIZED");
  }
  output->append(" STRIDE(%u)", vtx->stride);
  if (vtx->offset) {
    output->append(" OFFSET(%u)", vtx->offset);
  }
  output->append(" CONST(%u, %u)", vtx->const_index, vtx->const_index_sel);
  if (1) {
    // XXX
    output->append(" src_reg_am=%u", vtx->src_reg_am);
    output->append(" dst_reg_am=%u", vtx->dst_reg_am);
    output->append(" num_format_all=%u", vtx->num_format_all);
    output->append(" signed_rf_mode_all=%u", vtx->signed_rf_mode_all);
    output->append(" exp_adjust_all=%u", vtx->exp_adjust_all);
  }
  output->append("\n");

  // Translate.
  output->append("  ");
  output->append("r%u.xyzw", vtx->dst_reg);
  output->append(" = ");
  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  output->append("i.vf%u_%d.", fetch_slot, vtx->offset);
  // Pass one over dest does xyzw and fakes the special values.
  // TODO(benvanik): detect and set as rN = float4(samp.xyz, 1.0); / etc
  uint32_t dst_swiz = vtx->dst_swiz;
  for (int i = 0; i < 4; i++) {
    output->append("%c", chan_names[dst_swiz & 0x3]);
    dst_swiz >>= 3;
  }
  output->append(";\n");
  // Do another pass to set constant values.
  dst_swiz = vtx->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      output->append("  r%u.%c = 0.0;\n", vtx->dst_reg, chan_names[i]);
    } else if ((dst_swiz & 0x7) == 5) {
      output->append("  r%u.%c = 1.0;\n", vtx->dst_reg, chan_names[i]);
    }
    dst_swiz >>= 3;
  }
  return 0;
}

int TranslateTextureFetch(
  xe_gpu_translate_ctx_t& ctx, const instr_fetch_tex_t* tex, int sync) {
  Output* output = ctx.output;

  return 1;
}

struct {
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

}  // anonymous namespace


int D3D11Shader::TranslateExec(xe_gpu_translate_ctx_t& ctx, const instr_cf_exec_t& cf) {
  Output* output = ctx.output;

  output->append(
    "  // %s ADDR(0x%x) CNT(0x%x)",
    cf_instructions[cf.opc].name, cf.address, cf.count);
  if (cf.yeild) {
    output->append(" YIELD");
  }
  uint8_t vc = cf.vc_hi | (cf.vc_lo << 2);
  if (vc) {
    output->append(" VC(0x%x)", vc);
  }
  if (cf.bool_addr) {
    output->append(" BOOL_ADDR(0x%x)", cf.bool_addr);
  }
  if (cf.address_mode == ABSOLUTE_ADDR) {
    output->append(" ABSOLUTE_ADDR");
  }
  if (cf.is_cond_exec()) {
    output->append(" COND(%d)", cf.condition);
  }
  output->append("\n");

  uint32_t sequence = cf.serialize;
  for (uint32_t i = 0; i < cf.count; i++) {
    uint32_t alu_off = (cf.address + i);
    int sync = sequence & 0x2;
    if (sequence & 0x1) {
      const instr_fetch_t* fetch =
          (const instr_fetch_t*)(dwords_ + alu_off * 3);
      switch (fetch->opc) {
      case VTX_FETCH:
        if (TranslateVertexFetch(ctx, &fetch->vtx, sync)) {
          return 1;
        }
        break;
      case TEX_FETCH:
        if (TranslateTextureFetch(ctx, &fetch->tex, sync)) {
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
        XEASSERTALWAYS();
        break;
      }
    } else {
      const instr_alu_t* alu =
          (const instr_alu_t*)(dwords_ + alu_off * 3);
      if (TranslateALU(ctx, alu, sync)) {
        return 1;
      }
    }
    sequence >>= 2;
  }

  return 0;
}
