/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_provider.h"

#include <gflags/gflags.h>

#include "xenia/base/logging.h"
#include "xenia/ui/d3d12/d3d12_context.h"

DEFINE_bool(d3d12_debug, false, "Enable Direct3D 12 and DXGI debug layer.");
DEFINE_int32(d3d12_adapter_index, -1, "Index of the DXGI adapter to use. "
             "-1 for any physical adapter, -2 for WARP software rendering.");

namespace xe {
namespace ui {
namespace d3d12 {

std::unique_ptr<D3D12Provider> D3D12Provider::Create(Window* main_window) {
  std::unique_ptr<D3D12Provider> provider(new D3D12Provider(main_window));
  if (!provider->Initialize()) {
    xe::FatalError(
        "Unable to initialize Direct3D 12 graphics subsystem.\n"
        "\n"
        "Ensure that you have the latest drivers for your GPU and it supports "
        "Direct3D 12 feature level 11_0 and tiled resources tier 1.\n"
        "\n"
        "See http://xenia.jp/faq/ for more information and a list of supported "
        "GPUs.");
    return nullptr;
  }
  return provider;
}

D3D12Provider::D3D12Provider(Window* main_window)
    : GraphicsProvider(main_window) {}

D3D12Provider::~D3D12Provider() {
  if (direct_queue_ != nullptr) {
    direct_queue_->Release();
  }
  if (device_ != nullptr) {
    device_->Release();
  }
  if (dxgi_factory_ != nullptr) {
    dxgi_factory_->Release();
  }
}

bool D3D12Provider::Initialize() {
  // Enable the debug layer.
  bool debug = FLAGS_d3d12_debug;
  if (debug) {
    ID3D12Debug* debug_interface;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      debug_interface->Release();
    } else {
      XELOGW("Failed to enable the Direct3D 12 debug layer");
      debug = false;
    }
  }

  // Create the DXGI factory.
  IDXGIFactory2* dxgi_factory;
  if (FAILED(CreateDXGIFactory2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0,
                                IID_PPV_ARGS(&dxgi_factory)))) {
    XELOGE("Failed to create a DXGI factory");
    return false;
  }

  // Choose the adapter and create a device with required features.
  // TODO(Triang3l): Log adapter info (contains a wide string).
  uint32_t adapter_index = 0;
  IDXGIAdapter1* adapter = nullptr;
  ID3D12Device* device = nullptr;
  while (dxgi_factory->EnumAdapters1(adapter_index, &adapter) == S_OK) {
    DXGI_ADAPTER_DESC1 adapter_desc;
    if (SUCCEEDED(adapter->GetDesc1(&adapter_desc))) {
      if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&device)))) {
        if (IsDeviceSupported(device)) {
          if (FLAGS_d3d12_adapter_index >= 0) {
            if (adapter_index == FLAGS_d3d12_adapter_index) {
              break;
            }
          } else if (FLAGS_d3d12_adapter_index == -2) {
            if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
              break;
            }
          } else {
            if (!(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
              break;
            }
          }
        }
        device->Release();
        device = nullptr;
      }
    }
    adapter->Release();
    adapter = nullptr;
    ++adapter_index;
  }
  if (adapter != nullptr) {
    adapter->Release();
  }
  if (device == nullptr) {
    XELOGE("Failed to get an adapter supporting Direct3D feature level 11_0 "
           "with required options, or failed to create a Direct3D 12 device.");
    dxgi_factory->Release();
    return false;
  }

  // Create the command queue for graphics.
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_desc.NodeMask = 0;
  ID3D12CommandQueue* direct_queue;
  if (FAILED(device->CreateCommandQueue(&queue_desc,
                                        IID_PPV_ARGS(&direct_queue)))) {
    XELOGE("Failed to create a direct command queue");
    device->Release();
    dxgi_factory->Release();
  }

  dxgi_factory_ = dxgi_factory;
  device_ = device;
  direct_queue_ = direct_queue;

  // Get descriptor sizes for each type.
  descriptor_size_view_ = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  descriptor_size_sampler_ = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  descriptor_size_rtv_ =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  descriptor_size_dsv_ =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  // Check if programmable sample positions are supported (added in Creators
  // Update).
  programmable_sample_positions_tier_ = 0;
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2;
  if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2,
                                            &options2, sizeof(options2)))) {
    programmable_sample_positions_tier_ =
        uint32_t(options2.ProgrammableSamplePositionsTier);
  }
  XELOGD3D("Direct3D 12 device supports programmable sample positions tier %u",
           programmable_sample_positions_tier_);

  return true;
}

bool D3D12Provider::IsDeviceSupported(ID3D12Device* device) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options;
  if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                         sizeof(options)))) {
    return false;
  }

  // Tiled resources required for shared memory emulation without excessive
  // video memory usage.
  if (FLAGS_d3d12_tiled_resources &&
      options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1) {
    return false;
  }

  return true;
}

std::unique_ptr<GraphicsContext> D3D12Provider::CreateContext(
    Window* target_window) {
  auto new_context =
      std::unique_ptr<D3D12Context>(new D3D12Context(this, target_window));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

std::unique_ptr<GraphicsContext> D3D12Provider::CreateOffscreenContext() {
  auto new_context =
      std::unique_ptr<D3D12Context>(new D3D12Context(this, nullptr));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
