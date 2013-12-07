/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_backend.h>

#include <alloy/backend/tracing.h>
#include <alloy/backend/ivm/ivm_assembler.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::ivm;
using namespace alloy::runtime;


IVMBackend::IVMBackend(Runtime* runtime) :
    Backend(runtime) {
}

IVMBackend::~IVMBackend() {
  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
}

int IVMBackend::Initialize() {
  int result = Backend::Initialize();
  if (result) {
    return result;
  }

  alloy::tracing::WriteEvent(EventType::Init({
  }));

  return result;
}

Assembler* IVMBackend::CreateAssembler() {
  return new IVMAssembler(this);
}
