/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_CONTEXT_H_
#define XENIA_UI_D3D12_D3D12_CONTEXT_H_

#include <memory>

#include "xenia/ui/d3d12/d3d12_immediate_drawer.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/graphics_context.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Context : public GraphicsContext {
 public:
  ~D3D12Context() override;

  ImmediateDrawer* immediate_drawer() override;

  bool WasLost() override;

  bool BeginSwap() override;
  void EndSwap() override;

  std::unique_ptr<RawImage> Capture() override;

  D3D12Provider& GetD3D12Provider() const {
    return static_cast<D3D12Provider&>(*provider_);
  }

  // The format used by DWM.
  static constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
  ID3D12Resource* GetSwapChainBuffer(uint32_t buffer_index) const {
    return swap_chain_buffers_[buffer_index].Get();
  }
  uint32_t GetSwapChainBackBufferIndex() const {
    return swap_chain_back_buffer_index_;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE GetSwapChainBufferRTV(
      uint32_t buffer_index) const;
  D3D12_CPU_DESCRIPTOR_HANDLE GetSwapChainBackBufferRTV() const {
    return GetSwapChainBufferRTV(GetSwapChainBackBufferIndex());
  }
  void GetSwapChainSize(uint32_t& width, uint32_t& height) const {
    width = swap_chain_width_;
    height = swap_chain_height_;
  }
  // Inside the current BeginSwap/EndSwap pair.
  uint64_t GetSwapCurrentFenceValue() const {
    return swap_fence_current_value_;
  }
  uint64_t GetSwapCompletedFenceValue() const {
    return swap_fence_completed_value_;
  }
  ID3D12GraphicsCommandList* GetSwapCommandList() const {
    return swap_command_list_.Get();
  }

 private:
  friend class D3D12Provider;
  explicit D3D12Context(D3D12Provider* provider, Window* target_window);
  bool Initialize();

 private:
  bool InitializeSwapChainBuffers();
  void Shutdown();

  bool context_lost_ = false;

  static constexpr uint32_t kSwapChainBufferCount = 3;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
  uint32_t swap_chain_width_ = 0, swap_chain_height_ = 0;
  Microsoft::WRL::ComPtr<ID3D12Resource>
      swap_chain_buffers_[kSwapChainBufferCount];
  uint32_t swap_chain_back_buffer_index_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> swap_chain_rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE swap_chain_rtv_heap_start_;

  uint64_t swap_fence_current_value_ = 1;
  uint64_t swap_fence_completed_value_ = 0;
  HANDLE swap_fence_completion_event_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Fence> swap_fence_;

  static constexpr uint32_t kSwapCommandAllocatorCount = 3;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator>
      swap_command_allocators_[kSwapCommandAllocatorCount];
  // Current command allocator is:
  // ((swap_fence_current_value_ + (kSwapCommandAllocatorCount - 1))) %
  //     kSwapCommandAllocatorCount.
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> swap_command_list_;

  Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_device_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> dcomp_target_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> dcomp_visual_;

  std::unique_ptr<D3D12ImmediateDrawer> immediate_drawer_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_CONTEXT_H_
