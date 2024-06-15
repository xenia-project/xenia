/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_APPS_UNKNOWN_F7_APP_H_
#define XENIA_KERNEL_XAM_APPS_UNKNOWN_F7_APP_H_

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/app_manager.h"

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

class MessengerApp : public App {
 public:
  explicit MessengerApp(KernelState* kernel_state);

  X_RESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                               uint32_t buffer_length) override;
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_APPS_UNKNOWN_FE_APP_H_
