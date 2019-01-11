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
#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12Shader : public Shader {
 public:
  D3D12Shader(ShaderType shader_type, uint64_t data_hash,
              const uint32_t* dword_ptr, uint32_t dword_count);

  // For checking if it's a domain shader rather than a vertex shader when used
  // (since when a shader is used for the first time, it's translated either
  // into a vertex shader or a domain shader, depending on the primitive type).
  PrimitiveType GetDomainShaderPrimitiveType() const {
    return domain_shader_primitive_type_;
  }
  void SetDomainShaderPrimitiveType(PrimitiveType primitive_type) {
    domain_shader_primitive_type_ = primitive_type;
  }
  void SetTexturesAndSamplers(
      const DxbcShaderTranslator::TextureSRV* texture_srvs,
      uint32_t texture_srv_count,
      const DxbcShaderTranslator::SamplerBinding* sampler_bindings,
      uint32_t sampler_binding_count);

  void SetForcedEarlyZShaderObject(const std::vector<uint8_t>& shader_object) {
    forced_early_z_shader_ = shader_object;
  }
  // Returns the shader with forced early depth/stencil set with
  // SetForcedEarlyZShader after translation. If there's none (for example,
  // if the shader discards pixels or writes to the depth buffer), an empty
  // vector is returned.
  const std::vector<uint8_t>& GetForcedEarlyZShaderObject() const {
    return forced_early_z_shader_;
  }

  bool DisassembleDxbc(const ui::d3d12::D3D12Provider* provider);

  static constexpr uint32_t kMaxTextureSRVIndexBits =
      DxbcShaderTranslator::kMaxTextureSRVIndexBits;
  static constexpr uint32_t kMaxTextureSRVs =
      DxbcShaderTranslator::kMaxTextureSRVs;
  struct TextureSRV {
    uint32_t fetch_constant;
    TextureDimension dimension;
    bool is_signed;
    // Whether this SRV must be bound even if it's signed and all components are
    // unsigned and vice versa.
    bool is_sign_required;
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
  PrimitiveType domain_shader_primitive_type_ = PrimitiveType::kNone;

  std::vector<TextureSRV> texture_srvs_;
  uint32_t used_texture_mask_ = 0;
  std::vector<SamplerBinding> sampler_bindings_;

  std::vector<uint8_t> forced_early_z_shader_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_SHADER_H_
