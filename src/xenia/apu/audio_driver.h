/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_AUDIO_DRIVER_H_
#define XENIA_APU_AUDIO_DRIVER_H_

#include <xenia/core.h>
#include <xenia/xbox.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, cpu, Processor);
XEDECLARECLASS2(xe, cpu, XenonThreadState);


namespace xe {
namespace apu {


class AudioDriver {
public:
  AudioDriver(Emulator* emulator);
  virtual ~AudioDriver();

  virtual void SubmitFrame(uint32_t samples_ptr) = 0;

protected:
  Emulator*         emulator_;
  Memory*           memory_;
  cpu::Processor*   processor_;
};


}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_AUDIO_DRIVER_H_
