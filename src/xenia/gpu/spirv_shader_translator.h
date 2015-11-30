/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_

#include <memory>
#include <string>
#include <vector>

#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

class SpirvShaderTranslator : public ShaderTranslator {
 public:
  SpirvShaderTranslator();
  ~SpirvShaderTranslator() override;

 protected:
  std::vector<uint8_t> CompleteTranslation() override;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
