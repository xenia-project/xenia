/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/frontend.h>

#include <alloy/frontend/tracing.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::frontend;
using namespace alloy::runtime;


Frontend::Frontend(Runtime* runtime) :
    runtime_(runtime) {
}

Frontend::~Frontend() {
}

Memory* Frontend::memory() const {
  return runtime_->memory();
}

int Frontend::Initialize() {
  return 0;
}
