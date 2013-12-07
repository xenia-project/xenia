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


namespace xe {
namespace apu {


class AudioDriver {
public:
  virtual ~AudioDriver();

  Memory* memory() const { return memory_; }

  virtual void Initialize() = 0;

protected:
  AudioDriver(Memory* memory);

  Memory* memory_;
};


}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_AUDIO_DRIVER_H_
