/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/apps/apps.h"

#include "xenia/kernel/apps/xgi_app.h"
#include "xenia/kernel/apps/xlivebase_app.h"
#include "xenia/kernel/apps/xmp_app.h"

namespace xe {
namespace kernel {
namespace apps {

void RegisterApps(KernelState* kernel_state, XAppManager* manager) {
  manager->RegisterApp(std::make_unique<XXGIApp>(kernel_state));
  manager->RegisterApp(std::make_unique<XXLiveBaseApp>(kernel_state));
  manager->RegisterApp(std::make_unique<XXMPApp>(kernel_state));
}

}  // namespace apps
}  // namespace kernel
}  // namespace xe
