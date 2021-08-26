/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_context.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_immediate_drawer.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_util.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace d3d12 {

D3D12Context::D3D12Context(D3D12Provider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

D3D12Context::~D3D12Context() { Shutdown(); }

bool D3D12Context::Initialize() {
  context_lost_ = false;

  if (!target_window_) {
    return true;
  }

  const D3D12Provider& provider = GetD3D12Provider();
  IDXGIFactory2* dxgi_factory = provider.GetDXGIFactory();
  ID3D12Device* device = provider.GetDevice();
  ID3D12CommandQueue* direct_queue = provider.GetDirectQueue();

  swap_fence_current_value_ = 1;
  swap_fence_completed_value_ = 0;
  swap_fence_completion_event_ = CreateEvent(nullptr, false, false, nullptr);
  if (swap_fence_completion_event_ == nullptr) {
    XELOGE("Failed to create the composition fence completion event");
    Shutdown();
    return false;
  }
  // Create a fence for transient resources of compositing.
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&swap_fence_)))) {
    XELOGE("Failed to create the composition fence");
    Shutdown();
    return false;
  }

  // Create the swap chain.
  swap_chain_width_ = target_window_->scaled_width();
  swap_chain_height_ = target_window_->scaled_height();
  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
  swap_chain_desc.Width = swap_chain_width_;
  swap_chain_desc.Height = swap_chain_height_;
  swap_chain_desc.Format = kSwapChainFormat;
  swap_chain_desc.Stereo = FALSE;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.SampleDesc.Quality = 0;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = kSwapChainBufferCount;
  swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  swap_chain_desc.Flags = 0;
  IDXGISwapChain1* swap_chain_1;
  if (FAILED(dxgi_factory->CreateSwapChainForComposition(
          provider.GetDirectQueue(), &swap_chain_desc, nullptr,
          &swap_chain_1))) {
    XELOGE("Failed to create a DXGI swap chain for composition");
    Shutdown();
    return false;
  }
  if (FAILED(swap_chain_1->QueryInterface(IID_PPV_ARGS(&swap_chain_)))) {
    XELOGE("Failed to get version 3 of the DXGI swap chain interface");
    swap_chain_1->Release();
    Shutdown();
    return false;
  }
  swap_chain_1->Release();

  // Create a heap for RTV descriptors of swap chain buffers.
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = kSwapChainBufferCount;
  rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  rtv_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &rtv_heap_desc, IID_PPV_ARGS(&swap_chain_rtv_heap_)))) {
    XELOGE("Failed to create swap chain RTV descriptor heap");
    Shutdown();
    return false;
  }
  swap_chain_rtv_heap_start_ =
      swap_chain_rtv_heap_->GetCPUDescriptorHandleForHeapStart();

  // Get the buffers and create their RTV descriptors.
  if (!InitializeSwapChainBuffers()) {
    Shutdown();
    return false;
  }

  // Create the command list for compositing.
  for (uint32_t i = 0; i < kSwapCommandAllocatorCount; ++i) {
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&swap_command_allocators_[i])))) {
      XELOGE("Failed to create a composition command allocator");
      Shutdown();
      return false;
    }
  }
  if (FAILED(device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, swap_command_allocators_[0].Get(),
          nullptr, IID_PPV_ARGS(&swap_command_list_)))) {
    XELOGE("Failed to create the composition graphics command list");
    Shutdown();
    return false;
  }
  // Initially in open state, wait until BeginSwap.
  swap_command_list_->Close();

  // Associate the swap chain with the window via DirectComposition.
  if (FAILED(provider.CreateDCompositionDevice(nullptr,
                                               IID_PPV_ARGS(&dcomp_device_)))) {
    XELOGE("Failed to create a DirectComposition device");
    Shutdown();
    return false;
  }
  if (FAILED(dcomp_device_->CreateTargetForHwnd(
          reinterpret_cast<HWND>(target_window_->native_handle()), TRUE,
          &dcomp_target_))) {
    XELOGE("Failed to create a DirectComposition target for the window");
    Shutdown();
    return false;
  }
  if (FAILED(dcomp_device_->CreateVisual(&dcomp_visual_))) {
    XELOGE("Failed to create a DirectComposition visual");
    Shutdown();
    return false;
  }
  if (FAILED(dcomp_visual_->SetContent(swap_chain_.Get()))) {
    XELOGE(
        "Failed to set the content of the DirectComposition visual to the swap "
        "chain");
    Shutdown();
    return false;
  }
  if (FAILED(dcomp_target_->SetRoot(dcomp_visual_.Get()))) {
    XELOGE(
        "Failed to set the root of the DirectComposition target to the swap "
        "chain visual");
    Shutdown();
    return false;
  }
  if (FAILED(dcomp_device_->Commit())) {
    XELOGE("Failed to commit DirectComposition commands");
    Shutdown();
    return false;
  }

  // Initialize the immediate mode drawer if not offscreen.
  immediate_drawer_ = std::make_unique<D3D12ImmediateDrawer>(*this);
  if (!immediate_drawer_->Initialize()) {
    Shutdown();
    return false;
  }

  return true;
}

