/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_SYSTEM_H_
#define XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_SYSTEM_H_

#include <xaudio2.h>

#include "xenia/apu/audio_system.h"
#include "xenia/apu/xaudio2/xaudio2_apu-private.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioSystem : public AudioSystem {
 public:
  XAudio2AudioSystem(Emulator* emulator);
  virtual ~XAudio2AudioSystem();

  virtual X_RESULT CreateDriver(size_t index, HANDLE wait,
                                AudioDriver** out_driver);
  virtual void DestroyDriver(AudioDriver* driver);

 protected:
  virtual void Initialize();
};

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_SYSTEM_H_
