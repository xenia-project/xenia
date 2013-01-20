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

#include <xenia/kernel/export.h>


namespace xe {
namespace kernel {


class KernelModule {
public:
  KernelModule(xe_pal_ref pal, xe_memory_ref memory,
               shared_ptr<ExportResolver> resolver) {
    pal_ = xe_pal_retain(pal);
    memory_ = xe_memory_retain(memory);
    resolver_ = resolver;
  }

  virtual ~KernelModule() {
    xe_memory_release(memory_);
    xe_pal_release(pal_);
  }

protected:
  xe_pal_ref    pal_;
  xe_memory_ref memory_;
  shared_ptr<ExportResolver> resolver_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_KERNEL_MODULE_H_
