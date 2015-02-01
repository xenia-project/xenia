/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/input_system.h"

#include "xenia/emulator.h"
#include "xenia/cpu/processor.h"
#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {

InputSystem::InputSystem(Emulator* emulator)
    : emulator_(emulator), memory_(emulator->memory()) {}

InputSystem::~InputSystem() = default;

X_STATUS InputSystem::Setup() {
  processor_ = emulator_->processor();

  return X_STATUS_SUCCESS;
}

void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  drivers_.push_back(std::move(driver));
}

X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");

  for (auto& driver : drivers_) {
    if (XSUCCEEDED(driver->GetCapabilities(user_index, flags, out_caps))) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  for (auto& driver : drivers_) {
    if (driver->GetState(user_index, out_state) == X_ERROR_SUCCESS) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::SetState(uint32_t user_index,
                               X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");

  for (auto& driver : drivers_) {
    if (XSUCCEEDED(driver->SetState(user_index, vibration))) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");

  for (auto& driver : drivers_) {
    if (XSUCCEEDED(driver->GetKeystroke(user_index, flags, out_keystroke))) {
      return X_ERROR_SUCCESS;
    }
  }
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace hid
}  // namespace xe
