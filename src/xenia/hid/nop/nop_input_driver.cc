/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/nop/nop_input_driver.h>

#include <xenia/hid/hid-private.h>


using namespace xe;
using namespace xe::hid;
using namespace xe::hid::nop;


NopInputDriver::NopInputDriver(InputSystem* input_system) :
    InputDriver(input_system) {
}

NopInputDriver::~NopInputDriver() {
}

X_STATUS NopInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}
