/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_code_cache.h>

#include <alloy/backend/x64/tracing.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;


X64CodeCache::X64CodeCache(size_t chunk_size) :
    chunk_size_(chunk_size),
    head_chunk_(NULL), active_chunk_(NULL) {
  lock_ = AllocMutex();
}

X64CodeCache::~X64CodeCache() {
  LockMutex(lock_);
  auto chunk = head_chunk_;
  while (chunk) {
    auto next = chunk->next;
    delete chunk;
    chunk = next;
  }
  head_chunk_ = NULL;
  UnlockMutex(lock_);
  FreeMutex(lock_);
}

int X64CodeCache::Initialize() {
  return 0;
}

void* X64CodeCache::PlaceCode(void* machine_code, size_t code_size) {
  // Always move the code to land on 16b alignment. We do this by rounding up
  // to 16b so that all offsets are aligned.
  code_size = XEROUNDUP(code_size, 16);

  LockMutex(lock_);

  if (active_chunk_) {
    if (active_chunk_->capacity - active_chunk_->offset < code_size) {
      auto next = active_chunk_->next;
      if (!next) {
        XEASSERT(code_size < chunk_size_); // need to support larger chunks
        next = new CodeChunk(chunk_size_);
        active_chunk_->next = next;
      }
      active_chunk_ = next;
    }
  } else {
    head_chunk_ = active_chunk_ = new CodeChunk(chunk_size_);
  }

  void* final_address = active_chunk_->buffer + active_chunk_->offset;
  active_chunk_->offset += code_size;

  UnlockMutex(lock_);

  xe_copy_struct(final_address, machine_code, code_size);

  // This isn't needed on x64 (probably), but is convention.
  FlushInstructionCache(GetCurrentProcess(), final_address, code_size);
  return final_address;
}

X64CodeCache::CodeChunk::CodeChunk(size_t chunk_size) :
    next(NULL),
    capacity(chunk_size), buffer(0), offset(0) {
  buffer = (uint8_t*)VirtualAlloc(
      NULL, capacity,
      MEM_RESERVE | MEM_COMMIT,
      PAGE_EXECUTE_READWRITE);
}

X64CodeCache::CodeChunk::~CodeChunk() {
  if (buffer) {
    VirtualFree(buffer, 0, MEM_RELEASE);
  }
}
