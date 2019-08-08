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

#include "xenia/ui/d3d12/command_list.h"
#include "xenia/ui/d3d12/cpu_fence.h"
#include "xenia/ui/d3d12/d3d12_immediate_drawer.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/graphics_context.h"

#define FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Context : public GraphicsContext {
 public:
  ~D3D12Context() override;

  ImmediateDrawer* immediate_drawer() override;

  bool is_current() override;
  bool MakeCurrent() override;
  void ClearCurrent() override;

  bool WasLost() override { return context_lost_; }

  void BeginSwap() override;
  void EndSwap() override;

  std::unique_ptr<RawImage> Capture() override;

  D3D12Provider* GetD3D12Provider() const {
    return static_cast<D3D12Provider*>(provider_);
  }

  // The count of copies of transient objects (like command lists, dynamic
  // descriptor heaps) that must be kept when rendering with this context.
  static constexpr uint32_t kQueuedFrames = 3;
  // The current absolute frame number.
  uint64_t GetCurrentFrame() { return current_frame_; }
  // The last completed frame - it's fine to destroy objects used in it.
  uint64_t GetLastCompletedFrame() { return last_completed_frame_; }
  uint32_t GetCurrentQueueFrame() { return current_queue_frame_; }
  void AwaitAllFramesCompletion();

  static constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12Resource* GetSwapChainBuffer(uint32_t buffer_index) const {
    return swap_chain_buffers_[buffer_index];
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
  ID3D12GraphicsCommandList* GetSwapCommandList() const {
    return swap_command_lists_[current_queue_frame_]->GetCommandList();
  }

 private:
  friend class D3D12Provider;

  explicit D3D12Context(D3D12Provider* provider, Window* target_window);

 private:
  bool Initialize();
  bool InitializeSwapChainBuffers();
  void Shutdown();

  bool initialized_fully_ = false;

  bool context_lost_ = false;

  uint64_t current_frame_ = 1;
  uint64_t last_completed_frame_ = 0;
  uint32_t current_queue_frame_ = 1;
  std::unique_ptr<CPUFence> fences_[kQueuedFrames] = {};

  static constexpr uint32_t kSwapChainBufferCount = 3;
  IDXGISwapChain3* swap_chain_ = nullptr;
  uint32_t swap_chain_width_ = 0, swap_chain_height_ = 0;
  ID3D12Resource* swap_chain_buffers_[kSwapChainBufferCount] = {};
  uint32_t swap_chain_back_buffer_index_ = 0;
  ID3D12DescriptorHeap* swap_chain_rtv_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE swap_chain_rtv_heap_start_;
  std::unique_ptr<CommandList> swap_command_lists_[kQueuedFrames] = {};

  std::unique_ptr<D3D12ImmediateDrawer> immediate_drawer_ = nullptr;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_CONTEXT_H_
