/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam_content.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {


SHIM_CALL XamContentGetLicenseMask_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t mask_ptr = SHIM_GET_ARG_32(0);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "XamContentGetLicenseMask(%.8X, %.8X)",
      mask_ptr,
      overlapped_ptr);

  XEASSERTZERO(overlapped_ptr);

  // Arcade games seem to call this and check the result mask for random bits.
  // If we fail, the games seem to use a hardcoded mask, which is likely trial.
  // To be clever, let's just try setting all the bits.
  SHIM_SET_MEM_32(mask_ptr, 0xFFFFFFFF);

  SHIM_SET_RETURN(X_ERROR_SUCCESS);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterContentExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamContentGetLicenseMask, state);
}
