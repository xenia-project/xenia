/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_APPS_XLIVEBASE_APP_H_
#define XENIA_KERNEL_XBOXKRNL_APPS_XLIVEBASE_APP_H_

#include "xenia/kernel/app.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace apps {

class XXLiveBaseApp : public XApp {
 public:
  XXLiveBaseApp(KernelState* kernel_state);

  X_RESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                               uint32_t buffer_length) override;

 private:
};

}  // namespace apps
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_APPS_XLIVEBASE_APP_H_
