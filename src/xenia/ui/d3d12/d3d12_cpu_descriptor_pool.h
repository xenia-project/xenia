/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_CPU_DESCRIPTOR_POOL_H_
#define XENIA_UI_D3D12_D3D12_CPU_DESCRIPTOR_POOL_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace ui {
namespace d3d12 {

// Single-descriptor pool with reference counting and unique ownership of
// allocations, safe to use in environments where the order of releasing of the
// descriptor heap and of allocated descriptors is undefined.
class D3D12CpuDescriptorPool
    : public std::enable_shared_from_this<D3D12CpuDescriptorPool> {
 public:
  class Descriptor {
   public:
    Descriptor() = default;
    // shared_ptr to ensure correct release order with between render targets
    // and descriptor pools.
    Descriptor(std::shared_ptr<D3D12CpuDescriptorPool> pool, size_t index)
        : pool_(pool), index_(index) {}
    // Owns a descriptor in the pool exclusively.
    Descriptor(const Descriptor& descriptor) = delete;
    Descriptor& operator=(const Descriptor& descriptor) = delete;
    Descriptor(Descriptor&& descriptor) {
      pool_ = std::move(descriptor.pool_);
      index_ = descriptor.index_;
    }
    Descriptor& operator=(Descriptor&& descriptor) {
      if (IsValid() && pool_ == descriptor.pool_ &&
          index_ == descriptor.index_) {
        // If moving to self, don't free.
        return *this;
      }
      Free();
      pool_ = std::move(descriptor.pool_);
      index_ = descriptor.index_;
      return *this;
    }
    ~Descriptor() { Free(); }
    void Free() {
      if (!pool_) {
        return;
      }
      pool_->FreeDescriptor(index_);
      pool_.reset();
    }
    bool IsValid() const { return pool_.get() != nullptr; }
    operator bool() const { return IsValid(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetHandle() const {
      assert_true(IsValid());
      return pool_->GetHandle(index_);
    }

   private:
    std::shared_ptr<D3D12CpuDescriptorPool> pool_;
    size_t index_ = 0;
  };

  D3D12CpuDescriptorPool(const ui::d3d12::D3D12Provider& provider,
                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                         uint32_t heap_size_log2)
      : provider_(provider), type_(type), heap_size_log2_(heap_size_log2) {
    assert_true(heap_size_log2 <= 31);
  }
  D3D12CpuDescriptorPool(const D3D12CpuDescriptorPool& pool) = delete;
  D3D12CpuDescriptorPool& operator=(const D3D12CpuDescriptorPool& pool) =
      delete;
  // No point in moving, created only via make_shared.
  D3D12CpuDescriptorPool(D3D12CpuDescriptorPool&& pool) = delete;
  D3D12CpuDescriptorPool& operator=(D3D12CpuDescriptorPool&& pool) = delete;
  ~D3D12CpuDescriptorPool() {
    for (ID3D12DescriptorHeap* heap : heaps_) {
      heap->Release();
    }
  }

  Descriptor AllocateDescriptor();

 private:
  void FreeDescriptor(size_t index) { freed_indices_.push_back(index); }

  D3D12_CPU_DESCRIPTOR_HANDLE GetHandle(size_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start =
        heaps_[index >> heap_size_log2_]->GetCPUDescriptorHandleForHeapStart();
    uint32_t heap_local_index =
        uint32_t(index & ((uint32_t(1) << heap_size_log2_) - 1));
    return provider_.OffsetDescriptor(type_, heap_start, heap_local_index);
  }

  const ui::d3d12::D3D12Provider& provider_;
  D3D12_DESCRIPTOR_HEAP_TYPE type_;
  uint32_t heap_size_log2_;
  std::vector<ID3D12DescriptorHeap*> heaps_;
  std::vector<size_t> freed_indices_;
  uint32_t last_heap_allocated_ = 0;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_CPU_DESCRIPTOR_POOL_H_
