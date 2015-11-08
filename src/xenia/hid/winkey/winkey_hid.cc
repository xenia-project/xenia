/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/winkey_hid.h"

#include "xenia/hid/winkey/winkey_input_driver.h"

namespace xe {
namespace hid {
namespace winkey {

std::unique_ptr<InputDriver> Create(xe::ui::Window* window) {
  return std::make_unique<WinKeyInputDriver>(window);
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
