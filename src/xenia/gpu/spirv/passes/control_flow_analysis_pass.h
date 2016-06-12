/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_PASSES_CONTROL_FLOW_ANALYSIS_PASS_H_
#define XENIA_GPU_SPIRV_PASSES_CONTROL_FLOW_ANALYSIS_PASS_H_

#include "xenia/gpu/spirv/compiler_pass.h"

namespace xe {
namespace gpu {
namespace spirv {

// Control-flow analysis pass. Runs through control-flow and adds merge opcodes
// where necessary.
class ControlFlowAnalysisPass : public CompilerPass {
 public:
  ControlFlowAnalysisPass();

  bool Run(spv::Module* module) override;

 private:
};

}  // namespace spirv
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_PASSES_CONTROL_FLOW_ANALYSIS_PASS_H_