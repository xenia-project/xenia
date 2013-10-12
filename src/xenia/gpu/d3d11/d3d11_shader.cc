/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_shader.h>


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

  const void* byte_code = NULL;
  size_t byte_code_length = 0;

  // Create shader.
  HRESULT hr = device_->CreateVertexShader(
      byte_code, byte_code_length,
      NULL,
      &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader");
    return 1;
  }

  // Create input layout.
  uint32_t element_count = 0;
  D3D11_INPUT_ELEMENT_DESC* element_descs = 0;
  hr = device_->CreateInputLayout(
      element_descs,
      element_count,
      byte_code, byte_code_length,
      &input_layout_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader input layout");
    return 1;
  }

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

  const void* byte_code = NULL;
  size_t byte_code_length = 0;

  // Create shader.
  HRESULT hr = device_->CreatePixelShader(
      byte_code, byte_code_length,
      NULL,
      &handle_);
  if (FAILED(hr)) {
    XELOGE("D3D11: failed to create vertex shader");
    return 1;
  }
  is_prepared_ = true;
  return 0;
}
