/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_POOLS_H_
#define XENIA_UI_D3D12_POOLS_H_

#include <cstdint>

#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Context;

class UploadBufferPool {
 public:
  UploadBufferPool(D3D12Context* context, uint32_t page_size);
  ~UploadBufferPool();

  void BeginFrame();
  void EndFrame();
  void ClearCache();

  // Request to write data in a single piece, creating a new page if the current
  // one doesn't have enough free space.
  uint8_t* RequestFull(uint32_t size, ID3D12Resource** buffer_out,
                       uint32_t* offset_out,
                       D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out);
  // Request to write data in multiple parts, filling the buffer entirely.
  uint8_t* RequestPartial(uint32_t size, ID3D12Resource** buffer_out,
                          uint32_t* offset_out, uint32_t* size_out,
                          D3D12_GPU_VIRTUAL_ADDRESS* gpu_address_out);

 private:
  D3D12Context* context_;
  uint32_t page_size_;

  void EndPage();
  bool BeginNextPage();

  struct UploadBuffer {
    ID3D12Resource* buffer;
    UploadBuffer* next;
    uint64_t frame_sent;
  };

  // A list of unsent buffers, with the first one being the current.
  UploadBuffer* unsent_ = nullptr;
  // A list of sent buffers, moved to unsent in the beginning of a frame.
  UploadBuffer* sent_first_ = nullptr;
  UploadBuffer* sent_last_ = nullptr;

  uint32_t current_size_ = 0;
  uint8_t* current_mapping_ = nullptr;
  // Not updated until actually requested.
  D3D12_GPU_VIRTUAL_ADDRESS current_gpu_address_ = 0;

  // Reset in the beginning of a frame - don't try and fail to create a new page
  // if failed to create one in the current frame.
  bool page_creation_failed_ = false;
};

class DescriptorHeapPool {
 public:
  DescriptorHeapPool(D3D12Context* context, D3D12_DESCRIPTOR_HEAP_TYPE type,
                     uint32_t page_size);
  ~DescriptorHeapPool();

  void BeginFrame();
  void EndFrame();
  void ClearCache();

  // Because all descriptors for a single draw call must be in the same heap,
  // sometimes all descriptors, rather than only the modified portion of it,
  // needs to be written.
  //
  // This may happen if there's not enough free space even for a partial update
  // in the current heap, or if the heap which contains the unchanged part of
  // the descriptors is outdated.
  //
  // If something uses this pool to do partial updates, it must let this
  // function determine whether a partial update is possible. For this purpose,
  // this function returns a full update number - and it must be called with its
  // previous return value for the set of descriptors it's updating.
  //
  // If this function returns a value that is the same as previous_full_update,
  // a partial update needs to be done - and space for count_for_partial_update
  // is allocated.
  //
  // If it's different, all descriptors must be written again - and space for
  // count_for_full_update is allocated.
  //
  // If 0 is returned, there was an error.
  //
  // This MUST be called even if there's nothing to write in a partial update
  // (with count_for_partial_update being 0), because a full update may still be
  // required.
  uint64_t Request(uint64_t previous_full_update,
                   uint32_t count_for_partial_update,
                   uint32_t count_for_full_update, uint32_t& index_out);

  // The current heap, for binding and actually writing - may be called only
  // after a successful request because before a request, the heap may not exist
  // yet.
  ID3D12DescriptorHeap* GetLastRequestHeap() const { return unsent_->heap; }
  D3D12_CPU_DESCRIPTOR_HANDLE GetLastRequestHeapCPUStart() const {
    return current_heap_cpu_start_;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE GetLastRequestHeapGPUStart() const {
    return current_heap_gpu_start_;
  }

 private:
  D3D12Context* context_;
  D3D12_DESCRIPTOR_HEAP_TYPE type_;
  uint32_t page_size_;

  void EndPage();
  bool BeginNextPage();

  struct DescriptorHeap {
    ID3D12DescriptorHeap* heap;
    DescriptorHeap* next;
    uint64_t frame_sent;
  };

  // A list of unsent heaps, with the first one being the current.
  DescriptorHeap* unsent_ = nullptr;
  // A list of sent heaps, moved to unsent in the beginning of a frame.
  DescriptorHeap* sent_first_ = nullptr;
  DescriptorHeap* sent_last_ = nullptr;

  uint64_t current_page_ = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE current_heap_cpu_start_ = {};
  D3D12_GPU_DESCRIPTOR_HANDLE current_heap_gpu_start_ = {};
  uint32_t current_size_ = 0;

  // Reset in the beginning of a frame - don't try and fail to create a new page
  // if failed to create one in the current frame.
  bool page_creation_failed_ = false;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_POOLS_H_
