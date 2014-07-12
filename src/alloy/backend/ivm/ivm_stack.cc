/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_stack.h>

namespace alloy {
namespace backend {
namespace ivm {

IVMStack::IVMStack()
    : chunk_size_(2 * 1024 * 1024), head_chunk_(NULL), active_chunk_(NULL) {}

IVMStack::~IVMStack() {
  Chunk* chunk = head_chunk_;
  while (chunk) {
    Chunk* next = chunk->next;
    delete chunk;
    chunk = next;
  }
  head_chunk_ = NULL;
}

Register* IVMStack::Alloc(size_t register_count) {
  size_t size = register_count * sizeof(Register);
  if (active_chunk_) {
    if (active_chunk_->capacity - active_chunk_->offset < size) {
      Chunk* next = active_chunk_->next;
      if (!next) {
        assert_true(size < chunk_size_, "need to support larger chunks");
        next = new Chunk(chunk_size_);
        next->prev = active_chunk_;
        active_chunk_->next = next;
      }
      next->offset = 0;
      active_chunk_ = next;
    }
  } else {
    head_chunk_ = active_chunk_ = new Chunk(chunk_size_);
  }

  uint8_t* p = active_chunk_->buffer + active_chunk_->offset;
  active_chunk_->offset += size;
  return (Register*)p;
}

void IVMStack::Free(size_t register_count) {
  size_t size = register_count * sizeof(Register);
  if (active_chunk_->offset == size) {
    // Moving back a chunk.
    active_chunk_->offset = 0;
    if (active_chunk_->prev) {
      active_chunk_ = active_chunk_->prev;
    }
  } else {
    // Still in same chunk.
    active_chunk_->offset -= size;
  }
}

IVMStack::Chunk::Chunk(size_t chunk_size)
    : prev(NULL), next(NULL), capacity(chunk_size), buffer(0), offset(0) {
  buffer = (uint8_t*)xe_malloc(capacity);
}

IVMStack::Chunk::~Chunk() {
  if (buffer) {
    xe_free(buffer);
  }
}

}  // namespace ivm
}  // namespace backend
}  // namespace alloy
