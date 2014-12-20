/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/common.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/util/shim_utils.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/xbox.h>

namespace xe {
namespace kernel {

SHIM_CALL HalReturnToFirmware_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t routine = SHIM_GET_ARG_32(0);

  XELOGD("HalReturnToFirmware(%d)", routine);

  // void
  // IN FIRMWARE_REENTRY  Routine

  // Routine must be 1 'HalRebootRoutine'
  assert_true(routine == 1);

  // TODO(benvank): diediedie much more gracefully
  // Not sure how to blast back up the stack in LLVM without exceptions, though.
  XELOGE("Game requested shutdown via HalReturnToFirmware");
  exit(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterHalExports(ExportResolver* export_resolver,
                                              KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", HalReturnToFirmware, state);
}
