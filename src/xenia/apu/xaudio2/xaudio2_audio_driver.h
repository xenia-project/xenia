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

#include <xaudio2.h>

#include "xenia/apu/audio_driver.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioDriver : public AudioDriver {
 public:
  XAudio2AudioDriver(Emulator* emulator, HANDLE semaphore);
  ~XAudio2AudioDriver() override;

  void Initialize();
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 private:
  IXAudio2* audio_;
  IXAudio2MasteringVoice* mastering_voice_;
  IXAudio2SourceVoice* pcm_voice_;
  HANDLE semaphore_;

  class VoiceCallback;
  VoiceCallback* voice_callback_;

  static const uint32_t frame_count_ = XAUDIO2_MAX_QUEUED_BUFFERS;
  static const uint32_t frame_channels_ = 6;
  static const uint32_t channel_samples_ = 256;
  static const uint32_t frame_samples_ = frame_channels_ * channel_samples_;
  static const uint32_t frame_size_ = sizeof(float) * frame_samples_;
  float frames_[frame_count_][frame_samples_];
  uint32_t current_frame_;
};

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_DRIVER_H_
