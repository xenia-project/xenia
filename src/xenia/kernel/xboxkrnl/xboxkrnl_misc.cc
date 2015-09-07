/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

SHIM_CALL KeEnableFpuExceptions_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t enabled = SHIM_GET_ARG_32(0);
  XELOGD("KeEnableFpuExceptions(%d)", enabled);
  // TODO(benvanik): can we do anything about exceptions?
}

void RegisterMiscExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", KeEnableFpuExceptions, state);
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
