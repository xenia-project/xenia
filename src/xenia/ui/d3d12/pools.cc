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
#include "xenia/ui/d3d12/d3d12_context.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace ui {
namespace d3d12 {

UploadBufferPool::UploadBufferPool(D3D12Context* context, uint32_t page_size)
    : context_(context), page_size_(page_size) {}

UploadBufferPool::~UploadBufferPool() {
  // Allow mid-frame destruction in cases like device loss.
  if (current_mapping_ != nullptr) {
    // Don't care about the written range - destroying anyway.
    D3D12_RANGE written_range;
    written_range.Begin = 0;
    written_range.End = 0;
    unsent_->buffer->Unmap(0, &written_range);
    current_mapping_ = nullptr;
  }
  current_size_ = 0;
  ClearCache();
}

void UploadBufferPool::BeginFrame() {
  // Recycle submitted pages not used by the GPU anymore.
  uint64_t last_completed_frame = context_->GetLastCompletedFrame();
  while (sent_first_ != nullptr) {
    auto page = sent_first_;
    if (page->frame_sent > last_completed_frame) {
      break;
    }
    sent_first_ = page->next;
    page->next = unsent_;
    unsent_ = page;
  }
  if (sent_first_ == nullptr) {
    sent_last_ = nullptr;
  }

  // Try to create new pages again in this frame if failed in the previous.
  page_creation_failed_ = false;
}

void UploadBufferPool::EndFrame() {
  // If something is written to the current page, mark it as submitted.
  EndPage();
}

void UploadBufferPool::ClearCache() {
  assert(current_size_ == 0);
  while (unsent_ != nullptr) {
    auto next = unsent_->next;
    unsent_->buffer->Release();
    delete unsent_;
    unsent_ = next;
  }
  while (sent_first_ != nullptr) {
    auto next = sent_first_->next;
    sent_first_->buffer->Release();
    delete sent_first_;
    sent_first_ = next;
  }
  sent_last_ = nullptr;
}

uint8_t* UploadBufferPool::RequestFull(
    uint32_t size, ID3D12Resource** buffer_out, uint32_t* offset_out,
    D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out) {
  assert_true(size <= page_size_);
  if (size > page_size_) {
    return nullptr;
  }
  if (page_size_ - current_size_ < size || current_mapping_ == nullptr) {
    // Start a new page if can't fit all the bytes or don't have an open page.
    if (!BeginNextPage()) {
      return nullptr;
    }
  }
  if (buffer_out != nullptr) {
    *buffer_out = unsent_->buffer;
  }
  if (offset_out != nullptr) {
    *offset_out = current_size_;
  }
  if (gpu_address_out != nullptr) {
    if (current_gpu_address_ == 0) {
      current_gpu_address_ = unsent_->buffer->GetGPUVirtualAddress();
    }
    *gpu_address_out = current_gpu_address_ + current_size_;
  }
  uint8_t* mapping = current_mapping_ + current_size_;
  current_size_ += size;
  return mapping;
}

uint8_t* UploadBufferPool::RequestPartial(
    uint32_t size, ID3D12Resource** buffer_out, uint32_t* offset_out,
    uint32_t* size_out, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out) {
  if (current_size_ == page_size_ || current_mapping_ == nullptr) {
    // Start a new page if can't fit any bytes or don't have an open page.
    if (!BeginNextPage()) {
      return nullptr;
    }
  }
  size = std::min(size, page_size_ - current_size_);
  if (buffer_out != nullptr) {
    *buffer_out = unsent_->buffer;
  }
  if (offset_out != nullptr) {
    *offset_out = current_size_;
  }
  if (size_out != nullptr) {
    *size_out = size;
  }
  if (gpu_address_out != nullptr) {
    if (current_gpu_address_ == 0) {
      current_gpu_address_ = unsent_->buffer->GetGPUVirtualAddress();
    }
    *gpu_address_out = current_gpu_address_ + current_size_;
  }
  uint8_t* mapping = current_mapping_ + current_size_;
  current_size_ += size;
  return mapping;
}

void UploadBufferPool::EndPage() {
  if (current_mapping_ != nullptr) {
    D3D12_RANGE written_range;
    written_range.Begin = 0;
    written_range.End = current_size_;
    unsent_->buffer->Unmap(0, &written_range);
    current_mapping_ = nullptr;
  }
  if (current_size_ != 0) {
    auto page = unsent_;
    page->frame_sent = context_->GetCurrentFrame();
    unsent_ = page->next;
    page->next = nullptr;
    if (sent_last_ != nullptr) {
      sent_last_->next = page;
    } else {
      sent_first_ = page;
    }
    sent_last_ = page;
    current_size_ = 0;
  }
}

bool UploadBufferPool::BeginNextPage() {
  EndPage();

  if (page_creation_failed_) {
    return false;
  }

  if (unsent_ == nullptr) {
    auto device = context_->GetD3D12Provider()->GetDevice();
    D3D12_RESOURCE_DESC buffer_desc;
    util::FillBufferResourceDesc(buffer_desc, page_size_,
                                 D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* buffer_resource;
    if (FAILED(device->CreateCommittedResource(
            &util::kHeapPropertiesUpload, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&buffer_resource)))) {
      XELOGE("Failed to create a D3D upload buffer with %u bytes", page_size_);
      page_creation_failed_ = true;
      return false;
    }
    unsent_ = new UploadBuffer;
    unsent_->buffer = buffer_resource;
    unsent_->next = nullptr;
  }

  D3D12_RANGE read_range;
  read_range.Begin = 0;
  read_range.End = 0;
  void* mapping;
  if (FAILED(unsent_->buffer->Map(0, &read_range, &mapping))) {
    XELOGE("Failed to map a D3D upload buffer with %u bytes", page_size_);
    page_creation_failed_ = true;
    return false;
  }
  current_mapping_ = reinterpret_cast<uint8_t*>(mapping);
  current_gpu_address_ = 0;

  return true;
}

DescriptorHeapPool::DescriptorHeapPool(D3D12Context* context,
                                       D3D12_DESCRIPTOR_HEAP_TYPE type,
                                       uint32_t page_size)
    : context_(context), type_(type), page_size_(page_size) {}

DescriptorHeapPool::~DescriptorHeapPool() {
  // Allow mid-frame destruction in cases like device loss.
  current_size_ = 0;
  ClearCache();
}

void DescriptorHeapPool::BeginFrame() {
  // Don't hold old pages if few descriptors are written, also make 0 usable as
  // an invalid page index.
  ++current_page_;

  // Recycle submitted pages not used by the GPU anymore.
  uint64_t last_completed_frame = context_->GetLastCompletedFrame();
  while (sent_first_ != nullptr) {
    auto page = sent_first_;
    if (page->frame_sent > last_completed_frame) {
      break;
    }
    sent_first_ = page->next;
    page->next = unsent_;
    unsent_ = page;
  }
  if (sent_first_ == nullptr) {
    sent_last_ = nullptr;
  }

  // Try to create new pages again in this frame if failed in the previous.
  page_creation_failed_ = false;
}

void DescriptorHeapPool::EndFrame() { EndPage(); }

void DescriptorHeapPool::ClearCache() {
  assert_true(current_size_ == 0);
  while (unsent_ != nullptr) {
    auto next = unsent_->next;
    unsent_->heap->Release();
    delete unsent_;
    unsent_ = next;
  }
  while (sent_first_ != nullptr) {
    auto next = sent_first_->next;
    sent_first_->heap->Release();
    delete sent_first_;
    sent_first_ = next;
  }
  sent_last_ = nullptr;
}

uint64_t DescriptorHeapPool::Request(uint64_t previous_full_update,
                                     uint32_t count_for_partial_update,
                                     uint32_t count_for_full_update,
                                     uint32_t& index_out) {
  assert_true(count_for_partial_update <= count_for_full_update);
  assert_true(count_for_full_update <= page_size_);
  if (count_for_partial_update > count_for_full_update ||
      count_for_full_update > page_size_) {
    return 0;
  }

  if (page_creation_failed_) {
    // Don't touch the page index every call if there was a failure as well.
    return 0;
  }

  // If the last full update happened on the current page, a partial update is
  // possible.
  uint32_t count = previous_full_update == current_page_
                       ? count_for_partial_update
                       : count_for_full_update;

  // Go to the next page if there's not enough free space on the current one,
  // or because the previous page may be outdated. In this case, a full update
  // is necessary.
  if (page_size_ - current_size_ < count) {
    EndPage();
    ++current_page_;
    count = count_for_full_update;
  }

  // Create the page if needed (may be the first call for the page).
  if (unsent_ == nullptr) {
    auto device = context_->GetD3D12Provider()->GetDevice();
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    heap_desc.Type = type_;
    heap_desc.NumDescriptors = page_size_;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap_desc.NodeMask = 0;
    ID3D12DescriptorHeap* heap;
    if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)))) {
      XELOGE("Failed to create a heap for %u shader-visible descriptors",
             page_size_);
      page_creation_failed_ = true;
      return 0;
    }
    unsent_ = new DescriptorHeap;
    unsent_->heap = heap;
    unsent_->next = nullptr;
  }

  // If starting a new page, get the handles to the beginning of it.
  if (current_size_ == 0) {
    current_heap_cpu_start_ =
        unsent_->heap->GetCPUDescriptorHandleForHeapStart();
    current_heap_gpu_start_ =
        unsent_->heap->GetGPUDescriptorHandleForHeapStart();
  }
  index_out = current_size_;
  current_size_ += count;
  return current_page_;
}

void DescriptorHeapPool::EndPage() {
  if (current_size_ != 0) {
    auto page = unsent_;
    page->frame_sent = context_->GetCurrentFrame();
    unsent_ = page->next;
    page->next = nullptr;
    if (sent_last_ != nullptr) {
      sent_last_->next = page;
    } else {
      sent_first_ = page;
    }
    sent_last_ = page;
    current_size_ = 0;
  }
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
