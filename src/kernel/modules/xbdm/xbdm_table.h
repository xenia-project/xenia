/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBDM_TABLE_H_
#define XENIA_KERNEL_MODULES_XBDM_TABLE_H_

#include <xenia/kernel/export.h>


namespace xe {
namespace kernel {
namespace xbdm {


#define FLAG(t)             kXEKernelExportFlag##t


static KernelExport xbdm_export_table[] = {
  0,
};


#undef FLAG


}  // namespace xbdm
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBDM_TABLE_H_
