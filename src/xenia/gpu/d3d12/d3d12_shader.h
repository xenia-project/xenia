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

#include <vector>

#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/shader.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12Shader : public Shader {
 public:
  D3D12Shader(ShaderType shader_type, uint64_t data_hash,
              const uint32_t* dword_ptr, uint32_t dword_count);

  void SetTexturesAndSamplers(
      const DxbcShaderTranslator::TextureSRV* texture_srvs,
      uint32_t texture_srv_count,
      const DxbcShaderTranslator::SamplerBinding* sampler_bindings,
      uint32_t sampler_binding_count);

  bool DisassembleDXBC();

  static constexpr uint32_t kMaxTextureSRVIndexBits =
      DxbcShaderTranslator::kMaxTextureSRVIndexBits;
  static constexpr uint32_t kMaxTextureSRVs =
      DxbcShaderTranslator::kMaxTextureSRVs;
  struct TextureSRV {
    uint32_t fetch_constant;
    TextureDimension dimension;
  };
  const TextureSRV* GetTextureSRVs(uint32_t& count_out) const {
    count_out = uint32_t(texture_srvs_.size());
    return texture_srvs_.data();
  }
  const uint32_t GetUsedTextureMask() const { return used_texture_mask_; }

  static constexpr uint32_t kMaxSamplerBindingIndexBits =
      DxbcShaderTranslator::kMaxSamplerBindingIndexBits;
  static constexpr uint32_t kMaxSamplerBindings =
      DxbcShaderTranslator::kMaxSamplerBindings;
  struct SamplerBinding {
    uint32_t fetch_constant;
    TextureFilter mag_filter;
    TextureFilter min_filter;
    TextureFilter mip_filter;
    AnisoFilter aniso_filter;
  };
  const SamplerBinding* GetSamplerBindings(uint32_t& count_out) const {
    count_out = uint32_t(sampler_bindings_.size());
    return sampler_bindings_.data();
  }

 private:
  std::vector<TextureSRV> texture_srvs_;
  uint32_t used_texture_mask_ = 0;
  std::vector<SamplerBinding> sampler_bindings_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_SHADER_H_
