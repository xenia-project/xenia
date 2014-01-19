/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam_notify.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/objects/xnotify_listener.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {


SHIM_CALL XamNotifyCreateListener_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint64_t mask = SHIM_GET_ARG_64(0);
  uint32_t one = SHIM_GET_ARG_32(1);

  XELOGD(
      "XamNotifyCreateListener(%.8llX, %d)",
      mask,
      one);

  // r4=1 may indicate user process?

  XNotifyListener* listener = new XNotifyListener(state);
  listener->Initialize(mask);

  // Handle ref is incremented, so return that.
  uint32_t handle = listener->handle();
  listener->Release();

  SHIM_SET_RETURN_64(handle);
}


// http://ffplay360.googlecode.com/svn/Test/Common/AtgSignIn.cpp
SHIM_CALL XNotifyGetNext_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t match_id = SHIM_GET_ARG_32(1);
  uint32_t id_ptr = SHIM_GET_ARG_32(2);
  uint32_t param_ptr = SHIM_GET_ARG_32(3);

  XELOGD(
      "XNotifyGetNext(%.8X, %.8X, %.8X, %.8X)",
      handle,
      match_id,
      id_ptr,
      param_ptr);

  if (!handle) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  // Grab listener.
  XNotifyListener* listener = NULL;
  if (XFAILED(state->object_table()->GetObject(
      handle, (XObject**)&listener))) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  bool dequeued = false;
  uint32_t id = 0;
  uint32_t param = 0;
  if (match_id) {
    // Asking for a specific notification
    id = match_id;
    dequeued = listener->DequeueNotification(match_id, &param);
  } else {
    // Just get next.
    dequeued = listener->DequeueNotification(&id, &param);
  }

  if (listener) {
    listener->Release();
  }

  if (dequeued) {
    SHIM_SET_MEM_32(id_ptr, id);
    SHIM_SET_MEM_32(param_ptr, param);
  }

  SHIM_SET_RETURN_64(dequeued ? 1 : 0);
}


SHIM_CALL XNotifyPositionUI_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t position = SHIM_GET_ARG_32(0);

  XELOGD(
      "XNotifyPositionUI(%.8X)",
      position);

  // Ignored.
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterNotifyExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamNotifyCreateListener, state);
  SHIM_SET_MAPPING("xam.xex", XNotifyGetNext, state);
  SHIM_SET_MAPPING("xam.xex", XNotifyPositionUI, state);
}
