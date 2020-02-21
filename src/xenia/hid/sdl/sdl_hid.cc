/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/sdl/sdl_hid.h"

#include "xenia/hid/sdl/sdl_input_driver.h"

namespace xe {
namespace hid {
namespace sdl {

std::unique_ptr<InputDriver> Create(xe::ui::Window* window) {
  return std::make_unique<SDLInputDriver>(window);
}

}  // namespace sdl
}  // namespace hid
}  // namespace xe
