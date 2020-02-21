/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/sdl/sdl_input_driver.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

#include "xenia/base/cvar.h"
#include "xenia/ui/window.h"

// TODO(joellinn) make this path relative to the config folder.
DEFINE_string(mappings_file, "gamecontrollerdb.txt",
              "Filename of a database with custom game controller mappings.",
              "SDL");

namespace xe {
namespace hid {
namespace sdl {

SDLInputDriver::SDLInputDriver(xe::ui::Window* window)
    : InputDriver(window),
      sdl_events_initialized_(false),
      sdl_gamecontroller_initialized_(false),
      sdl_events_unflushed_(0),
      sdl_pumpevents_queued_(false),
      controllers_(),
      controllers_mutex_() {}

SDLInputDriver::~SDLInputDriver() {
  for (size_t i = 0; i < controllers_.size(); i++) {
    if (controllers_.at(i).sdl) {
      SDL_GameControllerClose(controllers_.at(i).sdl);
      controllers_.at(i) = {};
    }
  }
  if (sdl_events_initialized_) {
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    sdl_events_initialized_ = false;
  }
  if (sdl_gamecontroller_initialized_) {
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    sdl_gamecontroller_initialized_ = false;
  }
}

X_STATUS SDLInputDriver::Setup() {
  if (!TestSDLVersion()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // SDL_PumpEvents should only be run in the thread that initialized SDL - we
  // are hijacking the window loop thread for that.
  window_->loop()->PostSynchronous([&]() {
    // Initialize the event system early, so we catch device events for already
    // connected controllers.
    if (SDL_InitSubSystem(SDL_INIT_EVENTS) < 0) {
      return;
    }
    sdl_events_initialized_ = true;

    SDL_EventFilter event_filter{[](void* userdata, SDL_Event* event) -> int {
      if (!userdata) {
        assert_always();
        return 0;
      }

      // Event queue should never be (this) full
      assert(SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, SDL_FIRSTEVENT,
                            SDL_LASTEVENT) < 0xFFFF);

      const auto type = event->type;
      if (type < SDL_JOYAXISMOTION || type > SDL_CONTROLLERDEVICEREMAPPED) {
        return 0;
      }

      // If another part of xenia uses another SDL subsystem that generates
      // events, this may seem like a bad idea. They will however not subscribe
      // to controller events so we get away with that.
      const auto driver = static_cast<SDLInputDriver*>(userdata);
      // The queue could grow up to 3.5MB since it is never polled.
      if (++driver->sdl_events_unflushed_ > 64) {
        SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_CONTROLLERDEVICEREMAPPED);
        driver->sdl_events_unflushed_ = 0;
      }
      switch (type) {
        case SDL_CONTROLLERDEVICEADDED:
          driver->OnControllerDeviceAdded(event);
          break;
        case SDL_CONTROLLERDEVICEREMOVED:
          driver->OnControllerDeviceRemoved(event);
          break;
        case SDL_CONTROLLERAXISMOTION:
          driver->OnControllerDeviceAxisMotion(event);
          break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          driver->OnControllerDeviceButtonChanged(event);
          break;
        default:
          break;
      }
      return 0;
    }};
    // With an event watch we will always get notified, even if the event queue
    // is full, which can happen if another subsystem does not clear its events.
    SDL_AddEventWatch(event_filter, this);

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
      return;
    }
    sdl_gamecontroller_initialized_ = true;

    SDL_GameControllerAddMappingsFromFile(cvars::mappings_file.c_str());
  });
  return sdl_events_initialized_ && sdl_gamecontroller_initialized_;
}

X_RESULT SDLInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                         X_INPUT_CAPABILITIES* out_caps) {
  assert(sdl_events_initialized_ && sdl_gamecontroller_initialized_);

  QueueControllerUpdate();

  std::unique_lock<std::mutex> guard(controllers_mutex_);

  auto controller = GetControllerState(user_index);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  out_caps->flags = 0;
  out_caps->gamepad.buttons = 0xF3FF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = static_cast<int16_t>(0xFFFFu);
  out_caps->gamepad.thumb_ly = static_cast<int16_t>(0xFFFFu);
  out_caps->gamepad.thumb_rx = static_cast<int16_t>(0xFFFFu);
  out_caps->gamepad.thumb_ry = static_cast<int16_t>(0xFFFFu);
  out_caps->vibration.left_motor_speed = 0xFFFFu;
  out_caps->vibration.right_motor_speed = 0xFFFFu;
  return X_ERROR_SUCCESS;
}

