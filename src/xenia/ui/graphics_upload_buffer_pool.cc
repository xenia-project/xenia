/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/graphics_upload_buffer_pool.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace ui {

GraphicsUploadBufferPool::~GraphicsUploadBufferPool() { ClearCache(); }

void GraphicsUploadBufferPool::Reclaim(uint64_t completed_submission_index) {
  while (submitted_first_) {
    if (submitted_first_->last_submission_index_ > completed_submission_index) {
      break;
    }
    if (writable_last_) {
      writable_last_->next_ = submitted_first_;
    } else {
      writable_first_ = submitted_first_;
    }
    writable_last_ = submitted_first_;
    submitted_first_ = submitted_first_->next_;
    writable_last_->next_ = nullptr;
  }
  if (!submitted_first_) {
    submitted_last_ = nullptr;
  }
}

void GraphicsUploadBufferPool::ChangeSubmissionTimeline() {
  // Reclaim all submitted pages.
  if (writable_last_) {
    writable_last_->next_ = submitted_first_;
  } else {
    writable_first_ = submitted_first_;
  }
  writable_last_ = submitted_last_;
  submitted_first_ = nullptr;
  submitted_last_ = nullptr;

  // Mark all pages as never used yet in the new timeline.
  Page* page = writable_first_;
  while (page) {
    page->last_submission_index_ = 0;
    page = page->next_;
  }
}

void GraphicsUploadBufferPool::ClearCache() {
  // Called from the destructor - must not call virtual functions here.
  current_page_flushed_ = 0;
  current_page_used_ = 0;
  while (submitted_first_) {
    Page* next_ = submitted_first_->next_;
    delete submitted_first_;
    submitted_first_ = next_;
  }
  submitted_last_ = nullptr;
  while (writable_first_) {
    Page* next_ = writable_first_->next_;
    delete writable_first_;
    writable_first_ = next_;
  }
  writable_last_ = nullptr;
}

GraphicsUploadBufferPool::Page::~Page() {}

void GraphicsUploadBufferPool::FlushWrites() {
  if (current_page_flushed_ >= current_page_used_) {
    return;
  }
  assert_not_null(writable_first_);
  FlushPageWrites(writable_first_, current_page_flushed_,
                  current_page_used_ - current_page_flushed_);
  current_page_flushed_ = current_page_used_;
}

GraphicsUploadBufferPool::Page* GraphicsUploadBufferPool::Request(
    uint64_t submission_index, size_t size, size_t alignment,
    size_t& offset_out) {
  alignment = std::max(alignment, size_t(1));
  assert_true(xe::is_pow2(alignment));
  size = xe::align(size, alignment);
  assert_true(size <= page_size_);
  if (size > page_size_) {
    return nullptr;
  }
  assert_true(!current_page_used_ ||
              submission_index >= writable_first_->last_submission_index_);
  assert_true(!submitted_last_ ||
              submission_index >= submitted_last_->last_submission_index_);
  size_t current_page_used_aligned = xe::align(current_page_used_, alignment);
  if (current_page_used_aligned + size > page_size_ || !writable_first_) {
    // Start a new page if can't fit all the bytes or don't have an open page.
    if (writable_first_) {
      // Close the page that was current.
      FlushWrites();
      if (submitted_last_) {
        submitted_last_->next_ = writable_first_;
      } else {
        submitted_first_ = writable_first_;
      }
      submitted_last_ = writable_first_;
      writable_first_ = writable_first_->next_;
      submitted_last_->next_ = nullptr;
      if (!writable_first_) {
        writable_last_ = nullptr;
      }
    }
    if (!writable_first_) {
      // Create a new page if none available.
      writable_first_ = CreatePageImplementation();
      if (!writable_first_) {
        // Failed to create.
        return nullptr;
      }
      writable_first_->last_submission_index_ = submission_index;
      writable_first_->next_ = nullptr;
      writable_last_ = writable_first_;
      // After CreatePageImplementation (more specifically, the first successful
      // call), page_size_ may grow - but this doesn't matter here.
    }
    current_page_used_ = 0;
    current_page_used_aligned = 0;
    current_page_flushed_ = 0;
  }
  writable_first_->last_submission_index_ = submission_index;
  offset_out = current_page_used_aligned;
  current_page_used_ = current_page_used_aligned + size;
  return writable_first_;
}

GraphicsUploadBufferPool::Page* GraphicsUploadBufferPool::RequestPartial(
    uint64_t submission_index, size_t size, size_t alignment,
    size_t& offset_out, size_t& size_out) {
  alignment = std::max(alignment, size_t(1));
  assert_true(xe::is_pow2(alignment));
  size = xe::align(size, alignment);
  size = std::min(size, page_size_);
  size_t current_page_used_aligned = xe::align(current_page_used_, alignment);
  if (current_page_used_aligned + alignment <= page_size_) {
    size = std::min(
        size, (page_size_ - current_page_used_aligned) & ~(alignment - 1));
  }
  Page* page = Request(submission_index, size, alignment, offset_out);
  if (!page) {
    return nullptr;
  }
  size_out = size;
  return page;
}

void GraphicsUploadBufferPool::FlushPageWrites(Page* page, size_t offset,
                                               size_t size) {}

}  // namespace ui
}  // namespace xe
