/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_DESCRIPTOR_HEAP_POOL_H_
#define XENIA_UI_D3D12_D3D12_DESCRIPTOR_HEAP_POOL_H_

#include <cstdint>

#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace ui {
namespace d3d12 {

// Submission index is the fence value or a value derived from it (if reclaiming
// less often than once per fence value, for instance).

class D3D12DescriptorHeapPool {
 public:
  static constexpr uint64_t kHeapIndexInvalid = UINT64_MAX;

  D3D12DescriptorHeapPool(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                          uint32_t page_size);
  ~D3D12DescriptorHeapPool();

  void Reclaim(uint64_t completed_submission_index);
  void ChangeSubmissionTimeline();
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
  // this function returns the heap reset index - and it must be called with its
  // previous return value for the set of descriptors it's updating.
  //
  // If this function returns a value that is the same as previous_heap_index, a
  // partial update needs to be done - and space for count_for_partial_update is
  // allocated.
  //
  // If it's different, all descriptors must be written again - and space for
  // count_for_full_update is allocated.
  //
  // If kHeapIndexInvalid is returned, there was an error.
  //
  // This MUST be called even if there's nothing to write in a partial update
  // (with count_for_partial_update being 0), because a full update may still be
  // required.
  uint64_t Request(uint64_t submission_index, uint64_t previous_heap_index,
                   uint32_t count_for_partial_update,
                   uint32_t count_for_full_update, uint32_t& index_out);

  // The current heap, for binding and actually writing - may be called only
  // after a successful request because before a request, the heap may not exist
  // yet.
  ID3D12DescriptorHeap* GetLastRequestHeap() const {
    return writable_first_->heap.Get();
  }
  D3D12_CPU_DESCRIPTOR_HANDLE GetLastRequestHeapCPUStart() const {
    return writable_first_->cpu_start;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE GetLastRequestHeapGPUStart() const {
    return writable_first_->gpu_start;
  }

 private:
  ID3D12Device* device_;
  D3D12_DESCRIPTOR_HEAP_TYPE type_;
  uint32_t page_size_;

  struct Page {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start;
    uint64_t last_submission_index;
    Page* next;
  };

  // A list of heap with free space, with the first buffer being the one
  // currently being filled.
  Page* writable_first_ = nullptr;
  Page* writable_last_ = nullptr;
  // A list of full heaps that can be reclaimed when the GPU doesn't use them
  // anymore.
  Page* submitted_first_ = nullptr;
  Page* submitted_last_ = nullptr;
  // Monotonically increased when a new request is going to a different
  // ID3D12DescriptorHeap than the one that may be bound currently. See Request
  // for more information.
  uint64_t current_heap_index_ = 0;
  uint32_t current_page_used_ = 0;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_DESCRIPTOR_HEAP_POOL_H_
