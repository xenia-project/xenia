/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBDM_XBDM_MODULE_H_
#define XENIA_KERNEL_XBDM_XBDM_MODULE_H_

#include <string>

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_module.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xbdm/xbdm_ordinals.h"

namespace xe {
namespace kernel {
namespace xbdm {

class XbdmModule : public KernelModule {
 public:
  XbdmModule(Emulator* emulator, KernelState* kernel_state);
  virtual ~XbdmModule();

  static void RegisterExportTable(xe::cpu::ExportResolver* export_resolver);
};

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBDM_XBDM_MODULE_H_
