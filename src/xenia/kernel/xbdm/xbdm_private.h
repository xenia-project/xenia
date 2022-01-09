/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBDM_XBDM_PRIVATE_H_
#define XENIA_KERNEL_XBDM_XBDM_PRIVATE_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xbdm/xbdm_ordinals.h"

namespace xe {
namespace kernel {
namespace xbdm {

xe::cpu::Export* RegisterExport_xbdm(xe::cpu::Export* export_entry);

// Registration functions, one per file.
#define XE_MODULE_EXPORT_GROUP(m, n)                                  \
  void Register##n##Exports(xe::cpu::ExportResolver* export_resolver, \
                            KernelState* kernel_state);
#include "xbdm_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBDM_XBDM_PRIVATE_H_
