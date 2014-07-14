/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_shader_resource.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_geometry_shader.h>
#include <xenia/gpu/d3d11/d3d11_resource_cache.h>
#include <xenia/gpu/d3d11/d3d11_shader_translator.h>
#include <xenia/gpu/xenos/ucode.h>

#include <d3dcompiler.h>

using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


namespace {

ID3D10Blob* D3D11ShaderCompile(XE_GPU_SHADER_TYPE type,
                               const char* shader_source,
                               const char* disasm_source) {
  SCOPE_profile_cpu_f("gpu");

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
  size_t hash = xe_hash64(disasm_source, xestrlena(disasm_source)); // ?
  char file_name[poly::max_path];
  xesnprintfa(file_name, XECOUNT(file_name),
      "%s/gen_%.16llX.%s",
      base_path,
      hash,
      type == XE_GPU_SHADER_TYPE_VERTEX ? "vs" : "ps");

  if (FLAGS_dump_shaders.size()) {
    FILE* f = fopen(file_name, "w");
    fprintf(f, shader_source);
    fprintf(f, "\n\n");
    fprintf(f, "/*\n");
    fprintf(f, disasm_source);
    fprintf(f, " */\n");
    fclose(f);
  }

  // Compile shader to bytecode blob.
  ID3D10Blob* shader_blob = 0;
  ID3D10Blob* error_blob = 0;
  HRESULT hr = D3DCompile(
      shader_source, strlen(shader_source),
      file_name,
      defines, nullptr,
      "main",
      type == XE_GPU_SHADER_TYPE_VERTEX ? "vs_5_0" : "ps_5_0",
      flags1, flags2,
      &shader_blob, &error_blob);
  if (error_blob) {
    char* msg = (char*)error_blob->GetBufferPointer();
    XELOGE("D3D11: shader compile failed with %s", msg);
  }
  XESAFERELEASE(error_blob);
  if (FAILED(hr)) {
    return nullptr;
  }
  return shader_blob;
}

}  // namespace


D3D11VertexShaderResource::D3D11VertexShaderResource(
    D3D11ResourceCache* resource_cache,
    const MemoryRange& memory_range,
    const Info& info)
    : VertexShaderResource(memory_range, info),
      resource_cache_(resource_cache),
      handle_(nullptr),
      input_layout_(nullptr),
      translated_src_(nullptr) {
  xe_zero_struct(geometry_shaders_, sizeof(geometry_shaders_));
}

D3D11VertexShaderResource::~D3D11VertexShaderResource() {
  XESAFERELEASE(handle_);
  XESAFERELEASE(input_layout_);
  for (int i = 0; i < XECOUNT(geometry_shaders_); ++i) {
    delete geometry_shaders_[i];
  }
  xe_free(translated_src_);
}

int D3D11VertexShaderResource::Prepare(
    const xe_gpu_program_cntl_t& program_cntl) {
  SCOPE_profile_cpu_f("gpu");
  if (is_prepared_ || handle_) {
    return 0;
  }

  // TODO(benvanik): look in file based on hash/etc.
  void* byte_code = NULL;
  size_t byte_code_length = 0;

  // Translate and compile source.
  D3D11ShaderTranslator translator;
  int ret = translator.TranslateVertexShader(this, program_cntl);
  if (ret) {
    XELOGE("D3D11: failed to translate vertex shader");
    return ret;
  }
  translated_src_ = xestrdupa(translator.translated_src());

  ID3D10Blob* shader_blob = D3D11ShaderCompile(
      XE_GPU_SHADER_TYPE_VERTEX, translated_src_, disasm_src());
  if (!shader_blob) {
    return 1;
  }
  byte_code_length = shader_blob->GetBufferSize();
  byte_code = xe_malloc(byte_code_length);
  xe_copy_struct(
      byte_code, shader_blob->GetBufferPointer(), byte_code_length);
  XESAFERELEASE(shader_blob);

  // Create shader.
  HRESULT hr = resource_cache_->device()->CreateVertexShader(
      byte_code, byte_code_length,
      nullptr,
      &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader");
    xe_free(byte_code);
    return 1;
  }

  // Create input layout.
  ret = CreateInputLayout(byte_code, byte_code_length);
  xe_free(byte_code);
  if (ret) {
    return 1;
  }
  is_prepared_ = true;
  return 0;
}

