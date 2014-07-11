/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/backend.h>

#include <alloy/backend/tracing.h>

namespace alloy {
namespace backend {

using alloy::runtime::Runtime;

Backend::Backend(Runtime* runtime) : runtime_(runtime) {
  xe_zero_struct(&machine_info_, sizeof(machine_info_));
}

Backend::~Backend() {}

int Backend::Initialize() { return 0; }

void* Backend::AllocThreadData() { return NULL; }

void Backend::FreeThreadData(void* thread_data) {}

}  // namespace backend
}  // namespace alloy
