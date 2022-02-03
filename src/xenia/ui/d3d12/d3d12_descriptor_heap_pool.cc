/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_descriptor_heap_pool.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {

D3D12DescriptorHeapPool::D3D12DescriptorHeapPool(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t page_size)
    : device_(device), type_(type), page_size_(page_size) {}

D3D12DescriptorHeapPool::~D3D12DescriptorHeapPool() { ClearCache(); }

void D3D12DescriptorHeapPool::Reclaim(uint64_t completed_submission_index) {
  while (submitted_first_) {
    if (submitted_first_->last_submission_index > completed_submission_index) {
      break;
    }
    if (writable_last_) {
      writable_last_->next = submitted_first_;
    } else {
      writable_first_ = submitted_first_;
    }
    writable_last_ = submitted_first_;
    submitted_first_ = submitted_first_->next;
    writable_last_->next = nullptr;
  }
  if (!submitted_first_) {
    submitted_last_ = nullptr;
  }
}

void D3D12DescriptorHeapPool::ChangeSubmissionTimeline() {
  // Reclaim all submitted pages.
  if (writable_last_) {
    writable_last_->next = submitted_first_;
  } else {
    writable_first_ = submitted_first_;
  }
  writable_last_ = submitted_last_;
  submitted_first_ = nullptr;
  submitted_last_ = nullptr;

  // Mark all pages as never used yet in the new timeline.
  Page* page = writable_first_;
  while (page) {
    page->last_submission_index = 0;
    page = page->next;
  }
}

void D3D12DescriptorHeapPool::ClearCache() {
  // Not checking current_page_used_ != 0 because asking for 0 descriptors
  // returns a valid heap also - but actually the new heap will be different now
  // and the old one must be unbound since it doesn't exist anymore.
  ++current_heap_index_;
  current_page_used_ = 0;
  while (submitted_first_) {
    auto next = submitted_first_->next;
    delete submitted_first_;
    submitted_first_ = next;
  }
  submitted_last_ = nullptr;
  while (writable_first_) {
    auto next = writable_first_->next;
    delete writable_first_;
    writable_first_ = next;
  }
  writable_last_ = nullptr;
}

uint64_t D3D12DescriptorHeapPool::Request(uint64_t submission_index,
                                          uint64_t previous_heap_index,
                                          uint32_t count_for_partial_update,
                                          uint32_t count_for_full_update,
                                          uint32_t& index_out) {
  assert_true(count_for_partial_update <= count_for_full_update);
  assert_true(count_for_full_update <= page_size_);
  if (count_for_partial_update > count_for_full_update ||
      count_for_full_update > page_size_) {
    return kHeapIndexInvalid;
  }
  assert_true(!current_page_used_ ||
              submission_index >= writable_first_->last_submission_index);
  assert_true(!submitted_last_ ||
              submission_index >= submitted_last_->last_submission_index);
  // If the last full update happened on the current page, a partial update is
  // possible.
  uint32_t count = previous_heap_index == current_heap_index_
                       ? count_for_partial_update
                       : count_for_full_update;
  // Go to the next page if there's not enough free space on the current one,
  // or because the previous page may be outdated. In this case, a full update
  // is necessary.
  if (page_size_ - current_page_used_ < count) {
    // Close the page that was current.
    if (submitted_last_) {
      submitted_last_->next = writable_first_;
    } else {
      submitted_first_ = writable_first_;
    }
    submitted_last_ = writable_first_;
    writable_first_ = writable_first_->next;
    submitted_last_->next = nullptr;
    if (!writable_first_) {
      writable_last_ = nullptr;
    }
    ++current_heap_index_;
    current_page_used_ = 0;
    count = count_for_full_update;
  }
  // Create the page if needed (may be the first call for the page).
  if (!writable_first_) {
    D3D12_DESCRIPTOR_HEAP_DESC new_heap_desc;
    new_heap_desc.Type = type_;
    new_heap_desc.NumDescriptors = page_size_;
    new_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    new_heap_desc.NodeMask = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> new_heap;
    if (FAILED(device_->CreateDescriptorHeap(&new_heap_desc,
                                             IID_PPV_ARGS(&new_heap)))) {
      XELOGE("Failed to create a heap for {} shader-visible descriptors",
             page_size_);
      return kHeapIndexInvalid;
    }
    writable_first_ = new Page;
    writable_first_->heap = new_heap;
    writable_first_->cpu_start = new_heap->GetCPUDescriptorHandleForHeapStart();
    writable_first_->gpu_start = new_heap->GetGPUDescriptorHandleForHeapStart();
    writable_first_->last_submission_index = submission_index;
    writable_first_->next = nullptr;
    writable_last_ = writable_first_;
  }
  writable_first_->last_submission_index = submission_index;
  index_out = current_page_used_;
  current_page_used_ += count;
  return current_heap_index_;
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
