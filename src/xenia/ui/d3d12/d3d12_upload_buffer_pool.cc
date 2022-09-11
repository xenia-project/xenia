/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace ui {
namespace d3d12 {

// Align to D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT not to waste any space if
// it's smaller (the size of the heap backing the buffer will be aligned to
// D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT anyway).
D3D12UploadBufferPool::D3D12UploadBufferPool(const D3D12Provider& provider,
                                             size_t page_size)
    : GraphicsUploadBufferPool(xe::align(
          page_size, size_t(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT))),
      provider_(provider) {}

uint8_t* D3D12UploadBufferPool::Request(
    uint64_t submission_index, size_t size, size_t alignment,
    ID3D12Resource** buffer_out, size_t* offset_out,
    D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out) {
  size_t offset;
  const D3D12Page* page =
      static_cast<const D3D12Page*>(GraphicsUploadBufferPool::Request(
          submission_index, size, alignment, offset));
  if (!page) {
    return nullptr;
  }
  if (buffer_out) {
    *buffer_out = page->buffer_.Get();
  }
  if (offset_out) {
    *offset_out = offset;
  }
  if (gpu_address_out) {
    *gpu_address_out = page->gpu_address_ + offset;
  }
  return reinterpret_cast<uint8_t*>(page->mapping_) + offset;
}

uint8_t* D3D12UploadBufferPool::RequestPartial(
    uint64_t submission_index, size_t size, size_t alignment,
    ID3D12Resource** buffer_out, size_t* offset_out, size_t* size_out,
    D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out) {
  size_t offset, size_obtained;
  const D3D12Page* page =
      static_cast<const D3D12Page*>(GraphicsUploadBufferPool::RequestPartial(
          submission_index, size, alignment, offset, size_obtained));
  if (!page) {
    return nullptr;
  }
  if (buffer_out) {
    *buffer_out = page->buffer_.Get();
  }
  if (offset_out) {
    *offset_out = offset;
  }
  if (size_out) {
    *size_out = size_obtained;
  }
  if (gpu_address_out) {
    *gpu_address_out = page->gpu_address_ + offset;
  }
  return reinterpret_cast<uint8_t*>(page->mapping_) + offset;
}

GraphicsUploadBufferPool::Page*
D3D12UploadBufferPool::CreatePageImplementation() {
  D3D12_RESOURCE_DESC buffer_desc;
  util::FillBufferResourceDesc(buffer_desc, page_size_,
                               D3D12_RESOURCE_FLAG_NONE);
  Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

  if (!provider_.CreateUploadResource(
          provider_.GetHeapFlagCreateNotZeroed(), &buffer_desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, IID_PPV_ARGS(&buffer))) {
    XELOGE("Failed to create a D3D upload buffer with {} bytes", page_size_);
    return nullptr;
  }
  D3D12_RANGE read_range;
  read_range.Begin = 0;
  read_range.End = 0;
  void* mapping;
  if (FAILED(buffer->Map(0, &read_range, &mapping))) {
    XELOGE("Failed to map a D3D upload buffer with {} bytes", page_size_);
    buffer->Release();
    return nullptr;
  }
  // Unmapping will be done implicitly when the resource is destroyed.
  return new D3D12Page(buffer.Get(), mapping);
}

D3D12UploadBufferPool::D3D12Page::D3D12Page(ID3D12Resource* buffer,
                                            void* mapping)
    : buffer_(buffer), mapping_(mapping) {
  gpu_address_ = buffer_->GetGPUVirtualAddress();
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
