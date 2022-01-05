/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GRAPHICS_UPLOAD_BUFFER_POOL_H_
#define XENIA_UI_GRAPHICS_UPLOAD_BUFFER_POOL_H_

#include <cstddef>
#include <cstdint>

#include "xenia/base/literals.h"

namespace xe {
namespace ui {

using namespace xe::literals;

// Submission index is the fence value or a value derived from it (if reclaiming
// less often than once per fence value, for instance).

class GraphicsUploadBufferPool {
 public:
  // Taken from the Direct3D 12 MiniEngine sample (LinearAllocator
  // kCpuAllocatorPageSize). Large enough for most cases.
  static constexpr size_t kDefaultPageSize = 2_MiB;

  virtual ~GraphicsUploadBufferPool();

  void Reclaim(uint64_t completed_submission_index);
  void ClearCache();

  // Should be called before submitting anything using this pool, unless the
  // implementation doesn't require explicit flushing.
  void FlushWrites();

 protected:
  // Extended by the implementation.
  struct Page {
    virtual ~Page();
    uint64_t last_submission_index_;
    Page* next_;
  };

  GraphicsUploadBufferPool(size_t page_size) : page_size_(page_size) {}

  // Request to write data in a single piece, creating a new page if the current
  // one doesn't have enough free space.
  Page* Request(uint64_t submission_index, size_t size, size_t alignment,
                size_t& offset_out);
  // Request to write data in multiple parts, filling the buffer entirely.
  Page* RequestPartial(uint64_t submission_index, size_t size, size_t alignment,
                       size_t& offset_out, size_t& size_out);

  virtual Page* CreatePageImplementation() = 0;

  virtual void FlushPageWrites(Page* page, size_t offset, size_t size);

  // May be increased by the implementation on creation or on first allocation
  // to avoid wasting space if the real allocation turns out to be bigger than
  // the specified page size.
  size_t page_size_;

  // A list of buffers with free space, with the first buffer being the one
  // currently being filled.
  Page* writable_first_ = nullptr;
  Page* writable_last_ = nullptr;
  // A list of full buffers that can be reclaimed when the GPU doesn't use them
  // anymore.
  Page* submitted_first_ = nullptr;
  Page* submitted_last_ = nullptr;

  size_t current_page_used_ = 0;
  size_t current_page_flushed_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GRAPHICS_UPLOAD_BUFFER_POOL_H_
