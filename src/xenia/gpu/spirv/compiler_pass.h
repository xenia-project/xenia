/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_COMPILER_PASS_H_
#define XENIA_GPU_SPIRV_COMPILER_PASS_H_

#include "xenia/base/arena.h"

#include "third_party/glslang-spirv/SpvBuilder.h"
#include "third_party/spirv/GLSL.std.450.hpp11"

namespace xe {
namespace gpu {
namespace spirv {

class CompilerPass {
 public:
  CompilerPass() = default;
  virtual ~CompilerPass() {}

  virtual bool Run(spv::Module* module) = 0;

 private:
  xe::Arena ir_arena_;
};

}  // namespace spirv
}  // namespace gpu
}  // namespace xe

#endif