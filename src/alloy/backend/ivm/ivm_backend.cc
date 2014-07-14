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

namespace alloy {
namespace backend {
namespace ivm {

using alloy::runtime::Runtime;

IVMBackend::IVMBackend(Runtime* runtime) : Backend(runtime) {}

IVMBackend::~IVMBackend() = default;

int IVMBackend::Initialize() {
  int result = Backend::Initialize();
  if (result) {
    return result;
  }

  machine_info_.register_sets[0] = {
      0, "gpr", MachineInfo::RegisterSet::INT_TYPES, 16,
  };
  machine_info_.register_sets[1] = {
      1, "vec", MachineInfo::RegisterSet::FLOAT_TYPES |
                    MachineInfo::RegisterSet::VEC_TYPES,
      16,
  };

  return result;
}

void* IVMBackend::AllocThreadData() { return new IVMStack(); }

void IVMBackend::FreeThreadData(void* thread_data) {
  auto stack = (IVMStack*)thread_data;
  delete stack;
}

std::unique_ptr<Assembler> IVMBackend::CreateAssembler() {
  return std::make_unique<IVMAssembler>(this);
}

}  // namespace ivm
}  // namespace backend
}  // namespace alloy
