/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XAM_INFO_H_
#define XENIA_KERNEL_MODULES_XAM_INFO_H_

#include "kernel/modules/xam/xam_state.h"


namespace xe {
namespace kernel {
namespace xam {


void RegisterInfoExports(ExportResolver* export_resolver, XamState* state);


}  // namespace xam
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XAM_INFO_H_
