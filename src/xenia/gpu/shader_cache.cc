/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/shader_cache.h>

#include <xenia/gpu/shader.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


ShaderCache::ShaderCache() {
}

ShaderCache::~ShaderCache() {
  Clear();
}

Shader* ShaderCache::Create(
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length) {
  uint64_t hash = Hash(src_ptr, length);
  Shader* shader = CreateCore(type, src_ptr, length, hash);
  map_.insert({ hash, shader });
  return shader;
}

Shader* ShaderCache::CreateCore(
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) {
  return new Shader(type, src_ptr, length, hash);
}

Shader* ShaderCache::Find(
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length) {
  uint64_t hash = Hash(src_ptr, length);
  auto it = map_.find(hash);
  if (it != map_.end()) {
    return it->second;
  }
  return NULL;
}

Shader* ShaderCache::FindOrCreate(
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length) {
  SCOPE_profile_cpu_f("gpu");

  uint64_t hash = Hash(src_ptr, length);
  auto it = map_.find(hash);
  if (it != map_.end()) {
    return it->second;
  }
  Shader* shader = CreateCore(type, src_ptr, length, hash);
  map_.insert({ hash, shader });
  return shader;
}

void ShaderCache::Clear() {
  for (auto it = map_.begin(); it != map_.end(); ++it) {
    Shader* shader = it->second;
    delete shader;
  }
  map_.clear();
}

uint64_t ShaderCache::Hash(const uint8_t* src_ptr, size_t length) {
  return xe_hash64(src_ptr, length, 0);
}
