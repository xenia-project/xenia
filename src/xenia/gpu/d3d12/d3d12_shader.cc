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
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/dxbc_shader.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12Shader::D3D12Shader(xenos::ShaderType shader_type,
                         uint64_t ucode_data_hash, const uint32_t* ucode_dwords,
                         size_t ucode_dword_count,
                         std::endian ucode_source_endian)
    : DxbcShader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
                 ucode_source_endian) {}

void D3D12Shader::D3D12Translation::DisassembleDxbcAndDxil(
    const ui::d3d12::D3D12Provider& provider, bool disassemble_dxbc,
    IDxbcConverter* dxbc_converter, IDxcUtils* dxc_utils,
    IDxcCompiler* dxc_compiler) {
  std::string disassembly;
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
      disassembly.append(
          reinterpret_cast<const char*>(dxbc_disassembly->GetBufferPointer()));
      dxbc_disassembly->Release();
    } else {
      XELOGE("Failed to disassemble DXBC shader {:016X}",
             shader().ucode_data_hash());
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
              disassembly.append("\n\n");
            }
            is_first_disassembly = false;
            disassembly.append(reinterpret_cast<const char*>(
                dxil_disassembly_utf8->GetStringPointer()));
            dxil_disassembly_utf8->Release();
          } else {
            XELOGE("Failed to get DXIL shader {:016X} disassembly as UTF-8",
                   shader().ucode_data_hash());
          }
        } else {
          XELOGE("Failed to disassemble DXIL shader {:016X}",
                 shader().ucode_data_hash());
        }
      } else {
        XELOGE("Failed to create a blob with DXIL shader {:016X}",
               shader().ucode_data_hash());
        CoTaskMemFree(dxil);
      }
    } else {
      XELOGE("Failed to convert shader {:016X} to DXIL",
             shader().ucode_data_hash());
    }
  }
  set_host_disassembly(std::move(disassembly));
}

Shader::Translation* D3D12Shader::CreateTranslationInstance(
    uint64_t modification) {
  return new D3D12Translation(*this, modification);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
