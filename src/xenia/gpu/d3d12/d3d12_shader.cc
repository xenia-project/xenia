/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_shader.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace gpu {
namespace d3d12 {

constexpr uint32_t D3D12Shader::kMaxTextureSRVIndexBits;
constexpr uint32_t D3D12Shader::kMaxTextureSRVs;
constexpr uint32_t D3D12Shader::kMaxSamplerBindingIndexBits;
constexpr uint32_t D3D12Shader::kMaxSamplerBindings;

D3D12Shader::D3D12Shader(ShaderType shader_type, uint64_t data_hash,
                         const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count) {}

void D3D12Shader::SetTexturesAndSamplers(
    const DxbcShaderTranslator::TextureSRV* texture_srvs,
    uint32_t texture_srv_count,
    const DxbcShaderTranslator::SamplerBinding* sampler_bindings,
    uint32_t sampler_binding_count) {
  texture_srvs_.clear();
  texture_srvs_.reserve(texture_srv_count);
  used_texture_mask_ = 0;
  for (uint32_t i = 0; i < texture_srv_count; ++i) {
    TextureSRV srv;
    const DxbcShaderTranslator::TextureSRV& translator_srv = texture_srvs[i];
    srv.fetch_constant = translator_srv.fetch_constant;
    srv.dimension = translator_srv.dimension;
    srv.is_signed = translator_srv.is_signed;
    srv.is_sign_required = translator_srv.is_sign_required;
    texture_srvs_.push_back(srv);
    used_texture_mask_ |= 1u << translator_srv.fetch_constant;
  }
  sampler_bindings_.clear();
  sampler_bindings_.reserve(sampler_binding_count);
  for (uint32_t i = 0; i < sampler_binding_count; ++i) {
    SamplerBinding sampler;
    const DxbcShaderTranslator::SamplerBinding& translator_sampler =
        sampler_bindings[i];
    sampler.fetch_constant = translator_sampler.fetch_constant;
    sampler.mag_filter = translator_sampler.mag_filter;
    sampler.min_filter = translator_sampler.min_filter;
    sampler.mip_filter = translator_sampler.mip_filter;
    sampler.aniso_filter = translator_sampler.aniso_filter;
    sampler_bindings_.push_back(sampler);
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
