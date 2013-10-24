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
    emulator_(emulator) {
  memory_ = xe_memory_retain(emulator->memory());
}

InputSystem::~InputSystem() {
  for (std::vector<InputDriver*>::iterator it = drivers_.begin();
       it != drivers_.end(); ++it) {
    InputDriver* driver = *it;
    delete driver;
  }
  xe_memory_release(memory_);
}

X_STATUS InputSystem::Setup() {
  processor_ = emulator_->processor();

  return X_STATUS_SUCCESS;
}

void InputSystem::AddDriver(InputDriver* driver) {
  drivers_.push_back(driver);
}
