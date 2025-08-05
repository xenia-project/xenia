/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_SHADER_CACHE_H_
#define XENIA_GPU_METAL_METAL_SHADER_CACHE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace xe {
namespace gpu {
namespace metal {

// Simple cache for DXBC to DXIL conversions to avoid re-running Wine
class MetalShaderCache {
 public:
  struct CacheEntry {
    std::vector<uint8_t> dxil_data;
    uint64_t timestamp;
  };

  // Get cached DXIL data if available
  bool GetCachedDxil(uint64_t shader_hash, std::vector<uint8_t>& dxil_out);
  
  // Store DXIL data in cache
  void StoreDxil(uint64_t shader_hash, const std::vector<uint8_t>& dxil_data);
  
  // Clear all cached entries
  void Clear();
  
  // Get cache statistics
  size_t GetCacheSize() const { return cache_.size(); }
  size_t GetTotalBytes() const;
  
  // Save/load cache from disk (optional)
  bool SaveToFile(const std::string& path);
  bool LoadFromFile(const std::string& path);

 private:
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, CacheEntry> cache_;
};

// Global shader cache instance
extern std::unique_ptr<MetalShaderCache> g_metal_shader_cache;

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_SHADER_CACHE_H_