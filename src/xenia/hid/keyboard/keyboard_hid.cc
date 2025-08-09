/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/keyboard/keyboard_hid.h"

#include "xenia/hid/keyboard/keyboard_input_driver.h"

namespace xe {
namespace hid {
namespace keyboard {

std::unique_ptr<InputDriver> Create(xe::ui::Window* window,
                                    size_t window_z_order) {
  return std::make_unique<xe::hid::keyboard::KeyboardInputDriver>(window, window_z_order);
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
