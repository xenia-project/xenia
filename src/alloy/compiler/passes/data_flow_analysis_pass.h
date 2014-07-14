/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_DATA_FLOW_ANALYSIS_PASS_H_
#define ALLOY_COMPILER_PASSES_DATA_FLOW_ANALYSIS_PASS_H_

#include <alloy/compiler/compiler_pass.h>


namespace alloy {
namespace compiler {
namespace passes {


class DataFlowAnalysisPass : public CompilerPass {
public:
  DataFlowAnalysisPass();
  ~DataFlowAnalysisPass() override;

  int Run(hir::HIRBuilder* builder) override;

private:
  uint32_t LinearizeBlocks(hir::HIRBuilder* builder);
  void AnalyzeFlow(hir::HIRBuilder* builder, uint32_t block_count);
};


}  // namespace passes
}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASSES_DATA_FLOW_ANALYSIS_PASS_H_
