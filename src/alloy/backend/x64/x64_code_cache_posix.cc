/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_code_cache.h>

#include <poly/assert.h>

#include <sys/mman.h>

namespace alloy {
namespace backend {
namespace x64 {

class X64CodeChunk {
 public:
  X64CodeChunk(size_t chunk_size);
  ~X64CodeChunk();

 public:
  X64CodeChunk* next;
  size_t capacity;
  uint8_t* buffer;
  size_t offset;
};

X64CodeCache::X64CodeCache(size_t chunk_size)
    : chunk_size_(chunk_size), head_chunk_(NULL), active_chunk_(NULL) {}

X64CodeCache::~X64CodeCache() {
  std::lock_guard<std::mutex> guard(lock_);
  auto chunk = head_chunk_;
  while (chunk) {
    auto next = chunk->next;
    delete chunk;
    chunk = next;
  }
  head_chunk_ = NULL;
}

int X64CodeCache::Initialize() { return 0; }

void* X64CodeCache::PlaceCode(void* machine_code, size_t code_size,
                              size_t stack_size) {
  SCOPE_profile_cpu_f("alloy");

  // Always move the code to land on 16b alignment. We do this by rounding up
  // to 16b so that all offsets are aligned.
  code_size = XEROUNDUP(code_size, 16);

  lock_.lock();

  if (active_chunk_) {
    if (active_chunk_->capacity - active_chunk_->offset < code_size) {
      auto next = active_chunk_->next;
      if (!next) {
        assert_true(code_size < chunk_size_, "need to support larger chunks");
        next = new X64CodeChunk(chunk_size_);
        active_chunk_->next = next;
      }
      active_chunk_ = next;
    }
  } else {
    head_chunk_ = active_chunk_ = new X64CodeChunk(chunk_size_);
  }

  uint8_t* final_address = active_chunk_->buffer + active_chunk_->offset;
  active_chunk_->offset += code_size;

  lock_.unlock();

  // Copy code.
  xe_copy_struct(final_address, machine_code, code_size);

  return final_address;
}

X64CodeChunk::X64CodeChunk(size_t chunk_size)
    : next(NULL), capacity(chunk_size), buffer(0), offset(0) {
  buffer = (uint8_t*)mmap(nullptr, chunk_size, PROT_WRITE | PROT_EXEC,
                          MAP_ANON | MAP_PRIVATE, -1, 0);
}

X64CodeChunk::~X64CodeChunk() {
  if (buffer) {
    munmap(buffer, capacity);
  }
}

}  // namespace x64
}  // namespace backend
}  // namespace alloy
