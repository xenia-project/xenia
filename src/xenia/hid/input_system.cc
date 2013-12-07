/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/input_system.h>

#include <xenia/emulator.h>
#include <xenia/cpu/processor.h>
#include <xenia/hid/input_driver.h>


using namespace xe;
using namespace xe::hid;


InputSystem::InputSystem(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()) {
}

InputSystem::~InputSystem() {
  for (std::vector<InputDriver*>::iterator it = drivers_.begin();
       it != drivers_.end(); ++it) {
    InputDriver* driver = *it;
    delete driver;
  }
}

X_STATUS InputSystem::Setup() {
  processor_ = emulator_->processor();

  return X_STATUS_SUCCESS;
}

void InputSystem::AddDriver(InputDriver* driver) {
  drivers_.push_back(driver);
}

X_RESULT InputSystem::GetCapabilities(
    uint32_t user_index, uint32_t flags, X_INPUT_CAPABILITIES& out_caps) {
  for (std::vector<InputDriver*>::iterator it = drivers_.begin();
       it != drivers_.end(); ++it) {
    InputDriver* driver = *it;
    if (XSUCCEEDED(driver->GetCapabilities(user_index, flags, out_caps))) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE& out_state) {
  for (std::vector<InputDriver*>::iterator it = drivers_.begin();
       it != drivers_.end(); ++it) {
    InputDriver* driver = *it;
    if (driver->GetState(user_index, out_state) == X_ERROR_SUCCESS) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::SetState(
    uint32_t user_index, X_INPUT_VIBRATION& vibration) {
  for (std::vector<InputDriver*>::iterator it = drivers_.begin();
       it != drivers_.end(); ++it) {
    InputDriver* driver = *it;
    if (XSUCCEEDED(driver->SetState(user_index, vibration))) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}
