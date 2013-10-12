/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_shader_cache.h>

#include <xenia/gpu/d3d11/d3d11_shader.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


D3D11ShaderCache::D3D11ShaderCache(ID3D11Device* device) {
  device_ = device;
  device_->AddRef();
}

D3D11ShaderCache::~D3D11ShaderCache() {
  device_->Release();
}

Shader* D3D11ShaderCache::CreateCore(
    xenos::XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length,
    uint32_t hash) {
  return new D3D11Shader(
      device_,
      type,
      src_ptr, length,
      hash);
}