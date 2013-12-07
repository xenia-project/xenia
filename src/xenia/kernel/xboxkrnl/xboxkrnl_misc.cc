/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_misc.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xboxkrnl/kernel_state.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


void xeKeBugCheckEx(uint32_t code, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4) {
  XELOGD("*** STOP: 0x%.8X (0x%.8X, 0x%.8X, 0x%.8X, 0x%.8X)", code, param1, param2, param3, param4);
  DebugBreak();
  XEASSERTALWAYS();
}

SHIM_CALL KeBugCheck_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t code = SHIM_GET_ARG_32(0);
  xeKeBugCheckEx(code, 0, 0, 0, 0);
}

SHIM_CALL KeBugCheckEx_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t code = SHIM_GET_ARG_32(0);
  uint32_t param1 = SHIM_GET_ARG_32(1);
  uint32_t param2 = SHIM_GET_ARG_32(2);
  uint32_t param3 = SHIM_GET_ARG_32(3);
  uint32_t param4 = SHIM_GET_ARG_32(4);
  xeKeBugCheckEx(code, param1, param2, param3, param4);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterMiscExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", KeBugCheck, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeBugCheckEx, state);
}
