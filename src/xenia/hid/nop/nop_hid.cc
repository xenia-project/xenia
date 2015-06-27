/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/nop/nop_hid.h"

#include "xenia/hid/nop/nop_input_driver.h"

namespace xe {
namespace hid {
namespace nop {

std::unique_ptr<InputDriver> Create(InputSystem* input_system) {
  return std::make_unique<NopInputDriver>(input_system);
}

}  // namespace nop
}  // namespace hid
}  // namespace xe
