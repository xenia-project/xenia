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
  void UpdateUsedSlot(uint8_t slot, bool connected);
  uint8_t GetConnectedSlots() const { return connected_slot; }

  std::unique_lock<xe_unlikely_mutex> lock();

 private:
  xe::ui::Window* window_ = nullptr;

  std::vector<std::unique_ptr<InputDriver>> drivers_;

  X_INPUT_VIBRATION ModifyVibrationLevel(X_INPUT_VIBRATION* vibration);
  uint8_t connected_slot = 0b0001;
  xe_unlikely_mutex lock_;
};

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_INPUT_SYSTEM_H_
