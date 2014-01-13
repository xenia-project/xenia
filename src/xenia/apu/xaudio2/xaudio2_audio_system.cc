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
#include <xenia/apu/xaudio2/xaudio2_audio_driver.h>

#include <xenia/emulator.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::xaudio2;


XAudio2AudioSystem::XAudio2AudioSystem(Emulator* emulator) :
    audio_(0), mastering_voice_(0), pcm_voice_(0),
    AudioSystem(emulator) {
}

XAudio2AudioSystem::~XAudio2AudioSystem() {
}

void XAudio2AudioSystem::Initialize() {
  AudioSystem::Initialize();

  HRESULT hr;

  hr = XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    XELOGE("XAudio2Create failed with %.8X", hr);
    exit(1);
    return;
  }

  hr = audio_->CreateMasteringVoice(&mastering_voice_);
  if (FAILED(hr)) {
    XELOGE("CreateMasteringVoice failed with %.8X", hr);
    exit(1);
  }

  WAVEFORMATEX waveformat;
  waveformat.wFormatTag = WAVE_FORMAT_PCM;
  waveformat.nChannels = 1;
  waveformat.nSamplesPerSec = 44100;
  waveformat.nAvgBytesPerSec = 44100 * 2;
  waveformat.nBlockAlign = 2;
  waveformat.wBitsPerSample = 16;
  waveformat.cbSize = 0;
  hr = audio_->CreateSourceVoice(&pcm_voice_, &waveformat);
  if (FAILED(hr)) {
    XELOGE("CreateSourceVoice failed with %.8X", hr);
    exit(1);
  }

  pcm_voice_->Start();

  XEASSERTNULL(driver_);
  driver_ = new XAudio2AudioDriver(memory_);
}

void XAudio2AudioSystem::Pump() {
  //
}

void XAudio2AudioSystem::SubmitFrame(uint32_t samples_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  auto samples = reinterpret_cast<float*>(emulator_->memory()->membase() + samples_ptr);
  for (int i = 0; i < _countof(samples_); ++i)
  {
	  samples_[i] = XESWAPF32BE(*samples++);
  }

  // this is dumb and not right.
  XAUDIO2_BUFFER buffer;
  buffer.Flags = 0;
  buffer.AudioBytes = sizeof(samples_);
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
  AudioSystem::Shutdown();
}
