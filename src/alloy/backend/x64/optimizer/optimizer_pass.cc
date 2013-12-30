/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/optimizer/optimizer_pass.h>

#include <alloy/backend/x64/optimizer/optimizer.h>

using namespace alloy;
using namespace alloy::backend::x64::optimizer;


OptimizerPass::OptimizerPass() :
    runtime_(0), optimizer_(0) {
}

OptimizerPass::~OptimizerPass() {
}

int OptimizerPass::Initialize(Optimizer* optimizer) {
  runtime_ = optimizer->runtime();
  optimizer_ = optimizer;
  return 0;
}
