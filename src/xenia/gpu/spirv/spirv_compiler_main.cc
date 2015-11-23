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
#include "xenia/gpu/spirv/spirv_compiler.h"
#include "xenia/gpu/spirv/spirv_util.h"

namespace xe {
namespace gpu {
namespace spirv {

int compiler_main(const std::vector<std::wstring>& args) {
  SpirvCompiler c;
  return 0;
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-spirv-compiler",
                   L"xenia-gpu-spirv-compiler shader.bin",
                   xe::gpu::spirv::compiler_main);
