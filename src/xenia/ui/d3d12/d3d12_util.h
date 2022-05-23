/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_UTIL_H_
#define XENIA_UI_D3D12_D3D12_UTIL_H_

#include <utility>

#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace ui {
namespace d3d12 {
namespace util {

using DescriptorCpuGpuHandlePair =
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>;

extern const D3D12_HEAP_PROPERTIES kHeapPropertiesDefault;
extern const D3D12_HEAP_PROPERTIES kHeapPropertiesUpload;
extern const D3D12_HEAP_PROPERTIES kHeapPropertiesReadback;

template <typename T>
bool ReleaseAndNull(T& object) {
  if (object != nullptr) {
    object->Release();
    object = nullptr;
    return true;
  }
  return false;
};

ID3D12RootSignature* CreateRootSignature(const D3D12Provider& provider,
                                         const D3D12_ROOT_SIGNATURE_DESC& desc);

ID3D12PipelineState* CreateComputePipeline(ID3D12Device* device,
                                           const void* shader,
                                           size_t shader_size,
                                           ID3D12RootSignature* root_signature);

constexpr DXGI_FORMAT GetUintPow2DXGIFormat(uint32_t element_size_bytes_log2) {
  switch (element_size_bytes_log2) {
    case 0:
      return DXGI_FORMAT_R8_UINT;
    case 1:
      return DXGI_FORMAT_R16_UINT;
    case 2:
      return DXGI_FORMAT_R32_UINT;
    case 3:
      return DXGI_FORMAT_R32G32_UINT;
    case 4:
      return DXGI_FORMAT_R32G32B32A32_UINT;
    default:
      return DXGI_FORMAT_UNKNOWN;
  }
}

inline void FillBufferResourceDesc(D3D12_RESOURCE_DESC& desc, UINT64 size,
                                   D3D12_RESOURCE_FLAGS flags) {
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Alignment = 0;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
}

void CreateBufferRawSRV(ID3D12Device* device,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle,
                        ID3D12Resource* buffer, uint32_t size,
                        uint64_t offset = 0);
void CreateBufferRawUAV(ID3D12Device* device,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle,
                        ID3D12Resource* buffer, uint32_t size,
                        uint64_t offset = 0);
void CreateBufferTypedSRV(ID3D12Device* device,
                          D3D12_CPU_DESCRIPTOR_HANDLE handle,
                          ID3D12Resource* buffer, DXGI_FORMAT format,
                          uint32_t num_elements, uint64_t first_element = 0);
void CreateBufferTypedUAV(ID3D12Device* device,
                          D3D12_CPU_DESCRIPTOR_HANDLE handle,
                          ID3D12Resource* buffer, DXGI_FORMAT format,
                          uint32_t num_elements, uint64_t first_element = 0);

}  // namespace util
}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_UTIL_H_
