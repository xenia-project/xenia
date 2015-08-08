/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_MODULE_H_
#define XENIA_KERNEL_XBOXKRNL_MODULE_H_

#include <memory>

#include "xenia/base/threading.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/objects/xkernel_module.h"
#include "xenia/kernel/xboxkrnl_ordinals.h"

// All of the exported functions:
#include "xenia/kernel/xboxkrnl_rtl.h"

namespace xe {
namespace kernel {

class KernelState;

class XboxkrnlModule : public XKernelModule {
 public:
  XboxkrnlModule(Emulator* emulator, KernelState* kernel_state);
  virtual ~XboxkrnlModule();

  static void RegisterExportTable(xe::cpu::ExportResolver* export_resolver);

  int LaunchModule(const char* path);

 private:
  std::unique_ptr<xe::threading::HighResolutionTimer> timestamp_timer_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_MODULE_H_