X_RESULT SDLInputDriver::GetState(uint32_t user_index,
                                  X_INPUT_STATE* out_state) {
  assert(sdl_events_initialized_ && sdl_gamecontroller_initialized_);

  QueueControllerUpdate();

  std::unique_lock<std::mutex> guard(controllers_mutex_);

  auto controller = GetControllerState(user_index);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Make sure packet_number is only incremented by 1, even if there have been
  // multiple updates between GetState calls.
  if (controller->state_changed) {
    controller->state.packet_number++;
    controller->state_changed = false;
  }
  *out_state = controller->state;
  return X_ERROR_SUCCESS;
}

X_RESULT SDLInputDriver::SetState(uint32_t user_index,
                                  X_INPUT_VIBRATION* vibration) {
  assert(sdl_events_initialized_ && sdl_gamecontroller_initialized_);

  QueueControllerUpdate();

  std::unique_lock<std::mutex> guard(controllers_mutex_);

  auto controller = GetControllerState(user_index);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

#if SDL_VERSION_ATLEAST(2, 0, 9)
  if (SDL_GameControllerRumble(controller->sdl, vibration->left_motor_speed,
                               vibration->right_motor_speed, 0)) {
    return X_ERROR_FUNCTION_FAILED;
  } else {
    return X_ERROR_SUCCESS;
  }
#else
  return X_ERROR_SUCCESS;
#endif
}

X_RESULT SDLInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                      X_INPUT_KEYSTROKE* out_keystroke) {
  // TODO(joellinn) translate keyboard events for chatpad emulation.
  return X_ERROR_EMPTY;
}

