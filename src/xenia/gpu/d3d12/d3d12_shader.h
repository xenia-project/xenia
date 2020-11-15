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
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12Shader : public Shader {
 public:
  D3D12Shader(xenos::ShaderType shader_type, uint64_t data_hash,
              const uint32_t* dword_ptr, uint32_t dword_count);

  void SetTexturesAndSamplers(
      const DxbcShaderTranslator::TextureBinding* texture_bindings,
      uint32_t texture_binding_count,
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

  void DisassembleDxbc(const ui::d3d12::D3D12Provider& provider,
                       bool disassemble_dxbc,
                       IDxbcConverter* dxbc_converter = nullptr,
                       IDxcUtils* dxc_utils = nullptr,
                       IDxcCompiler* dxc_compiler = nullptr);

  static constexpr uint32_t kMaxTextureBindingIndexBits =
      DxbcShaderTranslator::kMaxTextureBindingIndexBits;
  static constexpr uint32_t kMaxTextureBindings =
      DxbcShaderTranslator::kMaxTextureBindings;
  struct TextureBinding {
    uint32_t bindless_descriptor_index;
    uint32_t fetch_constant;
    // Stacked and 3D are separate TextureBindings, even for bindless for null
    // descriptor handling simplicity.
    xenos::FetchOpDimension dimension;
    bool is_signed;
  };
  // Safe to hash and compare with memcmp for layout hashing.
  const TextureBinding* GetTextureBindings(uint32_t& count_out) const {
    count_out = uint32_t(texture_bindings_.size());
    return texture_bindings_.data();
  }
  const uint32_t GetUsedTextureMask() const { return used_texture_mask_; }

  static constexpr uint32_t kMaxSamplerBindingIndexBits =
      DxbcShaderTranslator::kMaxSamplerBindingIndexBits;
  static constexpr uint32_t kMaxSamplerBindings =
      DxbcShaderTranslator::kMaxSamplerBindings;
  struct SamplerBinding {
    uint32_t bindless_descriptor_index;
    uint32_t fetch_constant;
    xenos::TextureFilter mag_filter;
    xenos::TextureFilter min_filter;
    xenos::TextureFilter mip_filter;
    xenos::AnisoFilter aniso_filter;
  };
  const SamplerBinding* GetSamplerBindings(uint32_t& count_out) const {
    count_out = uint32_t(sampler_bindings_.size());
    return sampler_bindings_.data();
  }

  // For owning subsystems like the pipeline cache, accessors for unique
  // identifiers (used instead of hashes to make sure collisions can't happen)
  // of binding layouts used by the shader, for invalidation if a shader with an
  // incompatible layout was bound.
  size_t GetTextureBindingLayoutUserUID() const {
    return texture_binding_layout_user_uid_;
  }
  void SetTextureBindingLayoutUserUID(size_t uid) {
    texture_binding_layout_user_uid_ = uid;
  }
  size_t GetSamplerBindingLayoutUserUID() const {
    return sampler_binding_layout_user_uid_;
  }
  void SetSamplerBindingLayoutUserUID(size_t uid) {
    sampler_binding_layout_user_uid_ = uid;
  }

 private:
  std::vector<TextureBinding> texture_bindings_;
  std::vector<SamplerBinding> sampler_bindings_;
  size_t texture_binding_layout_user_uid_ = 0;
  size_t sampler_binding_layout_user_uid_ = 0;
  uint32_t used_texture_mask_ = 0;

  std::vector<uint8_t> forced_early_z_shader_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_SHADER_H_
