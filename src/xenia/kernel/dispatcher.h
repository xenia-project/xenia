/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_DISPATCHER_H_
#define XENIA_KERNEL_XBOXKRNL_DISPATCHER_H_

#include <mutex>

#include "xenia/base/mutex.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class KernelState;
class NativeList;

class Dispatcher {
 public:
  Dispatcher(KernelState* kernel_state);
  virtual ~Dispatcher();

  KernelState* kernel_state() const { return kernel_state_; }

  void Lock();
  void Unlock();

  NativeList* dpc_list() const { return dpc_list_; }

 private:
 private:
  KernelState* kernel_state_;

  xe::mutex lock_;
  NativeList* dpc_list_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_DISPATCHER_H_
