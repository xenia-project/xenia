/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam_msg.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {


SHIM_CALL XMsgInProcessCall_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t app = SHIM_GET_ARG_32(0);
  uint32_t message = SHIM_GET_ARG_32(1);
  uint32_t arg1 = SHIM_GET_ARG_32(2);
  uint32_t arg2 = SHIM_GET_ARG_32(3);

  XELOGD(
      "XMsgInProcessCall(%.8X, %.8X, %.8X, %.8X)",
      app, message, arg1, arg2);

  bool handled = false;

  // TODO(benvanik): build XMsg pump? async/sync/etc
  if (app == 0xFA) {
    // XMP = music
    // http://freestyledash.googlecode.com/svn-history/r1/trunk/Freestyle/Scenes/Media/Music/ScnMusic.cpp
    if (message == 0x00070009) {
      uint32_t a = SHIM_MEM_32(arg1 + 0); // 0x00000002
      uint32_t b = SHIM_MEM_32(arg1 + 4); // out ptr to 4b - expect 0
      XELOGD("XMPGetStatusEx(%.8X, %.8X)", a, b);
      XEASSERTZERO(arg2);
      XEASSERT(a == 2);
      SHIM_SET_MEM_32(b, 0);
      handled = true;
    } else if (message == 0x0007001A) {
      // dcz
      // arg1 = ?
      // arg2 = 0
    } else if (message == 0x0007001B) {
      uint32_t a = SHIM_MEM_32(arg1 + 0); // 0x00000002
      uint32_t b = SHIM_MEM_32(arg1 + 4); // out ptr to 4b - expect 0
      XELOGD("XMPGetStatus(%.8X, %.8X)", a, b);
      XEASSERTZERO(arg2);
      XEASSERT(a == 2);
      SHIM_SET_MEM_32(b, 0);
      handled = true;
    }
  }

  if (!handled) {
    XELOGE("Unimplemented XMsgInProcessCall message!");
  }

  SHIM_SET_RETURN_32(handled ? X_ERROR_SUCCESS : X_ERROR_NOT_FOUND);
}


SHIM_CALL XMsgCancelIORequest_shim(
  PPCContext* ppc_state, KernelState* state) {
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(0);
  uint32_t wait = SHIM_GET_ARG_32(1);

  XELOGD(
      "XMsgCancelIORequest(%.8X, %d)",
      overlapped_ptr, wait);

  // ?
  XELOGW("XMsgCancelIORequest NOT IMPLEMENTED (wait?)");

  SHIM_SET_RETURN_32(0);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterMsgExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XMsgInProcessCall, state);
  SHIM_SET_MAPPING("xam.xex", XMsgCancelIORequest, state);
}
