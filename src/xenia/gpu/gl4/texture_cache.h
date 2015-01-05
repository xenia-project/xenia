/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_TEXTURE_CACHE_H_
#define XENIA_GPU_GL4_TEXTURE_CACHE_H_

#include <mutex>
#include <unordered_map>
#include <vector>

#include <xenia/gpu/gl4/circular_buffer.h>
#include <xenia/gpu/gl4/gl_context.h>
#include <xenia/gpu/sampler_info.h>
#include <xenia/gpu/texture_info.h>
#include <xenia/memory.h>

namespace xe {
namespace gpu {
namespace gl4 {

class TextureCache {
 public:
  struct SamplerEntry {
    SamplerInfo sampler_info;
    GLuint handle;
  };
  struct TextureEntryView {
    SamplerEntry* sampler;
    uint64_t sampler_hash;
    GLuint64 texture_sampler_handle;
  };
  struct TextureEntry {
    TextureInfo texture_info;
    uintptr_t write_watch_handle;
    GLuint handle;
    bool pending_invalidation;
    std::vector<std::unique_ptr<TextureEntryView>> views;
  };

  TextureCache();
  ~TextureCache();

  bool Initialize(Memory* memory, CircularBuffer* scratch_buffer);
  void Shutdown();

  void Scavenge();
  void Clear();
  void EvictAllTextures();

  TextureEntryView* Demand(const TextureInfo& texture_info,
                           const SamplerInfo& sampler_info);

 private:
  SamplerEntry* LookupOrInsertSampler(const SamplerInfo& sampler_info,
                                      uint64_t opt_hash = 0);
  void EvictSampler(SamplerEntry* entry);
  TextureEntry* LookupOrInsertTexture(const TextureInfo& texture_info,
                                      uint64_t opt_hash = 0);
  void EvictTexture(TextureEntry* entry);

  bool UploadTexture2D(GLuint texture, const TextureInfo& texture_info);

  Memory* memory_;
  CircularBuffer* scratch_buffer_;
  std::unordered_map<uint64_t, SamplerEntry*> sampler_entries_;
  std::unordered_map<uint64_t, TextureEntry*> texture_entries_;

  std::mutex invalidated_textures_mutex_;
  std::vector<TextureEntry*>* invalidated_textures_;
  std::vector<TextureEntry*> invalidated_textures_sets_[2];
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_TEXTURE_CACHE_H_