void SDLInputDriver::OnControllerDeviceAdded(SDL_Event* event) {
  assert(window_->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  // Open the controller.
  const auto controller = SDL_GameControllerOpen(event->cdevice.which);
  if (!controller) {
    assert_always();
    return;
  }
  int user_id = -1;
#if SDL_VERSION_ATLEAST(2, 0, 9)
  // Check if the controller has a player index LED.
  user_id = SDL_GameControllerGetPlayerIndex(controller);
  // Is that id already taken?
  if (user_id < 0 || user_id >= controllers_.size() ||
      controllers_.at(user_id).sdl) {
    user_id = -1;
  }
#endif
  // No player index or already taken, just take the first free slot.
  if (user_id < 0) {
    for (size_t i = 0; i < controllers_.size(); i++) {
      if (!controllers_.at(i).sdl) {
        user_id = static_cast<int>(i);
        break;
      }
    }
  }
  if (user_id >= 0) {
    controllers_.at(user_id) = {controller, {}};
    // XInput seems to start with packet_number = 1 .
    controllers_.at(user_id).state_changed = true;
  } else {
    // No more controllers needed, close it.
    SDL_GameControllerClose(controller);
  }
}

void SDLInputDriver::OnControllerDeviceRemoved(SDL_Event* event) {
  assert(window_->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  // Find the disconnected gamecontroller and close it.
  bool found;
  size_t i;
  std::tie(found, i) = GetControllerIndexFromInstanceID(event->cdevice.which);
  assert(found);
  SDL_GameControllerClose(controllers_.at(i).sdl);
  controllers_.at(i) = {};
}

void SDLInputDriver::OnControllerDeviceAxisMotion(SDL_Event* event) {
  assert(window_->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  bool found;
  size_t i;
  std::tie(found, i) = GetControllerIndexFromInstanceID(event->caxis.which);
  assert(found);
  const auto pad = &controllers_.at(i).state.gamepad;
  switch (event->caxis.axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
      pad->thumb_lx = event->caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_LEFTY:
      pad->thumb_ly = ~event->caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_RIGHTX:
      pad->thumb_rx = event->caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_RIGHTY:
      pad->thumb_ry = ~event->caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
      pad->left_trigger = static_cast<uint8_t>(event->caxis.value >> 7);
      break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
      pad->right_trigger = static_cast<uint8_t>(event->caxis.value >> 7);
      break;
    default:
      assert_always();
      break;
  }
  controllers_.at(i).state_changed = true;
}

void SDLInputDriver::OnControllerDeviceButtonChanged(SDL_Event* event) {
  assert(window_->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  // Define a lookup table to map between SDL and XInput button codes.
  // These need to be in the order of the SDL_GameControllerButton enum.
  static constexpr std::array<uint16_t, SDL_CONTROLLER_BUTTON_MAX>
      xbutton_lookup = {X_INPUT_GAMEPAD_A,
                        X_INPUT_GAMEPAD_B,
                        X_INPUT_GAMEPAD_X,
                        X_INPUT_GAMEPAD_Y,
                        X_INPUT_GAMEPAD_BACK,
                        0 /* Guide button */,
                        X_INPUT_GAMEPAD_START,
                        X_INPUT_GAMEPAD_LEFT_THUMB,
                        X_INPUT_GAMEPAD_RIGHT_THUMB,
                        X_INPUT_GAMEPAD_LEFT_SHOULDER,
                        X_INPUT_GAMEPAD_RIGHT_SHOULDER,
                        X_INPUT_GAMEPAD_DPAD_UP,
                        X_INPUT_GAMEPAD_DPAD_DOWN,
                        X_INPUT_GAMEPAD_DPAD_LEFT,
                        X_INPUT_GAMEPAD_DPAD_RIGHT};

  bool found;
  size_t i;
  std::tie(found, i) = GetControllerIndexFromInstanceID(event->cbutton.which);
  assert(found);
  const auto controller = &controllers_.at(i);

  uint16_t xbuttons = controller->state.gamepad.buttons;
  // Lookup the XInput button code.
  auto xbutton = xbutton_lookup.at(event->cbutton.button);
  // Pressed or released?
  if (event->cbutton.state == SDL_PRESSED) {
    xbuttons |= xbutton;
  } else {
    xbuttons &= ~xbutton;
  }
  controller->state.gamepad.buttons = xbuttons;
  controller->state_changed = true;
}

std::pair<bool, size_t> SDLInputDriver::GetControllerIndexFromInstanceID(
    SDL_JoystickID instance_id) {
  // Loop through our controllers and try to match the given ID.
  for (size_t i = 0; i < controllers_.size(); i++) {
    auto controller = controllers_.at(i).sdl;
    if (!controller) {
      continue;
    }
    auto joystick = SDL_GameControllerGetJoystick(controller);
    assert(joystick);
    auto joy_instance_id = SDL_JoystickInstanceID(joystick);
    assert(joy_instance_id >= 0);
    if (joy_instance_id == instance_id) {
      return {true, i};
    }
  }
  return {false, 0};
}

SDLInputDriver::ControllerState* SDLInputDriver::GetControllerState(
    uint32_t user_index) {
  if (user_index >= controllers_.size()) {
    return nullptr;
  }
  auto controller = &controllers_.at(user_index);
  if (!controller->sdl) {
    return nullptr;
  }
  return controller;
}

bool SDLInputDriver::TestSDLVersion() {
#if SDL_VERSION_ATLEAST(2, 0, 9)
  // SDL 2.0.9 or newer is required for simple rumble support and player
  // index.
  const Uint8 min_patchlevel = 9;
#else
  // SDL 2.0.4 or newer is required to read game controller mappings from
  // file.
  const Uint8 min_patchlevel = 4;
#endif

  // With msvc delayed loading, exceptions are used to determine dll presence.
#if XE_PLATFORM_WIN32
  __try {
#endif  // XE_PLATFORM_WIN32
    SDL_version ver = {};
    SDL_GetVersion(&ver);
    if ((ver.major < 2) ||
        (ver.major == 2 && ver.minor == 0 && ver.patch < min_patchlevel)) {
      return false;
    }
#if XE_PLATFORM_WIN32
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#endif  // XE_PLATFORM_WIN32
  return true;
}

void SDLInputDriver::QueueControllerUpdate() {
  // To minimize consecutive event pumps do not queue before previous pump is
  // finished.
  bool is_queued = false;
  sdl_pumpevents_queued_.compare_exchange_strong(is_queued, true);
  if (!is_queued) {
    window_->loop()->Post([this]() {
      SDL_PumpEvents();
      sdl_pumpevents_queued_ = false;
    });
  }
}

}  // namespace sdl
}  // namespace hid
}  // namespace xe
