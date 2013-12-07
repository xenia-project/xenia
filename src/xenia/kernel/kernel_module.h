/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_MODULE_H_
#define XENIA_KERNEL_KERNEL_MODULE_H_

#include <xenia/common.h>
#include <xenia/core.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS1(xe, ExportResolver);


namespace xe {
namespace kernel {


class KernelModule {
public:
  KernelModule(Emulator* emulator);
  virtual ~KernelModule();

protected:
  Emulator*         emulator_;
  Memory*           memory_;
  ExportResolver*   export_resolver_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_KERNEL_MODULE_H_
