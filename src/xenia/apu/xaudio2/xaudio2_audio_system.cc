/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/xaudio2/xaudio2_audio_system.h>

#include <xenia/apu/apu-private.h>

#include <xenia/emulator.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::xaudio2;


class XAudio2AudioSystem::VoiceCallback : public IXAudio2VoiceCallback {
public:
  VoiceCallback(HANDLE wait_handle) : wait_handle_(wait_handle) {}
  ~VoiceCallback() {}

  void OnStreamEnd() {}
  void OnVoiceProcessingPassEnd() {}
  void OnVoiceProcessingPassStart(UINT32 SamplesRequired) {}
  void OnBufferEnd(void * pBufferContext) {
    SetEvent(wait_handle_);
  }
  void OnBufferStart(void * pBufferContext) {}
  void OnLoopEnd(void * pBufferContext) {}
  void OnVoiceError(void * pBufferContext, HRESULT Error) {}

private:
  HANDLE wait_handle_;
};


XAudio2AudioSystem::XAudio2AudioSystem(Emulator* emulator) :
    audio_(0), mastering_voice_(0), pcm_voice_(0),
    active_channels_(0),
    wait_handle_(NULL), voice_callback_(0),
    AudioSystem(emulator) {
}

XAudio2AudioSystem::~XAudio2AudioSystem() {
}

const DWORD ChannelMasks[] =
{
  0, // TODO: fixme
  0, // TODO: fixme
  SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY,
  0, // TODO: fixme
  0, // TODO: fixme
  0, // TODO: fixme
  SPEAKER_FRONT_LEFT | SPEAKER_FRONT_CENTER | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
  0, // TODO: fixme
};

void XAudio2AudioSystem::Initialize() {
  AudioSystem::Initialize();

  HRESULT hr;

  wait_handle_ = CreateEvent(NULL, TRUE, TRUE, NULL);

  voice_callback_ = new VoiceCallback(wait_handle_);

  hr = XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    XELOGE("XAudio2Create failed with %.8X", hr);
    XEASSERTALWAYS();
    return;
  }

  active_channels_ = 6;

  XAUDIO2_DEBUG_CONFIGURATION config;
  config.TraceMask        = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_INFO | XAUDIO2_LOG_DETAIL | XAUDIO2_LOG_STREAMING;
  config.BreakMask        = 0;
  config.LogThreadID      = FALSE;
  config.LogTiming        = TRUE;
  config.LogFunctionName  = TRUE;
  config.LogFileline      = TRUE;
  audio_->SetDebugConfiguration(&config);

<<<<<<< HEAD
  hr = audio_->CreateMasteringVoice(&mastering_voice_, active_channels_, 48000);
=======
  hr = audio_->CreateMasteringVoice(&mastering_voice_, 6, 48000);
>>>>>>> 6f09c12bc280b70e852867ba182dde6fef652f54
  if (FAILED(hr)) {
    XELOGE("CreateMasteringVoice failed with %.8X", hr);
    XEASSERTALWAYS();
    return;
  }

  WAVEFORMATIEEEFLOATEX waveformat;

  waveformat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  waveformat.Format.nChannels = active_channels_;
  waveformat.Format.nSamplesPerSec = 48000;
  waveformat.Format.wBitsPerSample = 32;
  waveformat.Format.nBlockAlign = (waveformat.Format.nChannels * waveformat.Format.wBitsPerSample) / 8;
  waveformat.Format.nAvgBytesPerSec = waveformat.Format.nSamplesPerSec * waveformat.Format.nBlockAlign;
  waveformat.Format.cbSize = sizeof(waveformat)-sizeof(WAVEFORMATEX);

  waveformat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  waveformat.Samples.wValidBitsPerSample = waveformat.Format.wBitsPerSample;
  waveformat.dwChannelMask = ChannelMasks[waveformat.Format.nChannels];

  hr = audio_->CreateSourceVoice(&pcm_voice_, &waveformat.Format,
                                 XAUDIO2_VOICE_NOPITCH | XAUDIO2_VOICE_NOSRC,
                                 XAUDIO2_DEFAULT_FREQ_RATIO, voice_callback_);
  if (FAILED(hr)) {
    XELOGE("CreateSourceVoice failed with %.8X", hr);
    XEASSERTALWAYS();
    return;
  }

  pcm_voice_->Start();
}

void XAudio2AudioSystem::Pump() {
  XAUDIO2_VOICE_STATE state;
  pcm_voice_->GetState(&state);
  auto n = state.BuffersQueued;
  if (n > 1) {
    // A lot of buffers are queued up, and until we use them block.
    ResetEvent(wait_handle_);
  }

  WaitForSingleObject(wait_handle_, INFINITE);
}

void XAudio2AudioSystem::SubmitFrame(uint32_t samples_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  auto samples = reinterpret_cast<float*>(
      emulator_->memory()->membase() + samples_ptr);
  
  // interleave the data
  for (int i = 0, o = 0; i < 256; ++i) {
    for (int j = 0; j < 6 && j < active_channels_; ++j) {
      samples_[o++] = XESWAPF32BE(*(samples + (j * 256) + i));
    }
  }

  XAUDIO2_BUFFER buffer;
  buffer.Flags = XAUDIO2_END_OF_STREAM;
  buffer.pAudioData = reinterpret_cast<BYTE*>(samples_);
  buffer.AudioBytes = sizeof(samples_);
  buffer.PlayBegin = 0;
  buffer.PlayLength = 256;
  buffer.LoopBegin = 0;
  buffer.LoopLength = 0;
  buffer.LoopCount = 0;
  buffer.pContext = 0;
  hr = pcm_voice_->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    XELOGE("SubmitSourceBuffer failed with %.8X", hr);
    XEASSERTALWAYS();
    return;
  }
  hr = pcm_voice_->Start(0);
  if (FAILED(hr)) {
    XELOGE("Start failed with %.8X", hr);
    XEASSERTALWAYS();
    return;
  }
}

void XAudio2AudioSystem::Shutdown() {
  pcm_voice_->Stop();
  pcm_voice_->DestroyVoice();
  pcm_voice_ = NULL;

  mastering_voice_->DestroyVoice();
  mastering_voice_ = NULL;

  audio_->StopEngine();
  XESAFERELEASE(audio_);

  delete voice_callback_;
  CloseHandle(wait_handle_);

  AudioSystem::Shutdown();
}
