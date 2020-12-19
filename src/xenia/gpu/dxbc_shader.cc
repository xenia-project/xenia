/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader.h"

#include <cstring>

namespace xe {
namespace gpu {

DxbcShader::DxbcShader(xenos::ShaderType shader_type, uint64_t data_hash,
                       const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count) {}

Shader::Translation* DxbcShader::CreateTranslationInstance(
    uint64_t modification) {
  return new DxbcTranslation(*this, modification);
}

}  // namespace gpu
}  // namespace xe
