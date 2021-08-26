/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/winkey_input_driver.h"

#include "xenia/base/platform_win.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"

namespace xe {
namespace hid {
namespace winkey {

WinKeyInputDriver::WinKeyInputDriver(xe::ui::Window* window)
    : InputDriver(window), packet_number_(1) {
  // Register a key listener.
  window->on_key_down.AddListener([this](ui::KeyEvent* evt) {
    if (!is_active()) {
      return;
    }

    auto global_lock = global_critical_region_.Acquire();

    KeyEvent key;
    key.virtual_key = evt->virtual_key();
    key.transition = true;
    key.prev_state = evt->prev_state();
    key.repeat_count = evt->repeat_count();
    key_events_.push(key);
  });
  window->on_key_up.AddListener([this](ui::KeyEvent* evt) {
    if (!is_active()) {
      return;
    }

    auto global_lock = global_critical_region_.Acquire();

    KeyEvent key;
    key.virtual_key = evt->virtual_key();
    key.transition = false;
    key.prev_state = evt->prev_state();
    key.repeat_count = evt->repeat_count();
    key_events_.push(key);
  });
}

WinKeyInputDriver::~WinKeyInputDriver() = default;

X_STATUS WinKeyInputDriver::Setup() { return X_STATUS_SUCCESS; }

X_RESULT WinKeyInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // TODO(benvanik): confirm with a real XInput controller.
  out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  out_caps->flags = 0;
  out_caps->gamepad.buttons = 0xFFFF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ly = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_rx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ry = (int16_t)0xFFFFu;
  out_caps->vibration.left_motor_speed = 0;
  out_caps->vibration.right_motor_speed = 0;
  return X_ERROR_SUCCESS;
}

#define IS_KEY_TOGGLED(key) ((GetKeyState(key) & 0x1) == 0x1)
#define IS_KEY_DOWN(key) ((GetAsyncKeyState(key) & 0x8000) == 0x8000)

X_RESULT WinKeyInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  packet_number_++;

  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;

  if (window()->has_focus() && is_active()) {
    if (IS_KEY_TOGGLED(VK_CAPITAL) || IS_KEY_DOWN(VK_SHIFT)) {
      // dpad toggled
      if (IS_KEY_DOWN('A')) {
        // A
        buttons |= 0x0004;  // XINPUT_GAMEPAD_DPAD_LEFT
      }
      if (IS_KEY_DOWN('D')) {
        // D
        buttons |= 0x0008;  // XINPUT_GAMEPAD_DPAD_RIGHT
      }
      if (IS_KEY_DOWN('S')) {
        // S
        buttons |= 0x0002;  // XINPUT_GAMEPAD_DPAD_DOWN
      }
      if (IS_KEY_DOWN('W')) {
        // W
        buttons |= 0x0001;  // XINPUT_GAMEPAD_DPAD_UP
      }
    } else {
      // left stick
      if (IS_KEY_DOWN('A')) {
        // A
        thumb_lx += SHRT_MIN;
      }
      if (IS_KEY_DOWN('D')) {
        // D
        thumb_lx += SHRT_MAX;
      }
      if (IS_KEY_DOWN('S')) {
        // S
        thumb_ly += SHRT_MIN;
      }
      if (IS_KEY_DOWN('W')) {
        // W
        thumb_ly += SHRT_MAX;
      }
    }

    if (IS_KEY_DOWN('F')) {
      // F
      buttons |= 0x0040;  // XINPUT_GAMEPAD_LEFT_THUMB
    }

    // Right stick
    if (IS_KEY_DOWN(VK_UP)) {
      // Up
      thumb_ry += SHRT_MAX;
    }
    if (IS_KEY_DOWN(VK_DOWN)) {
      // Down
      thumb_ry += SHRT_MIN;
    }
    if (IS_KEY_DOWN(VK_RIGHT)) {
      // Right
      thumb_rx += SHRT_MAX;
    }
    if (IS_KEY_DOWN(VK_LEFT)) {
      // Left
      thumb_rx += SHRT_MIN;
    }

    if (IS_KEY_DOWN('L')) {
      // L
      buttons |= 0x4000;  // XINPUT_GAMEPAD_X
    }
    if (IS_KEY_DOWN(VK_OEM_7)) {
      // '
      buttons |= 0x2000;  // XINPUT_GAMEPAD_B
    }
    if (IS_KEY_DOWN(VK_OEM_1)) {
      // ;
      buttons |= 0x1000;  // XINPUT_GAMEPAD_A
    }
    if (IS_KEY_DOWN('P')) {
      // P
      buttons |= 0x8000;  // XINPUT_GAMEPAD_Y
    }

    if (IS_KEY_DOWN('K')) {
      // K
      buttons |= 0x0080;  // XINPUT_GAMEPAD_RIGHT_THUMB
    }

    if (IS_KEY_DOWN('Q') || IS_KEY_DOWN('I')) {
      // Q / I
      left_trigger = 0xFF;
    }

    if (IS_KEY_DOWN('E') || IS_KEY_DOWN('O')) {
      // E / O
      right_trigger = 0xFF;
    }

    if (IS_KEY_DOWN('Z')) {
      // Z
      buttons |= 0x0020;  // XINPUT_GAMEPAD_BACK
    }
    if (IS_KEY_DOWN('X')) {
      // X
      buttons |= 0x0010;  // XINPUT_GAMEPAD_START
    }
    if (IS_KEY_DOWN('1')) {
      // 1
      buttons |= 0x0100;  // XINPUT_GAMEPAD_LEFT_SHOULDER
    }
    if (IS_KEY_DOWN('3')) {
      // 3
      buttons |= 0x0200;  // XINPUT_GAMEPAD_RIGHT_SHOULDER
    }
  }

