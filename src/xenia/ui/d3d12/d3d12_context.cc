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
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace d3d12 {

D3D12Context::D3D12Context(D3D12Provider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

D3D12Context::~D3D12Context() { Shutdown(); }

bool D3D12Context::Initialize() {
  auto provider = static_cast<D3D12Provider*>(provider_);
  auto dxgi_factory = provider->get_dxgi_factory();
  auto device = provider->get_device();
  auto direct_queue = provider->get_direct_queue();

  context_lost_ = false;

  // Create fences for synchronization of reuse and destruction of transient
  // objects (like command lists) and for global shutdown.
  for (uint32_t i = 0; i < kFrameQueueLength; ++i) {
    fences_[i] = CPUFence::Create(device, direct_queue);
    if (fences_[i] == nullptr) {
      Shutdown();
      return false;
    }
  }

  if (target_window_) {
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
    swap_chain_desc.Width = 1280;
    swap_chain_desc.Height = 720;
    swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        provider->get_direct_queue(),
        static_cast<HWND>(target_window_->native_handle()), &swap_chain_desc,
        nullptr, nullptr, &swap_chain_1))) {
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
    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
      if (FAILED(swap_chain_->GetBuffer(i,
          IID_PPV_ARGS(&swap_chain_buffers_[i])))) {
        XELOGE("Failed to get buffer %u of the swap chain", i);
        Shutdown();
        return false;
      }
    }
  }

  initialized_fully_ = true;
}

void D3D12Context::Shutdown() {
  if (initialized_fully_) {
    // TODO(Triang3l): Wait for submitted frame completion.
  }

  initialized_fully_ = false;

  immediate_drawer_.reset();

  if (swap_chain_ != nullptr) {
    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
      auto& buffer = swap_chain_buffers_[i];
      if (buffer == nullptr) {
        break;
      }
      buffer->Release();
      buffer = nullptr;
    }
    swap_chain_->Release();
  }

  for (uint32_t i = 0; i < kFrameQueueLength; ++i) {
    fences_[i].reset();
  }
}

ImmediateDrawer* D3D12Context::immediate_drawer() {
  return immediate_drawer_.get();
}

bool D3D12Context::is_current() { return false; }

bool D3D12Context::MakeCurrent() { return true; }

void D3D12Context::ClearCurrent() {}

std::unique_ptr<RawImage> D3D12Context::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
