/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_shader.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_geometry_shader.h>
#include <xenia/gpu/xenos/ucode.h>

#include <d3dcompiler.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


namespace {

const int OUTPUT_CAPACITY = 64 * 1024;

int GetFormatComponentCount(uint32_t format) {
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
    XEASSERTALWAYS();
    return 4;
  }
}

const char* GetFormatTypeName(
    uint32_t format, uint32_t format_comp_all, uint32_t num_format_all) {
  switch (format) {
  case FMT_32:
    return format_comp_all ? "int" : "uint";
  case FMT_32_FLOAT:
    return "float";
  case FMT_16_16:
  case FMT_32_32:
    if (!num_format_all) {
      return format_comp_all ? "snorm float2" : "unorm float2";
    } else {
      return format_comp_all ? "int2" : "uint2";
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
    if (!num_format_all) {
      return format_comp_all ? "snorm float4" : "unorm float4";
    } else {
      return format_comp_all ? "int4" : "uint4";
    }
  case FMT_16_16_16_16_FLOAT:
  case FMT_32_32_32_32_FLOAT:
    return "float4";
  default:
    XELOGE("Unknown vertex format: %d", format);
    XEASSERTALWAYS();
    return "float4";
  }
}

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
  const char* base_path = "";
  if (FLAGS_dump_shaders.size()) {
    base_path = FLAGS_dump_shaders.c_str();
  }
  char file_name[XE_MAX_PATH];
  xesnprintfa(file_name, XECOUNT(file_name),
      "%s/gen_%.16llX.%s",
      base_path,
      hash_,
      type_ == XE_GPU_SHADER_TYPE_VERTEX ? "vs" : "ps");

  if (FLAGS_dump_shaders.size()) {
    FILE* f = fopen(file_name, "w");
    fprintf(f, shader_source);
    fprintf(f, "\n\n");
    fprintf(f, "/*\n");
    fprintf(f, disasm_src_);
    fprintf(f, " */\n");
    fclose(f);
  }

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

void D3D11Shader::AppendTextureHeader(Output* output) {
  bool fetch_setup[32] = { false };

  // 1 texture per constant slot, 1 sampler per fetch.
  for (uint32_t  n = 0; n < tex_buffer_inputs_.count; n++) {
    auto& input = tex_buffer_inputs_.descs[n];
    auto& fetch = input.tex_fetch;

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
      output->append("%s x_texture_%d;\n", texture_type, fetch.const_idx);
    }

    // Add sampler.
    output->append("SamplerState x_sampler_%d;\n", n);
  }
}


