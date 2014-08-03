/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_APPS_XMP_APP_H_
#define XENIA_KERNEL_XBOXKRNL_APPS_XMP_APP_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/app.h>
#include <xenia/kernel/kernel_state.h>


namespace xe {
namespace kernel {
namespace apps {


class XXMPApp : public XApp {
public:
  XXMPApp(KernelState* kernel_state) : XApp(kernel_state, 0xFA) {}

  X_RESULT XMPGetStatus(uint32_t unk, uint32_t status_ptr);
  X_RESULT XMPGetStatusEx(uint32_t unk, uint32_t unk_ptr, uint32_t disabled_ptr);

  X_RESULT DispatchMessageSync(uint32_t message, uint32_t arg1, uint32_t arg2) override;
  X_RESULT DispatchMessageAsync(uint32_t message, uint32_t buffer_ptr, size_t buffer_length) override;
};


}  // namespace apps
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_APPS_XMP_APP_H_
