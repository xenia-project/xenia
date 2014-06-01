/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_CACHE_H_
#define XENIA_GPU_TEXTURE_CACHE_H_

#include <xenia/core.h>
#include <xenia/gpu/texture.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


// TODO(benvanik): overlapping textures.
// TODO(benvanik): multiple textures (differing formats/etc) per address.
class TextureCache {
public:
  TextureCache(Memory* memory);
  virtual ~TextureCache();
  
  Memory* memory() const { return memory_; }

  TextureView* FetchTexture(
      uint32_t address, const xenos::xe_gpu_texture_fetch_t& fetch);

protected:
  virtual Texture* CreateTexture(
      uint32_t address, const uint8_t* host_address,
      const xenos::xe_gpu_texture_fetch_t& fetch) = 0;

  Memory* memory_;

  // Mapped by guest address.
  std::unordered_map<uint32_t, Texture*> textures_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_TEXTURE_CACHE_H_
