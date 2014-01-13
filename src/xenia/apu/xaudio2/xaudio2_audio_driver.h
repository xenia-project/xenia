/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_DRIVER_H_
#define XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_DRIVER_H_

#include <xenia/core.h>

#include <xenia/apu/audio_driver.h>
#include <xenia/apu/xaudio2/xaudio2_apu-private.h>


namespace xe {
namespace apu {
namespace xaudio2 {


class XAudio2AudioDriver : public AudioDriver {
public:
  XAudio2AudioDriver(Memory* memory);
  virtual ~XAudio2AudioDriver();

  virtual void Initialize();

protected:
};


}  // namespace xaudio2
}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_DRIVER_H_
