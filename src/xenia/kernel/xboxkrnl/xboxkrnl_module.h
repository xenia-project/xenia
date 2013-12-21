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

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/export_resolver.h>
#include <xenia/kernel/kernel_module.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_ordinals.h>

// All of the exported functions:
#include <xenia/kernel/xboxkrnl/xboxkrnl_debug.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_hal.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_memory.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_module.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_rtl.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_threading.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_video.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {

class KernelState;


class XboxkrnlModule : public KernelModule {
public:
  XboxkrnlModule(Emulator* emulator);
  virtual ~XboxkrnlModule();

  KernelState* kernel_state() const { return kernel_state_; }

  int LaunchModule(const char* path);

private:
  KernelState* kernel_state_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_MODULE_H_
