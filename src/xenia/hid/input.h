/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_INPUT_H_
#define XENIA_HID_INPUT_H_

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"

namespace xe {
namespace hid {

enum X_INPUT_CAPS {
  X_INPUT_CAPS_FFB_SUPPORTED = 0x0001,
  X_INPUT_CAPS_WIRELESS = 0x0002,
  X_INPUT_CAPS_VOICE_SUPPORTED = 0x0004,
  X_INPUT_CAPS_PMD_SUPPORTED = 0x0008,
  X_INPUT_CAPS_NO_NAVIGATION = 0x0010,
};

enum X_INPUT_FLAG {
  X_INPUT_FLAG_GAMEPAD = 0x00000001,
  X_INPUT_FLAG_KEYBOARD = 0x00000002,
  X_INPUT_FLAG_UNKNOWN = 0x00000004,
  X_INPUT_FLAG_UNKNOWN2 = 0x00000008,
  X_INPUT_FLAG_MIC = 0x00000020,
  X_INPUT_FLAG_ANYDEVICE = 0x000000FF,
  X_INPUT_FLAG_ANY_USER = 1 << 30
};

enum X_INPUT_DEVTYPE {
  XINPUT_DEVTYPE_GAMEPAD = 0x00000001,
  XINPUT_DEVTYPE_KEYBOARD = 0x00000002,
};

enum X_INPUT_DEVSUBTYPE {
  XINPUT_DEVSUBTYPE_USB_KEYBOARD = 0x00000000,
  XINPUT_DEVSUBTYPE_GAMEPAD = 0x00000001,
};

enum X_INPUT_GAMEPAD_BUTTON {
  X_INPUT_GAMEPAD_DPAD_UP = 0x0001,
  X_INPUT_GAMEPAD_DPAD_DOWN = 0x0002,
  X_INPUT_GAMEPAD_DPAD_LEFT = 0x0004,
  X_INPUT_GAMEPAD_DPAD_RIGHT = 0x0008,
  X_INPUT_GAMEPAD_START = 0x0010,
  X_INPUT_GAMEPAD_BACK = 0x0020,
  X_INPUT_GAMEPAD_LEFT_THUMB = 0x0040,
  X_INPUT_GAMEPAD_RIGHT_THUMB = 0x0080,
  X_INPUT_GAMEPAD_LEFT_SHOULDER = 0x0100,
  X_INPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200,
  X_INPUT_GAMEPAD_GUIDE = 0x0400,
  X_INPUT_GAMEPAD_A = 0x1000,
  X_INPUT_GAMEPAD_B = 0x2000,
  X_INPUT_GAMEPAD_X = 0x4000,
  X_INPUT_GAMEPAD_Y = 0x8000,
};

// For VK_PAD, use ui::VirtualKey.

enum X_INPUT_KEYSTROKE_FLAGS {
  X_INPUT_KEYSTROKE_KEYDOWN = 0x0001,
  X_INPUT_KEYSTROKE_KEYUP = 0x0002,
  X_INPUT_KEYSTROKE_REPEAT = 0x0004,
};

struct X_INPUT_GAMEPAD {
  be<uint16_t> buttons;
  uint8_t left_trigger;
  uint8_t right_trigger;
  be<int16_t> thumb_lx;
  be<int16_t> thumb_ly;
  be<int16_t> thumb_rx;
  be<int16_t> thumb_ry;
};
static_assert_size(X_INPUT_GAMEPAD, 12);

struct X_INPUT_STATE {
  be<uint32_t> packet_number;
  X_INPUT_GAMEPAD gamepad;
};
static_assert_size(X_INPUT_STATE, sizeof(X_INPUT_GAMEPAD) + 4);

struct X_INPUT_VIBRATION {
  be<uint16_t> left_motor_speed;
  be<uint16_t> right_motor_speed;
};
static_assert_size(X_INPUT_VIBRATION, 4);

struct X_INPUT_CAPABILITIES {
  uint8_t type;
  uint8_t sub_type;
  be<uint16_t> flags;
  X_INPUT_GAMEPAD gamepad;
  X_INPUT_VIBRATION vibration;
};
static_assert_size(X_INPUT_CAPABILITIES,
                   sizeof(X_INPUT_GAMEPAD) + sizeof(X_INPUT_VIBRATION) + 4);

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinput_keystroke(v=vs.85).aspx
struct X_INPUT_KEYSTROKE {
  be<uint16_t> virtual_key;
  be<uint16_t> unicode;
  be<uint16_t> flags;
  uint8_t user_index;
  uint8_t hid_code;
};
static_assert_size(X_INPUT_KEYSTROKE, 8);

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_INPUT_H_
