/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_SHADER_H_
#define XENIA_GPU_D3D12_D3D12_SHADER_H_

#include "xenia/gpu/shader.h"
#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12Shader : public Shader {
 public:
  D3D12Shader(ShaderType shader_type, uint64_t data_hash,
              const uint32_t* dword_ptr, uint32_t dword_count);
  ~D3D12Shader() override;

  bool Prepare();

  const uint8_t* GetDXBC() const;
  size_t GetDXBCSize() const;

 private:
  ID3DBlob* blob_ = nullptr;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_SHADER_H_
