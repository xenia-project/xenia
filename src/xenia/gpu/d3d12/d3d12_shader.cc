/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_shader.h"

#include <gflags/gflags.h>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/gpu_flags.h"

DEFINE_bool(d3d12_shader_disasm, true,
            "Disassemble translated shaders after compilation.");

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

bool D3D12Shader::Prepare() {
  assert_null(blob_);
  assert_true(is_valid());

  const char* target;
  switch (shader_type_) {
    case ShaderType::kVertex:
      target = "vs_5_1";
      break;
    case ShaderType::kPixel:
      target = "ps_5_1";
      break;
    default:
      assert_unhandled_case(shader_type_);
      return false;
  }

  // TODO(Triang3l): Choose the appropriate optimization level based on compile
  // time and how invariance is handled in vertex shaders.
  ID3DBlob* error_blob = nullptr;
  bool compiled = SUCCEEDED(
      D3DCompile(translated_binary_.data(), translated_binary_.size(), nullptr,
                 nullptr, nullptr, "main", target,
                 D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &blob_, &error_blob));

  if (!compiled) {
    XELOGE("%s shader %.16llX compilation failed!", target, ucode_data_hash());
  }
  if (error_blob != nullptr) {
    if (compiled) {
      XELOGW("%s shader %.16llX compiled with warnings!", target,
             ucode_data_hash());
      XELOGW("%s",
             reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
      XELOGW("HLSL source:");
      // The buffer isn't terminated.
      translated_binary_.push_back(0);
      XELOGW("%s", reinterpret_cast<const char*>(translated_binary_.data()));
      translated_binary_.pop_back();
    } else {
      XELOGE("%s",
             reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
      XELOGE("HLSL source:");
      translated_binary_.push_back(0);
      XELOGE("%s", reinterpret_cast<const char*>(translated_binary_.data()));
      translated_binary_.pop_back();
    }
    error_blob->Release();
  }

  if (!compiled) {
    return false;
  }

  if (FLAGS_d3d12_shader_disasm) {
    ID3DBlob* disassembly_blob;
    if (SUCCEEDED(D3DDisassemble(blob_->GetBufferPointer(),
                                 blob_->GetBufferSize(), 0, nullptr,
                                 &disassembly_blob))) {
      host_disassembly_ =
          reinterpret_cast<const char*>(disassembly_blob->GetBufferPointer());
      disassembly_blob->Release();
    } else {
      XELOGE("Failed to disassemble DXBC for %s shader %.16llX", target,
             ucode_data_hash());
    }
  }

  return true;
}

const uint8_t* D3D12Shader::GetDXBC() const {
  assert_not_null(blob_);
  return reinterpret_cast<const uint8_t*>(blob_->GetBufferPointer());
}

size_t D3D12Shader::GetDXBCSize() const {
  assert_not_null(blob_);
  return blob_->GetBufferSize();
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
