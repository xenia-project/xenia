/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/xaudio2/xaudio2_audio_driver.h>

#include <xenia/apu/apu-private.h>

#include <xenia/emulator.h>

using namespace xe;
using namespace xe::apu;
using namespace xe::apu::xaudio2;


class XAudio2AudioDriver::VoiceCallback : public IXAudio2VoiceCallback {
public:
  VoiceCallback(HANDLE wait_handle) : wait_handle_(wait_handle) {}
  ~VoiceCallback() {}

  void OnStreamEnd() {}
  void OnVoiceProcessingPassEnd() {}
  void OnVoiceProcessingPassStart(uint32_t samples_required) {}
  void OnBufferEnd(void* context);
  void OnBufferStart(void* context) {}
  void OnLoopEnd(void* context) {}
  void OnVoiceError(void* context, HRESULT result) {}

private:
  HANDLE wait_handle_;
};

void XAudio2AudioDriver::VoiceCallback::OnBufferEnd(void* context) {
  SetEvent(wait_handle_);
}

XAudio2AudioDriver::XAudio2AudioDriver(Emulator* emulator, HANDLE wait) :
    audio_(0), mastering_voice_(0), pcm_voice_(0),
    wait_handle_(wait), voice_callback_(0),
    AudioDriver(emulator) {
}

XAudio2AudioDriver::~XAudio2AudioDriver() {
}

const DWORD ChannelMasks[] = {
  0, // TODO: fixme
  0, // TODO: fixme
  SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY,
  0, // TODO: fixme
  0, // TODO: fixme
  0, // TODO: fixme
  SPEAKER_FRONT_LEFT | SPEAKER_FRONT_CENTER | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
  0, // TODO: fixme
};

void XAudio2AudioDriver::Initialize() {
  HRESULT hr;

  voice_callback_ = new VoiceCallback(wait_handle_);

  hr = XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    XELOGE("XAudio2Create failed with %.8X", hr);
    assert_always();
    return;
  }

  XAUDIO2_DEBUG_CONFIGURATION config;
  config.TraceMask        = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
  config.BreakMask        = 0;
  config.LogThreadID      = FALSE;
  config.LogTiming        = TRUE;
  config.LogFunctionName  = TRUE;
  config.LogFileline      = TRUE;
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
  waveformat.Format.nBlockAlign = (waveformat.Format.nChannels * waveformat.Format.wBitsPerSample) / 8;
  waveformat.Format.nAvgBytesPerSec = waveformat.Format.nSamplesPerSec * waveformat.Format.nBlockAlign;
  waveformat.Format.cbSize = sizeof(WAVEFORMATIEEEFLOATEX) - sizeof(WAVEFORMATEX);

  waveformat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  waveformat.Samples.wValidBitsPerSample = waveformat.Format.wBitsPerSample;
  waveformat.dwChannelMask = ChannelMasks[waveformat.Format.nChannels];

  hr = audio_->CreateSourceVoice(&pcm_voice_, &waveformat.Format,
                                 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                 voice_callback_);
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

  SetEvent(wait_handle_);
}

void XAudio2AudioDriver::SubmitFrame(uint32_t frame_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  auto input_frame = reinterpret_cast<float*>(emulator_->memory()->membase() + frame_ptr);
  auto output_frame = reinterpret_cast<float*>(frame_);
  auto interleave_channels = frame_channels_;

  // interleave the data
  for (int index = 0, o = 0; index < channel_samples_; ++index) {
    for (int channel = 0, table = 0; channel < interleave_channels; ++channel, table += channel_samples_) {
      output_frame[o++] = poly::byte_swap(input_frame[table + index]);
    }
  }

  XAUDIO2_BUFFER buffer;
  buffer.Flags = 0;
  buffer.pAudioData = (BYTE*)frame_;
  buffer.AudioBytes = sizeof(frame_);
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
}

void XAudio2AudioDriver::Shutdown() {
  pcm_voice_->Stop();
  pcm_voice_->DestroyVoice();
  pcm_voice_ = NULL;

  mastering_voice_->DestroyVoice();
  mastering_voice_ = NULL;

  audio_->StopEngine();
  XESAFERELEASE(audio_);

  delete voice_callback_;
  CloseHandle(wait_handle_);
}
