/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/messenger_app.h"

#include "xenia/base/logging.h"

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

MessengerApp::MessengerApp(KernelState* kernel_state)
    : App(kernel_state, 0xF7) {}

X_RESULT MessengerApp::DispatchMessageSync(uint32_t message,
                                           uint32_t buffer_ptr,
                                           uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x00200002: {
      // Used on blades dashboard v5759 - 6717 when requesting to sign out
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        xe::be<uint32_t> user_index;
        xe::be<uint32_t> unk1;
        xe::be<uint32_t> unk2;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      XELOGD("MessengerUnk200002({:08X}, {:08X}, {:08X}), unimplemented",
             args->user_index.get(), args->unk1.get(), args->unk2.get());
      return X_E_FAIL;
    }
    case 0x00200018: {
      // Used after signing out in blades 6717
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        xe::be<uint32_t> user_index;
        xe::be<uint32_t> unk1;
        xe::be<uint32_t> unk2;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      XELOGD("MessengerUnk200018({:08X}, {:08X}, {:08X}), unimplemented",
             args->user_index.get(), args->unk1.get(), args->unk2.get());
      return X_E_FAIL;
    }
  }
  XELOGE(
      "Unimplemented Messenger message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_STATUS_UNSUCCESSFUL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
