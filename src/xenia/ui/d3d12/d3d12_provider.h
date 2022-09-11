/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_PROVIDER_H_
#define XENIA_UI_D3D12_D3D12_PROVIDER_H_

#include <memory>

#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/graphics_provider.h"
// chrispy: this is here to prevent clang format from moving d3d12_nvapi above
// the headers it depends on
#define HEADERFENCE
#undef HEADERFENCE
#include "xenia/gpu/d3d12/d3d12_nvapi.hpp"
#define XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace d3d12 {
enum {
  UPLOAD_RESULT_CREATE_FAILED = 0,
  UPLOAD_RESULT_CREATE_SUCCESS = 1,
  UPLOAD_RESULT_CREATE_CPUVISIBLE = 2
};
class D3D12Provider : public GraphicsProvider {
 public:
  ~D3D12Provider();

  static bool IsD3D12APIAvailable();

  static std::unique_ptr<D3D12Provider> Create();

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) override;

  std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() override;
  uint32_t CreateUploadResource(
      D3D12_HEAP_FLAGS HeapFlags, _In_ const D3D12_RESOURCE_DESC* pDesc,
      D3D12_RESOURCE_STATES InitialResourceState, REFIID riidResource,
      void** ppvResource, bool try_create_cpuvisible = false,
      const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr) const;

  IDXGIFactory2* GetDXGIFactory() const { return dxgi_factory_; }
  // nullptr if PIX not attached.
  IDXGraphicsAnalysis* GetGraphicsAnalysis() const {
    return graphics_analysis_;
  }
  ID3D12Device* GetDevice() const { return device_; }
  ID3D12CommandQueue* GetDirectQueue() const { return direct_queue_; }

  uint32_t GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const {
    return descriptor_sizes_[type];
  }
  uint32_t GetViewDescriptorSize() const {
    return GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
  uint32_t GetSamplerDescriptorSize() const {
    return GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }
  uint32_t GetRTVDescriptorSize() const {
    return GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }
  uint32_t GetDSVDescriptorSize() const {
    return GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  }
  template <typename T>
  T OffsetDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, T start,
                     uint32_t index) const {
    start.ptr += index * GetDescriptorSize(type);
    return start;
  }
  template <typename T>
  T OffsetViewDescriptor(T start, uint32_t index) const {
    start.ptr += index * GetViewDescriptorSize();
    return start;
  }
  template <typename T>
  T OffsetSamplerDescriptor(T start, uint32_t index) const {
    start.ptr += index * GetSamplerDescriptorSize();
    return start;
  }
  template <typename T>
  T OffsetRTVDescriptor(T start, uint32_t index) const {
    start.ptr += index * GetRTVDescriptorSize();
    return start;
  }
  template <typename T>
  T OffsetDSVDescriptor(T start, uint32_t index) const {
    start.ptr += index * GetDSVDescriptorSize();
    return start;
  }

  // Adapter info.
  GpuVendorID GetAdapterVendorID() const { return adapter_vendor_id_; }

  // Device features.
  D3D12_HEAP_FLAGS GetHeapFlagCreateNotZeroed() const {
    return heap_flag_create_not_zeroed_;
  }
  D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER
  GetProgrammableSamplePositionsTier() const {
    return programmable_sample_positions_tier_;
  }
  bool IsPSSpecifiedStencilReferenceSupported() const {
    return ps_specified_stencil_reference_supported_;
  }
  bool AreRasterizerOrderedViewsSupported() const {
    return rasterizer_ordered_views_supported_;
  }
  D3D12_RESOURCE_BINDING_TIER GetResourceBindingTier() const {
    return resource_binding_tier_;
  }
  D3D12_TILED_RESOURCES_TIER GetTiledResourcesTier() const {
    return tiled_resources_tier_;
  }
  bool AreUnalignedBlockTexturesSupported() const {
    return unaligned_block_textures_supported_;
  }
  uint32_t GetVirtualAddressBitsPerResource() const {
    return virtual_address_bits_per_resource_;
  }

  // Proxies for DirectX functions since they are loaded dynamically.
  HRESULT SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc,
                                 D3D_ROOT_SIGNATURE_VERSION version,
                                 ID3DBlob** blob_out,
                                 ID3DBlob** error_blob_out) const {
    return pfn_d3d12_serialize_root_signature_(desc, version, blob_out,
                                               error_blob_out);
  }
  HRESULT Disassemble(const void* src_data, size_t src_data_size, UINT flags,
                      const char* comments, ID3DBlob** disassembly_out) const {
    if (!pfn_d3d_disassemble_) {
      return E_NOINTERFACE;
    }
    return pfn_d3d_disassemble_(src_data, src_data_size, flags, comments,
                                disassembly_out);
  }
  HRESULT DxbcConverterCreateInstance(const CLSID& rclsid, const IID& riid,
                                      void** ppv) const {
    if (!pfn_dxilconv_dxc_create_instance_) {
      return E_NOINTERFACE;
    }
    return pfn_dxilconv_dxc_create_instance_(rclsid, riid, ppv);
  }
  HRESULT DxcCreateInstance(const CLSID& rclsid, const IID& riid,
                            void** ppv) const {
    if (!pfn_dxcompiler_dxc_create_instance_) {
      return E_NOINTERFACE;
    }
    return pfn_dxcompiler_dxc_create_instance_(rclsid, riid, ppv);
  }

 private:
  D3D12Provider() = default;

  static bool EnableIncreaseBasePriorityPrivilege();
  bool Initialize();

  typedef HRESULT(WINAPI* PFNCreateDXGIFactory2)(UINT Flags, REFIID riid,
                                                 _COM_Outptr_ void** ppFactory);
  typedef HRESULT(WINAPI* PFNDXGIGetDebugInterface1)(
      UINT Flags, REFIID riid, _COM_Outptr_ void** pDebug);

  HMODULE library_dxgi_ = nullptr;
  PFNCreateDXGIFactory2 pfn_create_dxgi_factory2_;
  // Needed during shutdown as well to report live objects, so may be nullptr.
  PFNDXGIGetDebugInterface1 pfn_dxgi_get_debug_interface1_ = nullptr;

  HMODULE library_d3d12_ = nullptr;
  PFN_D3D12_GET_DEBUG_INTERFACE pfn_d3d12_get_debug_interface_;
  PFN_D3D12_CREATE_DEVICE pfn_d3d12_create_device_;
  PFN_D3D12_SERIALIZE_ROOT_SIGNATURE pfn_d3d12_serialize_root_signature_;

  HMODULE library_d3dcompiler_ = nullptr;
  pD3DDisassemble pfn_d3d_disassemble_ = nullptr;

  HMODULE library_dxilconv_ = nullptr;
  DxcCreateInstanceProc pfn_dxilconv_dxc_create_instance_ = nullptr;

  HMODULE library_dxcompiler_ = nullptr;
  DxcCreateInstanceProc pfn_dxcompiler_dxc_create_instance_ = nullptr;

  IDXGIFactory2* dxgi_factory_ = nullptr;
  ID3D12Device* device_ = nullptr;
  ID3D12CommandQueue* direct_queue_ = nullptr;
  IDXGraphicsAnalysis* graphics_analysis_ = nullptr;

  uint32_t descriptor_sizes_[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

  GpuVendorID adapter_vendor_id_;

  D3D12_HEAP_FLAGS heap_flag_create_not_zeroed_;
  D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER programmable_sample_positions_tier_;
  D3D12_RESOURCE_BINDING_TIER resource_binding_tier_;
  D3D12_TILED_RESOURCES_TIER tiled_resources_tier_;
  uint32_t virtual_address_bits_per_resource_;
  bool ps_specified_stencil_reference_supported_;
  bool rasterizer_ordered_views_supported_;
  bool unaligned_block_textures_supported_;

  lightweight_nvapi::nvapi_state_t* nvapi_;
  lightweight_nvapi::cb_NvAPI_D3D12_CreateCommittedResource
      nvapi_createcommittedresource_ = nullptr;
  lightweight_nvapi::cb_NvAPI_D3D12_UseDriverHeapPriorities
      nvapi_usedriverheappriorities_ = nullptr;
  lightweight_nvapi::cb_NvAPI_D3D12_QueryCpuVisibleVidmem
      nvapi_querycpuvisiblevidmem_ = nullptr;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_PROVIDER_H_
