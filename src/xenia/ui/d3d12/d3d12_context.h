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

#include "xenia/ui/d3d12/cpu_fence.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/graphics_context.h"

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

  std::unique_ptr<RawImage> Capture() override;

  // The count of copies of transient objects (like command lists, dynamic
  // descriptor heaps) that must be kept when rendering with this context.
  static constexpr uint32_t kFrameQueueLength = 3;

 private:
  friend class D3D12Provider;

  explicit D3D12Context(D3D12Provider* provider, Window* target_window);

 private:
  bool Initialize();
  void Shutdown();

  bool initialized_fully_ = false;

  bool context_lost_ = false;

  std::unique_ptr<CPUFence> fences_[kFrameQueueLength] = {};

  static constexpr uint32_t kSwapChainBufferCount = 3;
  IDXGISwapChain3* swap_chain_ = nullptr;
  ID3D12Resource* swap_chain_buffers_[kSwapChainBufferCount] = {};

  std::unique_ptr<D3D12ImmediateDrawer> immediate_drawer_ = nullptr;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_CONTEXT_H_
