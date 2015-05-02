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

#include "xenia/emulator.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

class AudioDriver {
 public:
  AudioDriver(Emulator* emulator);
  virtual ~AudioDriver();

  virtual void SubmitFrame(uint32_t samples_ptr) = 0;

 protected:
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return memory_->TranslatePhysical(guest_address);
  }

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_AUDIO_DRIVER_H_
