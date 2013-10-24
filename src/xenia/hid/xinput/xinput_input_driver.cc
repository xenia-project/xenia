/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/xinput/xinput_input_driver.h>

#include <xenia/hid/hid-private.h>


using namespace xe;
using namespace xe::hid;
using namespace xe::hid::xinput;


XInputInputDriver::XInputInputDriver(InputSystem* input_system) :
    InputDriver(input_system) {
}

XInputInputDriver::~XInputInputDriver() {
}

X_STATUS XInputInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}
