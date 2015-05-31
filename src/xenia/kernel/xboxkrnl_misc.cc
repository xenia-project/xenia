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
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL KeEnableFpuExceptions_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t enabled = SHIM_GET_ARG_32(0);
  XELOGD("KeEnableFpuExceptions(%d)", enabled);
  // TODO(benvanik): can we do anything about exceptions?
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterMiscExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", KeEnableFpuExceptions, state);
}
