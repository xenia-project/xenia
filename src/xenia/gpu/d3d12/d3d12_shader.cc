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

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12Shader::D3D12Shader(ShaderType shader_type, uint64_t data_hash,
                         const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count) {}

D3D12Shader::~D3D12Shader() {
  if (blob_ != nullptr) {
    blob_->Release();
  }
}

void D3D12Shader::SetTexturesAndSamplers(
    const DxbcShaderTranslator::TextureSRV* texture_srvs,
    uint32_t texture_srv_count, const uint32_t* sampler_fetch_constants,
    uint32_t sampler_count) {
  used_texture_mask_ = 0;
  for (uint32_t i = 0; i < texture_srv_count; ++i) {
    TextureSRV& srv = texture_srvs_[i];
    const DxbcShaderTranslator::TextureSRV& translator_srv = texture_srvs[i];
    srv.fetch_constant = translator_srv.fetch_constant;
    srv.dimension = translator_srv.dimension;
    used_texture_mask_ |= 1u << translator_srv.fetch_constant;
  }
  texture_srv_count_ = texture_srv_count;
#if 0
  // If there's a texture, there's a sampler for it.
  used_texture_mask_ = 0;
  for (uint32_t i = 0; i < sampler_count; ++i) {
    uint32_t sampler_fetch_constant = sampler_fetch_constants[i];
    sampler_fetch_constants_[i] = sampler_fetch_constant;
    used_texture_mask_ |= 1u << sampler_fetch_constant;
  }
  sampler_count_ = sampler_count;
#endif
}

bool D3D12Shader::DisassembleDXBC() {
  if (!host_disassembly_.empty()) {
    return true;
  }
  ID3DBlob* blob;
  if (FAILED(D3DDisassemble(translated_binary().data(),
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
