/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/xinput/xinput_input_driver.h"

// Must be included before xinput.h to avoid windows.h conflicts:
#include "xenia/base/platform_win.h"

#include <xinput.h>  // NOLINT(build/include_order)

#include "xenia/hid/hid_flags.h"

namespace xe {
namespace hid {
namespace xinput {

XInputInputDriver::XInputInputDriver(xe::ui::Window* window)
    : InputDriver(window) {
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8 && _WIN32_WINNT < _WIN32_WINNT_WIN10)
  // TODO(gibbed): Is this necessary?
  XInputEnable(TRUE);
#endif
}

XInputInputDriver::~XInputInputDriver() {
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8 && _WIN32_WINNT < _WIN32_WINNT_WIN10)
  // TODO(gibbed): Is this necessary?
  XInputEnable(FALSE);
#endif
}

X_STATUS XInputInputDriver::Setup() { return X_STATUS_SUCCESS; }

X_RESULT XInputInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  XINPUT_CAPABILITIES native_caps;
  DWORD result = XInputGetCapabilities(user_index, flags, &native_caps);
  if (result) {
    return result;
  }

  out_caps->type = native_caps.Type;
  out_caps->sub_type = native_caps.SubType;
  out_caps->flags = native_caps.Flags;
  out_caps->gamepad.buttons = native_caps.Gamepad.wButtons;
  out_caps->gamepad.left_trigger = native_caps.Gamepad.bLeftTrigger;
  out_caps->gamepad.right_trigger = native_caps.Gamepad.bRightTrigger;
  out_caps->gamepad.thumb_lx = native_caps.Gamepad.sThumbLX;
  out_caps->gamepad.thumb_ly = native_caps.Gamepad.sThumbLY;
  out_caps->gamepad.thumb_rx = native_caps.Gamepad.sThumbRX;
  out_caps->gamepad.thumb_ry = native_caps.Gamepad.sThumbRY;
  out_caps->vibration.left_motor_speed = native_caps.Vibration.wLeftMotorSpeed;
  out_caps->vibration.right_motor_speed =
      native_caps.Vibration.wRightMotorSpeed;

  return result;
}

X_RESULT XInputInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  XINPUT_STATE native_state;
  DWORD result = XInputGetState(user_index, &native_state);
  if (result) {
    return result;
  }

  out_state->packet_number = native_state.dwPacketNumber;
  out_state->gamepad.buttons = native_state.Gamepad.wButtons;
  out_state->gamepad.left_trigger = native_state.Gamepad.bLeftTrigger;
  out_state->gamepad.right_trigger = native_state.Gamepad.bRightTrigger;
  out_state->gamepad.thumb_lx = native_state.Gamepad.sThumbLX;
  out_state->gamepad.thumb_ly = native_state.Gamepad.sThumbLY;
  out_state->gamepad.thumb_rx = native_state.Gamepad.sThumbRX;
  out_state->gamepad.thumb_ry = native_state.Gamepad.sThumbRY;

  return result;
}

X_RESULT XInputInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  XINPUT_VIBRATION native_vibration;
  native_vibration.wLeftMotorSpeed = vibration->left_motor_speed;
  native_vibration.wRightMotorSpeed = vibration->right_motor_speed;
  DWORD result = XInputSetState(user_index, &native_vibration);
  return result;
}

X_RESULT XInputInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  // We may want to filter flags/user_index before sending to native.
  // flags is reserved on desktop.
  DWORD result;

  // XInputGetKeystroke on Windows has a bug where it will return
  // ERROR_SUCCESS (0) even if the device is not connected:
  // http://stackoverflow.com/questions/23669238/xinputgetkeystroke-returning-error-success-while-controller-is-unplugged
  //
  // So we first check if the device is connected via XInputGetCapabilities, so
  // we are not passing back an uninitialized X_INPUT_KEYSTROKE structure:
  XINPUT_CAPABILITIES caps;
  result = XInputGetCapabilities(user_index, 0, &caps);
  if (result) {
    return result;
  }

  XINPUT_KEYSTROKE native_keystroke;
  result = XInputGetKeystroke(user_index, 0, &native_keystroke);
  if (result) {
    return result;
  }

  out_keystroke->virtual_key = native_keystroke.VirtualKey;
  out_keystroke->unicode = native_keystroke.Unicode;
  out_keystroke->flags = native_keystroke.Flags;
  out_keystroke->user_index = native_keystroke.UserIndex;
  out_keystroke->hid_code = native_keystroke.HidCode;
  // X_ERROR_EMPTY if no new keys
  // X_ERROR_DEVICE_NOT_CONNECTED if no device
  // X_ERROR_SUCCESS if key
  return result;
}

}  // namespace xinput
}  // namespace hid
}  // namespace xe
