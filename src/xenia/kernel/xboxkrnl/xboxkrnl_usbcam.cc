/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XUsbcamCreate_entry(dword_t buffer,
                                   dword_t buffer_size,  // 0x4B000 640x480?
                                   lpdword_t handle_out) {
  // This function should return success.
  // It looks like it only allocates space for usbcam support.
  // returning error code might cause games to initialize incorrectly.
  // "Carcassonne" initalization function checks for result from this
  // function. If value is different than 0 instead of loading
  // rest of the game it returns from initalization function and tries
  // to run game normally which causes crash, due to uninitialized data.
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XUsbcamCreate, kNone, kStub);

dword_result_t XUsbcamGetState_entry() {
  // 0 = not connected.
  // 1 = initialized
  // 2 = connected
  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(XUsbcamGetState, kNone, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Usbcam);
