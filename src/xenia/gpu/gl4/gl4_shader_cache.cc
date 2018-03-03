/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_shader_cache.h"

#include <cinttypes>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/gpu/gl4/gl4_gpu_flags.h"
#include "xenia/gpu/gl4/gl4_shader.h"
#include "xenia/gpu/glsl_shader_translator.h"
#include "xenia/gpu/gpu_flags.h"

#include "third_party/xxhash/xxhash.h"

namespace xe {
namespace gpu {
namespace gl4 {

GL4ShaderCache::GL4ShaderCache(GlslShaderTranslator* shader_translator)
    : shader_translator_(shader_translator) {}

GL4ShaderCache::~GL4ShaderCache() {}

void GL4ShaderCache::Reset() {
  shader_map_.clear();
  all_shaders_.clear();
}

GL4Shader* GL4ShaderCache::LookupOrInsertShader(ShaderType shader_type,
                                                const uint32_t* dwords,
                                                uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  GL4Shader* shader_ptr = nullptr;
  uint64_t hash = XXH64(dwords, dword_count * sizeof(uint32_t), 0);
  auto it = shader_map_.find(hash);
  if (it != shader_map_.end()) {
    // Shader has been previously loaded.
    // TODO(benvanik): compare bytes? Likelihood of collision is low.
    shader_ptr = it->second;
  } else {
    // Check filesystem cache.
    shader_ptr = FindCachedShader(shader_type, hash, dwords, dword_count);
    if (shader_ptr) {
      // Found!
      XELOGGPU("Loaded %s shader from cache (hash: %.16" PRIX64 ")",
               shader_type == ShaderType::kVertex ? "vertex" : "pixel", hash);
      return shader_ptr;
    }

    // Not found in cache - load from scratch.
    auto shader =
        std::make_unique<GL4Shader>(shader_type, hash, dwords, dword_count);
    shader_ptr = shader.get();
    shader_map_.insert({hash, shader_ptr});
    all_shaders_.emplace_back(std::move(shader));

    // Perform translation.
    // If this fails the shader will be marked as invalid and ignored later.
    if (shader_translator_->Translate(shader_ptr)) {
      shader_ptr->Prepare();
      if (shader_ptr->is_valid()) {
        CacheShader(shader_ptr);

        XELOGGPU("Generated %s shader at 0x%.16" PRIX64 " (%db):\n%s",
                 shader_type == ShaderType::kVertex ? "vertex" : "pixel",
                 dwords, dword_count * 4,
                 shader_ptr->ucode_disassembly().c_str());
      }

      // Dump shader files if desired.
      if (!FLAGS_dump_shaders.empty()) {
        shader_ptr->Dump(FLAGS_dump_shaders, "gl4");
      }
    } else {
      XELOGE("Shader failed translation");
    }
  }

  return shader_ptr;
}

void GL4ShaderCache::CacheShader(GL4Shader* shader) {
  if (FLAGS_shader_cache_dir.empty()) {
    // Cache disabled.
    return;
  }

  GLenum binary_format = 0;
  auto binary = shader->GetBinary(&binary_format);
  if (binary.size() == 0) {
    // No binary returned.
    return;
  }

  auto cache_dir = xe::to_absolute_path(xe::to_wstring(FLAGS_shader_cache_dir));
  xe::filesystem::CreateFolder(cache_dir);
  auto filename =
      cache_dir + xe::format_string(
                      L"%.16" PRIX64 ".%s", shader->ucode_data_hash(),
                      shader->type() == ShaderType::kPixel ? L"frag" : L"vert");
  auto file = xe::filesystem::OpenFile(filename, "wb");
  if (!file) {
    // Not fatal, but not too good.
    return;
  }

  std::vector<uint8_t> cached_shader_mem;
  // Resize this vector to the final filesize (- 1 to account for dummy array
  // in CachedShader)
  cached_shader_mem.resize(sizeof(CachedShader) + binary.size() - 1);
  auto cached_shader =
      reinterpret_cast<CachedShader*>(cached_shader_mem.data());
  cached_shader->magic = xe::byte_swap('XSHD');
  cached_shader->version = 0;  // TODO
  cached_shader->shader_type = uint8_t(shader->type());
  cached_shader->binary_len = uint32_t(binary.size());
  cached_shader->binary_format = binary_format;
  std::memcpy(cached_shader->binary, binary.data(), binary.size());

  fwrite(cached_shader_mem.data(), cached_shader_mem.size(), 1, file);
  fclose(file);
}

GL4Shader* GL4ShaderCache::FindCachedShader(ShaderType shader_type,
                                            uint64_t hash,
                                            const uint32_t* dwords,
                                            uint32_t dword_count) {
  if (FLAGS_shader_cache_dir.empty()) {
    // Cache disabled.
    return nullptr;
  }

  auto cache_dir = xe::to_absolute_path(xe::to_wstring(FLAGS_shader_cache_dir));
  auto filename =
      cache_dir +
      xe::format_string(L"%.16" PRIX64 ".%s", hash,
                        shader_type == ShaderType::kPixel ? L"frag" : L"vert");
  if (!xe::filesystem::PathExists(filename)) {
    return nullptr;
  }

  // Shader is cached. Open it up.
  auto map = xe::MappedMemory::Open(filename, MappedMemory::Mode::kRead);
  if (!map) {
    // Should not fail
    assert_always();
    return nullptr;
  }

  auto cached_shader = reinterpret_cast<CachedShader*>(map->data());
  // TODO: Compare versions
  if (cached_shader->magic != xe::byte_swap('XSHD')) {
    return nullptr;
  }

  auto shader =
      std::make_unique<GL4Shader>(shader_type, hash, dwords, dword_count);

  // Gather the binding points.
  // TODO: Make Shader do this on construction.
  // TODO: Regenerate microcode disasm/etc on load.
  shader_translator_->GatherAllBindingInformation(shader.get());
  if (!shader->LoadFromBinary(cached_shader->binary,
                              cached_shader->binary_format,
                              cached_shader->binary_len)) {
    // Failed to load from binary.
    return nullptr;
  }

  auto shader_ptr = shader.get();
  shader_map_.insert({hash, shader_ptr});
  all_shaders_.emplace_back(std::move(shader));
  return shader_ptr;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
