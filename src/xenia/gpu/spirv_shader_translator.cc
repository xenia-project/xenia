/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

namespace xe {
namespace gpu {

SpirvShaderTranslator::SpirvShaderTranslator() = default;

SpirvShaderTranslator::~SpirvShaderTranslator() = default;

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  return std::vector<uint8_t>();
}

}  // namespace gpu
}  // namespace xe
