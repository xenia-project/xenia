/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam/xam_user.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xam/xam_private.h>
#include <xenia/kernel/xam/xam_state.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {
namespace xam {


SHIM_CALL XamUserGetXUID_shim(
    PPCContext* ppc_state, XamState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t xuid_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "XamUserGetXUID(%d, %.8X)",
      user_index,
      xuid_ptr);

  if (xuid_ptr) {
    SHIM_SET_MEM_32(xuid_ptr, 0xBABEBABE);
  }

  SHIM_SET_RETURN(0);
}


SHIM_CALL XamUserGetSigninState_shim(
    PPCContext* ppc_state, XamState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);

  XELOGD(
      "XamUserGetSigninState(%d)",
      user_index);

  SHIM_SET_RETURN(0);
}


// XamUserReadProfileSettings


}  // namespace xam
}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterUserExports(
    ExportResolver* export_resolver, XamState* state) {
  SHIM_SET_MAPPING("xam.xex", XamUserGetXUID, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetSigninState, state);
}