D3D11VertexShader::D3D11VertexShader(
    ID3D11Device* device,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    handle_(0), input_layout_(0),
    D3D11Shader(device, XE_GPU_SHADER_TYPE_VERTEX,
                src_ptr, length, hash) {
  xe_zero_struct(geometry_shaders_, sizeof(geometry_shaders_));
}

D3D11VertexShader::~D3D11VertexShader() {
  for (size_t n = 0; n < XECOUNT(geometry_shaders_); n++) {
    delete geometry_shaders_[n];
  }
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
  size_t element_count = 0;
  for (uint32_t n = 0; n < vtx_buffer_inputs_.count; n++) {
    element_count += vtx_buffer_inputs_.descs[n].element_count;
  }
  if (!element_count) {
    XELOGW("D3D11: vertex shader with zero inputs -- retaining previous values?");
    input_layout_ = NULL;
    return 0;
  }

  D3D11_INPUT_ELEMENT_DESC* element_descs =
      (D3D11_INPUT_ELEMENT_DESC*)xe_alloca(
          sizeof(D3D11_INPUT_ELEMENT_DESC) * element_count);
  uint32_t el_index = 0;
  for (uint32_t n = 0; n < vtx_buffer_inputs_.count; n++) {
    auto& input = vtx_buffer_inputs_.descs[n];
    for (uint32_t m = 0; m < input.element_count; m++) {
      auto& el = input.elements[m];
      uint32_t vb_slot = input.input_index;
      uint32_t num_format_all = el.vtx_fetch.num_format_all;
      uint32_t format_comp_all = el.vtx_fetch.format_comp_all;
      DXGI_FORMAT vtx_format;
      switch (el.format) {
      case FMT_8_8_8_8:
        if (!num_format_all) {
          vtx_format = format_comp_all ?
              DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        } else {
          vtx_format = format_comp_all ?
              DXGI_FORMAT_R8G8B8A8_SINT : DXGI_FORMAT_R8G8B8A8_UINT;
        }
        break;
      case FMT_2_10_10_10:
        if (!num_format_all) {
          vtx_format = DXGI_FORMAT_R10G10B10A2_UNORM;
        } else {
          vtx_format = DXGI_FORMAT_R10G10B10A2_UINT;
        }
        break;
      // DXGI_FORMAT_R11G11B10_FLOAT?
      case FMT_16_16:
        if (!num_format_all) {
          vtx_format = format_comp_all ?
              DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM;
        } else {
          vtx_format = format_comp_all ?
              DXGI_FORMAT_R16G16_SINT : DXGI_FORMAT_R16G16_UINT;
        }
        break;
      case FMT_16_16_16_16:
        if (!num_format_all) {
          vtx_format = format_comp_all ?
              DXGI_FORMAT_R16G16B16A16_SNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
        } else {
          vtx_format = format_comp_all ?
              DXGI_FORMAT_R16G16B16A16_SINT : DXGI_FORMAT_R16G16B16A16_UINT;
        }
        break;
      case FMT_16_16_FLOAT:
        vtx_format = DXGI_FORMAT_R16G16_FLOAT;
        break;
      case FMT_16_16_16_16_FLOAT:
        vtx_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
      case FMT_32:
        vtx_format = format_comp_all ?
            DXGI_FORMAT_R32_SINT : DXGI_FORMAT_R32_UINT;
        break;
      case FMT_32_32:
        vtx_format = format_comp_all ?
            DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32G32_UINT;
        break;
      case FMT_32_32_32_32:
        vtx_format = format_comp_all ?
            DXGI_FORMAT_R32G32B32A32_SINT : DXGI_FORMAT_R32G32B32A32_UINT;
        break;
      case FMT_32_FLOAT:
        vtx_format = DXGI_FORMAT_R32_FLOAT;
        break;
      case FMT_32_32_FLOAT:
        vtx_format = DXGI_FORMAT_R32G32_FLOAT;
        break;
      case FMT_32_32_32_FLOAT:
        vtx_format = DXGI_FORMAT_R32G32B32_FLOAT;
        break;
      case FMT_32_32_32_32_FLOAT:
        vtx_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        break;
      default:
        XEASSERTALWAYS();
        break;
      }
      element_descs[el_index].SemanticName         = "XE_VF";
      element_descs[el_index].SemanticIndex        = el_index;
      element_descs[el_index].Format               = vtx_format;
      element_descs[el_index].InputSlot            = vb_slot;
      element_descs[el_index].AlignedByteOffset    = el.offset_words * 4;
      element_descs[el_index].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
      element_descs[el_index].InstanceDataStepRate = 0;
      el_index++;
    }
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
  ctx.tex_fetch_index = 0;

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

  AppendTextureHeader(output);

  // Transform utilities. We adjust the output position in various ways
  // as we can't do this via D3D11 APIs.
  output->append(
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
  output->append(
    "struct VS_INPUT {\n");
  uint32_t el_index = 0;
  for (uint32_t n = 0; n < vtx_buffer_inputs_.count; n++) {
    auto& input = vtx_buffer_inputs_.descs[n];
    for (uint32_t m = 0; m < input.element_count; m++) {
      auto& el = input.elements[m];
      auto& vtx = el.vtx_fetch;
      const char* type_name = GetFormatTypeName(
          el.format, el.vtx_fetch.format_comp_all, el.vtx_fetch.num_format_all);
      uint32_t fetch_slot = vtx.const_index * 3 + vtx.const_index_sel;
      output->append(
        "  %s vf%u_%d : XE_VF%u;\n",
        type_name, fetch_slot, vtx.offset, el_index);
      el_index++;
    }
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
      MAX_INTERPOLATORS);
  }
  if (alloc_counts_.point_size) {
    output->append(
      "  float4 oPointSize : PSIZE;\n");
  }
  output->append(
    "};\n");

  // Vertex shader main() header.
  output->append(
    "VS_OUTPUT main(VS_INPUT i) {\n"
    "  VS_OUTPUT o;\n");

  // Always write position, as some shaders seem to only write certain values.
  output->append(
    "  o.oPos = float4(0.0, 0.0, 0.0, 0.0);\n");
  if (alloc_counts_.point_size) {
    output->append(
      "  o.oPointSize = float4(1.0, 0.0, 0.0, 0.0);\n");
  }

  // TODO(benvanik): remove this, if possible (though the compiler may be smart
  //     enough to do it for us).
  if (alloc_counts_.params) {
    for (uint32_t n = 0; n < MAX_INTERPOLATORS; n++) {
      output->append(
        "  o.o[%d] = float4(0.0, 0.0, 0.0, 0.0);\n", n);
    }
  }

  // Add temporaries for any registers we may use.
  uint32_t temp_regs = program_cntl->vs_regs + program_cntl->ps_regs;
  for (uint32_t n = 0; n <= temp_regs; n++) {
    output->append(
      "  float4 r%d = c[%d];\n", n, n);
  }
  output->append("  float4 t;\n");

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
    "  o.oPos = applyViewport(o.oPos);\n"
    "  return o;\n"
    "};\n");

  set_translated_src(output->buffer);
  delete output;
  return translated_src_;
}

int D3D11VertexShader::DemandGeometryShader(GeometryShaderType type,
                                            D3D11GeometryShader** out_shader) {
  if (geometry_shaders_[type]) {
    *out_shader = geometry_shaders_[type];
    return 0;
  }

  // Demand generate.
  D3D11GeometryShader* shader = NULL;
  switch (type) {
  case POINT_SPRITE_SHADER:
    shader = new D3D11PointSpriteGeometryShader(device_, hash_);
    break;
  case RECT_LIST_SHADER:
    shader = new D3D11RectListGeometryShader(device_, hash_);
    break;
  case QUAD_LIST_SHADER:
    shader = new D3D11QuadListGeometryShader(device_, hash_);
    break;
  default:
    XEASSERTALWAYS();
    return 1;
  }
  if (!shader) {
    return 1;
  }

  if (shader->Prepare(this)) {
    delete shader;
    return 1;
  }

  geometry_shaders_[type] = shader;
  *out_shader = geometry_shaders_[type];
  return 0;
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
    XELOGE("D3D11: failed to create pixel shader");
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
  ctx.tex_fetch_index = 0;

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

  AppendTextureHeader(output);

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
      MAX_INTERPOLATORS);
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
  uint32_t temp_regs = program_cntl->vs_regs + program_cntl->ps_regs;
  for (uint32_t n = 0; n <= MAX(15, temp_regs); n++) {
    output->append(
      "  float4 r%d = c[%d];\n", n, n);
  }
  output->append("  float4 t;\n");

  // Bring registers local.
  if (input_alloc_counts.params) {
    for (uint32_t n = 0; n < MAX_INTERPOLATORS; n++) {
      output->append(
        "  r%d = i.o[%d];\n", n, n);
    }
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
  'x', 'y', 'z', 'w',
  // these only apply to FETCH dst's, and we shouldn't be using them:
  '0', '1', '?', '_',
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

void AppendDestRegName(
    xe_gpu_translate_ctx_t& ctx,
    uint32_t num, uint32_t dst_exp) {
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
        ctx.output->append("o.oPointSize");
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
}

void AppendDestReg(
    xe_gpu_translate_ctx_t& ctx,
    uint32_t num, uint32_t mask, uint32_t dst_exp) {
  if (mask != 0xF) {
    // If masking, store to a temporary variable and clean it up later.
    ctx.output->append("t");
  } else {
    // Store directly to output.
    AppendDestRegName(ctx, num, dst_exp);
  }
}

void AppendDestRegPost(
    xe_gpu_translate_ctx_t& ctx,
    uint32_t num, uint32_t mask, uint32_t dst_exp) {
  if (mask != 0xF) {
    // Masking.
    ctx.output->append("  ");
    AppendDestRegName(ctx, num, dst_exp);
    ctx.output->append(" = float4(");
    for (int i = 0; i < 4; i++) {
      // TODO(benvanik): mask out values? mix in old value as temp?
      // ctx.output->append("%c", (mask & 0x1) ? chan_names[i] : 'w');
      if (!(mask & 0x1)) {
        AppendDestRegName(ctx, num, dst_exp);
      } else {
        ctx.output->append("t");
      }
      ctx.output->append(".%c", chan_names[i]);
      mask >>= 1;
      if (i < 3) {
        ctx.output->append(", ");
      }
    }
    ctx.output->append(");\n");
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_SETXXv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu, const char* op) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("float4((");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").x %s (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").x ? 1.0 : 0.0, (");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").y %s (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").y ? 1.0 : 0.0, (");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").z %s (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").z ? 1.0 : 0.0, (");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").w %s (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").w ? 1.0 : 0.0)");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}
int TranslateALU_SETEv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXv(ctx, alu, "==");
}
int TranslateALU_SETGTv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXv(ctx, alu, ">");
}
int TranslateALU_SETGTEv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXv(ctx, alu, ">=");
}
int TranslateALU_SETNEv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXv(ctx, alu, "!=");
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_MULADDv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("mad(");
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
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_CNDXXv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu, const char* op) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  // TODO(benvanik): check argument order - could be 3 as compare and 1 and 2 as values.
  ctx.output->append("float4((");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").x %s 0.0 ? (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").x : (");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(").x, (");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").y %s 0.0 ? (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").y : (");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(").y, (");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").z %s 0.0 ? (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").z : (");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(").z, (");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").w %s 0.0 ? (", op);
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").w : (");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(").w)");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}
int TranslateALU_CNDEv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(ctx, alu, "==");
}
int TranslateALU_CNDGTEv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(ctx, alu, ">=");
}
int TranslateALU_CNDGTv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_CNDXXv(ctx, alu, ">");
}

