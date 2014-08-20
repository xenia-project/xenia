/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/xinput/xinput_hid.h>

#include <xenia/hid/xinput/xinput_input_driver.h>

namespace xe {
namespace hid {
namespace xinput {

void InitializeIfNeeded();
void CleanupOnShutdown();

void InitializeIfNeeded() {
  static bool has_initialized = false;
  if (has_initialized) {
    return;
  }
  has_initialized = true;

  //

  atexit(CleanupOnShutdown);
}

void CleanupOnShutdown() {}

InputDriver* xe::hid::xinput::Create(InputSystem* input_system) {
  InitializeIfNeeded();
  return new XInputInputDriver(input_system);
}

}  // namespace xinput
}  // namespace hid
}  // namespace xe
