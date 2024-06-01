/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_INPUT_SYSTEM_H_
#define XENIA_HID_INPUT_SYSTEM_H_

#include <bitset>
#include <memory>
#include <vector>
#include "xenia/base/mutex.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_driver.h"
#include "xenia/xbox.h"

namespace xe {
namespace ui {
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace hid {

static constexpr uint8_t max_allowed_controllers = 4;

class InputSystem {
 public:
  explicit InputSystem(xe::ui::Window* window);
  ~InputSystem();

  xe::ui::Window* window() const { return window_; }

  X_STATUS Setup();

  void AddDriver(std::unique_ptr<InputDriver> driver);

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps);
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state);
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration);
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke);

  void ToggleVibration();

  const std::bitset<max_allowed_controllers> GetConnectedSlots() const {
    return connected_slots;
  }

  std::unique_lock<xe_unlikely_mutex> lock();

 private:
  typedef std::pair<uint16_t, uint16_t> joystick_value;

  const std::string controller_slot_state_change_message[2] = {
      "Controller disconnected from slot {}.",
      "New controller connected to slot {}."};

  void UpdateUsedSlot(InputDriver* driver, uint8_t slot, bool connected);
  void AdjustDeadzoneLevels(const uint8_t slot, X_INPUT_GAMEPAD* gamepad);
  X_INPUT_VIBRATION ModifyVibrationLevel(X_INPUT_VIBRATION* vibration);

  xe::ui::Window* window_ = nullptr;

  std::vector<std::unique_ptr<InputDriver>> drivers_;

  std::bitset<max_allowed_controllers> connected_slots = {};
  std::array<std::pair<joystick_value, joystick_value>, max_allowed_controllers>
      controllers_max_joystick_value = {};

  xe_unlikely_mutex lock_;
};

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_INPUT_SYSTEM_H_
