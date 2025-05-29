/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_XBOXKRNL_DEBUG_MONITOR_H_
#define XENIA_KERNEL_KERNEL_XBOXKRNL_DEBUG_MONITOR_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

struct X_KEDEBUGMONITORDATA {
  xe::be<uint32_t> unk_00;       // 0x00
  xe::be<uint32_t> unk_04;       // 0x04
  xe::be<uint32_t> unk_08;       // 0x08
  xe::be<uint32_t> unk_0C;       // 0x0C
  xe::be<uint32_t> unk_10;       // 0x10
  xe::be<uint32_t> unk_14;       // 0x14
  xe::be<uint32_t> callback_fn;  // 0x18 function
  xe::be<uint32_t> unk_1C;       // 0x1C
  xe::be<uint32_t> unk_20;       // 0x20 Vd graphics data?
};

void KeDebugMonitorCallback(cpu::ppc::PPCContext* ppc_context,
                            kernel::KernelState* kernel_state);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_DEBUG_MONITOR_H_
