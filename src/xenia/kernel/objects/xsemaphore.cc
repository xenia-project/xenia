/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xsemaphore.h>


using namespace xe;
using namespace xe::kernel;


XSemaphore::XSemaphore(KernelState* kernel_state) :
    XObject(kernel_state, kTypeSemaphore),
    handle_(NULL) {
}

XSemaphore::~XSemaphore() {
  if (handle_) {
    CloseHandle(handle_);
  }
}

void XSemaphore::Initialize(int32_t initial_count, int32_t maximum_count) {
  assert_null(handle_);

  handle_ = CreateSemaphore(NULL, initial_count, maximum_count, NULL);
}

void XSemaphore::InitializeNative(void* native_ptr, DISPATCH_HEADER& header) {
  assert_null(handle_);

  // NOT IMPLEMENTED
  // We expect Initialize to be called shortly.
}

int32_t XSemaphore::ReleaseSemaphore(int32_t release_count) {
  LONG previous_count = 0;
  ::ReleaseSemaphore(handle_, release_count, &previous_count);
  return previous_count;
}
