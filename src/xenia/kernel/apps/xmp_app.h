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
#include <xenia/kernel/app.h>
#include <xenia/kernel/kernel_state.h>

namespace xe {
namespace kernel {
namespace apps {

class XXMPApp : public XApp {
 public:
  enum class Status : uint32_t {
    kStopped = 0,
    kPlaying = 1,
    kPaused = 2,
  };

  XXMPApp(KernelState* kernel_state);

  X_RESULT XMPGetStatus(uint32_t status_ptr);

  X_RESULT XMPContinue();
  X_RESULT XMPStop(uint32_t unk);
  X_RESULT XMPPause();
  X_RESULT XMPNext();
  X_RESULT XMPPrevious();

  X_RESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                               uint32_t buffer_length) override;

 private:
  static const uint32_t kMsgStatusChanged = 0xA000001;
  static const uint32_t kMsgStateChanged = 0xA000002;
  static const uint32_t kMsgDisableChanged = 0xA000003;

  void OnStatusChanged();

  Status status_;
  uint32_t disabled_;
  uint32_t unknown_state1_;
  uint32_t unknown_state2_;
  uint32_t unknown_flags_;
  float unknown_float_;
};

}  // namespace apps
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_APPS_XMP_APP_H_
