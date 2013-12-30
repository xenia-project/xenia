/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/optimizer/optimizer.h>

#include <alloy/backend/x64/tracing.h>
#include <alloy/backend/x64/optimizer/optimizer_pass.h>

using namespace alloy;
using namespace alloy::backend::x64::lir;
using namespace alloy::backend::x64::optimizer;
using namespace alloy::runtime;


Optimizer::Optimizer(Runtime* runtime) :
    runtime_(runtime) {
  alloy::tracing::WriteEvent(EventType::Init({
  }));
}

Optimizer::~Optimizer() {
  Reset();

  for (auto it = passes_.begin(); it != passes_.end(); ++it) {
    OptimizerPass* pass = *it;
    delete pass;
  }

  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
}

void Optimizer::AddPass(OptimizerPass* pass) {
  pass->Initialize(this);
  passes_.push_back(pass);
}

void Optimizer::Reset() {
}

int Optimizer::Optimize(LIRBuilder* builder) {
  // TODO(benvanik): sophisticated stuff. Run passes in parallel, run until they
  //                 stop changing things, etc.
  for (auto it = passes_.begin(); it != passes_.end(); ++it) {
    OptimizerPass* pass = *it;
    if (pass->Run(builder)) {
      return 1;
    }
  }

  return 0;
}
