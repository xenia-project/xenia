/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_backend.h>

#include <alloy/backend/ivm/ivm_assembler.h>
#include <alloy/backend/ivm/ivm_stack.h>
#include <alloy/backend/ivm/tracing.h>

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

  machine_info_.register_sets[0] = {
    0,
    "gpr",
    MachineInfo::RegisterSet::INT_TYPES,
    16,
  };
  machine_info_.register_sets[1] = {
    1,
    "vec",
    MachineInfo::RegisterSet::FLOAT_TYPES |
    MachineInfo::RegisterSet::VEC_TYPES,
    16,
  };

  alloy::tracing::WriteEvent(EventType::Init({
  }));

  return result;
}

void* IVMBackend::AllocThreadData() {
  return new IVMStack();
}

void IVMBackend::FreeThreadData(void* thread_data) {
  auto stack = (IVMStack*)thread_data;
  delete stack;
}

Assembler* IVMBackend::CreateAssembler() {
  return new IVMAssembler(this);
}
