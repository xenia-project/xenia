/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_DXBC_SHADER_H_
#define XENIA_GPU_DXBC_SHADER_H_

#include <vector>

#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

class DxbcShader : public Shader {
 public:
  class DxbcTranslation : public Translation {
   public:
    DxbcTranslation(DxbcShader& shader, uint32_t modification)
        : Translation(shader, modification) {}
  };

  DxbcShader(xenos::ShaderType shader_type, uint64_t data_hash,
             const uint32_t* dword_ptr, uint32_t dword_count);

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

 protected:
  Translation* CreateTranslationInstance(uint32_t modification) override;

 private:
  friend class DxbcShaderTranslator;

  std::vector<TextureBinding> texture_bindings_;
  std::vector<SamplerBinding> sampler_bindings_;
  uint32_t used_texture_mask_ = 0;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DXBC_SHADER_H_
