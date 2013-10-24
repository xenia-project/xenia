/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_XINPUT_XINPUT_DRIVER_H_
#define XENIA_HID_XINPUT_XINPUT_DRIVER_H_

#include <xenia/core.h>

#include <xenia/hid/input_driver.h>
#include <xenia/hid/nop/nop_hid-private.h>


namespace xe {
namespace hid {
namespace xinput {


class XInputInputDriver : public InputDriver {
public:
  XInputInputDriver(InputSystem* input_system);
  virtual ~XInputInputDriver();

  virtual X_STATUS Setup();

protected:
};


}  // namespace xinput
}  // namespace hid
}  // namespace xe


#endif  // XENIA_HID_XINPUT_XINPUT_DRIVER_H_
