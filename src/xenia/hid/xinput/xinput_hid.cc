/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/xinput/xinput_hid.h"

#include "xenia/hid/xinput/xinput_input_driver.h"

namespace xe {
namespace hid {
namespace xinput {

std::unique_ptr<InputDriver> Create(InputSystem* input_system) {
  return std::make_unique<XInputInputDriver>(input_system);
}

}  // namespace xinput
}  // namespace hid
}  // namespace xe
