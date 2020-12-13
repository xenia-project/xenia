/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/sdl/sdl_input_driver.h"

#include <array>

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/helper/sdl/sdl_helper.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/ui/window.h"

// TODO(joellinn) make this path relative to the config folder.
DEFINE_path(mappings_file, "gamecontrollerdb.txt",
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
      controllers_mutex_(),
      keystroke_states_() {}

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
  window()->loop()->PostSynchronous([&]() {
    if (!xe::helper::sdl::SDLHelper::Prepare()) {
      return;
    }
    // Initialize the event system early, so we catch device events for already
    // connected controllers.
    if (SDL_InitSubSystem(SDL_INIT_EVENTS) < 0) {
      return;
    }
    sdl_events_initialized_ = true;

    SDL_EventFilter event_filter{[](void* userdata, SDL_Event* event) -> int {
      if (!userdata || !event) {
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
          driver->OnControllerDeviceAdded(*event);
          break;
        case SDL_CONTROLLERDEVICEREMOVED:
          driver->OnControllerDeviceRemoved(*event);
          break;
        case SDL_CONTROLLERAXISMOTION:
          driver->OnControllerDeviceAxisMotion(*event);
          break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          driver->OnControllerDeviceButtonChanged(*event);
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

    if (!cvars::mappings_file.empty()) {
      if (!std::filesystem::exists(cvars::mappings_file)) {
        XELOGW("SDL GameControllerDB: file '{}' does not exist.",
               xe::path_to_utf8(cvars::mappings_file));
      } else {
        auto mappings_file = filesystem::OpenFile(cvars::mappings_file, "rb");
        if (!mappings_file) {
          XELOGE("SDL GameControllerDB: failed to open file '{}'.",
                 xe::path_to_utf8(cvars::mappings_file));
        } else {
          auto mappings_result = SDL_GameControllerAddMappingsFromRW(
              SDL_RWFromFP(mappings_file, SDL_TRUE), 1);
          if (mappings_result < 0) {
            XELOGE("SDL GameControllerDB: error loading file '{}': {}.",
                   xe::path_to_utf8(cvars::mappings_file), mappings_result);
          } else {
            XELOGI("SDL GameControllerDB: loaded {} mappings.",
                   mappings_result);
          }
        }
      }
    }
  });
  return sdl_events_initialized_ && sdl_gamecontroller_initialized_;
}

X_RESULT SDLInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                         X_INPUT_CAPABILITIES* out_caps) {
  assert(sdl_events_initialized_ && sdl_gamecontroller_initialized_);
  if (user_index >= HID_SDL_USER_COUNT) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  QueueControllerUpdate();

  std::unique_lock<std::mutex> guard(controllers_mutex_);

  auto controller = GetControllerState(user_index);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  out_caps->flags = 0;
  out_caps->gamepad.buttons =
      0xF3FF | (cvars::guide_button ? X_INPUT_GAMEPAD_GUIDE : 0x0);
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
  if (user_index >= HID_SDL_USER_COUNT) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  auto is_active = this->is_active();

  if (is_active) {
    QueueControllerUpdate();
  }

  std::unique_lock<std::mutex> guard(controllers_mutex_);

  auto controller = GetControllerState(user_index);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Make sure packet_number is only incremented by 1, even if there have been
  // multiple updates between GetState calls. Also track `is_active` to
  // increment the packet number if it changed.
  if ((is_active != controller->is_active) ||
      (is_active && controller->state_changed)) {
    controller->state.packet_number++;
    controller->is_active = is_active;
    controller->state_changed = false;
  }
  std::memcpy(out_state, &controller->state, sizeof(*out_state));
  if (!is_active) {
    // Simulate an "untouched" controller. When we become active again the
    // pressed buttons aren't lost and will be visible again.
    std::memset(&out_state->gamepad, 0, sizeof(out_state->gamepad));
  }
  return X_ERROR_SUCCESS;
}

X_RESULT SDLInputDriver::SetState(uint32_t user_index,
                                  X_INPUT_VIBRATION* vibration) {
  assert(sdl_events_initialized_ && sdl_gamecontroller_initialized_);
  if (user_index >= HID_SDL_USER_COUNT) {
    return X_ERROR_BAD_ARGUMENTS;
  }

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

X_RESULT SDLInputDriver::GetKeystroke(uint32_t users, uint32_t flags,
                                      X_INPUT_KEYSTROKE* out_keystroke) {
  // TODO(JoelLinn): Figure out the flags
  // https://github.com/evilC/UCR/blob/0489929e2a8e39caa3484c67f3993d3fba39e46f/Libraries/XInput.ahk#L85-L98
  assert(sdl_events_initialized_ && sdl_gamecontroller_initialized_);
  bool user_any = users == 0xFF;
  if (users >= HID_SDL_USER_COUNT && !user_any) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if (!out_keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  // The order of this list is also the order in which events are send if
  // multiple buttons change at once.
  static_assert(sizeof(X_INPUT_GAMEPAD::buttons) == 2);
  static const std::array<std::underlying_type<X_INPUT_GAMEPAD_VK>::type, 34>
      vk_lookup = {
          // 00 - True buttons from xinput button field
          X_INPUT_GAMEPAD_VK_DPAD_UP,
          X_INPUT_GAMEPAD_VK_DPAD_DOWN,
          X_INPUT_GAMEPAD_VK_DPAD_LEFT,
          X_INPUT_GAMEPAD_VK_DPAD_RIGHT,
          X_INPUT_GAMEPAD_VK_START,
          X_INPUT_GAMEPAD_VK_BACK,
          X_INPUT_GAMEPAD_VK_LTHUMB_PRESS,
          X_INPUT_GAMEPAD_VK_RTHUMB_PRESS,
          X_INPUT_GAMEPAD_VK_LSHOULDER,
          X_INPUT_GAMEPAD_VK_RSHOULDER,
          0, /* Guide has no VK */
          0, /* Unknown */
          X_INPUT_GAMEPAD_VK_A,
          X_INPUT_GAMEPAD_VK_B,
          X_INPUT_GAMEPAD_VK_X,
          X_INPUT_GAMEPAD_VK_Y,
          // 16 - Fake buttons generated from analog inputs
          X_INPUT_GAMEPAD_VK_LTRIGGER,
          X_INPUT_GAMEPAD_VK_RTRIGGER,
          // 18
          X_INPUT_GAMEPAD_VK_LTHUMB_UP,
          X_INPUT_GAMEPAD_VK_LTHUMB_DOWN,
          X_INPUT_GAMEPAD_VK_LTHUMB_RIGHT,
          X_INPUT_GAMEPAD_VK_LTHUMB_LEFT,
          X_INPUT_GAMEPAD_VK_LTHUMB_UPLEFT,
          X_INPUT_GAMEPAD_VK_LTHUMB_UPRIGHT,
          X_INPUT_GAMEPAD_VK_LTHUMB_DOWNRIGHT,
          X_INPUT_GAMEPAD_VK_LTHUMB_DOWNLEFT,
          // 26
          X_INPUT_GAMEPAD_VK_RTHUMB_UP,
          X_INPUT_GAMEPAD_VK_RTHUMB_DOWN,
          X_INPUT_GAMEPAD_VK_RTHUMB_RIGHT,
          X_INPUT_GAMEPAD_VK_RTHUMB_LEFT,
          X_INPUT_GAMEPAD_VK_RTHUMB_UPLEFT,
          X_INPUT_GAMEPAD_VK_RTHUMB_UPRIGHT,
          X_INPUT_GAMEPAD_VK_RTHUMB_DOWNRIGHT,
          X_INPUT_GAMEPAD_VK_RTHUMB_DOWNLEFT,
      };

  auto is_active = this->is_active();

  if (is_active) {
    QueueControllerUpdate();
  }

  std::unique_lock<std::mutex> guard(controllers_mutex_);

  for (uint32_t user_index = (user_any ? 0 : users);
       user_index < (user_any ? HID_SDL_USER_COUNT : users + 1); user_index++) {
    auto controller = GetControllerState(user_index);
    if (!controller) {
      if (user_any) {
        continue;
      } else {
        return X_ERROR_DEVICE_NOT_CONNECTED;
      }
    }

    // If input is not active (e.g. due to a dialog overlay), force buttons to
    // "unpressed". The algorithm will automatically send UP events when
    // `is_active()` goes low and DOWN events when it goes high again.
    const uint64_t curr_butts =
        is_active ? (controller->state.gamepad.buttons |
                     AnalogToKeyfield(controller->state.gamepad))
                  : uint64_t(0);
    KeystrokeState& last = keystroke_states_.at(user_index);

    // Handle repeating
    auto guest_now = Clock::QueryGuestUptimeMillis();
    static_assert(HID_SDL_REPEAT_DELAY >= HID_SDL_REPEAT_RATE);
    if (last.repeat_state == RepeatState::Waiting &&
        (last.repeat_time + HID_SDL_REPEAT_DELAY < guest_now)) {
      last.repeat_state = RepeatState::Repeating;
    }
    if (last.repeat_state == RepeatState::Repeating &&
        (last.repeat_time + HID_SDL_REPEAT_RATE < guest_now)) {
      last.repeat_time = guest_now;
      auto vk = vk_lookup.at(last.repeat_butt_idx);
      assert_not_zero(vk);
      out_keystroke->virtual_key = vk;
      out_keystroke->unicode = 0;
      out_keystroke->user_index = user_index;
      out_keystroke->hid_code = 0;
      out_keystroke->flags =
          X_INPUT_KEYSTROKE_KEYDOWN | X_INPUT_KEYSTROKE_REPEAT;
      return X_ERROR_SUCCESS;
    }

    auto butts_changed = curr_butts ^ last.buttons;
    if (!butts_changed) {
      continue;
    }

    // First try to clear buttons with up events. This is to match xinput
    // behaviour when transitioning thumb sticks, e.g. so that THUMB_UPLEFT is
    // up before THUMB_LEFT is down.
    for (auto [clear_pass, i] = std::tuple{true, 0}; i < 2;
         clear_pass = false, i++) {
      for (uint8_t i = 0; i < std::size(vk_lookup); i++) {
        auto fbutton = uint64_t(1) << i;
        if (!(butts_changed & fbutton)) {
          continue;
        }
        auto vk = vk_lookup.at(i);
        if (!vk) {
          continue;
        }

        out_keystroke->virtual_key = vk;
        out_keystroke->unicode = 0;
        out_keystroke->user_index = user_index;
        out_keystroke->hid_code = 0;

        bool is_pressed = curr_butts & fbutton;
        if (clear_pass && !is_pressed) {
          // up
          out_keystroke->flags = X_INPUT_KEYSTROKE_KEYUP;
          last.buttons &= ~fbutton;
          last.repeat_state = RepeatState::Idle;
          return X_ERROR_SUCCESS;
        }
        if (!clear_pass && is_pressed) {
          // down
          out_keystroke->flags = X_INPUT_KEYSTROKE_KEYDOWN;
          last.buttons |= fbutton;
          last.repeat_state = RepeatState::Waiting;
          last.repeat_butt_idx = i;
          last.repeat_time = guest_now;
          return X_ERROR_SUCCESS;
        }
      }
    }
  }
  return X_ERROR_EMPTY;
}

void SDLInputDriver::OnControllerDeviceAdded(const SDL_Event& event) {
  assert(window()->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  // Open the controller.
  const auto controller = SDL_GameControllerOpen(event.cdevice.which);
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

void SDLInputDriver::OnControllerDeviceRemoved(const SDL_Event& event) {
  assert(window()->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  // Find the disconnected gamecontroller and close it.
  auto idx = GetControllerIndexFromInstanceID(event.cdevice.which);
  assert(idx);
  SDL_GameControllerClose(controllers_.at(*idx).sdl);
  controllers_.at(*idx) = {};
  keystroke_states_.at(*idx) = {};
}

void SDLInputDriver::OnControllerDeviceAxisMotion(const SDL_Event& event) {
  assert(window()->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  auto idx = GetControllerIndexFromInstanceID(event.caxis.which);
  assert(idx);
  auto& pad = controllers_.at(*idx).state.gamepad;
  switch (event.caxis.axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
      pad.thumb_lx = event.caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_LEFTY:
      pad.thumb_ly = ~event.caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_RIGHTX:
      pad.thumb_rx = event.caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_RIGHTY:
      pad.thumb_ry = ~event.caxis.value;
      break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
      pad.left_trigger = static_cast<uint8_t>(event.caxis.value >> 7);
      break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
      pad.right_trigger = static_cast<uint8_t>(event.caxis.value >> 7);
      break;
    default:
      assert_always();
      break;
  }
  controllers_.at(*idx).state_changed = true;
}

void SDLInputDriver::OnControllerDeviceButtonChanged(const SDL_Event& event) {
  assert(window()->loop()->is_on_loop_thread());
  std::unique_lock<std::mutex> guard(controllers_mutex_);

  // Define a lookup table to map between SDL and XInput button codes.
  // These need to be in the order of the SDL_GameControllerButton enum.
  static const std::array<std::underlying_type<X_INPUT_GAMEPAD_BUTTON>::type,
                          SDL_CONTROLLER_BUTTON_MAX>
      xbutton_lookup = {X_INPUT_GAMEPAD_A,
                        X_INPUT_GAMEPAD_B,
                        X_INPUT_GAMEPAD_X,
                        X_INPUT_GAMEPAD_Y,
                        X_INPUT_GAMEPAD_BACK,
                        X_INPUT_GAMEPAD_GUIDE,
                        X_INPUT_GAMEPAD_START,
                        X_INPUT_GAMEPAD_LEFT_THUMB,
                        X_INPUT_GAMEPAD_RIGHT_THUMB,
                        X_INPUT_GAMEPAD_LEFT_SHOULDER,
                        X_INPUT_GAMEPAD_RIGHT_SHOULDER,
                        X_INPUT_GAMEPAD_DPAD_UP,
                        X_INPUT_GAMEPAD_DPAD_DOWN,
                        X_INPUT_GAMEPAD_DPAD_LEFT,
                        X_INPUT_GAMEPAD_DPAD_RIGHT};

  auto idx = GetControllerIndexFromInstanceID(event.cbutton.which);
  assert(idx);
  auto& controller = controllers_.at(*idx);

  uint16_t xbuttons = controller.state.gamepad.buttons;
  // Lookup the XInput button code.
  auto xbutton = xbutton_lookup.at(event.cbutton.button);
  // Pressed or released?
  if (event.cbutton.state == SDL_PRESSED) {
    if (xbutton == X_INPUT_GAMEPAD_GUIDE && !cvars::guide_button) {
      return;
    }
    xbuttons |= xbutton;
  } else {
    xbuttons &= ~xbutton;
  }
  controller.state.gamepad.buttons = xbuttons;
  controller.state_changed = true;
}

std::optional<size_t> SDLInputDriver::GetControllerIndexFromInstanceID(
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
      return i;
    }
  }
  return std::nullopt;
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

bool SDLInputDriver::TestSDLVersion() const {
#if SDL_VERSION_ATLEAST(2, 0, 9)
  // SDL 2.0.9 or newer is required for simple rumble support and player
  // index.
  const Uint8 min_patchlevel = 9;
#else
  // SDL 2.0.4 or newer is required to read game controller mappings from
  // file.
  const Uint8 min_patchlevel = 4;
#endif

  SDL_version ver = {};
  SDL_GetVersion(&ver);
  if ((ver.major < 2) ||
      (ver.major == 2 && ver.minor == 0 && ver.patch < min_patchlevel)) {
    return false;
  }
  return true;
}

void SDLInputDriver::QueueControllerUpdate() {
  // To minimize consecutive event pumps do not queue before previous pump is
  // finished.
  bool is_queued = false;
  sdl_pumpevents_queued_.compare_exchange_strong(is_queued, true);
  if (!is_queued) {
    window()->loop()->Post([this]() {
      SDL_PumpEvents();
      sdl_pumpevents_queued_ = false;
    });
  }
}

// Check if the analog inputs exceed their thresholds to become a button press
// and build the bitfield.
inline uint64_t SDLInputDriver::AnalogToKeyfield(
    const X_INPUT_GAMEPAD& gamepad) const {
  uint64_t f = 0;

  f |= static_cast<uint64_t>(gamepad.left_trigger > HID_SDL_TRIGG_THRES) << 16;
  f |= static_cast<uint64_t>(gamepad.right_trigger > HID_SDL_TRIGG_THRES) << 17;

  auto thumb_x = gamepad.thumb_lx;
  auto thumb_y = gamepad.thumb_ly;
  for (size_t i = 0; i <= 8; i = i + 8) {
    uint64_t u = thumb_y > HID_SDL_THUMB_THRES;
    uint64_t d = thumb_y < ~HID_SDL_THUMB_THRES;
    uint64_t r = thumb_x > HID_SDL_THUMB_THRES;
    uint64_t l = thumb_x < ~HID_SDL_THUMB_THRES;
    if (u && l) {
      u = l = 0;
      f |= uint64_t(1) << (22 + i);
    }
    if (u && r) {
      u = r = 0;
      f |= uint64_t(1) << (23 + i);
    }
    if (d && r) {
      d = r = 0;
      f |= uint64_t(1) << (24 + i);
    }
    if (d && l) {
      d = l = 0;
      f |= uint64_t(1) << (25 + i);
    }
    f |= u << (18 + i);
    f |= d << (19 + i);
    f |= r << (20 + i);
    f |= l << (21 + i);

    thumb_x = gamepad.thumb_rx;
    thumb_y = gamepad.thumb_ry;
  }
  return f;
}

}  // namespace sdl
}  // namespace hid
}  // namespace xe
