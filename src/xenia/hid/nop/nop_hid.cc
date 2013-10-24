/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/nop/nop_hid.h>

#include <xenia/hid/nop/nop_input_driver.h>


using namespace xe;
using namespace xe::hid;
using namespace xe::hid::nop;


namespace {
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

  void CleanupOnShutdown() {
  }
}


InputDriver* xe::hid::nop::Create(InputSystem* input_system) {
  InitializeIfNeeded();
  return new NopInputDriver(input_system);
}
