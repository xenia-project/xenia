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
#include "xenia/kernel/objects/xevent.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL XMsgInProcessCall_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t app = SHIM_GET_ARG_32(0);
  uint32_t message = SHIM_GET_ARG_32(1);
  uint32_t arg1 = SHIM_GET_ARG_32(2);
  uint32_t arg2 = SHIM_GET_ARG_32(3);

  XELOGD("XMsgInProcessCall(%.8X, %.8X, %.8X, %.8X)", app, message, arg1, arg2);

  auto result =
      state->app_manager()->DispatchMessageSync(app, message, arg1, arg2);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgInProcessCall: app %.8X undefined", app);
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMsgSystemProcessCall_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t app = SHIM_GET_ARG_32(0);
  uint32_t message = SHIM_GET_ARG_32(1);
  uint32_t buffer = SHIM_GET_ARG_32(2);
  uint32_t buffer_length = SHIM_GET_ARG_32(3);

  XELOGD("XMsgSystemProcessCall(%.8X, %.8X, %.8X, %.8X)", app, message, buffer,
         buffer_length);

  auto result = state->app_manager()->DispatchMessageAsync(app, message, buffer,
                                                           buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgSystemProcessCall: app %.8X undefined", app);
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMsgStartIORequest_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t app = SHIM_GET_ARG_32(0);
  uint32_t message = SHIM_GET_ARG_32(1);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(2);
  uint32_t buffer = SHIM_GET_ARG_32(3);
  uint32_t buffer_length = SHIM_GET_ARG_32(4);

  XELOGD("XMsgStartIORequest(%.8X, %.8X, %.8X, %.8X, %d)", app, message,
         overlapped_ptr, buffer, buffer_length);

  auto result = state->app_manager()->DispatchMessageAsync(app, message, buffer,
                                                           buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgStartIORequest: app %.8X undefined", app);
  }
  if (overlapped_ptr) {
    state->CompleteOverlappedImmediate(overlapped_ptr, result);
    result = X_ERROR_IO_PENDING;
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMsgStartIORequestEx_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t app = SHIM_GET_ARG_32(0);
  uint32_t message = SHIM_GET_ARG_32(1);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(2);
  uint32_t buffer = SHIM_GET_ARG_32(3);
  uint32_t buffer_length = SHIM_GET_ARG_32(4);
  uint32_t unknown_ptr = SHIM_GET_ARG_32(5);

  XELOGD("XMsgStartIORequestEx(%.8X, %.8X, %.8X, %.8X, %d, %.8X)", app, message,
         overlapped_ptr, buffer, buffer_length, unknown_ptr);

  auto result = state->app_manager()->DispatchMessageAsync(app, message, buffer,
                                                           buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    XELOGE("XMsgStartIORequestEx: app %.8X undefined", app);
  }
  if (overlapped_ptr) {
    state->CompleteOverlappedImmediate(overlapped_ptr, result);
    result = X_ERROR_IO_PENDING;
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMsgCancelIORequest_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(0);
  uint32_t wait = SHIM_GET_ARG_32(1);

  XELOGD("XMsgCancelIORequest(%.8X, %d)", overlapped_ptr, wait);

  X_HANDLE event_handle = XOverlappedGetEvent(SHIM_MEM_ADDR(overlapped_ptr));
  if (event_handle && wait) {
    auto ev = state->object_table()->LookupObject<XEvent>(event_handle);
    if (ev) {
      ev->Wait(0, 0, true, nullptr);
    }
  }

  SHIM_SET_RETURN_32(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterMsgExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XMsgInProcessCall, state);
  SHIM_SET_MAPPING("xam.xex", XMsgSystemProcessCall, state);
  SHIM_SET_MAPPING("xam.xex", XMsgStartIORequest, state);
  SHIM_SET_MAPPING("xam.xex", XMsgStartIORequestEx, state);
  SHIM_SET_MAPPING("xam.xex", XMsgCancelIORequest, state);
}
