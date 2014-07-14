/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_backend.h>

#include <alloy/backend/x64/x64_assembler.h>
#include <alloy/backend/x64/x64_code_cache.h>
#include <alloy/backend/x64/x64_sequences.h>
#include <alloy/backend/x64/x64_thunk_emitter.h>

namespace alloy {
namespace backend {
namespace x64 {

using alloy::runtime::Runtime;

X64Backend::X64Backend(Runtime* runtime) : Backend(runtime), code_cache_(0) {}

X64Backend::~X64Backend() { delete code_cache_; }

int X64Backend::Initialize() {
  int result = Backend::Initialize();
  if (result) {
    return result;
  }

  RegisterSequences();

  machine_info_.register_sets[0] = {
      0, "gpr", MachineInfo::RegisterSet::INT_TYPES, X64Emitter::GPR_COUNT,
  };
  machine_info_.register_sets[1] = {
      1, "xmm", MachineInfo::RegisterSet::FLOAT_TYPES |
                    MachineInfo::RegisterSet::VEC_TYPES,
      X64Emitter::XMM_COUNT,
  };

  code_cache_ = new X64CodeCache();
  result = code_cache_->Initialize();
  if (result) {
    return result;
  }

  // Generate thunks used to transition between jitted code and host code.
  auto allocator = std::make_unique<XbyakAllocator>();
  auto thunk_emitter = std::make_unique<X64ThunkEmitter>(this, allocator.get());
  host_to_guest_thunk_ = thunk_emitter->EmitHostToGuestThunk();
  guest_to_host_thunk_ = thunk_emitter->EmitGuestToHostThunk();

  return result;
}

std::unique_ptr<Assembler> X64Backend::CreateAssembler() {
  return std::make_unique<X64Assembler>(this);
}

}  // namespace x64
}  // namespace backend
}  // namespace alloy
