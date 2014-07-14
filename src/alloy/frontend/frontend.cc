/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/frontend.h>

#include <alloy/runtime/runtime.h>

namespace alloy {
namespace frontend {

Frontend::Frontend(runtime::Runtime* runtime) : runtime_(runtime) {}

Frontend::~Frontend() = default;

Memory* Frontend::memory() const { return runtime_->memory(); }

int Frontend::Initialize() { return 0; }

}  // namespace frontend
}  // namespace alloy
