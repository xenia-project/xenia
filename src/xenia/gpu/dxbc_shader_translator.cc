/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include <cstring>

#include "third_party/dxbc/DXBCChecksum.h"
#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

namespace xe {
namespace gpu {
using namespace ucode;

DxbcShaderTranslator::DxbcShaderTranslator() {}
DxbcShaderTranslator::~DxbcShaderTranslator() = default;

void DxbcShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  shader_code_.clear();
}

std::vector<uint8_t> DxbcShaderTranslator::CompleteTranslation() {
  shader_object_.clear();

  // Shader object header.
  shader_object_.push_back('CBXD');
  // Reserve space for the checksum.
  for (uint32_t i = 0; i < 4; ++i) {
    shader_object_.push_back(0);
  }
  shader_object_.push_back(1);
  // Reserve space for the size.
  shader_object_.push_back(0);
  // 5 chunks - RDEF, ISGN, OSGN, SHEX, STAT.
  shader_object_.push_back(5);

  // Copy bytes out.
  // TODO(Triang3l): avoid copy?
  std::vector<uint8_t> shader_object_bytes;
  shader_object_bytes.resize(shader_object_.size() * sizeof(uint32_t));
  std::memcpy(shader_object_bytes.data(), shader_object_.data(),
              shader_object_bytes.size());
  return shader_object_bytes;
}

}  // namespace gpu
}  // namespace xe
