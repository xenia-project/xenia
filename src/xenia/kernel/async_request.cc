/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/async_request.h"

#include "xenia/kernel/xobject.h"
#include "xenia/kernel/objects/xevent.h"

namespace xe {
namespace kernel {

XAsyncRequest::XAsyncRequest(KernelState* kernel_state, XObject* object,
                             CompletionCallback callback,
                             void* callback_context)
    : kernel_state_(kernel_state),
      object_(object),
      callback_(callback),
      callback_context_(callback_context),
      apc_routine_(0),
      apc_context_(0) {
  object_->Retain();
}

XAsyncRequest::~XAsyncRequest() {
  for (auto it = wait_events_.begin(); it != wait_events_.end(); ++it) {
    (*it)->Release();
  }
  object_->Release();
}

void XAsyncRequest::AddWaitEvent(XEvent* ev) {
  ev->Retain();
  wait_events_.push_back(ev);
}

}  // namespace kernel
}  // namespace xe
