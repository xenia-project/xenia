/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_INPUT_SYSTEM_H_
#define XENIA_HID_INPUT_SYSTEM_H_

#include <xenia/core.h>
#include <xenia/xbox.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, cpu, Processor);


namespace xe {
namespace hid {

class InputDriver;


class InputSystem {
public:
  InputSystem(Emulator* emulator);
  ~InputSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  X_STATUS Setup();

  void AddDriver(InputDriver* driver);

  X_RESULT GetCapabilities(
      uint32_t user_index, uint32_t flags, X_INPUT_CAPABILITIES& out_caps);
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE& out_state);
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION& vibration);

private:
  Emulator*         emulator_;
  Memory*           memory_;
  cpu::Processor*   processor_;

  std::vector<InputDriver*> drivers_;
};


}  // namespace hid
}  // namespace xe


#endif  // XENIA_HID_INPUT_SYSTEM_H_
