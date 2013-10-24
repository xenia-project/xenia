/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/xinput/xinput_input_driver.h>

#include <xenia/hid/hid-private.h>

#include <Xinput.h>


using namespace xe;
using namespace xe::hid;
using namespace xe::hid::xinput;


XInputInputDriver::XInputInputDriver(InputSystem* input_system) :
    InputDriver(input_system) {
  XInputEnable(TRUE);
}

XInputInputDriver::~XInputInputDriver() {
  XInputEnable(FALSE);
}

X_STATUS XInputInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}

XRESULT XInputInputDriver::GetCapabilities(
    uint32_t user_index, uint32_t flags, X_INPUT_CAPABILITIES& out_caps) {
  XINPUT_CAPABILITIES native_caps;
  DWORD result = XInputGetCapabilities(user_index, flags, &native_caps);
  if (result) {
    return result;
  }

  out_caps.type                   = native_caps.Type;
  out_caps.sub_type               = native_caps.SubType;
  out_caps.flags                  = native_caps.Flags;
  out_caps.gamepad.buttons        = native_caps.Gamepad.wButtons;
  out_caps.gamepad.left_trigger   = native_caps.Gamepad.bLeftTrigger;
  out_caps.gamepad.right_trigger  = native_caps.Gamepad.bRightTrigger;
  out_caps.gamepad.thumb_lx       = native_caps.Gamepad.sThumbLX;
  out_caps.gamepad.thumb_ly       = native_caps.Gamepad.sThumbLY;
  out_caps.gamepad.thumb_rx       = native_caps.Gamepad.sThumbRX;
  out_caps.gamepad.thumb_ry       = native_caps.Gamepad.sThumbRY;
  out_caps.vibration.left_motor_speed =
      native_caps.Vibration.wLeftMotorSpeed;
  out_caps.vibration.right_motor_speed =
      native_caps.Vibration.wRightMotorSpeed;

  return result;
}

XRESULT XInputInputDriver::GetState(
    uint32_t user_index, X_INPUT_STATE& out_state) {
  XINPUT_STATE native_state;
  DWORD result = XInputGetState(user_index, &native_state);
  if (result) {
    return result;
  }

  out_state.packet_number         = native_state.dwPacketNumber;
  out_state.gamepad.buttons       = native_state.Gamepad.wButtons;
  out_state.gamepad.left_trigger  = native_state.Gamepad.bLeftTrigger;
  out_state.gamepad.right_trigger = native_state.Gamepad.bRightTrigger;
  out_state.gamepad.thumb_lx      = native_state.Gamepad.sThumbLX;
  out_state.gamepad.thumb_ly      = native_state.Gamepad.sThumbLY;
  out_state.gamepad.thumb_rx      = native_state.Gamepad.sThumbRX;
  out_state.gamepad.thumb_ry      = native_state.Gamepad.sThumbRY;

  return result;
}

XRESULT XInputInputDriver::SetState(
    uint32_t user_index, X_INPUT_VIBRATION& vibration) {
  XINPUT_VIBRATION native_vibration;
  native_vibration.wLeftMotorSpeed = vibration.left_motor_speed;
  native_vibration.wRightMotorSpeed= vibration.right_motor_speed;
  DWORD result = XInputSetState(user_index, &native_vibration);
  return result;
}
