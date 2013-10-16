/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/async_request.h>

#include <xenia/kernel/modules/xboxkrnl/xobject.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xevent.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XAsyncRequest::XAsyncRequest(
    XObject* object,
    CompletionCallback callback, void* callback_context) :
    object_(object),
    callback_(callback), callback_context_(callback_context),
    wait_event_(0),
    apc_routine_(0), apc_context_(0) {
  object_->Retain();
}

XAsyncRequest::~XAsyncRequest() {
  if (wait_event_) {
    wait_event_->Release();
  }
  object_->Release();
}