bool D3D12Context::InitializeSwapChainBuffers() {
  // Get references to the buffers.
  for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
    if (FAILED(
            swap_chain_->GetBuffer(i, IID_PPV_ARGS(&swap_chain_buffers_[i])))) {
      XELOGE("Failed to get buffer {} of the swap chain", i);
      return false;
    }
  }

  // Get the back buffer index for the first draw.
  swap_chain_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();

  // Create RTV descriptors for the swap chain buffers.
  ID3D12Device* device = GetD3D12Provider().GetDevice();
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
  rtv_desc.Format = kSwapChainFormat;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_desc.Texture2D.MipSlice = 0;
  rtv_desc.Texture2D.PlaneSlice = 0;
  for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
    device->CreateRenderTargetView(swap_chain_buffers_[i].Get(), &rtv_desc,
                                   GetSwapChainBufferRTV(i));
  }

  return true;
}

void D3D12Context::Shutdown() {
  if (!target_window_) {
    return;
  }

  if (!context_lost_ && swap_fence_ &&
      swap_fence_->GetCompletedValue() + 1 < swap_fence_current_value_) {
    swap_fence_->SetEventOnCompletion(swap_fence_current_value_ - 1,
                                      swap_fence_completion_event_);
    WaitForSingleObject(swap_fence_completion_event_, INFINITE);
  }

  immediate_drawer_.reset();

  dcomp_visual_.Reset();
  dcomp_target_.Reset();
  dcomp_device_.Reset();

  swap_command_list_.Reset();
  for (uint32_t i = 0; i < kSwapCommandAllocatorCount; ++i) {
    swap_command_allocators_[i].Reset();
  }

  for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
    swap_chain_buffers_[i].Reset();
  }
  swap_chain_rtv_heap_.Reset();
  swap_chain_.Reset();

  // First release the fence since it may reference the event.
  swap_fence_.Reset();
  if (swap_fence_completion_event_) {
    CloseHandle(swap_fence_completion_event_);
    swap_fence_completion_event_ = nullptr;
  }
  swap_fence_current_value_ = 1;
  swap_fence_completed_value_ = 0;
}

ImmediateDrawer* D3D12Context::immediate_drawer() {
  return immediate_drawer_.get();
}

bool D3D12Context::WasLost() { return context_lost_; }

