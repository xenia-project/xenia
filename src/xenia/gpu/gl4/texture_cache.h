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

#include <vector>

#include <xenia/gpu/gl4/circular_buffer.h>
#include <xenia/gpu/gl4/gl_context.h>
#include <xenia/gpu/sampler_info.h>
#include <xenia/gpu/texture_info.h>

namespace xe {
namespace gpu {
namespace gl4 {

class TextureCache {
 public:
  struct EntryView {
    SamplerInfo sampler_info;
    GLuint sampler;
    GLuint64 texture_sampler_handle;
  };
  struct Entry {
    TextureInfo texture_info;
    GLuint base_texture;
    std::vector<EntryView> views;
  };

  TextureCache();
  ~TextureCache();

  bool Initialize(CircularBuffer* scratch_buffer);
  void Shutdown();
  void Clear();

  EntryView* Demand(void* host_base, size_t length,
                    const TextureInfo& texture_info,
                    const SamplerInfo& sampler_info);

 private:
  bool SetupTexture(GLuint texture, const TextureInfo& texture_info);
  bool SetupSampler(GLuint sampler, const TextureInfo& texture_info,
                    const SamplerInfo& sampler_info);

  bool UploadTexture2D(GLuint texture, void* host_base, size_t length,
                       const TextureInfo& texture_info,
                       const SamplerInfo& sampler_info);

  CircularBuffer* scratch_buffer_;
  std::vector<Entry> entries_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_TEXTURE_CACHE_H_
