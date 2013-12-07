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

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::runtime;


Backend::Backend(Runtime* runtime) :
    runtime_(runtime) {
}

Backend::~Backend() {
}

int Backend::Initialize() {
  return 0;
}

void* Backend::AllocThreadData() {
  return NULL;
}

void Backend::FreeThreadData(void* thread_data) {
}
