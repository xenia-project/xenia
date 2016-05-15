/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_
#define XENIA_GPU_SPIRV_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_

#include "xenia/gpu/spirv/compiler_pass.h"

namespace xe {
namespace gpu {
namespace spirv {

// Control-flow simplification pass. Combines adjacent blocks and marks
// any unreachable blocks.
class ControlFlowSimplificationPass : public CompilerPass {
 public:
  ControlFlowSimplificationPass();

  bool Run(spv::Module* module) override;

 private:
};

}  // namespace spirv
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_