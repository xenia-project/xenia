/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/nop/nop_input_driver.h"

#include "xenia/hid/hid_flags.h"

namespace xe {
namespace hid {
namespace nop {

NopInputDriver::NopInputDriver(xe::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {}

NopInputDriver::~NopInputDriver() = default;

X_STATUS NopInputDriver::Setup() { return X_STATUS_SUCCESS; }

// TODO(benvanik): spoof a device so that games don't stop waiting for
//     a controller to be plugged in.

X_RESULT NopInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                         X_INPUT_CAPABILITIES* out_caps) {
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT NopInputDriver::GetState(uint32_t user_index,
                                  X_INPUT_STATE* out_state) {
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT NopInputDriver::SetState(uint32_t user_index,
                                  X_INPUT_VIBRATION* vibration) {
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT NopInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                      X_INPUT_KEYSTROKE* out_keystroke) {
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

InputType NopInputDriver::GetInputType() const { return InputType::Other; }

}  // namespace nop
}  // namespace hid
}  // namespace xe
