/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_ASYNC_REQUEST_H_
#define XENIA_KERNEL_XBOXKRNL_ASYNC_REQUEST_H_

#include <vector>

#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class KernelState;
class XEvent;
class XObject;

class XAsyncRequest {
 public:
  typedef void (*CompletionCallback)(XAsyncRequest* request, void* context);

  XAsyncRequest(KernelState* kernel_state, XObject* object,
                CompletionCallback callback, void* callback_context);
  virtual ~XAsyncRequest();

  KernelState* kernel_state() const { return kernel_state_; }
  XObject* object() const { return object_; }

  void AddWaitEvent(XEvent* ev);

  // Complete(result)

 protected:
  KernelState* kernel_state_;
  XObject* object_;
  CompletionCallback callback_;
  void* callback_context_;

  std::vector<XEvent*> wait_events_;
  uint32_t apc_routine_;
  uint32_t apc_context_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_ASYNC_REQUEST_H_
