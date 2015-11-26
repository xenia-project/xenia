/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SHADER_TRANSLATOR_H_

#include <memory>
#include <vector>

#include "xenia/gpu/xenos.h"
#include "xenia/ui/spirv/spirv_util.h"

namespace xe {
namespace gpu {

class TranslatedShader {
 public:
  TranslatedShader(ShaderType shader_type, uint64_t ucode_data_hash,
                   const uint32_t* ucode_words, size_t ucode_word_count);
  ~TranslatedShader();

  ShaderType type() const { return shader_type_; }
  const uint32_t* ucode_words() const { return ucode_data_.data(); }
  size_t ucode_word_count() const { return ucode_data_.size(); }

  bool is_valid() const { return is_valid_; }

 private:
  ShaderType shader_type_;
  std::vector<uint32_t> ucode_data_;
  uint64_t ucode_data_hash_;

  bool is_valid_ = false;
};

class ShaderTranslator {
 public:
  ShaderTranslator();

  std::unique_ptr<TranslatedShader> Translate(ShaderType shader_type,
                                              uint64_t ucode_data_hash,
                                              const uint32_t* ucode_words,
                                              size_t ucode_word_count);

 private:
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_TRANSLATOR_H_
