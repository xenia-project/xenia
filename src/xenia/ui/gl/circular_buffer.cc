/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/circular_buffer.h"

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace ui {
namespace gl {

CircularBuffer::CircularBuffer(size_t capacity, size_t alignment)
    : capacity_(capacity),
      alignment_(alignment),
      write_head_(0),
      dirty_start_(UINT64_MAX),
      dirty_end_(0),
      buffer_(0),
      gpu_base_(0),
      host_base_(nullptr) {}

CircularBuffer::~CircularBuffer() { Shutdown(); }

bool CircularBuffer::Initialize() {
  glCreateBuffers(1, &buffer_);
  glNamedBufferStorage(buffer_, capacity_, nullptr,
                       GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT);
  host_base_ = reinterpret_cast<uint8_t*>(glMapNamedBufferRange(
      buffer_, 0, capacity_,
      GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT | GL_MAP_PERSISTENT_BIT));
  assert_not_null(host_base_);
  if (!host_base_) {
    return false;
  }

  return true;
}

void CircularBuffer::Shutdown() {
  if (!buffer_) {
    return;
  }
  glUnmapNamedBuffer(buffer_);
  glDeleteBuffers(1, &buffer_);
  buffer_ = 0;
}

bool CircularBuffer::CanAcquire(size_t length) {
  size_t aligned_length = xe::round_up(length, alignment_);
  return write_head_ + aligned_length <= capacity_;
}

CircularBuffer::Allocation CircularBuffer::Acquire(size_t length) {
  // Addresses must always be % 256.
  size_t aligned_length = xe::round_up(length, alignment_);
  assert_true(aligned_length <= capacity_, "Request too large");
  if (write_head_ + aligned_length > capacity_) {
    // Flush and wait.
    WaitUntilClean();
  }

  Allocation allocation;
  allocation.host_ptr = host_base_ + write_head_;
  allocation.gpu_ptr = gpu_base_ + write_head_;
  allocation.offset = write_head_;
  allocation.length = length;
  allocation.aligned_length = aligned_length;
  allocation.cache_key = 0;
  write_head_ += aligned_length;
  return allocation;
}

bool CircularBuffer::AcquireCached(uint32_t key, size_t length,
                                   Allocation* out_allocation) {
  uint64_t full_key = key | (length << 32);
  auto it = allocation_cache_.find(full_key);
  if (it != allocation_cache_.end()) {
    uintptr_t write_head = it->second;
    size_t aligned_length = xe::round_up(length, alignment_);
    out_allocation->host_ptr = host_base_ + write_head;
    out_allocation->gpu_ptr = gpu_base_ + write_head;
    out_allocation->offset = write_head;
    out_allocation->length = length;
    out_allocation->aligned_length = aligned_length;
    out_allocation->cache_key = full_key;
    return true;
  } else {
    *out_allocation = Acquire(length);
    out_allocation->cache_key = full_key;
    return false;
  }
}

void CircularBuffer::Discard(Allocation allocation) {
  write_head_ -= allocation.aligned_length;
}

void CircularBuffer::Commit(Allocation allocation) {
  uintptr_t start = allocation.gpu_ptr - gpu_base_;
  uintptr_t end = start + allocation.aligned_length;
  dirty_start_ = std::min(dirty_start_, start);
  dirty_end_ = std::max(dirty_end_, end);
  assert_true(dirty_end_ <= capacity_);
  if (allocation.cache_key) {
    allocation_cache_.insert({allocation.cache_key, allocation.offset});
  }
}

void CircularBuffer::Flush() {
  if (dirty_start_ == dirty_end_ || dirty_start_ == UINT64_MAX) {
    return;
  }
  glFlushMappedNamedBufferRange(buffer_, dirty_start_,
                                dirty_end_ - dirty_start_);
  dirty_start_ = UINT64_MAX;
  dirty_end_ = 0;
}

void CircularBuffer::ClearCache() { allocation_cache_.clear(); }

void CircularBuffer::WaitUntilClean() {
  Flush();
  glFinish();
  write_head_ = 0;
  ClearCache();
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
