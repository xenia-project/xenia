/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/buffer_cache.h>

#include <xenia/gpu/buffer.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


BufferCache::BufferCache() {
}

BufferCache::~BufferCache() {
  Clear();
}

IndexBuffer* BufferCache::FetchIndexBuffer(
    const IndexBufferInfo& info,
    const uint8_t* src_ptr, size_t length) {
  size_t key = hash_combine(info.endianness, info.index_32bit, info.index_count, info.index_size);
  size_t hash = xe_hash64(src_ptr, length);
  auto it = index_buffer_map_.find(key);
  if (it != index_buffer_map_.end()) {
    if (hash == it->second->hash()) {
      return it->second;
    } else {
      return it->second->FetchDirty(hash) ? it->second : nullptr;
    }
  } else {
    auto buffer = CreateIndexBuffer(info, src_ptr, length);
    index_buffer_map_.insert({ key, buffer });
    if (!buffer->FetchNew(hash)) {
      return nullptr;
    }
    return buffer;
  }
}

void BufferCache::Clear() {
  for (auto it = index_buffer_map_.begin();
       it != index_buffer_map_.end(); ++it) {
    auto buffer = it->second;
    delete buffer;
  }
  index_buffer_map_.clear();
}
