/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XAM_H_
#define XENIA_KERNEL_MODULES_XAM_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/kernel_module.h>


namespace xe {
namespace kernel {
namespace xam {


class XamModule : public KernelModule {
public:
  XamModule(xe_pal_ref pal, xe_memory_ref memory,
            shared_ptr<ExportResolver> resolver);
  virtual ~XamModule();
};


}  // namespace xam
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XAM_H_
