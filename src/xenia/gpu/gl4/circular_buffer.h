/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_CIRCULAR_BUFFER_H_
#define XENIA_GPU_GL4_CIRCULAR_BUFFER_H_

#include <xenia/gpu/gl4/gl_context.h>

namespace xe {
namespace gpu {
namespace gl4 {

// TODO(benvanik): uh, make this circular.
// TODO(benvanik): fences to prevent this from ever flushing.
class CircularBuffer {
 public:
  CircularBuffer(size_t capacity);
  ~CircularBuffer();

  struct Allocation {
    void* host_ptr;
    GLuint64 gpu_ptr;
    size_t length;
  };

  bool Initialize();

  GLuint handle() const { return buffer_; }

  Allocation Acquire(size_t length);
  void Commit(Allocation allocation);

  void WaitUntilClean();

 private:
  size_t capacity_;
  uintptr_t write_head_;
  GLuint buffer_;
  GLuint64 gpu_base_;
  uint8_t* host_base_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_CIRCULAR_BUFFER_H_
