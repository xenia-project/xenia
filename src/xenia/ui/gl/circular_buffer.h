/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_CIRCULAR_BUFFER_H_
#define XENIA_UI_GL_CIRCULAR_BUFFER_H_

#include <unordered_map>

#include "xenia/ui/gl/gl.h"

namespace xe {
namespace ui {
namespace gl {

// TODO(benvanik): uh, make this circular.
// TODO(benvanik): fences to prevent this from ever flushing.
class CircularBuffer {
 public:
  CircularBuffer(size_t capacity, size_t alignment = 256);
  ~CircularBuffer();

  struct Allocation {
    void* host_ptr;
    GLuint64 gpu_ptr;
    size_t offset;
    size_t length;
    size_t aligned_length;
    uint64_t cache_key;  // 0 if caching disabled.
  };

  bool Initialize();
  void Shutdown();

  GLuint handle() const { return buffer_; }
  GLuint64 gpu_handle() const { return gpu_base_; }
  size_t capacity() const { return capacity_; }

  bool CanAcquire(size_t length);
  Allocation Acquire(size_t length);
  bool AcquireCached(uint32_t key, size_t length, Allocation* out_allocation);
  void Discard(Allocation allocation);
  void Commit(Allocation allocation);
  void Flush();
  void ClearCache();

  void WaitUntilClean();

 private:
  size_t capacity_;
  size_t alignment_;
  uintptr_t write_head_;
  uintptr_t dirty_start_;
  uintptr_t dirty_end_;
  GLuint buffer_;
  GLuint64 gpu_base_;
  uint8_t* host_base_;

  std::unordered_map<uint64_t, uintptr_t> allocation_cache_;
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_CIRCULAR_BUFFER_H_
