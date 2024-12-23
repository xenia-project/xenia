/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_INPUT_DRIVER_H_
#define XENIA_HID_INPUT_DRIVER_H_

#include <cstddef>
#include <functional>

#include "xenia/hid/input.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

namespace xe {
namespace ui {
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace hid {

class InputSystem;

enum InputType { None, Controller = 1, Keyboard = 2, Other = 4 };

class InputDriver {
 public:
  virtual ~InputDriver() = default;

  virtual X_STATUS Setup() = 0;

  virtual X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                                   X_INPUT_CAPABILITIES* out_caps) = 0;
  virtual X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) = 0;
  virtual X_RESULT SetState(uint32_t user_index,
                            X_INPUT_VIBRATION* vibration) = 0;
  virtual X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                                X_INPUT_KEYSTROKE* out_keystroke) = 0;

  virtual InputType GetInputType() const = 0;

  void set_is_active_callback(std::function<bool()> is_active_callback) {
    is_active_callback_ = is_active_callback;
  }

 protected:
  explicit InputDriver(xe::ui::Window* window, size_t window_z_order)
      : window_(window), window_z_order_(window_z_order) {}

  xe::ui::Window* window() const { return window_; }
  size_t window_z_order() const { return window_z_order_; }

  bool is_active() const {
    return !is_active_callback_ || is_active_callback_();
  }

 private:
  xe::ui::Window* window_;
  size_t window_z_order_;
  std::function<bool()> is_active_callback_ = nullptr;
};

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_INPUT_DRIVER_H_
