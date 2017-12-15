/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xaudio2/xaudio2_audio_driver.h"

// Must be included before xaudio2.h so we get the right windows.h include.
#include "xenia/base/platform_win.h"

#include <xaudio2.h>  // NOLINT(build/include_order)

#include "xenia/apu/apu_flags.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioDriver::VoiceCallback : public IXAudio2VoiceCallback {
 public:
  explicit VoiceCallback(xe::threading::Semaphore* semaphore)
      : semaphore_(semaphore) {}
  ~VoiceCallback() {}

  void OnStreamEnd() {}
  void OnVoiceProcessingPassEnd() {}
  void OnVoiceProcessingPassStart(uint32_t samples_required) {}
  void OnBufferEnd(void* context) {
    auto ret = semaphore_->Release(1, nullptr);
    assert_true(ret);
  }
  void OnBufferStart(void* context) {}
  void OnLoopEnd(void* context) {}
  void OnVoiceError(void* context, HRESULT result) {}

 private:
  xe::threading::Semaphore* semaphore_ = nullptr;
};

XAudio2AudioDriver::XAudio2AudioDriver(Memory* memory,
                                       xe::threading::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {
  static_assert(frame_count_ == XAUDIO2_MAX_QUEUED_BUFFERS,
                "xaudio header differs");
}

XAudio2AudioDriver::~XAudio2AudioDriver() = default;

const DWORD ChannelMasks[] = {
    0,
    0,
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY,
    0,
    0,
    0,
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_CENTER | SPEAKER_FRONT_RIGHT |
        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
    0,
};

void XAudio2AudioDriver::Initialize() {
  HRESULT hr;

  voice_callback_ = new VoiceCallback(semaphore_);

  hr = XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    XELOGE("XAudio2Create failed with %.8X", hr);
    assert_always();
    return;
  }

  XAUDIO2_DEBUG_CONFIGURATION config;
  config.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
  config.BreakMask = 0;
  config.LogThreadID = FALSE;
  config.LogTiming = TRUE;
  config.LogFunctionName = TRUE;
  config.LogFileline = TRUE;
  audio_->SetDebugConfiguration(&config);

  hr = audio_->CreateMasteringVoice(&mastering_voice_);
  if (FAILED(hr)) {
    XELOGE("CreateMasteringVoice failed with %.8X", hr);
    assert_always();
    return;
  }

  WAVEFORMATIEEEFLOATEX waveformat;

  waveformat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  waveformat.Format.nChannels = frame_channels_;
  waveformat.Format.nSamplesPerSec = 48000;
  waveformat.Format.wBitsPerSample = 32;
  waveformat.Format.nBlockAlign =
      (waveformat.Format.nChannels * waveformat.Format.wBitsPerSample) / 8;
  waveformat.Format.nAvgBytesPerSec =
      waveformat.Format.nSamplesPerSec * waveformat.Format.nBlockAlign;
  waveformat.Format.cbSize =
      sizeof(WAVEFORMATIEEEFLOATEX) - sizeof(WAVEFORMATEX);

  waveformat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  waveformat.Samples.wValidBitsPerSample = waveformat.Format.wBitsPerSample;
  waveformat.dwChannelMask = ChannelMasks[waveformat.Format.nChannels];

  hr = audio_->CreateSourceVoice(
      &pcm_voice_, &waveformat.Format,
      0,  // XAUDIO2_VOICE_NOSRC | XAUDIO2_VOICE_NOPITCH,
      XAUDIO2_MAX_FREQ_RATIO, voice_callback_);
  if (FAILED(hr)) {
    XELOGE("CreateSourceVoice failed with %.8X", hr);
    assert_always();
    return;
  }

  hr = pcm_voice_->Start();
  if (FAILED(hr)) {
    XELOGE("Start failed with %.8X", hr);
    assert_always();
    return;
  }

  if (FLAGS_mute) {
    pcm_voice_->SetVolume(0.0f);
  }
}

void XAudio2AudioDriver::SubmitFrame(uint32_t frame_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  XAUDIO2_VOICE_STATE state;
  pcm_voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
  assert_true(state.BuffersQueued < frame_count_);

  auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  auto output_frame = reinterpret_cast<float*>(frames_[current_frame_]);
  auto interleave_channels = frame_channels_;

  // interleave the data
  for (uint32_t index = 0, o = 0; index < channel_samples_; ++index) {
    for (uint32_t channel = 0, table = 0; channel < interleave_channels;
         ++channel, table += channel_samples_) {
      output_frame[o++] = xe::byte_swap(input_frame[table + index]);
    }
  }

  XAUDIO2_BUFFER buffer;
  buffer.Flags = 0;
  buffer.pAudioData = reinterpret_cast<BYTE*>(output_frame);
  buffer.AudioBytes = frame_size_;
  buffer.PlayBegin = 0;
  buffer.PlayLength = channel_samples_;
  buffer.LoopBegin = XAUDIO2_NO_LOOP_REGION;
  buffer.LoopLength = 0;
  buffer.LoopCount = 0;
  buffer.pContext = 0;
  hr = pcm_voice_->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    XELOGE("SubmitSourceBuffer failed with %.8X", hr);
    assert_always();
    return;
  }

  current_frame_ = (current_frame_ + 1) % frame_count_;

  // Update playback ratio to our time scalar.
  // This will keep audio in sync with the game clock.
  pcm_voice_->SetFrequencyRatio(
      static_cast<float>(xe::Clock::guest_time_scalar()));
}

void XAudio2AudioDriver::Shutdown() {
  pcm_voice_->Stop();
  pcm_voice_->DestroyVoice();
  pcm_voice_ = NULL;

  mastering_voice_->DestroyVoice();
  mastering_voice_ = NULL;

  audio_->StopEngine();
  audio_->Release();

  delete voice_callback_;
}

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe
