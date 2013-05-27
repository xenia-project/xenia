/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_MODULE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_MODULE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/kernel_module.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_ordinals.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {

class KernelState;


class XboxkrnlModule : public KernelModule {
public:
  XboxkrnlModule(Runtime* runtime);
  virtual ~XboxkrnlModule();

  int LaunchModule(const char* path);

private:
  auto_ptr<KernelState> kernel_state_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_MODULE_H_
