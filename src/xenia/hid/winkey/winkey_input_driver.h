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

#include <queue>

#include "xenia/base/mutex.h"
#include "xenia/hid/input_driver.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace hid {
namespace winkey {

class WinKeyInputDriver : public InputDriver {
 public:
  explicit WinKeyInputDriver(xe::ui::Window* window);
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
    ui::VirtualKey virtual_key = ui::VirtualKey::kNone;
    int repeat_count = 0;
    bool transition = false;  // going up(false) or going down(true)
    bool prev_state = false;  // down(true) or up(false)
  };

  xe::global_critical_region global_critical_region_;
  std::queue<KeyEvent> key_events_;

  uint32_t packet_number_;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_