int TranslateALU_DOT4v(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("dot(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(", ");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(")");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_DOT3v(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("dot(float4(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").xyz, float4(");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").xyz)");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_DOT2ADDv(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("dot(float4(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(").xy, float4(");
  AppendSrcReg(ctx, alu.src2_reg, alu.src2_sel, alu.src2_swiz, alu.src2_reg_negate, alu.src2_reg_abs);
  ctx.output->append(").xy) + ");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".x");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

// CUBEv

int TranslateALU_MAX4v(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.vector_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("max(");
  ctx.output->append("max(");
  ctx.output->append("max(");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(".x, ");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(".y), ");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(".z), ");
  AppendSrcReg(ctx, alu.src1_reg, alu.src1_sel, alu.src1_swiz, alu.src1_reg_negate, alu.src1_reg_abs);
  ctx.output->append(".w)");
  if (alu.vector_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.vector_dest, alu.vector_write_mask, alu.export_data);
  return 0;
}

// ...

int TranslateALU_MAXs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  if ((alu.src3_swiz & 0x3) == (((alu.src3_swiz >> 2) + 1) & 0x3)) {
    // This is a mov.
    AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  } else {
    ctx.output->append("max(");
    AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
    ctx.output->append(".x, ");
    AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
    ctx.output->append(".y).xxxx");
  }
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_MINs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("min(");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".x, ");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".y).xxxx");
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_SETXXs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu, const char* op) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("((");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".x %s 0.0) ? 1.0 : 0.0).xxxx", op);
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int TranslateALU_SETEs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXs(ctx, alu, "==");
}
int TranslateALU_SETGTs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXs(ctx, alu, ">");
}
int TranslateALU_SETGTEs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXs(ctx, alu, ">=");
}
int TranslateALU_SETNEs(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SETXXs(ctx, alu, "!=");
}

