/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_shader_cache.h"

#include <fstream>
#include <chrono>

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace metal {

// Global shader cache instance
std::unique_ptr<MetalShaderCache> g_metal_shader_cache;

bool MetalShaderCache::GetCachedDxil(uint64_t shader_hash, 
                                     std::vector<uint8_t>& dxil_out) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = cache_.find(shader_hash);
  if (it != cache_.end()) {
    dxil_out = it->second.dxil_data;
    XELOGD("MetalShaderCache: Hit for shader {:016X}", shader_hash);
    return true;
  }
  
  return false;
}

void MetalShaderCache::StoreDxil(uint64_t shader_hash, 
                                 const std::vector<uint8_t>& dxil_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  CacheEntry entry;
  entry.dxil_data = dxil_data;
  entry.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  
  cache_[shader_hash] = std::move(entry);
  XELOGD("MetalShaderCache: Stored shader {:016X} ({} bytes)", 
         shader_hash, dxil_data.size());
}

void MetalShaderCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  XELOGD("MetalShaderCache: Cleared all entries");
}

size_t MetalShaderCache::GetTotalBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t total = 0;
  for (const auto& [hash, entry] : cache_) {
    total += entry.dxil_data.size();
  }
  return total;
}

bool MetalShaderCache::SaveToFile(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  
  // Write header
  uint32_t magic = 0x4C58444D; // 'MDXL'
  uint32_t version = 1;
  uint32_t count = static_cast<uint32_t>(cache_.size());
  
  file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  file.write(reinterpret_cast<const char*>(&version), sizeof(version));
  file.write(reinterpret_cast<const char*>(&count), sizeof(count));
  
  // Write entries
  for (const auto& [hash, entry] : cache_) {
    uint64_t shader_hash = hash;
    uint32_t data_size = static_cast<uint32_t>(entry.dxil_data.size());
    
    file.write(reinterpret_cast<const char*>(&shader_hash), sizeof(shader_hash));
    file.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    file.write(reinterpret_cast<const char*>(entry.dxil_data.data()), data_size);
    file.write(reinterpret_cast<const char*>(&entry.timestamp), sizeof(entry.timestamp));
  }
  
  return file.good();
}

bool MetalShaderCache::LoadFromFile(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  
  // Read header
  uint32_t magic, version, count;
  file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  file.read(reinterpret_cast<char*>(&version), sizeof(version));
  file.read(reinterpret_cast<char*>(&count), sizeof(count));
  
  if (magic != 0x4C58444D || version != 1) {
    return false;
  }
  
  // Read entries
  cache_.clear();
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t shader_hash;
    uint32_t data_size;
    
    file.read(reinterpret_cast<char*>(&shader_hash), sizeof(shader_hash));
    file.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
    
    CacheEntry entry;
    entry.dxil_data.resize(data_size);
    file.read(reinterpret_cast<char*>(entry.dxil_data.data()), data_size);
    file.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    if (!file.good()) {
      cache_.clear();
      return false;
    }
    
    cache_[shader_hash] = std::move(entry);
  }
  
  XELOGD("MetalShaderCache: Loaded {} entries from {}", count, path);
  return true;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe