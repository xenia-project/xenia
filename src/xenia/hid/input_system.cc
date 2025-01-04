/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include "xenia/hid/input_system.h"

#include "xenia/base/profiling.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_driver.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace hid {

DEFINE_bool(vibration, true, "Toggle controller vibration.", "HID");

DEFINE_double(left_stick_deadzone_percentage, 0.0,
              "Defines deadzone level for left stick. Allowed range [0.0-1.0].",
              "HID");
DEFINE_double(
    right_stick_deadzone_percentage, 0.0,
    "Defines deadzone level for right stick. Allowed range [0.0-1.0].", "HID");

InputSystem::InputSystem(xe::ui::Window* window) : window_(window) {}

InputSystem::~InputSystem() = default;

X_STATUS InputSystem::Setup() { return X_STATUS_SUCCESS; }

void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  drivers_.push_back(std::move(driver));
}

void InputSystem::UpdateUsedSlot(InputDriver* driver, uint8_t slot,
                                 bool connected) {
  if (slot == XUserIndexAny) {
    XELOGW("{} received requrest for slot any! Unsupported", __func__);
    return;
  }

  // Do not report passthrough as a controller.
  if (driver && driver->GetInputType() == InputType::Keyboard) {
    return;
  }

  if (connected_slots.test(slot) == connected) {
    // No state change, so nothing to do.
    return;
  }

  XELOGI(controller_slot_state_change_message[connected].c_str(), slot);
  connected_slots.flip(slot);
  if (kernel::kernel_state()) {
    kernel::kernel_state()->BroadcastNotification(
        kXNotificationSystemInputDevicesChanged, 0);
  }

  if (driver) {
    X_INPUT_CAPABILITIES capabilities = {};
    const X_RESULT result = driver->GetCapabilities(slot, 0, &capabilities);
    if (result != X_STATUS_SUCCESS) {
      return;
    }

    controllers_max_joystick_value[slot] = {
        {capabilities.gamepad.thumb_lx, capabilities.gamepad.thumb_ly},
        {capabilities.gamepad.thumb_rx, capabilities.gamepad.thumb_ry}};
  }
}

std::vector<InputDriver*> InputSystem::FilterDrivers(uint32_t flags) {
  std::vector<InputDriver*> filtered_drivers;
  for (auto& driver : drivers_) {
    if (driver->GetInputType() == InputType::None) {
      continue;
    }

    if ((flags & driver->GetInputType()) != 0) {
      filtered_drivers.push_back(driver.get());
    }
  }
  return filtered_drivers;
}

