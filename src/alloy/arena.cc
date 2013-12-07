/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/arena.h>

using namespace alloy;


Arena::Arena(size_t chunk_size) :
    chunk_size_(chunk_size),
    head_chunk_(NULL), active_chunk_(NULL) {
}

Arena::~Arena() {
  Reset();
  Chunk* chunk = head_chunk_;
  while (chunk) {
    Chunk* next = chunk->next;
    delete chunk;
    chunk = next;
  }
  head_chunk_ = NULL;
}

void Arena::Reset() {
  active_chunk_ = head_chunk_;
  if (active_chunk_) {
    active_chunk_->offset = 0;
  }
}

void* Arena::Alloc(size_t size) {
  if (active_chunk_) {
    if (active_chunk_->capacity - active_chunk_->offset < size) {
      Chunk* next = active_chunk_->next;
      if (!next) {
        XEASSERT(size < size); // need to support larger chunks
        next = new Chunk(chunk_size_);
        active_chunk_->next = next;
      }
      active_chunk_ = next;
    }
  } else {
    head_chunk_ = active_chunk_ = new Chunk(chunk_size_);
  }

  uint8_t* p = active_chunk_->buffer + active_chunk_->offset;
  active_chunk_->offset += size;
  return p;
}

void* Arena::CloneContents() {
  size_t total_length = 0;
  Chunk* chunk = head_chunk_;
  while (chunk) {
    total_length += chunk->offset;
    if (chunk == active_chunk_) {
      break;
    }
    chunk = chunk->next;
  }
  void* result = xe_malloc(total_length);
  uint8_t* p = (uint8_t*)result;
  chunk = head_chunk_;
  while (chunk) {
    xe_copy_struct(p, chunk->buffer, chunk->offset);
    p += chunk->offset;
    if (chunk == active_chunk_) {
      break;
    }
    chunk = chunk->next;
  }
  return result;
}

Arena::Chunk::Chunk(size_t chunk_size) :
    next(NULL),
    capacity(chunk_size), buffer(0), offset(0) {
  buffer = (uint8_t*)xe_malloc(capacity);
}

Arena::Chunk::~Chunk() {
  if (buffer) {
    xe_free(buffer);
  }
}
