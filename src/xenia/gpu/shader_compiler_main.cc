/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include <string>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/string.h"
#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

int shader_compiler_main(const std::vector<std::wstring>& args) {
  ShaderTranslator c;
  return 0;
}

}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-shader-compiler",
                   L"xenia-gpu-shader-compiler shader.bin",
                   xe::gpu::shader_compiler_main);
