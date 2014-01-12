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
#include <xenia/kernel/xboxkrnl_ordinals.h>
#include <xenia/kernel/objects/xkernel_module.h>

// All of the exported functions:
#include <xenia/kernel/xboxkrnl_debug.h>
#include <xenia/kernel/xboxkrnl_hal.h>
#include <xenia/kernel/xboxkrnl_memory.h>
#include <xenia/kernel/xboxkrnl_module.h>
#include <xenia/kernel/xboxkrnl_rtl.h>
#include <xenia/kernel/xboxkrnl_threading.h>
#include <xenia/kernel/xboxkrnl_video.h>


namespace xe {
namespace kernel {

class KernelState;


class XboxkrnlModule : public XKernelModule {
public:
  XboxkrnlModule(Emulator* emulator, KernelState* kernel_state);
  virtual ~XboxkrnlModule();

  int LaunchModule(const char* path);

private:
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_MODULE_H_
