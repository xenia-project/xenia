/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader.h"

#include "xenia/base/logging.h"
#include "xenia/base/memory.h"

namespace xe {
namespace gpu {

Shader::Shader(ShaderType shader_type, uint64_t data_hash,
               const uint32_t* dword_ptr, uint32_t dword_count)
    : shader_type_(shader_type), data_hash_(data_hash) {
  data_.resize(dword_count);
  xe::copy_and_swap(data_.data(), dword_ptr, dword_count);
}

Shader::~Shader() = default;

bool Shader::Prepare(ShaderTranslator* shader_translator) {
  // Perform translation.
  translated_shader_ = shader_translator->Translate(shader_type_, data_hash_,
                                                    data_.data(), data_.size());
  if (!translated_shader_) {
    XELOGE("Shader failed translation");
    return false;
  }

  return true;
}
}  //  namespace gpu
}  //  namespace xe
