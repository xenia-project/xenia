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

#include <d3dx11.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11Shader::D3D11Shader(
    ID3D11Device* device,
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    Shader(type, src_ptr, length, hash) {
  device_ = device;
  device_->AddRef();
}

D3D11Shader::~D3D11Shader() {
  XESAFERELEASE(device_);
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

  void* byte_code = NULL;
  size_t byte_code_length = 0;


  if (!byte_code) {
    return 1;
  }

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
    element_descs[n].SemanticName         = "XEVF";
    element_descs[n].SemanticIndex        = n;
    element_descs[n].Format               = vtx_format;
    // TODO(benvanik): pick slot in same way that driver does.
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

int D3D11PixelShader::Prepare(xe_gpu_program_cntl_t* program_cntl) {
  if (handle_) {
    return 0;
  }

  void* byte_code = NULL;
  size_t byte_code_length = 0;


  if (!byte_code) {
    return 1;
  }

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
