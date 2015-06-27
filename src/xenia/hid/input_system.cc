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
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_driver.h"
#include "xenia/profiling.h"

#include "xenia/hid/nop/nop_hid.h"
#if XE_PLATFORM_WIN32
#include "xenia/hid/winkey/winkey_hid.h"
#include "xenia/hid/xinput/xinput_hid.h"
#endif  // WIN32

namespace xe {
namespace hid {

std::unique_ptr<InputSystem> InputSystem::Create(Emulator* emulator) {
  auto input_system = std::make_unique<InputSystem>(emulator);

  if (FLAGS_hid.compare("nop") == 0) {
    input_system->AddDriver(xe::hid::nop::Create(input_system.get()));
#if XE_PLATFORM_WIN32
  } else if (FLAGS_hid.compare("winkey") == 0) {
    input_system->AddDriver(xe::hid::winkey::Create(input_system.get()));
  } else if (FLAGS_hid.compare("xinput") == 0) {
    input_system->AddDriver(xe::hid::xinput::Create(input_system.get()));
#endif  // WIN32
  } else {
    // Create all available.
    bool any_created = false;

// NOTE: in any mode we create as many as we can, falling back to nop.

#if XE_PLATFORM_WIN32
    auto xinput_driver = xe::hid::xinput::Create(input_system.get());
    if (xinput_driver) {
      input_system->AddDriver(std::move(xinput_driver));
      any_created = true;
    }
    auto winkey_driver = xe::hid::winkey::Create(input_system.get());
    if (winkey_driver) {
      input_system->AddDriver(std::move(winkey_driver));
      any_created = true;
    }
#endif  // WIN32

    // Fallback to nop if none created.
    if (!any_created) {
      input_system->AddDriver(xe::hid::nop::Create(input_system.get()));
    }
  }

  return input_system;
}

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

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetCapabilities(user_index, flags, out_caps);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetState(user_index, out_state);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::SetState(uint32_t user_index,
                               X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->SetState(user_index, vibration);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetKeystroke(user_index, flags, out_keystroke);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace hid
}  // namespace xe