X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");

  std::vector<InputDriver*> filtered_drivers = FilterDrivers(flags);

  for (auto& driver : filtered_drivers) {
    X_RESULT result = driver->GetCapabilities(user_index, flags, out_caps);
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetState(uint32_t user_index, uint32_t flags,
                               X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  std::vector<InputDriver*> filtered_drivers = FilterDrivers(flags);

  for (auto& driver : filtered_drivers) {
    X_RESULT result = driver->GetState(user_index, out_state);
    if (result == X_ERROR_SUCCESS) {
      UpdateUsedSlot(driver, user_index, true);
      AdjustDeadzoneLevels(user_index, &out_state->gamepad);

      if (out_state->gamepad.buttons != 0) {
        last_used_slot = user_index;
      }
      return result;
    }
  }
  UpdateUsedSlot(nullptr, user_index, false);
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::SetState(uint32_t user_index,
                               X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");
  X_INPUT_VIBRATION modified_vibration = ModifyVibrationLevel(vibration);
  for (auto& driver : drivers_) {
    X_RESULT result = driver->SetState(user_index, &modified_vibration);
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");

  std::vector<InputDriver*> filtered_drivers = FilterDrivers(flags);

  bool any_connected = false;
  for (auto& driver : filtered_drivers) {
    // connected_slots
    X_RESULT result = driver->GetKeystroke(user_index, flags, out_keystroke);
    if (result == X_ERROR_INVALID_PARAMETER ||
        result == X_ERROR_DEVICE_NOT_CONNECTED) {
      continue;
    }

    any_connected = true;

    if (result == X_ERROR_SUCCESS) {
      last_used_slot = user_index;
      return result;
    }

    if (result == X_ERROR_EMPTY) {
      continue;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

bool InputSystem::GetVibrationCvar() { return cvars::vibration; }

void InputSystem::ToggleVibration() {
  OVERRIDE_bool(vibration, !cvars::vibration);
  // Send instant update to vibration state to prevent awaiting for next tick.
  X_INPUT_VIBRATION vibration = X_INPUT_VIBRATION();

  for (uint8_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    SetState(user_index, &vibration);
  }
}

void InputSystem::AdjustDeadzoneLevels(const uint8_t slot,
                                       X_INPUT_GAMEPAD* gamepad) {
  if (slot > XUserMaxUserCount) {
    return;
  }

  // Left stick
  if (cvars::left_stick_deadzone_percentage > 0.0 &&
      cvars::left_stick_deadzone_percentage < 1.0) {
    const double deadzone_lx_percentage =
        controllers_max_joystick_value[slot].first.first *
        cvars::left_stick_deadzone_percentage;
    const double deadzone_ly_percentage =
        controllers_max_joystick_value[slot].first.second *
        cvars::left_stick_deadzone_percentage;

    const double theta = std::atan2(static_cast<double>(gamepad->thumb_ly),
                                    static_cast<double>(gamepad->thumb_lx));

    const double deadzone_y_value = std::sin(theta) * deadzone_ly_percentage;
    const double deadzone_x_value = std::cos(theta) * deadzone_lx_percentage;

    if (gamepad->thumb_ly > -deadzone_y_value &&
        gamepad->thumb_ly < deadzone_y_value) {
      gamepad->thumb_ly = 0;
    }

    if (gamepad->thumb_lx > -deadzone_x_value &&
        gamepad->thumb_lx < deadzone_x_value) {
      gamepad->thumb_lx = 0;
    }
  }

  // Right stick
  if (cvars::right_stick_deadzone_percentage > 0.0 &&
      cvars::right_stick_deadzone_percentage < 1.0) {
    const double deadzone_rx_percentage =
        controllers_max_joystick_value[slot].second.first *
        cvars::right_stick_deadzone_percentage;
    const double deadzone_ry_percentage =
        controllers_max_joystick_value[slot].second.second *
        cvars::right_stick_deadzone_percentage;

    const double theta = std::atan2(static_cast<double>(gamepad->thumb_ry),
                                    static_cast<double>(gamepad->thumb_rx));

    const double deadzone_y_value = std::sin(theta) * deadzone_ry_percentage;
    const double deadzone_x_value = std::cos(theta) * deadzone_rx_percentage;

    if (gamepad->thumb_ry > -deadzone_y_value &&
        gamepad->thumb_ry < deadzone_y_value) {
      gamepad->thumb_ry = 0;
    }

    if (gamepad->thumb_rx > -deadzone_x_value &&
        gamepad->thumb_rx < deadzone_x_value) {
      gamepad->thumb_rx = 0;
    }
  }
}

X_INPUT_VIBRATION InputSystem::ModifyVibrationLevel(
    X_INPUT_VIBRATION* vibration) {
  X_INPUT_VIBRATION modified_vibration = *vibration;
  if (cvars::vibration) {
    return modified_vibration;
  }

  // TODO(Gliniak): Use modifier instead of boolean value.
  modified_vibration.left_motor_speed = 0;
  modified_vibration.right_motor_speed = 0;
  return modified_vibration;
}
std::unique_lock<xe_unlikely_mutex> InputSystem::lock() {
  return std::unique_lock<xe_unlikely_mutex>{lock_};
}
}  // namespace hid
}  // namespace xe
