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
#include "xenia/ui/window.h"

DEFINE_bool(d3d12_random_clear_color, false,
            "Randomize presentation back buffer clear color.", "D3D12");

namespace xe {
namespace ui {
namespace d3d12 {

D3D12Context::D3D12Context(D3D12Provider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

D3D12Context::~D3D12Context() { Shutdown(); }

bool D3D12Context::Initialize() {
  auto provider = GetD3D12Provider();
  auto dxgi_factory = provider->GetDXGIFactory();
  auto device = provider->GetDevice();
  auto direct_queue = provider->GetDirectQueue();

  context_lost_ = false;

  current_frame_ = 1;
  // No frames have been completed yet.
  last_completed_frame_ = 0;
  // Keep in sync with the modulo because why not.
  current_queue_frame_ = 1;

  // Create fences for synchronization of reuse and destruction of transient
  // objects (like command lists) and for global shutdown.
  for (uint32_t i = 0; i < kQueuedFrames; ++i) {
    fences_[i] = CPUFence::Create(device, direct_queue);
    if (fences_[i] == nullptr) {
      Shutdown();
      return false;
    }
  }

  if (target_window_) {
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

    // Create command lists for compositing.
    for (uint32_t i = 0; i < kQueuedFrames; ++i) {
      swap_command_lists_[i] = CommandList::Create(
          device, direct_queue, D3D12_COMMAND_LIST_TYPE_DIRECT);
      if (swap_command_lists_[i] == nullptr) {
        Shutdown();
        return false;
      }
    }

    // Initialize the immediate mode drawer if not offscreen.
    immediate_drawer_ = std::make_unique<D3D12ImmediateDrawer>(this);
    if (!immediate_drawer_->Initialize()) {
      Shutdown();
      return false;
    }
  }

  initialized_fully_ = true;
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
  if (initialized_fully_ && !context_lost_) {
    AwaitAllFramesCompletion();
  }

  initialized_fully_ = false;

  immediate_drawer_.reset();

  if (swap_chain_ != nullptr) {
    for (uint32_t i = 0; i < kQueuedFrames; ++i) {
      swap_command_lists_[i].reset();
    }

    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
      auto& buffer = swap_chain_buffers_[i];
      if (buffer == nullptr) {
        break;
      }
      buffer->Release();
      buffer = nullptr;
    }

    if (swap_chain_rtv_heap_ != nullptr) {
      swap_chain_rtv_heap_->Release();
      swap_chain_rtv_heap_ = nullptr;
    }

    swap_chain_->Release();
  }

  for (uint32_t i = 0; i < kQueuedFrames; ++i) {
    fences_[i].reset();
  }
}

ImmediateDrawer* D3D12Context::immediate_drawer() {
  return immediate_drawer_.get();
}

bool D3D12Context::is_current() { return false; }

bool D3D12Context::MakeCurrent() { return true; }

void D3D12Context::ClearCurrent() {}

void D3D12Context::BeginSwap() {
  if (context_lost_) {
    return;
  }

  // Await the availability of transient objects for the new frame.
  // The frame number is incremented in EndSwap so it can be treated the same
  // way both when inside a frame and when outside of it (it's tied to actual
  // submissions).
  fences_[current_queue_frame_]->Await();
  // Update the completed frame if didn't explicitly await all queued frames.
  if (last_completed_frame_ + kQueuedFrames < current_frame_) {
    last_completed_frame_ = current_frame_ - kQueuedFrames;
  }

  if (target_window_ != nullptr) {
    // Resize the swap chain if the window is resized.
    uint32_t target_window_width = target_window_->scaled_width();
    uint32_t target_window_height = target_window_->scaled_height();
    if (swap_chain_width_ != target_window_width ||
        swap_chain_height_ != target_window_height) {
      // Await the completion of swap chain use.
      // Context loss is also faked if resizing fails. In this case, before the
      // context is shut down to be recreated, frame completion must be awaited
      // (this isn't done if the context is truly lost).
      AwaitAllFramesCompletion();
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

    // Bind the back buffer as a render target and clear it.
    auto command_list = swap_command_lists_[current_queue_frame_].get();
    auto graphics_command_list = command_list->BeginRecording();
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource =
        swap_chain_buffers_[swap_chain_back_buffer_index_];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    graphics_command_list->ResourceBarrier(1, &barrier);
    D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv = GetSwapChainBackBufferRTV();
    graphics_command_list->OMSetRenderTargets(1, &back_buffer_rtv, TRUE,
                                              nullptr);
    float clear_color[4];
    if (cvars::d3d12_random_clear_color) {
      clear_color[0] =
          rand() / float(RAND_MAX);  // NOLINT(runtime/threadsafe_fn)
      clear_color[1] = 1.0f;
      clear_color[2] = 0.0f;
    } else {
      clear_color[0] = 238.0f / 255.0f;
      clear_color[1] = 238.0f / 255.0f;
      clear_color[2] = 238.0f / 255.0f;
    }
    clear_color[3] = 1.0f;
    graphics_command_list->ClearRenderTargetView(back_buffer_rtv, clear_color,
                                                 0, nullptr);
  }
}

void D3D12Context::EndSwap() {
  if (context_lost_) {
    return;
  }

  if (target_window_ != nullptr) {
    // Switch the back buffer to presentation state.
    auto command_list = swap_command_lists_[current_queue_frame_].get();
    auto graphics_command_list = command_list->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource =
        swap_chain_buffers_[swap_chain_back_buffer_index_];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    graphics_command_list->ResourceBarrier(1, &barrier);
    command_list->Execute();
    // Present and check if the context was lost.
    HRESULT result = swap_chain_->Present(0, 0);
    if (result == DXGI_ERROR_DEVICE_RESET ||
        result == DXGI_ERROR_DEVICE_REMOVED) {
      context_lost_ = true;
      return;
    }
    // Get the back buffer index for the next frame.
    swap_chain_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
  }

  // Go to the next transient object frame.
  fences_[current_queue_frame_]->Enqueue();
  ++current_queue_frame_;
  if (current_queue_frame_ >= kQueuedFrames) {
    current_queue_frame_ -= kQueuedFrames;
  }
  ++current_frame_;
}

std::unique_ptr<RawImage> D3D12Context::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

void D3D12Context::AwaitAllFramesCompletion() {
  // Await the last frame since previous frames must be completed before it.
  if (context_lost_) {
    return;
  }
  uint32_t await_frame = current_queue_frame_ + (kQueuedFrames - 1);
  if (await_frame >= kQueuedFrames) {
    await_frame -= kQueuedFrames;
  }
  fences_[await_frame]->Await();
  last_completed_frame_ = current_frame_ - 1;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetSwapChainBufferRTV(
    uint32_t buffer_index) const {
  return GetD3D12Provider()->OffsetRTVDescriptor(swap_chain_rtv_heap_start_,
                                                 buffer_index);
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
