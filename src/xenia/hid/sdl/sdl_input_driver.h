/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_SDL_SDL_INPUT_DRIVER_H_
#define XENIA_HID_SDL_SDL_INPUT_DRIVER_H_

#include <SDL2/SDL.h>

#include <array>
#include <atomic>
#include <mutex>

#include "xenia/hid/input_driver.h"

#define HID_SDL_USER_COUNT (4)

namespace xe {
namespace hid {
namespace sdl {

class SDLInputDriver : public InputDriver {
 public:
  explicit SDLInputDriver(xe::ui::Window* window);
  ~SDLInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

 protected:
  struct ControllerState {
    SDL_GameController* sdl;
    bool state_changed;
    X_INPUT_STATE state;
  };

 protected:
  void OnControllerDeviceAdded(SDL_Event* event);
  void OnControllerDeviceRemoved(SDL_Event* event);
  void OnControllerDeviceAxisMotion(SDL_Event* event);
  void OnControllerDeviceButtonChanged(SDL_Event* event);
  std::pair<bool, size_t> GetControllerIndexFromInstanceID(
      SDL_JoystickID instance_id);
  ControllerState* GetControllerState(uint32_t user_index);
  bool TestSDLVersion();
  void QueueControllerUpdate();

 protected:
  bool sdl_events_initialized_;
  bool sdl_gamecontroller_initialized_;
  int sdl_events_unflushed_;
  std::atomic<bool> sdl_pumpevents_queued_;
  std::array<ControllerState, HID_SDL_USER_COUNT> controllers_;
  std::mutex controllers_mutex_;
};

}  // namespace sdl
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_SDL_SDL_INPUT_DRIVER_H_
