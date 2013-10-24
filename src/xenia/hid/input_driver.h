/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_INPUT_DRIVER_H_
#define XENIA_HID_INPUT_DRIVER_H_

#include <xenia/core.h>
#include <xenia/xbox.h>


namespace xe {
namespace hid {

class InputSystem;


class InputDriver {
public:
  virtual ~InputDriver();

  virtual X_STATUS Setup() = 0;

protected:
  InputDriver(InputSystem* input_system);

  InputSystem* input_system_;
};


}  // namespace hid
}  // namespace xe


#endif  // XENIA_HID_INPUT_DRIVER_H_
