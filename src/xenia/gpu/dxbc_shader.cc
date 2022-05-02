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

DxbcShader::DxbcShader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
                       const uint32_t* ucode_dwords, size_t ucode_dword_count,
                       std::endian ucode_source_endian)
    : Shader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
             ucode_source_endian) {}

Shader::Translation* DxbcShader::CreateTranslationInstance(
    uint64_t modification) {
  return new DxbcTranslation(*this, modification);
}

}  // namespace gpu
}  // namespace xe
