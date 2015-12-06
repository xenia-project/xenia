/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_H_
#define XENIA_GPU_SHADER_H_

#include <string>
#include <vector>

#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

class Shader {
 public:
  virtual ~Shader();

  ShaderType type() const { return shader_type_; }
  bool is_valid() const { return !!translated_shader_; }
  const std::string& host_disassembly() const { return host_disassembly_; }
  TranslatedShader* translated_shader() const {
    return translated_shader_.get();
  }

  const uint32_t* data() const { return data_.data(); }
  uint32_t dword_count() const { return uint32_t(data_.size()); }

  virtual bool Prepare(ShaderTranslator* shader_translator);

 protected:
  Shader(ShaderType shader_type, uint64_t data_hash, const uint32_t* dword_ptr,
         uint32_t dword_count);

  ShaderType shader_type_;
  uint64_t data_hash_;
  std::vector<uint32_t> data_;

  std::string translated_disassembly_;
  std::vector<uint8_t> translated_binary_;
  std::string host_disassembly_;
  std::string error_log_;

  std::unique_ptr<TranslatedShader> translated_shader_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_H_
