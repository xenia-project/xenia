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

#include "xenia/base/logging.h"
#include "xenia/hid/hid_flags.h"

namespace xe {
namespace hid {
namespace xinput {

XInputInputDriver::XInputInputDriver(xe::ui::Window* window,
                                     size_t window_z_order)
    : InputDriver(window, window_z_order),
      module_(nullptr),
      XInputGetCapabilities_(nullptr),
      XInputGetState_(nullptr),
      XInputGetKeystroke_(nullptr),
      XInputSetState_(nullptr),
      XInputEnable_(nullptr),
      connected_user_mask_(0) {}

XInputInputDriver::~XInputInputDriver() {
  if (module_) {
    FreeLibrary((HMODULE)module_);
    module_ = nullptr;
    XInputGetCapabilities_ = nullptr;
    XInputGetState_ = nullptr;
    XInputGetKeystroke_ = nullptr;
    XInputSetState_ = nullptr;
    XInputEnable_ = nullptr;
  }
}

X_STATUS XInputInputDriver::Setup() {
  HMODULE module = LoadLibraryW(L"xinput1_4.dll");
  if (!module) {
    module = LoadLibraryW(L"xinput1_3.dll");
  }
  if (!module) {
    return X_STATUS_DLL_NOT_FOUND;
  }

  // Required.
  auto xigc = GetProcAddress(module, "XInputGetCapabilities");
  auto xigs = GetProcAddress(module, "XInputGetState");
  auto xigk = GetProcAddress(module, "XInputGetKeystroke");
  auto xiss = GetProcAddress(module, "XInputSetState");

  // Not required.
  auto xie = GetProcAddress(module, "XInputEnable");

  // Only fail when we don't have the bare essentials;
  if (!xigc || !xigs || !xigk || !xiss) {
    FreeLibrary(module);
    return X_STATUS_PROCEDURE_NOT_FOUND;
  }

  module_ = module;
  XInputGetCapabilities_ = xigc;
  XInputGetState_ = xigs;
  XInputGetKeystroke_ = xigk;
  XInputSetState_ = xiss;
  XInputEnable_ = xie;

  if (cvars::guide_button) {
    // Theoretically there is XInputGetStateEx
    // but thats undocumented and milage varies.
    XELOGW("XInput: Guide button support is not implemented.");
  }
  return X_STATUS_SUCCESS;
}

// Verifies that a connected XInput slot is a real controller, not a phantom
// device (e.g. the wireless receiver reporting a ghost slot).
//
// The Xbox 360 wireless adapter causes XInputGetCapabilities to return
// success on slots where no controller is physically connected. These
// phantom slots return XInputGetState with packet_number 0 and all-zero
// gamepad data. This helper filters them out.
static bool IsRealController(const decltype(&XInputGetState) xigs,
                             uint32_t user_index) {
  XINPUT_STATE test_state;
  DWORD result = xigs(user_index, &test_state);
  if (result != ERROR_SUCCESS) {
    return false;
  }
  // Phantom slots return packet 0 + all zeros.
  // Real controllers have packet_number > 0 or non-zero gamepad data
  // even when idle (controller presence is detectable).
  if (test_state.dwPacketNumber == 0) {
    // Packet 0 with all-zero gamepad = phantom.
    if (test_state.Gamepad.wButtons == 0 &&
        test_state.Gamepad.bLeftTrigger == 0 &&
        test_state.Gamepad.bRightTrigger == 0 &&
        test_state.Gamepad.sThumbLX == 0 &&
        test_state.Gamepad.sThumbLY == 0 &&
        test_state.Gamepad.sThumbRX == 0 &&
        test_state.Gamepad.sThumbRY == 0) {
      return false;
    }
  }
  return true;
}

X_RESULT XInputInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  XINPUT_CAPABILITIES native_caps;
  auto xigc = (decltype(&XInputGetCapabilities))XInputGetCapabilities_;
  DWORD result = xigc(user_index, flags, &native_caps);
  if (result) {
    // Remove from connected mask on disconnect.
    connected_user_mask_ &= ~(1u << user_index);
    return result;
  }

  // Filter phantom slots from wireless adapters.
  // A slot that claims to be connected but has no real state is a ghost.
  auto xigs = (decltype(&XInputGetState))XInputGetState_;
  if (!IsRealController(xigs, user_index)) {
    connected_user_mask_ &= ~(1u << user_index);
    return X_ERROR_DEVICE_NOT_CONNECTED;
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

  connected_user_mask_ |= (1u << user_index);
  return result;
}

X_RESULT XInputInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  // Skip phantom slots that slipped through (belt-and-suspenders).
  if (!(connected_user_mask_ & (1u << user_index))) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  XINPUT_STATE native_state;
  auto xigs = (decltype(&XInputGetState))XInputGetState_;
  DWORD result = xigs(user_index, &native_state);
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
  auto xiss = (decltype(&XInputSetState))XInputSetState_;
  DWORD result = xiss(user_index, &native_vibration);
  return result;
}

X_RESULT XInputInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  // We may want to filter flags/user_index before sending to native.
  // flags is reserved on desktop.
  DWORD result;

  // XInputGetKeystroke on Windows has a bug where it will return
  // ERROR_SUCCESS (0) even if the device is not connected:
  // https://stackoverflow.com/questions/23669238/xinputgetkeystroke-returning-error-success-while-controller-is-unplugged
  //
  // So we first check if the device is connected via XInputGetCapabilities, so
  // we are not passing back an uninitialized X_INPUT_KEYSTROKE structure.
  // If any user (0xFF) is polled this bug does not occur but GetCapabilities
  // would fail so we need to skip it.
  if (user_index != 0xFF) {
    // Check whether we believe this slot has a real controller.
    if (!(connected_user_mask_ & (1u << user_index))) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }

    XINPUT_CAPABILITIES caps;
    auto xigc = (decltype(&XInputGetCapabilities))XInputGetCapabilities_;
    result = xigc(user_index, 0, &caps);
    if (result) {
      return result;
    }
  }

  XINPUT_KEYSTROKE native_keystroke;
  auto xigk = (decltype(&XInputGetKeystroke))XInputGetKeystroke_;
  result = xigk(user_index, 0, &native_keystroke);
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
