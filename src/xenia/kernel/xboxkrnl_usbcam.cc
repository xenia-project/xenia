/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/logging.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL XUsbcamCreate_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t unk1 = SHIM_GET_ARG_32(0);  // E
  uint32_t unk2 = SHIM_GET_ARG_32(1);  // 0x4B000
  uint32_t unk3_ptr = SHIM_GET_ARG_32(3);

  XELOGD("XUsbcamCreate(%.8X, %.8X, %.8X)", unk1, unk2, unk3_ptr);

  // 0 = success.
  SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
}

SHIM_CALL XUsbcamGetState_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("XUsbcamGetState()");
  // 0 = not connected.
  SHIM_SET_RETURN_32(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterUsbcamExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", XUsbcamCreate, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XUsbcamGetState, state);
}
