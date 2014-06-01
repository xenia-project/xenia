/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/texture_cache.h>

#include <xenia/gpu/xenos/ucode.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


// https://github.com/ivmai/bdwgc/blob/master/os_dep.c

TextureCache::TextureCache(Memory* memory)
    : memory_(memory) {
}

TextureCache::~TextureCache() {
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    auto texture = it->second;
    delete texture;
  }
  textures_.clear();
}

TextureView* TextureCache::FetchTexture(
    uint32_t address, const xenos::xe_gpu_texture_fetch_t& fetch) {
  auto it = textures_.find(address);
  if (it == textures_.end()) {
    // Texture not found.
    const uint8_t* host_address = memory_->Translate(address);
    auto texture = CreateTexture(address, host_address, fetch);
    if (!texture) {
      return nullptr;
    }
    textures_.insert({ address, texture });
    return texture->Fetch(fetch);
  } else {
    // Texture found.
    return it->second->Fetch(fetch);
  }
}
