/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_PROVIDER_H_
#define XENIA_UI_D3D12_D3D12_PROVIDER_H_

#include <memory>

#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Provider : public GraphicsProvider {
 public:
  ~D3D12Provider() override;

  static bool IsD3D12APIAvailable();

  static std::unique_ptr<D3D12Provider> Create(Window* main_window);

  std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) override;
  std::unique_ptr<GraphicsContext> CreateOffscreenContext() override;

  IDXGIFactory2* GetDXGIFactory() const { return dxgi_factory_; }
  // nullptr if PIX not attached.
  IDXGraphicsAnalysis* GetGraphicsAnalysis() const {
    return graphics_analysis_;
  }
  ID3D12Device* GetDevice() const { return device_; }
  ID3D12CommandQueue* GetDirectQueue() const { return direct_queue_; }

  uint32_t GetViewDescriptorSize() const { return descriptor_size_view_; }
  uint32_t GetSamplerDescriptorSize() const { return descriptor_size_sampler_; }
  uint32_t GetRTVDescriptorSize() const { return descriptor_size_rtv_; }
  uint32_t GetDSVDescriptorSize() const { return descriptor_size_dsv_; }
  template <typename T>
  inline T OffsetViewDescriptor(T start, uint32_t index) const {
    start.ptr += index * descriptor_size_view_;
    return start;
  }
  template <typename T>
  inline T OffsetSamplerDescriptor(T start, uint32_t index) const {
    start.ptr += index * descriptor_size_sampler_;
    return start;
  }
  template <typename T>
  inline T OffsetRTVDescriptor(T start, uint32_t index) const {
    start.ptr += index * descriptor_size_rtv_;
    return start;
  }
  template <typename T>
  inline T OffsetDSVDescriptor(T start, uint32_t index) const {
    start.ptr += index * descriptor_size_dsv_;
    return start;
  }

  // Adapter info.
  uint32_t GetAdapterVendorID() const { return adapter_vendor_id_; }

  // Device features.
  uint32_t GetProgrammableSamplePositionsTier() const {
    return programmable_sample_positions_tier_;
  }
  bool AreRasterizerOrderedViewsSupported() const {
    return rasterizer_ordered_views_supported_;
  }
  uint32_t GetTiledResourcesTier() const { return tiled_resources_tier_; }
  uint32_t GetVirtualAddressBitsPerResource() const {
    return virtual_address_bits_per_resource_;
  }

  // Proxies for Direct3D 12 functions since they are loaded dynamically.
  inline HRESULT SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc,
                                        D3D_ROOT_SIGNATURE_VERSION version,
                                        ID3DBlob** blob_out,
                                        ID3DBlob** error_blob_out) const {
    return pfn_d3d12_serialize_root_signature_(desc, version, blob_out,
                                               error_blob_out);
  }
  inline HRESULT Disassemble(const void* src_data, size_t src_data_size,
                             UINT flags, const char* comments,
                             ID3DBlob** disassembly_out) const {
    return pfn_d3d_disassemble_(src_data, src_data_size, flags, comments,
                                disassembly_out);
  }

 private:
  explicit D3D12Provider(Window* main_window);

  enum InitializationResult : uint32_t {
    kSucceeded,
    kDeviceInitializationFailed,
    kLibraryLoadFailed,
  };

  InitializationResult Initialize();

  HMODULE library_dxgi_ = nullptr;
  HMODULE library_d3d12_ = nullptr;
  HMODULE library_d3dcompiler_ = nullptr;

  typedef HRESULT(WINAPI* PFNCreateDXGIFactory2)(UINT Flags, REFIID riid,
                                                 _COM_Outptr_ void** ppFactory);
  typedef HRESULT(WINAPI* PFNDXGIGetDebugInterface1)(
      UINT Flags, REFIID riid, _COM_Outptr_ void** pDebug);

  PFNCreateDXGIFactory2 pfn_create_dxgi_factory2_;
  PFNDXGIGetDebugInterface1 pfn_dxgi_get_debug_interface1_;
  PFN_D3D12_GET_DEBUG_INTERFACE pfn_d3d12_get_debug_interface_;
  PFN_D3D12_CREATE_DEVICE pfn_d3d12_create_device_;
  PFN_D3D12_SERIALIZE_ROOT_SIGNATURE pfn_d3d12_serialize_root_signature_;
  pD3DDisassemble pfn_d3d_disassemble_;

  IDXGIFactory2* dxgi_factory_ = nullptr;
  IDXGraphicsAnalysis* graphics_analysis_ = nullptr;
  ID3D12Device* device_ = nullptr;
  ID3D12CommandQueue* direct_queue_ = nullptr;

  uint32_t descriptor_size_view_;
  uint32_t descriptor_size_sampler_;
  uint32_t descriptor_size_rtv_;
  uint32_t descriptor_size_dsv_;

  uint32_t adapter_vendor_id_;

  uint32_t programmable_sample_positions_tier_;
  bool rasterizer_ordered_views_supported_;
  uint32_t tiled_resources_tier_;
  uint32_t virtual_address_bits_per_resource_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_PROVIDER_H_
