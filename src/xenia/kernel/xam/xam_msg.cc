/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XMsgInProcessCall(dword_t app, dword_t message, dword_t arg1,
                                 dword_t arg2) {
  auto result = kernel_state()->app_manager()->DispatchMessageSync(app, message,
                                                                   arg1, arg2);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgInProcessCall: app %.8X undefined", (uint32_t)app);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XMsgInProcessCall, kNone, kImplemented);

dword_result_t XMsgSystemProcessCall(dword_t app, dword_t message,
                                     dword_t buffer, dword_t buffer_length) {
  auto result = kernel_state()->app_manager()->DispatchMessageAsync(
      app, message, buffer, buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgSystemProcessCall: app %.8X undefined", (uint32_t)app);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XMsgSystemProcessCall, kNone, kImplemented);

dword_result_t XMsgStartIORequest(dword_t app, dword_t message,
                                  pointer_t<XAM_OVERLAPPED> overlapped_ptr,
                                  dword_t buffer, dword_t buffer_length) {
  auto result = kernel_state()->app_manager()->DispatchMessageAsync(
      app, message, buffer, buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgStartIORequest: app %.8X undefined", (uint32_t)app);
    XThread::SetLastError(X_ERROR_NOT_FOUND);
  }
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    result = X_ERROR_IO_PENDING;
  }
  if (result == X_ERROR_SUCCESS || X_ERROR_IO_PENDING) {
    XThread::SetLastError(0);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XMsgStartIORequest, kNone, kImplemented);

dword_result_t XMsgStartIORequestEx(dword_t app, dword_t message,
                                    pointer_t<XAM_OVERLAPPED> overlapped_ptr,
                                    dword_t buffer, dword_t buffer_length,
                                    lpdword_t unknown_ptr) {
  auto result = kernel_state()->app_manager()->DispatchMessageAsync(
      app, message, buffer, buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgStartIORequestEx: app %.8X undefined", (uint32_t)app);
  }
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    result = X_ERROR_IO_PENDING;
  }
  return result;
}
DECLARE_XAM_EXPORT1(XMsgStartIORequestEx, kNone, kImplemented);

dword_result_t XMsgCancelIORequest(pointer_t<XAM_OVERLAPPED> overlapped_ptr,
                                   dword_t wait) {
  X_HANDLE event_handle = XOverlappedGetEvent(overlapped_ptr);
  if (event_handle && wait) {
    auto ev =
        kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
    if (ev) {
      ev->Wait(0, 0, true, nullptr);
    }
  }

  return 0;
}
DECLARE_XAM_EXPORT1(XMsgCancelIORequest, kNone, kImplemented);

void RegisterMsgExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe