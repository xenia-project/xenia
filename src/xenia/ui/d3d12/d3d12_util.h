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

#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace ui {
namespace d3d12 {
namespace util {

template <typename T>
inline bool ReleaseAndNull(T& object) {
  if (object != nullptr) {
    object->Release();
    object = nullptr;
    return true;
  }
  return false;
};

ID3D12RootSignature* CreateRootSignature(ID3D12Device* device,
                                         const D3D12_ROOT_SIGNATURE_DESC& desc);

ID3D12PipelineState* CreateComputePipeline(ID3D12Device* device,
                                           const void* shader,
                                           size_t shader_size,
                                           ID3D12RootSignature* root_signature);

}  // namespace util
}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_UTIL_H_
