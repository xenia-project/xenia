/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_backend.h"

#include "xenia/cpu/backend/x64/x64_assembler.h"
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/backend/x64/x64_sequences.h"
#include "xenia/cpu/backend/x64/x64_thunk_emitter.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

X64Backend::X64Backend(Processor* processor)
    : Backend(processor), code_cache_(nullptr) {}

X64Backend::~X64Backend() { delete code_cache_; }

bool X64Backend::Initialize() {
  if (!Backend::Initialize()) {
    return false;
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
  if (!code_cache_->Initialize()) {
    return false;
  }

  // Generate thunks used to transition between jitted code and host code.
  auto allocator = std::make_unique<XbyakAllocator>();
  auto thunk_emitter = std::make_unique<X64ThunkEmitter>(this, allocator.get());
  host_to_guest_thunk_ = thunk_emitter->EmitHostToGuestThunk();
  guest_to_host_thunk_ = thunk_emitter->EmitGuestToHostThunk();

  return true;
}

void X64Backend::CommitExecutableRange(uint32_t guest_low,
                                       uint32_t guest_high) {
  code_cache_->CommitExecutableRange(guest_low, guest_high);
}

std::unique_ptr<Assembler> X64Backend::CreateAssembler() {
  return std::make_unique<X64Assembler>(this);
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
