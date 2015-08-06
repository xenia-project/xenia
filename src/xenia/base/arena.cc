/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/arena.h"

#include <cstring>
#include <memory>

#include "xenia/base/assert.h"

namespace xe {

Arena::Arena(size_t chunk_size)
    : chunk_size_(chunk_size), head_chunk_(nullptr), active_chunk_(nullptr) {}

Arena::~Arena() {
  Reset();
  Chunk* chunk = head_chunk_;
  while (chunk) {
    Chunk* next = chunk->next;
    delete chunk;
    chunk = next;
  }
  head_chunk_ = nullptr;
}

void Arena::Reset() {
  active_chunk_ = head_chunk_;
  if (active_chunk_) {
    active_chunk_->offset = 0;
  }
}

void Arena::DebugFill() {
  auto chunk = head_chunk_;
  while (chunk) {
    std::memset(chunk->buffer, 0xCD, chunk->capacity);
    chunk = chunk->next;
  }
}

void* Arena::Alloc(size_t size) {
  if (active_chunk_) {
    if (active_chunk_->capacity - active_chunk_->offset < size + 4096) {
      Chunk* next = active_chunk_->next;
      if (!next) {
        assert_true(size < chunk_size_, "need to support larger chunks");
        next = new Chunk(chunk_size_);
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
  return p;
}

void Arena::Rewind(size_t size) { active_chunk_->offset -= size; }

size_t Arena::CalculateSize() {
  size_t total_length = 0;
  Chunk* chunk = head_chunk_;
  while (chunk) {
    total_length += chunk->offset;
    if (chunk == active_chunk_) {
      break;
    }
    chunk = chunk->next;
  }
  return total_length;
}

void* Arena::CloneContents() {
  size_t total_length = CalculateSize();
  auto result = reinterpret_cast<uint8_t*>(malloc(total_length));
  auto p = result;
  Chunk* chunk = head_chunk_;
  while (chunk) {
    std::memcpy(p, chunk->buffer, chunk->offset);
    p += chunk->offset;
    if (chunk == active_chunk_) {
      break;
    }
    chunk = chunk->next;
  }
  return result;
}

void Arena::CloneContents(void* buffer, size_t buffer_length) {
  uint8_t* p = reinterpret_cast<uint8_t*>(buffer);
  Chunk* chunk = head_chunk_;
  while (chunk) {
    std::memcpy(p, chunk->buffer, chunk->offset);
    p += chunk->offset;
    if (chunk == active_chunk_) {
      break;
    }
    chunk = chunk->next;
  }
}

Arena::Chunk::Chunk(size_t chunk_size)
    : next(nullptr), capacity(chunk_size), buffer(0), offset(0) {
  buffer = reinterpret_cast<uint8_t*>(malloc(capacity));
}

Arena::Chunk::~Chunk() {
  if (buffer) {
    free(buffer);
  }
}

}  // namespace xe
