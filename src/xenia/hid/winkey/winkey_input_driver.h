/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_
#define XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_

#include <mutex>
#include <queue>

#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {
namespace winkey {

class WinKeyInputDriver : public InputDriver {
 public:
  explicit WinKeyInputDriver(InputSystem* input_system);
  ~WinKeyInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

 protected:
  struct KeyEvent {
    int vkey = 0;
    int repeat_count = 0;
    bool transition = false;  // going up(false) or going down(true)
    bool prev_state = false;  // down(true) or up(false)
  };
  std::queue<KeyEvent> key_events_;
  std::mutex key_event_mutex_;

  uint32_t packet_number_;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_