int D3D11VertexShaderResource::CreateInputLayout(const void* byte_code,
                                                 size_t byte_code_length) {
  size_t element_count = 0;
  const auto& inputs = buffer_inputs();
  for (uint32_t n = 0; n < inputs.count; n++) {
    element_count += inputs.descs[n].info.element_count;
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
  for (uint32_t n = 0; n < inputs.count; n++) {
    const auto& input = inputs.descs[n];
    for (uint32_t m = 0; m < input.info.element_count; m++) {
      const auto& el = input.info.elements[m];
      uint32_t vb_slot = input.input_index;
      DXGI_FORMAT vtx_format;
      switch (el.format) {
      case FMT_8_8_8_8:
        if (el.is_normalized) {
          vtx_format = el.is_signed ?
              DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        } else {
          vtx_format = el.is_signed ?
              DXGI_FORMAT_R8G8B8A8_SINT : DXGI_FORMAT_R8G8B8A8_UINT;
        }
        break;
      case FMT_2_10_10_10:
        if (el.is_normalized) {
          vtx_format = DXGI_FORMAT_R10G10B10A2_UNORM;
        } else {
          vtx_format = DXGI_FORMAT_R10G10B10A2_UINT;
        }
        break;
      // DXGI_FORMAT_R11G11B10_FLOAT?
      case FMT_16_16:
        if (el.is_normalized) {
          vtx_format = el.is_signed ?
              DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM;
        } else {
          vtx_format = el.is_signed ?
              DXGI_FORMAT_R16G16_SINT : DXGI_FORMAT_R16G16_UINT;
        }
        break;
      case FMT_16_16_16_16:
        if (el.is_normalized) {
          vtx_format = el.is_signed ?
              DXGI_FORMAT_R16G16B16A16_SNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
        } else {
          vtx_format = el.is_signed ?
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
        vtx_format = el.is_signed ?
            DXGI_FORMAT_R32_SINT : DXGI_FORMAT_R32_UINT;
        break;
      case FMT_32_32:
        vtx_format = el.is_signed ?
            DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32G32_UINT;
        break;
      case FMT_32_32_32_32:
        vtx_format = el.is_signed ?
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
        assert_always();
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
  HRESULT hr = resource_cache_->device()->CreateInputLayout(
      element_descs,
      (UINT)element_count,
      byte_code, byte_code_length,
      &input_layout_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader input layout");
    return 1;
  }

  return 0;
}

int D3D11VertexShaderResource::DemandGeometryShader(
    GeometryShaderType type, D3D11GeometryShader** out_shader) {
  if (geometry_shaders_[type]) {
    *out_shader = geometry_shaders_[type];
    return 0;
  }

  // Demand generate.
  auto device = resource_cache_->device();
  D3D11GeometryShader* shader = nullptr;
  switch (type) {
  case POINT_SPRITE_SHADER:
    shader = new D3D11PointSpriteGeometryShader(device);
    break;
  case RECT_LIST_SHADER:
    shader = new D3D11RectListGeometryShader(device);
    break;
  case QUAD_LIST_SHADER:
    shader = new D3D11QuadListGeometryShader(device);
    break;
  default:
    assert_always();
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

D3D11PixelShaderResource::D3D11PixelShaderResource(
    D3D11ResourceCache* resource_cache,
    const MemoryRange& memory_range,
    const Info& info)
    : PixelShaderResource(memory_range, info),
      resource_cache_(resource_cache),
      handle_(nullptr),
      translated_src_(nullptr) {
}

D3D11PixelShaderResource::~D3D11PixelShaderResource() {
  XESAFERELEASE(handle_);
  xe_free(translated_src_);
}

int D3D11PixelShaderResource::Prepare(const xe_gpu_program_cntl_t& program_cntl,
                                      VertexShaderResource* input_shader) {
  SCOPE_profile_cpu_f("gpu");
  if (is_prepared_ || handle_) {
    return 0;
  }

  // TODO(benvanik): look in file based on hash/etc.
  void* byte_code = NULL;
  size_t byte_code_length = 0;

  // Translate and compile source.
  D3D11ShaderTranslator translator;
  int ret = translator.TranslatePixelShader(this,
                                            program_cntl,
                                            input_shader->alloc_counts());
  if (ret) {
    XELOGE("D3D11: failed to translate pixel shader");
    return ret;
  }
  translated_src_ = xestrdupa(translator.translated_src());

  ID3D10Blob* shader_blob = D3D11ShaderCompile(
      XE_GPU_SHADER_TYPE_PIXEL, translated_src_, disasm_src());
  if (!shader_blob) {
    return 1;
  }
  byte_code_length = shader_blob->GetBufferSize();
  byte_code = xe_malloc(byte_code_length);
  xe_copy_struct(
      byte_code, shader_blob->GetBufferPointer(), byte_code_length);
  XESAFERELEASE(shader_blob);

  // Create shader.
  HRESULT hr = resource_cache_->device()->CreatePixelShader(
      byte_code, byte_code_length,
      nullptr,
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
