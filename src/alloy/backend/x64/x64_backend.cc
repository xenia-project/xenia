/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_backend.h>

#include <alloy/backend/x64/tracing.h>
#include <alloy/backend/x64/x64_assembler.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::runtime;


X64Backend::X64Backend(Runtime* runtime) :
    Backend(runtime) {
}

X64Backend::~X64Backend() {
  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
}

int X64Backend::Initialize() {
  int result = Backend::Initialize();
  if (result) {
    return result;
  }

  alloy::tracing::WriteEvent(EventType::Init({
  }));

  return result;
}

Assembler* X64Backend::CreateAssembler() {
  return new X64Assembler(this);
}
