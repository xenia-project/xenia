/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/optimizer/passes/redundant_mov_pass.h>

using namespace alloy;
using namespace alloy::backend::x64::lir;
using namespace alloy::backend::x64::optimizer;
using namespace alloy::backend::x64::optimizer::passes;


RedundantMovPass::RedundantMovPass() :
    OptimizerPass() {
}

RedundantMovPass::~RedundantMovPass() {
}

int RedundantMovPass::Run(LIRBuilder* builder) {
  return 0;
}
