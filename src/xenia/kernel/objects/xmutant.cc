/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xmutant.h>


using namespace xe;
using namespace xe::kernel;


XMutant::XMutant(KernelState* kernel_state) :
    XObject(kernel_state, kTypeMutant),
    handle_(NULL) {
}

XMutant::~XMutant() {
  if (handle_) {
    CloseHandle(handle_);
  }
}

void XMutant::Initialize(bool initial_owner) {
  assert_null(handle_);

  handle_ = CreateMutex(NULL, initial_owner ? TRUE : FALSE, NULL);
}

void XMutant::InitializeNative(void* native_ptr, DISPATCH_HEADER& header) {
  assert_null(handle_);

  // Haven't seen this yet, but it's possible.
  assert_always();
}

X_STATUS XMutant::ReleaseMutant(
    uint32_t priority_increment, bool abandon, bool wait) {
  // TODO(benvanik): abandoning.
  assert_false(abandon);
  BOOL result = ReleaseMutex(handle_);
  if (result) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_MUTANT_NOT_OWNED;
  }
}
