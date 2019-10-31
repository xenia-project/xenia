/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/pools.h"

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace ui {
namespace d3d12 {

UploadBufferPool::UploadBufferPool(ID3D12Device* device, uint32_t page_size)
    : device_(device), page_size_(page_size) {}

UploadBufferPool::~UploadBufferPool() { ClearCache(); }

void UploadBufferPool::Reclaim(uint64_t completed_submission_index) {
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

void UploadBufferPool::ClearCache() {
  current_page_used_ = 0;
  // Deleting anyway, so assuming data not needed anymore.
  D3D12_RANGE written_range;
  written_range.Begin = 0;
  written_range.End = 0;
  while (submitted_first_) {
    auto next = submitted_first_->next;
    submitted_first_->buffer->Unmap(0, &written_range);
    submitted_first_->buffer->Release();
    delete submitted_first_;
    submitted_first_ = next;
  }
  submitted_last_ = nullptr;
  while (writable_first_) {
    auto next = writable_first_->next;
    writable_first_->buffer->Unmap(0, &written_range);
    writable_first_->buffer->Release();
    delete writable_first_;
    writable_first_ = next;
  }
  writable_last_ = nullptr;
}

uint8_t* UploadBufferPool::Request(uint64_t submission_index, uint32_t size,
                                   ID3D12Resource** buffer_out,
                                   uint32_t* offset_out,
                                   D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out) {
  assert_true(size <= page_size_);
  if (size > page_size_) {
    return nullptr;
  }
  assert_true(!current_page_used_ ||
              submission_index >= writable_first_->last_submission_index);
  assert_true(!submitted_last_ ||
              submission_index >= submitted_last_->last_submission_index);
  if (page_size_ - current_page_used_ < size || !writable_first_) {
    // Start a new page if can't fit all the bytes or don't have an open page.
    if (writable_first_) {
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
    }
    if (!writable_first_) {
      // Create a new page if none available.
      D3D12_RESOURCE_DESC new_buffer_desc;
      util::FillBufferResourceDesc(new_buffer_desc, page_size_,
                                   D3D12_RESOURCE_FLAG_NONE);
      ID3D12Resource* new_buffer;
      if (FAILED(device_->CreateCommittedResource(
              &util::kHeapPropertiesUpload, D3D12_HEAP_FLAG_NONE,
              &new_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&new_buffer)))) {
        XELOGE("Failed to create a D3D upload buffer with %u bytes",
               page_size_);
        return nullptr;
      }
      D3D12_RANGE read_range;
      read_range.Begin = 0;
      read_range.End = 0;
      void* new_buffer_mapping;
      if (FAILED(new_buffer->Map(0, &read_range, &new_buffer_mapping))) {
        XELOGE("Failed to map a D3D upload buffer with %u bytes", page_size_);
        new_buffer->Release();
        return nullptr;
      }
      writable_first_ = new Page;
      writable_first_->buffer = new_buffer;
      writable_first_->gpu_address = new_buffer->GetGPUVirtualAddress();
      writable_first_->mapping = new_buffer_mapping;
      writable_first_->last_submission_index = submission_index;
      writable_first_->next = nullptr;
      writable_last_ = writable_first_;
    }
    current_page_used_ = 0;
  }
  writable_first_->last_submission_index = submission_index;
  if (buffer_out) {
    *buffer_out = writable_first_->buffer;
  }
  if (offset_out) {
    *offset_out = current_page_used_;
  }
  if (gpu_address_out) {
    *gpu_address_out = writable_first_->gpu_address + current_page_used_;
  }
  uint8_t* mapping =
      reinterpret_cast<uint8_t*>(writable_first_->mapping) + current_page_used_;
  current_page_used_ += size;
  return mapping;
}

uint8_t* UploadBufferPool::RequestPartial(
    uint64_t submission_index, uint32_t size, ID3D12Resource** buffer_out,
    uint32_t* offset_out, uint32_t* size_out,
    D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out) {
  size = std::min(size, page_size_);
  if (current_page_used_ < page_size_) {
    size = std::min(size, page_size_ - current_page_used_);
  }
  uint8_t* mapping =
      Request(submission_index, size, buffer_out, offset_out, gpu_address_out);
  if (!mapping) {
    return nullptr;
  }
  if (size_out) {
    *size_out = size;
  }
  return mapping;
}

constexpr uint64_t DescriptorHeapPool::kHeapIndexInvalid;

DescriptorHeapPool::DescriptorHeapPool(ID3D12Device* device,
                                       D3D12_DESCRIPTOR_HEAP_TYPE type,
                                       uint32_t page_size)
    : device_(device), type_(type), page_size_(page_size) {}

DescriptorHeapPool::~DescriptorHeapPool() { ClearCache(); }

void DescriptorHeapPool::Reclaim(uint64_t completed_submission_index) {
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

void DescriptorHeapPool::ClearCache() {
  // Not checking current_page_used_ != 0 because asking for 0 descriptors
  // returns a valid heap also - but actually the new heap will be different now
  // and the old one must be unbound since it doesn't exist anymore.
  ++current_heap_index_;
  current_page_used_ = 0;
  while (submitted_first_) {
    auto next = submitted_first_->next;
    submitted_first_->heap->Release();
    delete submitted_first_;
    submitted_first_ = next;
  }
  submitted_last_ = nullptr;
  while (writable_first_) {
    auto next = writable_first_->next;
    writable_first_->heap->Release();
    delete writable_first_;
    writable_first_ = next;
  }
  writable_last_ = nullptr;
}

uint64_t DescriptorHeapPool::Request(uint64_t submission_index,
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
    ID3D12DescriptorHeap* new_heap;
    if (FAILED(device_->CreateDescriptorHeap(&new_heap_desc,
                                             IID_PPV_ARGS(&new_heap)))) {
      XELOGE("Failed to create a heap for %u shader-visible descriptors",
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
