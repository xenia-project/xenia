/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xmutant.h"

namespace xe {
namespace kernel {

XMutant::XMutant(KernelState* kernel_state)
    : XObject(kernel_state, kTypeMutant) {}

XMutant::~XMutant() = default;

void XMutant::Initialize(bool initial_owner) {
  assert_false(mutant_);

  mutant_ = xe::threading::Mutant::Create(initial_owner);
}

void XMutant::InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header) {
  assert_false(mutant_);

  // Haven't seen this yet, but it's possible.
  assert_always();
}

X_STATUS XMutant::ReleaseMutant(uint32_t priority_increment, bool abandon,
                                bool wait) {
  // TODO(benvanik): abandoning.
  assert_false(abandon);
  if (mutant_->Release()) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_MUTANT_NOT_OWNED;
  }
}

}  // namespace kernel
}  // namespace xe