bool D3D12Context::BeginSwap() {
  if (!target_window_ || context_lost_) {
    return false;
  }

  // Resize the swap chain if the window is resized.
  uint32_t target_window_width = target_window_->scaled_width();
  uint32_t target_window_height = target_window_->scaled_height();
  if (swap_chain_width_ != target_window_width ||
      swap_chain_height_ != target_window_height) {
    // Await the completion of swap chain use.
    // Context loss is also faked if resizing fails. In this case, before the
    // context is shut down to be recreated, frame completion must be awaited
    // (this isn't done if the context is truly lost).
    if (swap_fence_completed_value_ + 1 < swap_fence_current_value_) {
      swap_fence_->SetEventOnCompletion(swap_fence_current_value_ - 1,
                                        swap_fence_completion_event_);
      WaitForSingleObject(swap_fence_completion_event_, INFINITE);
      swap_fence_completed_value_ = swap_fence_current_value_ - 1;
    }
    // All buffer references must be released before resizing.
    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
      swap_chain_buffers_[i].Reset();
    }
    if (FAILED(swap_chain_->ResizeBuffers(
            kSwapChainBufferCount, target_window_width, target_window_height,
            kSwapChainFormat, 0))) {
      context_lost_ = true;
      return false;
    }
    swap_chain_width_ = target_window_width;
    swap_chain_height_ = target_window_height;
    if (!InitializeSwapChainBuffers()) {
      context_lost_ = true;
      return false;
    }
  }

  // Wait for a swap command allocator to become free.
  // Command allocator 0 is used when swap_fence_current_value_ is 1, 4, 7...
  swap_fence_completed_value_ = swap_fence_->GetCompletedValue();
  if (swap_fence_completed_value_ + kSwapCommandAllocatorCount <
      swap_fence_current_value_) {
    swap_fence_->SetEventOnCompletion(
        swap_fence_current_value_ - kSwapCommandAllocatorCount,
        swap_fence_completion_event_);
    WaitForSingleObject(swap_fence_completion_event_, INFINITE);
    swap_fence_completed_value_ = swap_fence_->GetCompletedValue();
  }

  // Start the command list.
  uint32_t command_allocator_index =
      uint32_t((swap_fence_current_value_ + (kSwapCommandAllocatorCount - 1)) %
               kSwapCommandAllocatorCount);
  ID3D12CommandAllocator* command_allocator =
      swap_command_allocators_[command_allocator_index].Get();
  command_allocator->Reset();
  swap_command_list_->Reset(command_allocator, nullptr);

  // Bind the back buffer as a render target and clear it.
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource =
      swap_chain_buffers_[swap_chain_back_buffer_index_].Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  swap_command_list_->ResourceBarrier(1, &barrier);
  D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv = GetSwapChainBackBufferRTV();
  swap_command_list_->OMSetRenderTargets(1, &back_buffer_rtv, TRUE, nullptr);
  float clear_color[4];
  GetClearColor(clear_color);
  swap_command_list_->ClearRenderTargetView(back_buffer_rtv, clear_color, 0,
                                            nullptr);

  return true;
}

void D3D12Context::EndSwap() {
  if (!target_window_ || context_lost_) {
    return;
  }

  ID3D12CommandQueue* direct_queue = GetD3D12Provider().GetDirectQueue();

  // Switch the back buffer to presentation state.
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource =
      swap_chain_buffers_[swap_chain_back_buffer_index_].Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  swap_command_list_->ResourceBarrier(1, &barrier);

  // Submit the command list.
  swap_command_list_->Close();
  ID3D12CommandList* execute_command_lists[] = {swap_command_list_.Get()};
  direct_queue->ExecuteCommandLists(1, execute_command_lists);

  // Present and check if the context was lost.
  if (FAILED(swap_chain_->Present(0, 0))) {
    context_lost_ = true;
    return;
  }

  // Signal the fence to wait for frame resources to become free again.
  direct_queue->Signal(swap_fence_.Get(), swap_fence_current_value_++);

  // Get the back buffer index for the next frame.
  swap_chain_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

std::unique_ptr<RawImage> D3D12Context::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetSwapChainBufferRTV(
    uint32_t buffer_index) const {
  return GetD3D12Provider().OffsetRTVDescriptor(swap_chain_rtv_heap_start_,
                                                buffer_index);
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
