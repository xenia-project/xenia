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

#include <xenia/core.h>

#include <xenia/apu/audio_system.h>
#include <xenia/apu/xaudio2/xaudio2_apu-private.h>

#include <xaudio2.h>


namespace xe {
namespace apu {
namespace xaudio2 {


class XAudio2AudioSystem : public AudioSystem {
public:
  XAudio2AudioSystem(Emulator* emulator);
  virtual ~XAudio2AudioSystem();

  virtual void Shutdown();

  virtual void SubmitFrame(uint32_t samples_ptr);

protected:
  virtual void Initialize();
  virtual void Pump();

private:
  IXAudio2* audio_;
  IXAudio2MasteringVoice* mastering_voice_;
  IXAudio2SourceVoice* pcm_voice_;
  float samples_[1536];
};


}  // namespace xaudio2
}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_SYSTEM_H_
