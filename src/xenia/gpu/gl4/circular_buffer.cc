/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/circular_buffer.h>

#include <poly/assert.h>
#include <poly/math.h>
#include <xenia/gpu/gpu-private.h>

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();
extern "C" WGLEWContext* wglewGetContext();

CircularBuffer::CircularBuffer(size_t capacity)
    : capacity_(capacity),
      write_head_(0),
      buffer_(0),
      gpu_base_(0),
      host_base_(nullptr) {}

CircularBuffer::~CircularBuffer() {
  glUnmapNamedBuffer(buffer_);
  glDeleteBuffers(1, &buffer_);
}

bool CircularBuffer::Initialize() {
  glCreateBuffers(1, &buffer_);
  glNamedBufferStorage(buffer_, capacity_, nullptr,
                       GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT);
  host_base_ = reinterpret_cast<uint8_t*>(glMapNamedBufferRange(
      buffer_, 0, capacity_, GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT |
                                 GL_MAP_UNSYNCHRONIZED_BIT |
                                 GL_MAP_PERSISTENT_BIT));
  assert_not_null(host_base_);
  if (!host_base_) {
    return false;
  }
  glMakeNamedBufferResidentNV(buffer_, GL_WRITE_ONLY);
  glGetNamedBufferParameterui64vNV(buffer_, GL_BUFFER_GPU_ADDRESS_NV,
                                   &gpu_base_);
  return true;
}

CircularBuffer::Allocation CircularBuffer::Acquire(size_t length) {
  // Addresses must always be % 256.
  length = poly::round_up(length, 256);

  assert_true(length <= capacity_, "Request too large");

  if (write_head_ + length > capacity_) {
    // Flush and wait.
    WaitUntilClean();
  }

  Allocation allocation;
  allocation.host_ptr = host_base_ + write_head_;
  allocation.gpu_ptr = gpu_base_ + write_head_;
  allocation.length = length;
  write_head_ += length;
  return allocation;
}

void CircularBuffer::Commit(Allocation allocation) {
  glFlushMappedNamedBufferRange(buffer_, allocation.gpu_ptr - gpu_base_,
                                allocation.length);
}

void CircularBuffer::WaitUntilClean() {
  glFinish();
  write_head_ = 0;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
