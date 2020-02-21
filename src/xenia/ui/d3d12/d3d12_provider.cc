/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_provider.h"

#include <malloc.h>
#include <cstdlib>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/d3d12/d3d12_context.h"

DEFINE_bool(d3d12_debug, false, "Enable Direct3D 12 and DXGI debug layer.",
            "D3D12");
DEFINE_int32(d3d12_adapter, -1,
             "Index of the DXGI adapter to use. "
             "-1 for any physical adapter, -2 for WARP software rendering.",
             "D3D12");

namespace xe {
namespace ui {
namespace d3d12 {

bool D3D12Provider::IsD3D12APIAvailable() {
  HMODULE library_d3d12 = LoadLibrary(L"D3D12.dll");
  if (!library_d3d12) {
    return false;
  }
  FreeLibrary(library_d3d12);
  return true;
}

std::unique_ptr<D3D12Provider> D3D12Provider::Create(Window* main_window) {
  std::unique_ptr<D3D12Provider> provider(new D3D12Provider(main_window));
  if (!provider->Initialize()) {
    xe::FatalError(
        "Unable to initialize Direct3D 12 graphics subsystem.\n"
        "\n"
        "Ensure that you have the latest drivers for your GPU and it supports "
        "Direct3D 12 feature level 11_0.\n"
        "\n"
        "See https://xenia.jp/faq/ for more information and a list of "
        "supported GPUs.");
    return nullptr;
  }
  return provider;
}

D3D12Provider::D3D12Provider(Window* main_window)
    : GraphicsProvider(main_window) {}

D3D12Provider::~D3D12Provider() {
  if (graphics_analysis_ != nullptr) {
    graphics_analysis_->Release();
  }
  if (direct_queue_ != nullptr) {
    direct_queue_->Release();
  }
  if (device_ != nullptr) {
    device_->Release();
  }
  if (dxgi_factory_ != nullptr) {
    dxgi_factory_->Release();
  }

  if (library_d3dcompiler_ != nullptr) {
    FreeLibrary(library_d3dcompiler_);
  }
  if (library_d3d12_ != nullptr) {
    FreeLibrary(library_d3d12_);
  }
  if (library_dxgi_ != nullptr) {
    FreeLibrary(library_dxgi_);
  }
}

bool D3D12Provider::Initialize() {
  // Load the libraries.
  library_dxgi_ = LoadLibrary(L"dxgi.dll");
  library_d3d12_ = LoadLibrary(L"D3D12.dll");
  library_d3dcompiler_ = LoadLibrary(L"D3DCompiler_47.dll");
  if (library_dxgi_ == nullptr || library_d3d12_ == nullptr ||
      library_d3dcompiler_ == nullptr) {
    XELOGE("Failed to load dxgi.dll, D3D12.dll and D3DCompiler_47.dll.");
    return false;
  }
  bool libraries_loaded = true;
  libraries_loaded &=
      (pfn_create_dxgi_factory2_ = PFNCreateDXGIFactory2(
           GetProcAddress(library_dxgi_, "CreateDXGIFactory2"))) != nullptr;
  libraries_loaded &=
      (pfn_dxgi_get_debug_interface1_ = PFNDXGIGetDebugInterface1(
           GetProcAddress(library_dxgi_, "DXGIGetDebugInterface1"))) != nullptr;
  libraries_loaded &=
      (pfn_d3d12_get_debug_interface_ = PFN_D3D12_GET_DEBUG_INTERFACE(
           GetProcAddress(library_d3d12_, "D3D12GetDebugInterface"))) !=
      nullptr;
  libraries_loaded &=
      (pfn_d3d12_create_device_ = PFN_D3D12_CREATE_DEVICE(
           GetProcAddress(library_d3d12_, "D3D12CreateDevice"))) != nullptr;
  libraries_loaded &=
      (pfn_d3d12_serialize_root_signature_ = PFN_D3D12_SERIALIZE_ROOT_SIGNATURE(
           GetProcAddress(library_d3d12_, "D3D12SerializeRootSignature"))) !=
      nullptr;
  libraries_loaded &= (pfn_d3d_disassemble_ = pD3DDisassemble(GetProcAddress(
                           library_d3dcompiler_, "D3DDisassemble"))) != nullptr;
  if (!libraries_loaded) {
    return false;
  }

  // Enable the debug layer.
  bool debug = cvars::d3d12_debug;
  if (debug) {
    ID3D12Debug* debug_interface;
    if (SUCCEEDED(
            pfn_d3d12_get_debug_interface_(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      debug_interface->Release();
    } else {
      XELOGW("Failed to enable the Direct3D 12 debug layer");
      debug = false;
    }
  }

  // Create the DXGI factory.
  IDXGIFactory2* dxgi_factory;
  if (FAILED(pfn_create_dxgi_factory2_(debug ? DXGI_CREATE_FACTORY_DEBUG : 0,
                                       IID_PPV_ARGS(&dxgi_factory)))) {
    XELOGE("Failed to create a DXGI factory");
    return false;
  }

  // Choose the adapter.
  uint32_t adapter_index = 0;
  IDXGIAdapter1* adapter = nullptr;
  while (dxgi_factory->EnumAdapters1(adapter_index, &adapter) == S_OK) {
    DXGI_ADAPTER_DESC1 adapter_desc;
    if (SUCCEEDED(adapter->GetDesc1(&adapter_desc))) {
      if (SUCCEEDED(pfn_d3d12_create_device_(adapter, D3D_FEATURE_LEVEL_11_0,
                                             _uuidof(ID3D12Device), nullptr))) {
        if (cvars::d3d12_adapter >= 0) {
          if (adapter_index == cvars::d3d12_adapter) {
            break;
          }
        } else if (cvars::d3d12_adapter == -2) {
          if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            break;
          }
        } else {
          if (!(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            break;
          }
        }
      }
    }
    adapter->Release();
    adapter = nullptr;
    ++adapter_index;
  }
  if (adapter == nullptr) {
    XELOGE("Failed to get an adapter supporting Direct3D feature level 11_0.");
    dxgi_factory->Release();
    return false;
  }
  DXGI_ADAPTER_DESC adapter_desc;
  if (FAILED(adapter->GetDesc(&adapter_desc))) {
    XELOGE("Failed to get the DXGI adapter description.");
    adapter->Release();
    dxgi_factory->Release();
    return false;
  }
  adapter_vendor_id_ = adapter_desc.VendorId;
  int adapter_name_mb_size = WideCharToMultiByte(
      CP_UTF8, 0, adapter_desc.Description, -1, nullptr, 0, nullptr, nullptr);
  if (adapter_name_mb_size != 0) {
    char* adapter_name_mb =
        reinterpret_cast<char*>(alloca(adapter_name_mb_size));
    if (WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1,
                            adapter_name_mb, adapter_name_mb_size, nullptr,
                            nullptr) != 0) {
      XELOGD3D("DXGI adapter: %s (vendor %.4X, device %.4X)", adapter_name_mb,
               adapter_desc.VendorId, adapter_desc.DeviceId);
    }
  }

  // Create the Direct3D 12 device.
  ID3D12Device* device;
  if (FAILED(pfn_d3d12_create_device_(adapter, D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&device)))) {
    XELOGE("Failed to create a Direct3D 12 feature level 11_0 device.");
    adapter->Release();
    dxgi_factory->Release();
    return false;
  }
  adapter->Release();

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

  // Check if optional features are supported.
  rasterizer_ordered_views_supported_ = false;
  tiled_resources_tier_ = 0;
  D3D12_FEATURE_DATA_D3D12_OPTIONS options;
  if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                            &options, sizeof(options)))) {
    rasterizer_ordered_views_supported_ = options.ROVsSupported ? true : false;
    tiled_resources_tier_ = uint32_t(options.TiledResourcesTier);
  }
  programmable_sample_positions_tier_ = 0;
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2;
  if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2,
                                            &options2, sizeof(options2)))) {
    programmable_sample_positions_tier_ =
        uint32_t(options2.ProgrammableSamplePositionsTier);
  }
  virtual_address_bits_per_resource_ = 0;
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT virtual_address_support;
  if (SUCCEEDED(device->CheckFeatureSupport(
          D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &virtual_address_support,
          sizeof(virtual_address_support)))) {
    virtual_address_bits_per_resource_ =
        virtual_address_support.MaxGPUVirtualAddressBitsPerResource;
  }
  XELOGD3D("Direct3D 12 device features:");
  XELOGD3D("* Max GPU virtual address bits per resource: %u",
           virtual_address_bits_per_resource_);
  XELOGD3D("* Programmable sample positions: tier %u",
           programmable_sample_positions_tier_);
  XELOGD3D("* Rasterizer-ordered views: %s",
           rasterizer_ordered_views_supported_ ? "yes" : "no");
  XELOGD3D("* Tiled resources: tier %u", tiled_resources_tier_);

  // Get the graphics analysis interface, will silently fail if PIX is not
  // attached.
  pfn_dxgi_get_debug_interface1_(0, IID_PPV_ARGS(&graphics_analysis_));

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
