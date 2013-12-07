/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_NOP_NOP_AUDIO_DRIVER_H_
#define XENIA_APU_NOP_NOP_AUDIO_DRIVER_H_

#include <xenia/core.h>

#include <xenia/apu/audio_driver.h>
#include <xenia/apu/nop/nop_apu-private.h>


namespace xe {
namespace apu {
namespace nop {


class NopAudioDriver : public AudioDriver {
public:
  NopAudioDriver(Memory* memory);
  virtual ~NopAudioDriver();

  virtual void Initialize();

protected:
};


}  // namespace nop
}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_NOP_NOP_AUDIO_DRIVER_H_
