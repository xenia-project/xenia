/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_XINPUT_XINPUT_INPUT_DRIVER_H_
#define XENIA_HID_XINPUT_XINPUT_INPUT_DRIVER_H_

#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {
namespace xinput {

class XInputInputDriver final : public InputDriver {
 public:
  explicit XInputInputDriver(xe::ui::Window* window, size_t window_z_order);
  ~XInputInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;
  virtual InputType GetInputType() const override;

 private:
  void* module_;
  void* XInputGetCapabilities_;
  void* XInputGetState_;
  void* XInputGetStateEx_;
  void* XInputGetKeystroke_;
  void* XInputSetState_;
  void* XInputEnable_;
};

}  // namespace xinput
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_XINPUT_XINPUT_INPUT_DRIVER_H_
