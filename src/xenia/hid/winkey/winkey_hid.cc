/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/winkey/winkey_hid.h>

#include <xenia/hid/winkey/winkey_input_driver.h>

namespace xe {
namespace hid {
namespace winkey {

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

InputDriver* xe::hid::winkey::Create(InputSystem* input_system) {
  InitializeIfNeeded();
  return new WinKeyInputDriver(input_system);
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
