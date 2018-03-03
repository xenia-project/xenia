/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_SHADER_CACHE_H_
#define XENIA_GPU_GL4_SHADER_CACHE_H_

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
class GlslShaderTranslator;

namespace gl4 {

class GL4Shader;

class GL4ShaderCache {
 public:
  GL4ShaderCache(GlslShaderTranslator* shader_translator);
  ~GL4ShaderCache();

  void Reset();
  GL4Shader* LookupOrInsertShader(ShaderType shader_type,
                                  const uint32_t* dwords, uint32_t dword_count);

 private:
  // Cached shader file format.
  struct CachedShader {
    uint32_t magic;
    uint32_t version;        // Version of the shader translator used.
    uint8_t shader_type;     // ShaderType enum
    uint32_t binary_len;     // Code length
    uint32_t binary_format;  // Binary format (from OpenGL)
    uint8_t binary[1];       // Code
  };

  void CacheShader(GL4Shader* shader);
  GL4Shader* FindCachedShader(ShaderType shader_type, uint64_t hash,
                              const uint32_t* dwords, uint32_t dword_count);

  GlslShaderTranslator* shader_translator_ = nullptr;
  std::vector<std::unique_ptr<GL4Shader>> all_shaders_;
  std::unordered_map<uint64_t, GL4Shader*> shader_map_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_SHADER_CACHE_H_
