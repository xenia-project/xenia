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

namespace xe {
namespace kernel {

Dispatcher::Dispatcher(KernelState* kernel_state)
    : kernel_state_(kernel_state) {
  dpc_list_ = new NativeList(kernel_state->memory());
}

Dispatcher::~Dispatcher() { delete dpc_list_; }

void Dispatcher::Lock() { lock_.lock(); }

void Dispatcher::Unlock() { lock_.unlock(); }

}  // namespace kernel
}  // namespace xe
