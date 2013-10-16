/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_ASYNC_REQUEST_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_ASYNC_REQUEST_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {

class XEvent;
class XObject;


class XAsyncRequest {
public:
  typedef void (*CompletionCallback)(XAsyncRequest* request, void* context);

  XAsyncRequest(
      XObject* object,
      CompletionCallback callback, void* callback_context);
  virtual ~XAsyncRequest();

  XObject* object() const { return object_; }

  XEvent* wait_event() const { return wait_event_; }
  uint32_t apc_routine() const { return apc_routine_; }
  uint32_t apc_context() const { return apc_context_; }

  // Complete(result)

protected:
  XObject*            object_;
  CompletionCallback  callback_;
  void*               callback_context_;

  XEvent*     wait_event_;
  uint32_t    apc_routine_;
  uint32_t    apc_context_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_ASYNC_REQUEST_H_
