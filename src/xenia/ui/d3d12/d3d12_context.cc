/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_context.h"

#include <cstdlib>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_immediate_drawer.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_util.h"
#include "xenia/ui/window.h"

DEFINE_bool(d3d12_random_clear_color, false,
            "Randomize presentation back buffer clear color.", "D3D12");

namespace xe {
namespace ui {
namespace d3d12 {

constexpr uint32_t D3D12Context::kSwapCommandAllocatorCount;
constexpr uint32_t D3D12Context::kSwapChainBufferCount;

D3D12Context::D3D12Context(D3D12Provider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

D3D12Context::~D3D12Context() { Shutdown(); }

bool D3D12Context::Initialize() {
  auto provider = GetD3D12Provider();
  auto dxgi_factory = provider->GetDXGIFactory();
  auto device = provider->GetDevice();
  auto direct_queue = provider->GetDirectQueue();

  context_lost_ = false;

  if (target_window_) {
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
    if (FAILED(dxgi_factory->CreateSwapChainForHwnd(
            provider->GetDirectQueue(),
            static_cast<HWND>(target_window_->native_handle()),
            &swap_chain_desc, nullptr, nullptr, &swap_chain_1))) {
      XELOGE("Failed to create a DXGI swap chain");
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
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         swap_command_allocators_[0], nullptr,
                                         IID_PPV_ARGS(&swap_command_list_)))) {
      XELOGE("Failed to create the composition graphics command list");
      Shutdown();
      return false;
    }
    // Initially in open state, wait until BeginSwap.
    swap_command_list_->Close();

    // Initialize the immediate mode drawer if not offscreen.
    immediate_drawer_ = std::make_unique<D3D12ImmediateDrawer>(this);
    if (!immediate_drawer_->Initialize()) {
      Shutdown();
      return false;
    }
  }

  return true;
}

bool D3D12Context::InitializeSwapChainBuffers() {
  // Get references to the buffers.
  for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
    if (FAILED(
            swap_chain_->GetBuffer(i, IID_PPV_ARGS(&swap_chain_buffers_[i])))) {
      XELOGE("Failed to get buffer %u of the swap chain", i);
      return false;
    }
  }

  // Get the back buffer index for the first draw.
  swap_chain_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();

  // Create RTV descriptors for the swap chain buffers.
  auto device = GetD3D12Provider()->GetDevice();
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
  rtv_desc.Format = kSwapChainFormat;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_desc.Texture2D.MipSlice = 0;
  rtv_desc.Texture2D.PlaneSlice = 0;
  for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
    device->CreateRenderTargetView(swap_chain_buffers_[i], &rtv_desc,
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

  util::ReleaseAndNull(swap_command_list_);
  for (uint32_t i = 0; i < kSwapCommandAllocatorCount; ++i) {
    auto& swap_command_allocator = swap_command_allocators_[i];
    if (!swap_command_allocator) {
      break;
    }
    swap_command_allocator->Release();
    swap_command_allocator = nullptr;
  }

  if (swap_chain_) {
    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
      auto& swap_chain_buffer = swap_chain_buffers_[i];
      if (!swap_chain_buffer) {
        break;
      }
      swap_chain_buffer->Release();
      swap_chain_buffer = nullptr;
    }

    util::ReleaseAndNull(swap_chain_rtv_heap_);

    swap_chain_->Release();
    swap_chain_ = nullptr;
  }

  // First release the fence since it may reference the event.
  util::ReleaseAndNull(swap_fence_);
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

bool D3D12Context::is_current() { return false; }

bool D3D12Context::MakeCurrent() { return true; }

void D3D12Context::ClearCurrent() {}

void D3D12Context::BeginSwap() {
  if (!target_window_ || context_lost_) {
    return;
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
      swap_chain_buffers_[i]->Release();
      swap_chain_buffers_[i] = nullptr;
    }
    if (FAILED(swap_chain_->ResizeBuffers(
            kSwapChainBufferCount, target_window_width, target_window_height,
            kSwapChainFormat, 0))) {
      context_lost_ = true;
      return;
    }
    swap_chain_width_ = target_window_width;
    swap_chain_height_ = target_window_height;
    if (!InitializeSwapChainBuffers()) {
      context_lost_ = true;
      return;
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
  auto command_allocator = swap_command_allocators_[command_allocator_index];
  command_allocator->Reset();
  swap_command_list_->Reset(command_allocator, nullptr);

  // Bind the back buffer as a render target and clear it.
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource =
      swap_chain_buffers_[swap_chain_back_buffer_index_];
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  swap_command_list_->ResourceBarrier(1, &barrier);
  D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv = GetSwapChainBackBufferRTV();
  swap_command_list_->OMSetRenderTargets(1, &back_buffer_rtv, TRUE, nullptr);
  float clear_color[4];
  if (cvars::d3d12_random_clear_color) {
    clear_color[0] = rand() / float(RAND_MAX);  // NOLINT(runtime/threadsafe_fn)
    clear_color[1] = 1.0f;
    clear_color[2] = 0.0f;
  } else {
    clear_color[0] = 238.0f / 255.0f;
    clear_color[1] = 238.0f / 255.0f;
    clear_color[2] = 238.0f / 255.0f;
  }
  clear_color[3] = 1.0f;
  swap_command_list_->ClearRenderTargetView(back_buffer_rtv, clear_color, 0,
                                            nullptr);
}

void D3D12Context::EndSwap() {
  if (!target_window_ || context_lost_) {
    return;
  }

  auto direct_queue = GetD3D12Provider()->GetDirectQueue();

  // Switch the back buffer to presentation state.
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource =
      swap_chain_buffers_[swap_chain_back_buffer_index_];
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  swap_command_list_->ResourceBarrier(1, &barrier);

  // Submit the command list.
  swap_command_list_->Close();
  ID3D12CommandList* execute_command_lists[] = {swap_command_list_};
  direct_queue->ExecuteCommandLists(1, execute_command_lists);

  // Present and check if the context was lost.
  HRESULT result = swap_chain_->Present(0, 0);
  if (result == DXGI_ERROR_DEVICE_RESET ||
      result == DXGI_ERROR_DEVICE_REMOVED) {
    context_lost_ = true;
    return;
  }

  // Signal the fence to wait for frame resources to become free again.
  direct_queue->Signal(swap_fence_, swap_fence_current_value_++);

  // Get the back buffer index for the next frame.
  swap_chain_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

std::unique_ptr<RawImage> D3D12Context::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetSwapChainBufferRTV(
    uint32_t buffer_index) const {
  return GetD3D12Provider()->OffsetRTVDescriptor(swap_chain_rtv_heap_start_,
                                                 buffer_index);
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
