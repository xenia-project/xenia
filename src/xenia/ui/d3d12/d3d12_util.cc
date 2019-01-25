/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_util.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {
namespace util {

const D3D12_HEAP_PROPERTIES kHeapPropertiesDefault = {D3D12_HEAP_TYPE_DEFAULT};
const D3D12_HEAP_PROPERTIES kHeapPropertiesUpload = {D3D12_HEAP_TYPE_UPLOAD};
const D3D12_HEAP_PROPERTIES kHeapPropertiesReadback = {
    D3D12_HEAP_TYPE_READBACK};

ID3D12RootSignature* CreateRootSignature(
    D3D12Provider* provider, const D3D12_ROOT_SIGNATURE_DESC& desc) {
  ID3DBlob* blob;
  ID3DBlob* error_blob = nullptr;
  if (FAILED(provider->SerializeRootSignature(
          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error_blob))) {
    XELOGE("Failed to serialize a root signature");
    if (error_blob != nullptr) {
      XELOGE("%s",
             reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
      error_blob->Release();
    }
    return nullptr;
  }
  if (error_blob != nullptr) {
    error_blob->Release();
  }
  ID3D12RootSignature* root_signature = nullptr;
  provider->GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(),
                                             blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root_signature));
  blob->Release();
  return root_signature;
}

ID3D12PipelineState* CreateComputePipeline(
    ID3D12Device* device, const void* shader, size_t shader_size,
    ID3D12RootSignature* root_signature) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
  desc.pRootSignature = root_signature;
  desc.CS.pShaderBytecode = shader;
  desc.CS.BytecodeLength = shader_size;
  desc.NodeMask = 0;
  desc.CachedPSO.pCachedBlob = nullptr;
  desc.CachedPSO.CachedBlobSizeInBytes = 0;
  desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  ID3D12PipelineState* pipeline = nullptr;
  device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
  return pipeline;
}

void CreateRawBufferSRV(ID3D12Device* device,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle,
                        ID3D12Resource* buffer, uint32_t size,
                        uint64_t offset) {
  assert_false(size & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  assert_false(offset & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = offset >> 2;
  desc.Buffer.NumElements = size >> 2;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(buffer, &desc, handle);
}

void CreateRawBufferUAV(ID3D12Device* device,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle,
                        ID3D12Resource* buffer, uint32_t size,
                        uint64_t offset) {
  assert_false(size & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  assert_false(offset & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = offset >> 2;
  desc.Buffer.NumElements = size >> 2;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.CounterOffsetInBytes = 0;
  desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  device->CreateUnorderedAccessView(buffer, nullptr, &desc, handle);
}

}  // namespace util
}  // namespace d3d12
}  // namespace ui
}  // namespace xe
