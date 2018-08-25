/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_util.h"

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {
namespace util {

ID3D12RootSignature* CreateRootSignature(
    ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc) {
  ID3DBlob* blob;
  ID3DBlob* error_blob = nullptr;
  if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &blob, &error_blob))) {
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
  device->CreateRootSignature(0, blob->GetBufferPointer(),
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

}  // namespace util
}  // namespace d3d12
}  // namespace ui
}  // namespace xe
