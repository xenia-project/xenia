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

#include <xaudio2.h>


namespace xe {
namespace apu {
namespace xaudio2 {


class XAudio2AudioDriver : public AudioDriver {
public:
  XAudio2AudioDriver(Emulator* emulator, HANDLE wait);
  virtual ~XAudio2AudioDriver();

  virtual void Initialize();
  virtual void SubmitFrame(uint32_t frame_ptr);
  virtual void Shutdown();

private:
  IXAudio2* audio_;
  IXAudio2MasteringVoice* mastering_voice_;
  IXAudio2SourceVoice* pcm_voice_;
  static const int frame_channels_ = 6;
  static const int channel_samples_ = 256;
  float frame_[frame_channels_ * channel_samples_];
  HANDLE wait_handle_;

  class VoiceCallback;
  VoiceCallback* voice_callback_;
};


}  // namespace xaudio2
}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_DRIVER_H_
