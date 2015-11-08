/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {

InputDriver::InputDriver(xe::ui::Window* window) : window_(window) {}

InputDriver::~InputDriver() = default;

}  // namespace hid
}  // namespace xe
