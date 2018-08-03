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

#include "xenia/gpu/hlsl_shader_translator.h"
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

  void SetTexturesAndSamplers(
      const HlslShaderTranslator::TextureSRV* texture_srvs,
      uint32_t texture_srv_count, const uint32_t* sampler_fetch_constants,
      uint32_t sampler_count);

  bool Prepare();

  const uint8_t* GetDXBC() const;
  size_t GetDXBCSize() const;

  struct TextureSRV {
    uint32_t fetch_constant;
    TextureDimension dimension;
  };
  const TextureSRV* GetTextureSRVs(uint32_t& count_out) const {
    count_out = texture_srv_count_;
    return texture_srvs_;
  }
  const uint32_t* GetSamplerFetchConstants(uint32_t& count_out) const {
    count_out = sampler_count_;
    return sampler_fetch_constants_;
  }
  const uint32_t GetUsedTextureMask() const { return used_texture_mask_; }

 private:
  ID3DBlob* blob_ = nullptr;

  // Up to 32 2D array textures, 32 3D textures and 32 cube textures.
  TextureSRV texture_srvs_[96];
  uint32_t texture_srv_count_ = 0;
  uint32_t sampler_fetch_constants_[32];
  uint32_t sampler_count_ = 0;
  uint32_t used_texture_mask_ = 0;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_SHADER_H_
