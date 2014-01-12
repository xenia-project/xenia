/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/dispatcher.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/native_list.h>


using namespace xe;
using namespace xe::kernel;


Dispatcher::Dispatcher(KernelState* kernel_state) :
    kernel_state_(kernel_state) {
  lock_ = xe_mutex_alloc();
  dpc_list_ = new NativeList(kernel_state->memory());
}

Dispatcher::~Dispatcher() {
  delete dpc_list_;
  xe_mutex_free(lock_);
}

void Dispatcher::Lock() {
  xe_mutex_lock(lock_);
}

void Dispatcher::Unlock() {
  xe_mutex_unlock(lock_);
}