int TranslateALU_RECIP_IEEE(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  ctx.output->append("(1.0 / ");
  AppendSrcReg(ctx, alu.src3_reg, alu.src3_sel, alu.src3_swiz, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(")");
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}

int TranslateALU_MUL_CONST_0(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 = (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  ctx.output->append("(");
  AppendSrcReg(ctx, alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".%c * ", chan_names[swiz_a]);
  AppendSrcReg(ctx, reg2, 1, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".%c", chan_names[swiz_b]);
  ctx.output->append(").xxxx");
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int TranslateALU_MUL_CONST_1(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_MUL_CONST_0(ctx, alu);
}

int TranslateALU_ADD_CONST_0(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 = (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  ctx.output->append("(");
  AppendSrcReg(ctx, alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".%c + ", chan_names[swiz_a]);
  AppendSrcReg(ctx, reg2, 1, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".%c", chan_names[swiz_b]);
  ctx.output->append(").xxxx");
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int TranslateALU_ADD_CONST_1(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_ADD_CONST_0(ctx, alu);
}

int TranslateALU_SUB_CONST_0(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  AppendDestReg(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  ctx.output->append(" = ");
  if (alu.scalar_clamp) {
    ctx.output->append("saturate(");
  }
  uint32_t src3_swiz = alu.src3_swiz & ~0x3C;
  uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
  uint32_t swiz_b = (src3_swiz & 0x3);
  uint32_t reg2 = (alu.scalar_opc & 1) | (alu.src3_swiz & 0x3C) | (alu.src3_sel << 1);
  ctx.output->append("(");
  AppendSrcReg(ctx, alu.src3_reg, 0, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".%c - ", chan_names[swiz_a]);
  AppendSrcReg(ctx, reg2, 1, 0, alu.src3_reg_negate, alu.src3_reg_abs);
  ctx.output->append(".%c", chan_names[swiz_b]);
  ctx.output->append(").xxxx");
  if (alu.scalar_clamp) {
    ctx.output->append(")");
  }
  ctx.output->append(";\n");
  AppendDestRegPost(ctx, alu.scalar_dest, alu.scalar_write_mask, alu.export_data);
  return 0;
}
int TranslateALU_SUB_CONST_1(
    xe_gpu_translate_ctx_t& ctx, const instr_alu_t& alu) {
  return TranslateALU_SUB_CONST_0(ctx, alu);
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
  ALU_INSTR_IMPL(SETEv,              2),  // 4
  ALU_INSTR_IMPL(SETGTv,             2),  // 5
  ALU_INSTR_IMPL(SETGTEv,            2),  // 6
  ALU_INSTR_IMPL(SETNEv,             2),  // 7
  ALU_INSTR_IMPL(FRACv,              1),  // 8
  ALU_INSTR_IMPL(TRUNCv,             1),  // 9
  ALU_INSTR_IMPL(FLOORv,             1),  // 10
  ALU_INSTR_IMPL(MULADDv,            3),  // 11
  ALU_INSTR_IMPL(CNDEv,              3),  // 12
  ALU_INSTR_IMPL(CNDGTEv,            3),  // 13
  ALU_INSTR_IMPL(CNDGTv,             3),  // 14
  ALU_INSTR_IMPL(DOT4v,              2),  // 15
  ALU_INSTR_IMPL(DOT3v,              2),  // 16
  ALU_INSTR_IMPL(DOT2ADDv,           3),  // 17 -- ???
  ALU_INSTR(CUBEv,              2),  // 18
  ALU_INSTR_IMPL(MAX4v,              1),  // 19
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
  ALU_INSTR_IMPL(MAXs,               1),  // 5
  ALU_INSTR_IMPL(MINs,               1),  // 6
  ALU_INSTR_IMPL(SETEs,              1),  // 7
  ALU_INSTR_IMPL(SETGTs,             1),  // 8
  ALU_INSTR_IMPL(SETGTEs,            1),  // 9
  ALU_INSTR_IMPL(SETNEs,             1),  // 10
  ALU_INSTR(FRACs,              1),  // 11
  ALU_INSTR(TRUNCs,             1),  // 12
  ALU_INSTR(FLOORs,             1),  // 13
  ALU_INSTR(EXP_IEEE,           1),  // 14
  ALU_INSTR(LOG_CLAMP,          1),  // 15
  ALU_INSTR(LOG_IEEE,           1),  // 16
  ALU_INSTR(RECIP_CLAMP,        1),  // 17
  ALU_INSTR(RECIP_FF,           1),  // 18
  ALU_INSTR_IMPL(RECIP_IEEE,         1),  // 19
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
  ALU_INSTR_IMPL(MUL_CONST_0,        2),  // 42
  ALU_INSTR_IMPL(MUL_CONST_1,        2),  // 43
  ALU_INSTR_IMPL(ADD_CONST_0,        2),  // 44
  ALU_INSTR_IMPL(ADD_CONST_1,        2),  // 45
  ALU_INSTR_IMPL(SUB_CONST_0,        2),  // 46
  ALU_INSTR_IMPL(SUB_CONST_1,        2),  // 47
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
    output->append("\t");
    if (is.name) {
      output->append("\t    \t%s\t", is.name);
    } else {
      output->append("\t    \tOP(%u)\t", alu->scalar_opc);
    }
    print_dstreg(output,
                 alu->scalar_dest, alu->scalar_write_mask, alu->export_data);
    output->append(" = ");
    if (is.num_srcs == 2) {
      // ADD_CONST_0 dest, [const], [reg]
      uint32_t src3_swiz = alu->src3_swiz & ~0x3C;
      uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
      uint32_t swiz_b = (src3_swiz & 0x3);
      print_srcreg(output,
                   alu->src3_reg, 0, 0,
                   alu->src3_reg_negate, alu->src3_reg_abs);
      output->append(".%c", chan_names[swiz_a]);
      output->append(", ");
      uint32_t reg2 = (alu->scalar_opc & 1) | (alu->src3_swiz & 0x3C) | (alu->src3_sel << 1);
      print_srcreg(output,
                   reg2, 1, 0,
                   alu->src3_reg_negate, alu->src3_reg_abs);
      output->append(".%c", chan_names[swiz_b]);
    } else {
      print_srcreg(output,
                   alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                   alu->src3_reg_negate, alu->src3_reg_abs);
    }
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
  output->append(" = float4(");
  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  // TODO(benvanik): detect xyzw = xyzw, etc.
  // TODO(benvanik): detect and set as rN = float4(samp.xyz, 1.0); / etc
  uint32_t component_count = GetFormatComponentCount(vtx->format);
  uint32_t dst_swiz = vtx->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      output->append("0.0");
    } else if ((dst_swiz & 0x7) == 5) {
      output->append("1.0");
    } else if ((dst_swiz & 0x7) == 6) {
      // ?
      output->append("?");
    } else if ((dst_swiz & 0x7) == 7) {
      output->append("r%u.%c", vtx->dst_reg, chan_names[i]);
    } else {
      output->append("i.vf%u_%d.%c",
                     fetch_slot, vtx->offset,
                     chan_names[dst_swiz & 0x3]);
    }
    if (i < 3) {
      output->append(", ");
    }
    dst_swiz >>= 3;
  }
  output->append(");\n");
  return 0;
}

int TranslateTextureFetch(
  xe_gpu_translate_ctx_t& ctx, const instr_fetch_tex_t* tex, int sync) {
  Output* output = ctx.output;

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
  output->append("  //   %sFETCH:\t", sync ? "(S)" : "   ");
  if (tex->pred_select) {
    output->append(tex->pred_condition ? "EQ" : "NE");
  }
  print_fetch_dst(output, tex->dst_reg, tex->dst_swiz);
  output->append(" = R%u.", tex->src_reg);
  for (int i = 0; i < 3; i++) {
    output->append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  output->append(" CONST(%u)", tex->const_idx);
  if (tex->fetch_valid_only) {
    output->append(" VALID_ONLY");
  }
  if (tex->tx_coord_denorm) {
    output->append(" DENORM");
  }
  if (tex->mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" MAG(%s)", filter[tex->mag_filter]);
  }
  if (tex->min_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" MIN(%s)", filter[tex->min_filter]);
  }
  if (tex->mip_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" MIP(%s)", filter[tex->mip_filter]);
  }
  if (tex->aniso_filter != ANISO_FILTER_USE_FETCH_CONST) {
    output->append(" ANISO(%s)", aniso_filter[tex->aniso_filter]);
  }
  if (tex->arbitrary_filter != ARBITRARY_FILTER_USE_FETCH_CONST) {
    output->append(" ARBITRARY(%s)", arbitrary_filter[tex->arbitrary_filter]);
  }
  if (tex->vol_mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" VOL_MAG(%s)", filter[tex->vol_mag_filter]);
  }
  if (tex->vol_min_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" VOL_MIN(%s)", filter[tex->vol_min_filter]);
  }
  if (!tex->use_comp_lod) {
    output->append(" LOD(%u)", tex->use_comp_lod);
    output->append(" LOD_BIAS(%u)", tex->lod_bias);
  }
  if (tex->use_reg_lod) {
    output->append(" REG_LOD(%u)", tex->use_reg_lod);
  }
  if (tex->use_reg_gradients) {
    output->append(" USE_REG_GRADIENTS");
  }
  output->append(" LOCATION(%s)", sample_loc[tex->sample_location]);
  if (tex->offset_x || tex->offset_y || tex->offset_z) {
    output->append(" OFFSET(%u,%u,%u)", tex->offset_x, tex->offset_y, tex->offset_z);
  }
  output->append("\n");

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
  output->append("  ");
  output->append("r%u.xyzw", tex->dst_reg);
  output->append(" = ");
  output->append(
      "x_texture_%d.Sample(x_sampler_%d, r%u.",
      tex->const_idx,
      ctx.tex_fetch_index++, // hacky way to line up to tex buffers
      tex->src_reg);
  src_swiz = tex->src_swiz;
  for (int i = 0; i < src_component_count; i++) {
    output->append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  output->append(").");

  // Pass one over dest does xyzw and fakes the special values.
  // TODO(benvanik): detect and set as rN = float4(samp.xyz, 1.0); / etc
  uint32_t dst_swiz = tex->dst_swiz;
  for (int i = 0; i < 4; i++) {
    output->append("%c", chan_names[dst_swiz & 0x3]);
    dst_swiz >>= 3;
  }
  output->append(";\n");
  // Do another pass to set constant values.
  dst_swiz = tex->dst_swiz;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      output->append("  r%u.%c = 0.0;\n", tex->dst_reg, chan_names[i]);
    } else if ((dst_swiz & 0x7) == 5) {
      output->append("  r%u.%c = 1.0;\n", tex->dst_reg, chan_names[i]);
    }
    dst_swiz >>= 3;
  }
  return 0;
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