  out_state->packet_number = packet_number_;
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = left_trigger;
  out_state->gamepad.right_trigger = right_trigger;
  out_state->gamepad.thumb_lx = thumb_lx;
  out_state->gamepad.thumb_ly = thumb_ly;
  out_state->gamepad.thumb_rx = thumb_rx;
  out_state->gamepad.thumb_ry = thumb_ry;

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (!is_active()) {
    return X_ERROR_EMPTY;
  }

  X_RESULT result = X_ERROR_EMPTY;

  ui::VirtualKey xinput_virtual_key = ui::VirtualKey::kNone;
  uint16_t unicode = 0;
  uint16_t keystroke_flags = 0;
  uint8_t hid_code = 0;

  // Pop from the queue.
  KeyEvent evt;
  {
    auto global_lock = global_critical_region_.Acquire();
    if (key_events_.empty()) {
      // No keys!
      return X_ERROR_EMPTY;
    }
    evt = key_events_.front();
    key_events_.pop();
  }

  switch (evt.virtual_key) {
    case ui::VirtualKey::kOem1:  // ;
      xinput_virtual_key = ui::VirtualKey::kXInputPadA;
      break;
    case ui::VirtualKey::kOem7:  // '
      xinput_virtual_key = ui::VirtualKey::kXInputPadB;
      break;
    case ui::VirtualKey::kL:
      xinput_virtual_key = ui::VirtualKey::kXInputPadX;
      break;
    case ui::VirtualKey::kP:
      xinput_virtual_key = ui::VirtualKey::kXInputPadY;
      break;
    case ui::VirtualKey::k3:
      xinput_virtual_key = ui::VirtualKey::kXInputPadRShoulder;
      break;
    case ui::VirtualKey::k1:
      xinput_virtual_key = ui::VirtualKey::kXInputPadLShoulder;
      break;
    case ui::VirtualKey::kQ:
    case ui::VirtualKey::kI:
      xinput_virtual_key = ui::VirtualKey::kXInputPadLTrigger;
      break;
    case ui::VirtualKey::kE:
    case ui::VirtualKey::kO:
      xinput_virtual_key = ui::VirtualKey::kXInputPadRTrigger;
      break;
    case ui::VirtualKey::kX:
      xinput_virtual_key = ui::VirtualKey::kXInputPadStart;
      break;
    case ui::VirtualKey::kZ:
      xinput_virtual_key = ui::VirtualKey::kXInputPadBack;
      break;
    case ui::VirtualKey::kUp:
      xinput_virtual_key = ui::VirtualKey::kXInputPadRThumbUp;
      break;
    case ui::VirtualKey::kDown:
      xinput_virtual_key = ui::VirtualKey::kXInputPadRThumbDown;
      break;
    case ui::VirtualKey::kRight:
      xinput_virtual_key = ui::VirtualKey::kXInputPadRThumbRight;
      break;
    case ui::VirtualKey::kLeft:
      xinput_virtual_key = ui::VirtualKey::kXInputPadRThumbLeft;
      break;
    default:
      // TODO(DrChat): Some other way to toggle this...
      if (IS_KEY_TOGGLED(VK_CAPITAL) || IS_KEY_DOWN(VK_SHIFT)) {
        // D-pad toggled.
        switch (evt.virtual_key) {
          case ui::VirtualKey::kW:
            xinput_virtual_key = ui::VirtualKey::kXInputPadDpadUp;
            break;
          case ui::VirtualKey::kS:
            xinput_virtual_key = ui::VirtualKey::kXInputPadDpadDown;
            break;
          case ui::VirtualKey::kA:
            xinput_virtual_key = ui::VirtualKey::kXInputPadDpadLeft;
            break;
          case ui::VirtualKey::kD:
            xinput_virtual_key = ui::VirtualKey::kXInputPadDpadRight;
            break;
          default:
            break;
        }
      } else {
        // Left thumbstick.
        switch (evt.virtual_key) {
          case ui::VirtualKey::kW:
            xinput_virtual_key = ui::VirtualKey::kXInputPadLThumbUp;
            break;
          case ui::VirtualKey::kS:
            xinput_virtual_key = ui::VirtualKey::kXInputPadLThumbDown;
            break;
          case ui::VirtualKey::kA:
            xinput_virtual_key = ui::VirtualKey::kXInputPadLThumbLeft;
            break;
          case ui::VirtualKey::kD:
            xinput_virtual_key = ui::VirtualKey::kXInputPadLThumbRight;
            break;
          default:
            break;
        }
      }
  }

  if (xinput_virtual_key != ui::VirtualKey::kNone) {
    if (evt.transition == true) {
      keystroke_flags |= 0x0001;  // XINPUT_KEYSTROKE_KEYDOWN
    } else if (evt.transition == false) {
      keystroke_flags |= 0x0002;  // XINPUT_KEYSTROKE_KEYUP
    }

    if (evt.prev_state == evt.transition) {
      keystroke_flags |= 0x0004;  // XINPUT_KEYSTROKE_REPEAT
    }

    result = X_ERROR_SUCCESS;
  }

  out_keystroke->virtual_key = uint16_t(xinput_virtual_key);
  out_keystroke->unicode = unicode;
  out_keystroke->flags = keystroke_flags;
  out_keystroke->user_index = 0;
  out_keystroke->hid_code = hid_code;

  // X_ERROR_EMPTY if no new keys
  // X_ERROR_DEVICE_NOT_CONNECTED if no device
  // X_ERROR_SUCCESS if key
  return result;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
