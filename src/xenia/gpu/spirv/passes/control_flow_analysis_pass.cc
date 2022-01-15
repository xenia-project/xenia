/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv/passes/control_flow_analysis_pass.h"

namespace xe {
namespace gpu {
namespace spirv {

ControlFlowAnalysisPass::ControlFlowAnalysisPass() {}

bool ControlFlowAnalysisPass::Run(spv::Module* module) {
  for (auto function : module->getFunctions()) {
    // For each OpBranchConditional, see if we can find a point where control
    // flow converges and then append an OpSelectionMerge.
    // Potential problems: while loops constructed from branch instructions
  }

  return true;
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe
