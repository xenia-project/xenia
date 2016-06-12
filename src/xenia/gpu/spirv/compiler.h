/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_COMPILER_H_
#define XENIA_GPU_SPIRV_COMPILER_H_

#include "xenia/base/arena.h"
#include "xenia/gpu/spirv/compiler_pass.h"

#include "third_party/glslang-spirv/SpvBuilder.h"
#include "third_party/spirv/GLSL.std.450.hpp11"

namespace xe {
namespace gpu {
namespace spirv {

// SPIR-V Compiler. Designed to optimize SPIR-V code before feeding it into the
// drivers.
class Compiler {
 public:
  Compiler();

  void AddPass(std::unique_ptr<CompilerPass> pass);
  void Reset();
  bool Compile(spv::Module* module);

 private:
  std::vector<std::unique_ptr<CompilerPass>> compiler_passes_;
};

}  // namespace spirv
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_COMPILER_H_