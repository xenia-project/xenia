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

void D3D12Shader::DisassembleDxbc(const ui::d3d12::D3D12Provider& provider,
                                  bool disassemble_dxbc,
                                  IDxbcConverter* dxbc_converter,
                                  IDxcUtils* dxc_utils,
                                  IDxcCompiler* dxc_compiler) {
  bool is_first_disassembly = true;
  if (disassemble_dxbc) {
    ID3DBlob* dxbc_disassembly;
    if (SUCCEEDED(provider.Disassemble(translated_binary().data(),
                                       translated_binary().size(),
                                       D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING |
                                           D3D_DISASM_ENABLE_INSTRUCTION_OFFSET,
                                       nullptr, &dxbc_disassembly))) {
      assert_true(is_first_disassembly);
      is_first_disassembly = false;
      host_disassembly_.append(
          reinterpret_cast<const char*>(dxbc_disassembly->GetBufferPointer()));
      dxbc_disassembly->Release();
    } else {
      XELOGE("Failed to disassemble DXBC shader {:016X}", ucode_data_hash());
    }
  }
  if (dxbc_converter && dxc_utils && dxc_compiler) {
    void* dxil;
    UINT32 dxil_size;
    if (SUCCEEDED(dxbc_converter->Convert(
            translated_binary().data(), UINT32(translated_binary().size()),
            nullptr, &dxil, &dxil_size, nullptr)) &&
        dxil != nullptr) {
      IDxcBlobEncoding* dxil_blob;
      if (SUCCEEDED(dxc_utils->CreateBlobFromPinned(dxil, dxil_size, DXC_CP_ACP,
                                                    &dxil_blob))) {
        IDxcBlobEncoding* dxil_disassembly;
        bool dxil_disassembled =
            SUCCEEDED(dxc_compiler->Disassemble(dxil_blob, &dxil_disassembly));
        dxil_blob->Release();
        CoTaskMemFree(dxil);
        if (dxil_disassembled) {
          IDxcBlobUtf8* dxil_disassembly_utf8;
          bool dxil_disassembly_got_utf8 = SUCCEEDED(dxc_utils->GetBlobAsUtf8(
              dxil_disassembly, &dxil_disassembly_utf8));
          dxil_disassembly->Release();
          if (dxil_disassembly_got_utf8) {
            if (!is_first_disassembly) {
              host_disassembly_.append("\n\n");
            }
            is_first_disassembly = false;
            host_disassembly_.append(reinterpret_cast<const char*>(
                dxil_disassembly_utf8->GetStringPointer()));
            dxil_disassembly_utf8->Release();
          } else {
            XELOGE("Failed to get DXIL shader {:016X} disassembly as UTF-8",
                   ucode_data_hash());
          }
        } else {
          XELOGE("Failed to disassemble DXIL shader {:016X}",
                 ucode_data_hash());
        }
      } else {
        XELOGE("Failed to create a blob with DXIL shader {:016X}",
               ucode_data_hash());
        CoTaskMemFree(dxil);
      }
    } else {
      XELOGE("Failed to convert shader {:016X} to DXIL", ucode_data_hash());
    }
  }
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
