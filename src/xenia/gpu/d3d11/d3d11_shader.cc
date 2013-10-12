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
  // TODO(benvanik): create shader/translate/etc.
}

D3D11Shader::~D3D11Shader() {
}
