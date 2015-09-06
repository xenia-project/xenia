/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DISPATCHER_H_
#define XENIA_KERNEL_DISPATCHER_H_

#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class KernelState;
class NativeList;

// All access must be guarded by the global critical section.
class Dispatcher {
 public:
  explicit Dispatcher(KernelState* kernel_state);
  virtual ~Dispatcher();

  KernelState* kernel_state() const { return kernel_state_; }

  NativeList* dpc_list() const { return dpc_list_; }

 private:
  KernelState* kernel_state_;
  NativeList* dpc_list_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_DISPATCHER_H_
