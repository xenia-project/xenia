/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_XBOXKRNL_CERT_MONITOR_H_
#define XENIA_KERNEL_KERNEL_XBOXKRNL_CERT_MONITOR_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

struct X_KECERTMONITORDATA {
  xe::be<uint32_t> callback_fn;
};

void KeCertMonitorCallback(cpu::ppc::PPCContext* ppc_context,
                           kernel::KernelState* kernel_state);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_XBOXKRNL_CERT_MONITOR_H_
