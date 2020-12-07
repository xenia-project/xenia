/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader.h"

#include <cinttypes>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/string.h"
#include "xenia/gpu/ucode.h"

namespace xe {
namespace gpu {
using namespace ucode;

Shader::Shader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
               const uint32_t* ucode_dwords, size_t ucode_dword_count)
    : shader_type_(shader_type), ucode_data_hash_(ucode_data_hash) {
  // We keep ucode data in host native format so it's easier to work with.
  ucode_data_.resize(ucode_dword_count);
  xe::copy_and_swap(ucode_data_.data(), ucode_dwords, ucode_dword_count);
}

Shader::~Shader() {
  for (auto it : translations_) {
    delete it.second;
  }
}

std::string Shader::Translation::GetTranslatedBinaryString() const {
  std::string result;
  result.resize(translated_binary_.size());
  std::memcpy(const_cast<char*>(result.data()), translated_binary_.data(),
              translated_binary_.size());
  return result;
}

std::filesystem::path Shader::Translation::Dump(
    const std::filesystem::path& base_path, const char* path_prefix) {
  std::filesystem::path path = base_path;
  // Ensure target path exists.
  if (!path.empty()) {
    path = std::filesystem::absolute(path);
    std::filesystem::create_directories(path);
  }
  path = path /
         fmt::format(
             "shader_{:016X}_{:08X}.{}.{}", shader().ucode_data_hash(),
             modification(), path_prefix,
             shader().type() == xenos::ShaderType::kVertex ? "vert" : "frag");
  FILE* f = filesystem::OpenFile(path, "wb");
  if (f) {
    fwrite(translated_binary_.data(), 1, translated_binary_.size(), f);
    fprintf(f, "\n\n");
    auto ucode_disasm_ptr = shader().ucode_disassembly().c_str();
    while (*ucode_disasm_ptr) {
      auto line_end = std::strchr(ucode_disasm_ptr, '\n');
      fprintf(f, "// ");
      fwrite(ucode_disasm_ptr, 1, line_end - ucode_disasm_ptr + 1, f);
      ucode_disasm_ptr = line_end + 1;
    }
    fprintf(f, "\n\n");
    if (!host_disassembly_.empty()) {
      fprintf(f, "\n\n/*\n%s\n*/\n", host_disassembly_.c_str());
    }
    fclose(f);
  }
  return std::move(path);
}

Shader::Translation* Shader::GetOrCreateTranslation(uint32_t modification,
                                                    bool* is_new) {
  auto it = translations_.find(modification);
  if (it != translations_.end()) {
    if (is_new) {
      *is_new = false;
    }
    return it->second;
  }
  Translation* translation = CreateTranslationInstance(modification);
  translations_.emplace(modification, translation);
  if (is_new) {
    *is_new = true;
  }
  return translation;
}

void Shader::DestroyTranslation(uint32_t modification) {
  auto it = translations_.find(modification);
  if (it == translations_.end()) {
    return;
  }
  delete it->second;
  translations_.erase(it);
}

std::filesystem::path Shader::DumpUcodeBinary(
    const std::filesystem::path& base_path) {
  // Ensure target path exists.
  std::filesystem::path path = base_path;
  if (!path.empty()) {
    path = std::filesystem::absolute(path);
    std::filesystem::create_directories(path);
  }
  path = path /
         fmt::format("shader_{:016X}.ucode.bin.{}", ucode_data_hash(),
                     type() == xenos::ShaderType::kVertex ? "vert" : "frag");

  FILE* f = filesystem::OpenFile(path, "wb");
  if (f) {
    fwrite(ucode_data().data(), 4, ucode_data().size(), f);
    fclose(f);
  }
  return std::move(path);
}

Shader::Translation* Shader::CreateTranslationInstance(uint32_t modification) {
  // Default implementation for simple cases like ucode disassembly.
  return new Translation(*this, modification);
}

}  //  namespace gpu
}  //  namespace xe
