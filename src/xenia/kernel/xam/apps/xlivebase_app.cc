/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xlivebase_app.h"

#include "xenia/base/logging.h"
#include "xenia/base/threading.h"

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

XLiveBaseApp::XLiveBaseApp(KernelState* kernel_state)
    : App(kernel_state, 0xFC) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

X_RESULT XLiveBaseApp::DispatchMessageSync(uint32_t message,
                                           uint32_t buffer_ptr,
                                           uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x00058004: {
      // Called on startup, seems to just return a bool in the buffer.
      assert_true(!buffer_length || buffer_length == 4);
      XELOGD("XLiveBaseGetLogonId(%.8X)", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer + 0, 1);  // ?
      return X_ERROR_SUCCESS;
    }
    case 0x00058006: {
      assert_true(!buffer_length || buffer_length == 4);
      XELOGD("XLiveBaseGetNatType(%.8X)", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer + 0, 1);  // XONLINE_NAT_OPEN
      return X_ERROR_SUCCESS;
    }
    case 0x00058007: {
      /*
              Occurs if title calls XOnlineGetServiceInfo, expects dwServiceId
         and pServiceInfo. pServiceInfo should contain pointer to
         XONLINE_SERVICE_INFO structure.
      */
      XELOGD("CXLiveLogon::GetServiceInfo(%.8X, %.8X)", buffer_ptr,
             buffer_length);
      return 1229;  // ERROR_CONNECTION_INVALID
    }
    case 0x00058020: {
      // 0x00058004 is called right before this.
      // We should create a XamEnumerate-able empty list here, but I'm not
      // sure of the format.
      // buffer_length seems to be the same ptr sent to 0x00058004.
      XELOGD("CXLiveFriends::Enumerate(%.8X, %.8X) unimplemented", buffer_ptr,
             buffer_length);
      return X_STATUS_UNSUCCESSFUL;
    }
    case 0x00058023: {
      XELOGD(
          "CXLiveMessaging::XMessageGameInviteGetAcceptedInfo(%.8X, %.8X) "
          "unimplemented",
          buffer_ptr, buffer_length);
      return X_STATUS_UNSUCCESSFUL;
    }
    case 0x00058046: {
      // Required to be successful for Forza 4 to detect signed-in profile
      // Doesn't seem to set anything in the given buffer, probably only takes
      // input
      XELOGD("XLiveBaseUnk58046(%.8X, %.8X) unimplemented", buffer_ptr,
             buffer_length);
      return X_ERROR_SUCCESS;
    }
  }
  XELOGE(
      "Unimplemented XLIVEBASE message app=%.8X, msg=%.8X, arg1=%.8X, "
      "arg2=%.8X",
      app_id(), message, buffer_ptr, buffer_length);
  return X_STATUS_UNSUCCESSFUL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
