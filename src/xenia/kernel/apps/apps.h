/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_APPS_APPS_H_
#define XENIA_KERNEL_XBOXKRNL_APPS_APPS_H_

#include "xenia/kernel/app.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace apps {

void RegisterApps(KernelState* kernel_state, XAppManager* manager);

}  // namespace apps
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_APPS_APPS_H_
