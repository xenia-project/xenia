/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_H_
#define XENIA_KERNEL_XAM_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/export_resolver.h>
#include <xenia/kernel/xam_ordinals.h>
#include <xenia/kernel/objects/xkernel_module.h>

// All of the exported functions:
#include <xenia/kernel/xam_info.h>


namespace xe {
namespace kernel {


class XamModule : public XKernelModule {
public:
  XamModule(Emulator* emulator, KernelState* kernel_state);
  virtual ~XamModule();

private:
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XAM_H_
