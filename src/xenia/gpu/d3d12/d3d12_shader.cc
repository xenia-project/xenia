/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_shader.h"

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace gpu {
namespace d3d12 {

constexpr uint32_t D3D12Shader::kMaxTextureBindingIndexBits;
constexpr uint32_t D3D12Shader::kMaxTextureBindings;
constexpr uint32_t D3D12Shader::kMaxSamplerBindingIndexBits;
constexpr uint32_t D3D12Shader::kMaxSamplerBindings;

D3D12Shader::D3D12Shader(xenos::ShaderType shader_type, uint64_t data_hash,
                         const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count) {}

void D3D12Shader::SetTexturesAndSamplers(
    const DxbcShaderTranslator::TextureBinding* texture_bindings,
    uint32_t texture_binding_count,
    const DxbcShaderTranslator::SamplerBinding* sampler_bindings,
    uint32_t sampler_binding_count) {
  texture_bindings_.clear();
  texture_bindings_.reserve(texture_binding_count);
  used_texture_mask_ = 0;
  for (uint32_t i = 0; i < texture_binding_count; ++i) {
    TextureBinding& binding = texture_bindings_.emplace_back();
    // For a stable hash.
    std::memset(&binding, 0, sizeof(binding));
    const DxbcShaderTranslator::TextureBinding& translator_binding =
        texture_bindings[i];
    binding.bindless_descriptor_index =
        translator_binding.bindless_descriptor_index;
    binding.fetch_constant = translator_binding.fetch_constant;
    binding.dimension = translator_binding.dimension;
    binding.is_signed = translator_binding.is_signed;
    used_texture_mask_ |= 1u << translator_binding.fetch_constant;
  }
  sampler_bindings_.clear();
  sampler_bindings_.reserve(sampler_binding_count);
  for (uint32_t i = 0; i < sampler_binding_count; ++i) {
    SamplerBinding binding;
    const DxbcShaderTranslator::SamplerBinding& translator_binding =
        sampler_bindings[i];
    binding.bindless_descriptor_index =
        translator_binding.bindless_descriptor_index;
    binding.fetch_constant = translator_binding.fetch_constant;
    binding.mag_filter = translator_binding.mag_filter;
    binding.min_filter = translator_binding.min_filter;
    binding.mip_filter = translator_binding.mip_filter;
    binding.aniso_filter = translator_binding.aniso_filter;
    sampler_bindings_.push_back(binding);
  }
}

bool D3D12Shader::DisassembleDxbc(const ui::d3d12::D3D12Provider* provider) {
  if (!host_disassembly_.empty()) {
    return true;
  }
  ID3DBlob* blob;
  if (FAILED(provider->Disassemble(translated_binary().data(),
                                   translated_binary().size(),
                                   D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING |
                                       D3D_DISASM_ENABLE_INSTRUCTION_OFFSET,
                                   nullptr, &blob))) {
    return false;
  }
  host_disassembly_ = reinterpret_cast<const char*>(blob->GetBufferPointer());
  blob->Release();
  return true;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
