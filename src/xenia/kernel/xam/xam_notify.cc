/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam/xam_notify.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xam/xam_private.h>
#include <xenia/kernel/xam/xam_state.h>
#include <xenia/kernel/xboxkrnl/objects/xnotify_listener.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xam {


SHIM_CALL XamNotifyCreateListener_shim(
    PPCContext* ppc_state, XamState* state) {
  uint64_t mask = SHIM_GET_ARG_64(0);
  uint32_t one = SHIM_GET_ARG_32(1);

  XELOGD(
      "XamNotifyCreateListener(%.8llX, %d)",
      mask,
      one);

  // r4=1 may indicate user process?

  //return handle;

  SHIM_SET_RETURN(0);
}


// http://ffplay360.googlecode.com/svn/Test/Common/AtgSignIn.cpp
SHIM_CALL XNotifyGetNext_shim(
    PPCContext* ppc_state, XamState* state) {
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

  // get from handle
  XNotifyListener* listener = 0;

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

  if (dequeued) {
    SHIM_SET_MEM_32(id_ptr, id);
    SHIM_SET_MEM_32(param_ptr, param);
  }

  SHIM_SET_RETURN(dequeued ? 1 : 0);
}


}  // namespace xam
}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterNotifyExports(
    ExportResolver* export_resolver, XamState* state) {
  SHIM_SET_MAPPING("xam.xex", XamNotifyCreateListener, state);
  SHIM_SET_MAPPING("xam.xex", XNotifyGetNext, state);
}
