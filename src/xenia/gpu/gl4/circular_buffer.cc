/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/circular_buffer.h"

#include "poly/assert.h"
#include "poly/math.h"
#include "xenia/gpu/gl4/gl4_gpu-private.h"
#include "xenia/gpu/gpu-private.h"

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();
extern "C" WGLEWContext* wglewGetContext();

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

  if (FLAGS_vendor_gl_extensions && GLEW_NV_shader_buffer_load) {
    // To use this bindlessly we must make it resident.
    glMakeNamedBufferResidentNV(buffer_, GL_READ_ONLY);
    glGetNamedBufferParameterui64vNV(buffer_, GL_BUFFER_GPU_ADDRESS_NV,
                                     &gpu_base_);
  }
  return true;
}

void CircularBuffer::Shutdown() {
  if (!buffer_) {
    return;
  }
  glUnmapNamedBuffer(buffer_);
  if (FLAGS_vendor_gl_extensions && GLEW_NV_shader_buffer_load) {
    glMakeNamedBufferNonResidentNV(buffer_);
  }
  glDeleteBuffers(1, &buffer_);
  buffer_ = 0;
}

bool CircularBuffer::CanAcquire(size_t length) {
  size_t aligned_length = poly::round_up(length, alignment_);
  return write_head_ + aligned_length <= capacity_;
}

CircularBuffer::Allocation CircularBuffer::Acquire(size_t length) {
  // Addresses must always be % 256.
  size_t aligned_length = poly::round_up(length, alignment_);
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
  write_head_ += aligned_length;
  return allocation;
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

void CircularBuffer::WaitUntilClean() {
  Flush();
  glFinish();
  write_head_ = 0;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
