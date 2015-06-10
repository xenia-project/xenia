/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_ARENA_H_
#define XENIA_BASE_ARENA_H_

#include <cstddef>
#include <cstdint>

namespace xe {

class Arena {
 public:
  Arena(size_t chunk_size = 4 * 1024 * 1024);
  ~Arena();

  void Reset();
  void DebugFill();

  void* Alloc(size_t size);
  template <typename T>
  T* Alloc() {
    return reinterpret_cast<T*>(Alloc(sizeof(T)));
  }
  void Rewind(size_t size);

  void* CloneContents();

 private:
  class Chunk {
   public:
    Chunk(size_t chunk_size);
    ~Chunk();

    Chunk* next;

    size_t capacity;
    uint8_t* buffer;
    size_t offset;
  };

 private:
  size_t chunk_size_;
  Chunk* head_chunk_;
  Chunk* active_chunk_;
};

}  // namespace xe

#endif  // XENIA_BASE_ARENA_H_
