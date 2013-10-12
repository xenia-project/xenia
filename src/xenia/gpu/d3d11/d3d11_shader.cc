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
  device_->Release();
}


D3D11VertexShader::D3D11VertexShader(
    ID3D11Device* device,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    handle_(0),
    D3D11Shader(device, XE_GPU_SHADER_TYPE_VERTEX,
                src_ptr, length, hash) {
}

D3D11VertexShader::~D3D11VertexShader() {
  if (handle_) handle_->Release();
}

int D3D11VertexShader::Prepare() {
  if (handle_) {
    return 0;
  }

  // TODO(benvanik): translate/etc.
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
  if (handle_) handle_->Release();
}

int D3D11PixelShader::Prepare() {
  if (handle_) {
    return 0;
  }

  // TODO(benvanik): translate/etc.
  return 0;
}
