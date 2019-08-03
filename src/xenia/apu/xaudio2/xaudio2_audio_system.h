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

#include "xenia/apu/audio_system.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioSystem : public AudioSystem {
 public:
  explicit XAudio2AudioSystem(cpu::Processor* processor);
  ~XAudio2AudioSystem() override;

  static bool IsAvailable() { return true; }

  static std::unique_ptr<AudioSystem> Create(cpu::Processor* processor);

  X_RESULT CreateDriver(size_t index, xe::threading::Semaphore* semaphore,
                        AudioDriver** out_driver) override;
  void DestroyDriver(AudioDriver* driver) override;

 protected:
  void Initialize() override;
};

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_SYSTEM_H_
