/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/keyboard/keyboard_input_driver.h"

#include "xenia/base/logging.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"

#define XE_HID_KEYBOARD_BINDING(button, description, cvar_name, \
                              cvar_default_value)             \
  DEFINE_string(cvar_name, cvar_default_value,                \
                "List of keys to bind to " description        \
                ", separated by spaces",                      \
                "HID.WinKey")
#include "keyboard_binding_table.inc"
#undef XE_HID_KEYBOARD_BINDING

namespace xe {
namespace hid {
namespace keyboard {

void KeyboardInputDriver::ParseKeyBinding(ui::VirtualKey output_key,
                                        const std::string_view description,
                                        const std::string_view source_tokens) {
  for (const std::string_view source_token :
       utf8::split(source_tokens, " ", true)) {
    KeyBinding key_binding;
    key_binding.output_key = output_key;

    std::string_view token = source_token;

    if (utf8::starts_with(token, "_")) {
      key_binding.uppercase = false;
      token = token.substr(1);
    } else if (utf8::starts_with(token, "^")) {
      key_binding.uppercase = true;
      token = token.substr(1);
    }

    if (utf8::starts_with(token, "0x")) {
      token = token.substr(2);
      key_binding.input_key = static_cast<ui::VirtualKey>(
          string_util::from_string<uint16_t>(token, true));
    } else if (token.size() == 1 && (token[0] >= 'A' && token[0] <= 'Z') ||
               (token[0] >= '0' && token[0] <= '9')) {
      key_binding.input_key = static_cast<ui::VirtualKey>(token[0]);
    }

    if (key_binding.input_key == ui::VirtualKey::kNone) {
      XELOGW("winkey: failed to parse binding \"{}\" for controller input {}.",
             source_token, description);
      continue;
    }

    key_bindings_.push_back(key_binding);
    XELOGI("winkey: \"{}\" binds key 0x{:X} to controller input {}.",
           source_token, key_binding.input_key, description);
  }
}

KeyboardInputDriver::KeyboardInputDriver(xe::ui::Window* window,
                                     size_t window_z_order)
    : InputDriver(window, window_z_order), window_input_listener_(*this) {
#define XE_HID_KEYBOARD_BINDING(button, description, cvar_name,          \
                              cvar_default_value)                      \
  ParseKeyBinding(xe::ui::VirtualKey::kXInputPad##button, description, \
                  cvars::cvar_name);
#include "keyboard_binding_table.inc"
#undef XE_HID_KEYBOARD_BINDING

  window->AddInputListener(&window_input_listener_, window_z_order);
}

KeyboardInputDriver::~KeyboardInputDriver() {
  window()->RemoveInputListener(&window_input_listener_);
}

X_STATUS KeyboardInputDriver::Setup() { return X_STATUS_SUCCESS; }

X_RESULT KeyboardInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
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

X_RESULT KeyboardInputDriver::GetState(uint32_t user_index,
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

  if (window()->HasFocus() && is_active()) {
    for (const KeyBinding& b : key_bindings_) {
      if (b.is_pressed) {
        switch (b.output_key) {
          case ui::VirtualKey::kXInputPadA:
            buttons |= 0x1000;  // XINPUT_GAMEPAD_A
            break;
          case ui::VirtualKey::kXInputPadY:
            buttons |= 0x8000;  // XINPUT_GAMEPAD_Y
            break;
          case ui::VirtualKey::kXInputPadB:
            buttons |= 0x2000;  // XINPUT_GAMEPAD_B
            break;
          case ui::VirtualKey::kXInputPadX:
            buttons |= 0x4000;  // XINPUT_GAMEPAD_X
            break;
          case ui::VirtualKey::kXInputPadDpadLeft:
            buttons |= 0x0004;  // XINPUT_GAMEPAD_DPAD_LEFT
            break;
          case ui::VirtualKey::kXInputPadDpadRight:
            buttons |= 0x0008;  // XINPUT_GAMEPAD_DPAD_RIGHT
            break;
          case ui::VirtualKey::kXInputPadDpadDown:
            buttons |= 0x0002;  // XINPUT_GAMEPAD_DPAD_DOWN
            break;
          case ui::VirtualKey::kXInputPadDpadUp:
            buttons |= 0x0001;  // XINPUT_GAMEPAD_DPAD_UP
            break;
          case ui::VirtualKey::kXInputPadRThumbPress:
            buttons |= 0x0080;  // XINPUT_GAMEPAD_RIGHT_THUMB
            break;
          case ui::VirtualKey::kXInputPadLThumbPress:
            buttons |= 0x0040;  // XINPUT_GAMEPAD_LEFT_THUMB
            break;
          case ui::VirtualKey::kXInputPadBack:
            buttons |= 0x0020;  // XINPUT_GAMEPAD_BACK
            break;
          case ui::VirtualKey::kXInputPadStart:
            buttons |= 0x0010;  // XINPUT_GAMEPAD_START
            break;
          case ui::VirtualKey::kXInputPadLShoulder:
            buttons |= 0x0100;  // XINPUT_GAMEPAD_LEFT_SHOULDER
            break;
          case ui::VirtualKey::kXInputPadRShoulder:
            buttons |= 0x0200;  // XINPUT_GAMEPAD_RIGHT_SHOULDER
            break;
          case ui::VirtualKey::kXInputPadLTrigger:
            left_trigger = 0xFF;
            break;
          case ui::VirtualKey::kXInputPadRTrigger:
            right_trigger = 0xFF;
            break;
          case ui::VirtualKey::kXInputPadLThumbLeft:
            thumb_lx += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadLThumbRight:
            thumb_lx += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadLThumbDown:
            thumb_ly += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadLThumbUp:
            thumb_ly += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbUp:
            thumb_ry += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbDown:
            thumb_ry += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadRThumbRight:
            thumb_rx += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbLeft:
            thumb_rx += SHRT_MIN;
            break;
          default:
            assert_unhandled_case(b.output_key);
        }
      }
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

X_RESULT KeyboardInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
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

  for (const KeyBinding& b : key_bindings_) {
    if (b.input_key == evt.virtual_key && b.uppercase == evt.is_capital) {
      xinput_virtual_key = b.output_key;
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

void KeyboardInputDriver::KeyboardWindowInputListener::OnKeyDown(ui::KeyEvent& e) {
  driver_.OnKey(e, true);
}

void KeyboardInputDriver::KeyboardWindowInputListener::OnKeyUp(ui::KeyEvent& e) {
  driver_.OnKey(e, false);
}

void KeyboardInputDriver::OnKey(ui::KeyEvent& e, bool is_down) {
  if (!is_active()) {
    return;
  }

  KeyEvent key;
  key.virtual_key = e.virtual_key();
  key.transition = is_down;
  key.prev_state = e.prev_state();
  key.repeat_count = e.repeat_count();
  key.is_capital = e.is_shift_pressed();

  auto global_lock = global_critical_region_.Acquire();
  key_events_.push(key);
  for (auto& key_binding : key_bindings_) {
    if (key_binding.input_key == key.virtual_key) {
      if (key_binding.uppercase == e.is_shift_pressed()) {
        key_binding.is_pressed = is_down;
      }
    }
  }
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
