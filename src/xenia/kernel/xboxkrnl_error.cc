/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl_error.h"

#include <algorithm>

#include "xenia/base/atomic.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"

namespace xe {
namespace kernel {

SHIM_CALL RtlNtStatusToDosError_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t status = SHIM_GET_ARG_32(0);

  XELOGD("RtlNtStatusToDosError(%.4X)", status);

  if (!status || (status & 0x20000000)) {
    SHIM_SET_RETURN_32(status);
    return;
  }
  
  if ((status >> 16) == 0x8007) {
    SHIM_SET_RETURN_32(status & 0xFFFF);
    return;
  }

  if ((status & 0xF0000000) == 0xD0000000) {
    // High bits doesn't matter.
    status &= ~0x30000000;
  }

  // TODO(benvanik): implement lookup table.
  XELOGE("RtlNtStatusToDosError lookup NOT IMPLEMENTED");

  uint32_t result = 317;  // ERROR_MR_MID_NOT_FOUND

  SHIM_SET_RETURN_32(result);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterErrorExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlNtStatusToDosError, state);
}
