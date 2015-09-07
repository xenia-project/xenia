/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xsemaphore.h"

namespace xe {
namespace kernel {

XSemaphore::XSemaphore(KernelState* kernel_state)
    : XObject(kernel_state, kTypeSemaphore) {}

XSemaphore::~XSemaphore() = default;

void XSemaphore::Initialize(int32_t initial_count, int32_t maximum_count) {
  assert_false(semaphore_);

  CreateNative(sizeof(X_KSEMAPHORE));

  semaphore_ = xe::threading::Semaphore::Create(initial_count, maximum_count);
}

void XSemaphore::InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header) {
  assert_false(semaphore_);

  auto semaphore = reinterpret_cast<X_KSEMAPHORE*>(native_ptr);
  semaphore_ = xe::threading::Semaphore::Create(semaphore->header.signal_state,
                                                semaphore->limit);
}

int32_t XSemaphore::ReleaseSemaphore(int32_t release_count) {
  int32_t previous_count = 0;
  semaphore_->Release(release_count, &previous_count);
  return previous_count;
}

}  // namespace kernel
}  // namespace xe
