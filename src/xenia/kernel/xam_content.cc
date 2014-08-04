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

  // Arcade games seem to call this and check the result mask for random bits.
  // If we fail, the games seem to use a hardcoded mask, which is likely trial.
  // To be clever, let's just try setting all the bits.
  SHIM_SET_MEM_32(mask_ptr, 0xFFFFFFFF);

  if (overlapped_ptr) {
    state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}


// http://gameservice.googlecode.com/svn-history/r14/trunk/ContentManager.cpp
SHIM_CALL XamContentCreateEnumerator_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);
  uint32_t arg3 = SHIM_GET_ARG_32(3);
  uint32_t arg4 = SHIM_GET_ARG_32(4);
  uint32_t arg5 = SHIM_GET_ARG_32(5);
  uint32_t handle_ptr = SHIM_GET_ARG_32(6);

  XELOGD(
      "XamContentCreateEnumerator(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
      arg0, arg1, arg2, arg3, arg4, arg5, handle_ptr);

  SHIM_SET_MEM_32(handle_ptr, X_INVALID_HANDLE_VALUE);

  SHIM_SET_RETURN_32(X_ERROR_NO_MORE_FILES);
}


SHIM_CALL XamShowDeviceSelectorUI_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);
  uint32_t arg3 = SHIM_GET_ARG_32(3);
  uint32_t arg4 = SHIM_GET_ARG_32(4);
  uint32_t arg5 = SHIM_GET_ARG_32(5);

  XELOGD(
      "XamShowDeviceSelectorUI(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
      arg0, arg1, arg2, arg3, arg4, arg5);
  SHIM_SET_RETURN_32(997);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterContentExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamContentGetLicenseMask, state);
  SHIM_SET_MAPPING("xam.xex", XamContentCreateEnumerator, state);
  SHIM_SET_MAPPING("xam.xex", XamShowDeviceSelectorUI, state);
}
