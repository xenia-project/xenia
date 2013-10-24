/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam/xam_input.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xam/xam_private.h>
#include <xenia/kernel/xam/xam_state.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {
namespace xam {


SHIM_CALL XamInputGetState_shim(
    xe_ppc_state_t* ppc_state, XamState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t state_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "XamInputGetState(%d, %.8X)",
      user_index,
      state_ptr);

  SHIM_SET_RETURN(X_STATUS_NOT_IMPLEMENTED);
}


}  // namespace xam
}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterInputExports(
    ExportResolver* export_resolver, XamState* state) {
  SHIM_SET_MAPPING("xam.xex", XamInputGetState, state);
}
