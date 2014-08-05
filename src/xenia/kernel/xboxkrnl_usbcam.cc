/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_usbcam.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {

SHIM_CALL XUsbcamCreate_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk1 = SHIM_GET_ARG_32(0);
  uint32_t unk2 = SHIM_GET_ARG_32(1);

  XELOGD(
      "XUsbcamCreate(%.8X, %.8X)",
      unk1, unk2);

  SHIM_SET_RETURN_32(-1);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterUsbcamExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", XUsbcamCreate, state);
}
