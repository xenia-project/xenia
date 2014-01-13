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
    wait_handle_(NULL), voice_callback_(0),
    AudioSystem(emulator) {
}

XAudio2AudioSystem::~XAudio2AudioSystem() {
}

void XAudio2AudioSystem::Initialize() {
  AudioSystem::Initialize();

  HRESULT hr;

  wait_handle_ = CreateEvent(NULL, TRUE, TRUE, NULL);

  voice_callback_ = new VoiceCallback(wait_handle_);

  hr = XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    XELOGE("XAudio2Create failed with %.8X", hr);
    exit(1);
    return;
  }

  XAUDIO2_DEBUG_CONFIGURATION config;
  config.TraceMask        = XAUDIO2_LOG_ERRORS;
  config.BreakMask        = 0;
  config.LogThreadID      = FALSE;
  config.LogTiming        = TRUE;
  config.LogFunctionName  = TRUE;
  config.LogFileline      = TRUE;
  audio_->SetDebugConfiguration(&config);

  hr = audio_->CreateMasteringVoice(&mastering_voice_);
  if (FAILED(hr)) {
    XELOGE("CreateMasteringVoice failed with %.8X", hr);
    exit(1);
  }

  WAVEFORMATIEEEFLOATEX waveformat;
  waveformat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  waveformat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  waveformat.Format.nChannels = 6;
  waveformat.Format.nSamplesPerSec = 44100;
  waveformat.Format.wBitsPerSample = 32;
  waveformat.Format.nBlockAlign = (waveformat.Format.nChannels * waveformat.Format.wBitsPerSample) / 8; // 4
  waveformat.Format.nAvgBytesPerSec = waveformat.Format.nSamplesPerSec * waveformat.Format.nBlockAlign; // 44100 * 4
  waveformat.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX);
  waveformat.Samples.wValidBitsPerSample = waveformat.Format.wBitsPerSample;
  waveformat.dwChannelMask =
	  SPEAKER_FRONT_LEFT | SPEAKER_FRONT_CENTER | SPEAKER_FRONT_RIGHT |
	  SPEAKER_LOW_FREQUENCY |
	  SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
  hr = audio_->CreateSourceVoice(
      &pcm_voice_, &waveformat, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
      voice_callback_);
  if (FAILED(hr)) {
    XELOGE("CreateSourceVoice failed with %.8X", hr);
    exit(1);
  }

  //

  pcm_voice_->Start();
}

void XAudio2AudioSystem::Pump() {
  XAUDIO2_VOICE_STATE state;
  pcm_voice_->GetState(&state);
  auto n = state.BuffersQueued;
  if (n > 30) {
    // A lot of buffers are queued up, and until we use them block.
    ResetEvent(wait_handle_);
  }

  WaitForSingleObject(wait_handle_, INFINITE);
}

void XAudio2AudioSystem::SubmitFrame(uint32_t samples_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  int sample_count = 6 * 256;
  auto samples = reinterpret_cast<float*>(
      emulator_->memory()->membase() + samples_ptr);
  for (int i = 0; i < sample_count; ++i) {
	  samples_[i] = XESWAPF32BE(*(samples + i));
  }

  // this is dumb and not right.
  XAUDIO2_BUFFER buffer;
  buffer.Flags = 0;
  buffer.AudioBytes = sample_count * sizeof(float);
  buffer.pAudioData = reinterpret_cast<BYTE*>(samples_);
  buffer.PlayBegin = 0;
  buffer.PlayLength = 0;
  buffer.LoopBegin = 0;
  buffer.LoopLength = 0;
  buffer.LoopCount = 0;
  buffer.pContext = 0;
  hr = pcm_voice_->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    XELOGE("SubmitSourceBuffer failed with %.8X", hr);
    exit(1);
  }
  hr = pcm_voice_->Start(0);
  if (FAILED(hr)) {
    XELOGE("Start failed with %.8X", hr);
    exit(1);
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
