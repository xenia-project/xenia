/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xaudio2/xaudio2_audio_driver.h"

// Must be included before xaudio2.h so we get the right windows.h include.
#include "xenia/base/platform_win.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/conversion.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioDriver::VoiceCallback : public api::IXAudio2VoiceCallback {
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
    : AudioDriver(memory), semaphore_(semaphore) {}

XAudio2AudioDriver::~XAudio2AudioDriver() = default;

bool XAudio2AudioDriver::Initialize() {
  HRESULT hr;

  voice_callback_ = new VoiceCallback(semaphore_);

  // Load the XAudio2 DLL dynamically. Needed both for 2.7 and for
  // differentiating between 2.8 and later versions. Windows 8.1 SDK references
  // XAudio2_8.dll in xaudio2.lib, which is available on Windows 8.1, however,
  // Windows 10 SDK references XAudio2_9.dll in it, which is only available in
  // Windows 10, and XAudio2_8.dll is linked through a different .lib -
  // xaudio2_8.lib, so easier not to link the .lib at all.
  xaudio2_module_ = reinterpret_cast<void*>(LoadLibraryW(L"XAudio2_8.dll"));
  if (xaudio2_module_) {
    api_minor_version_ = 8;
  } else {
    xaudio2_module_ = reinterpret_cast<void*>(LoadLibraryW(L"XAudio2_7.dll"));
    if (xaudio2_module_) {
      api_minor_version_ = 7;
    } else {
      XELOGE("Failed to load XAudio 2.8 or 2.7 library DLL");
      assert_always();
      return false;
    }
  }

  // First CPU (2.8 default). XAUDIO2_ANY_PROCESSOR (2.7 default) steals too
  // much time from other things. Ideally should process audio on what roughly
  // represents thread 4 (5th) on the Xbox 360 (2.7 default on the console), or
  // even beyond the 6 guest cores.
  api::XAUDIO2_PROCESSOR processor = 0x00000001;
  if (api_minor_version_ >= 8) {
    union {
      // clang-format off
      HRESULT (__stdcall* xaudio2_create)(
          api::IXAudio2_8** xaudio2_out, UINT32 flags,
          api::XAUDIO2_PROCESSOR xaudio2_processor);
      // clang-format on
      FARPROC xaudio2_create_ptr;
    };
    xaudio2_create_ptr = GetProcAddress(
        reinterpret_cast<HMODULE>(xaudio2_module_), "XAudio2Create");
    if (!xaudio2_create_ptr) {
      XELOGE("XAudio2Create not found in XAudio2_8.dll");
      assert_always();
      return false;
    }
    hr = xaudio2_create(&objects_.api_2_8.audio, 0, processor);
    if (FAILED(hr)) {
      XELOGE("XAudio2Create failed with {:08X}", hr);
      assert_always();
      return false;
    }
    return InitializeObjects(objects_.api_2_8);
  } else {
    hr = CoCreateInstance(__uuidof(api::XAudio2_7), NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&objects_.api_2_7.audio));
    if (FAILED(hr)) {
      XELOGE("CoCreateInstance for XAudio2 failed with {:08X}", hr);
      assert_always();
      return false;
    }
    hr = objects_.api_2_7.audio->Initialize(0, processor);
    if (FAILED(hr)) {
      XELOGE("IXAudio2::Initialize failed with {:08X}", hr);
      assert_always();
      return false;
    }
    return InitializeObjects(objects_.api_2_7);
  }
}

template <typename Objects>
bool XAudio2AudioDriver::InitializeObjects(Objects& objects) {
  HRESULT hr;

  api::XAUDIO2_DEBUG_CONFIGURATION config;
  config.TraceMask = api::XE_XAUDIO2_LOG_ERRORS | api::XE_XAUDIO2_LOG_WARNINGS;
  config.BreakMask = 0;
  config.LogThreadID = FALSE;
  config.LogTiming = TRUE;
  config.LogFunctionName = TRUE;
  config.LogFileline = TRUE;
  objects.audio->SetDebugConfiguration(&config);

  hr = objects.audio->CreateMasteringVoice(&objects.mastering_voice);
  if (FAILED(hr)) {
    XELOGE("IXAudio2::CreateMasteringVoice failed with {:08X}", hr);
    assert_always();
    return false;
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
  static const DWORD kChannelMasks[] = {
      0,
      0,
      SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT,
      0,
      0,
      0,
      SPEAKER_FRONT_LEFT | SPEAKER_FRONT_CENTER | SPEAKER_FRONT_RIGHT |
          SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
      0,
  };
  waveformat.dwChannelMask = kChannelMasks[waveformat.Format.nChannels];

  hr = objects.audio->CreateSourceVoice(
      &objects.pcm_voice, &waveformat.Format,
      0,  // api::XE_XAUDIO2_VOICE_NOSRC | api::XE_XAUDIO2_VOICE_NOPITCH,
      api::XE_XAUDIO2_MAX_FREQ_RATIO, voice_callback_);
  if (FAILED(hr)) {
    XELOGE("IXAudio2::CreateSourceVoice failed with {:08X}", hr);
    assert_always();
    return false;
  }

  hr = objects.pcm_voice->Start();
  if (FAILED(hr)) {
    XELOGE("IXAudio2SourceVoice::Start failed with {:08X}", hr);
    assert_always();
    return false;
  }

  if (cvars::mute) {
    objects.pcm_voice->SetVolume(0.0f);
  }

  return true;
}

void XAudio2AudioDriver::SubmitFrame(uint32_t frame_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  api::XAUDIO2_VOICE_STATE state;
  if (api_minor_version_ >= 8) {
    objects_.api_2_8.pcm_voice->GetState(&state,
                                         api::XE_XAUDIO2_VOICE_NOSAMPLESPLAYED);
  } else {
    objects_.api_2_7.pcm_voice->GetState(&state);
  }
  assert_true(state.BuffersQueued < frame_count_);

  auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  auto output_frame = reinterpret_cast<float*>(frames_[current_frame_]);
  auto interleave_channels = frame_channels_;

  // interleave the data
  conversion::sequential_6_BE_to_interleaved_6_LE(output_frame, input_frame,
                                                  channel_samples_);

  api::XAUDIO2_BUFFER buffer;
  buffer.Flags = 0;
  buffer.pAudioData = reinterpret_cast<BYTE*>(output_frame);
  buffer.AudioBytes = frame_size_;
  buffer.PlayBegin = 0;
  buffer.PlayLength = channel_samples_;
  buffer.LoopBegin = api::XE_XAUDIO2_NO_LOOP_REGION;
  buffer.LoopLength = 0;
  buffer.LoopCount = 0;
  buffer.pContext = 0;
  if (api_minor_version_ >= 8) {
    hr = objects_.api_2_8.pcm_voice->SubmitSourceBuffer(&buffer);
  } else {
    hr = objects_.api_2_7.pcm_voice->SubmitSourceBuffer(&buffer);
  }
  if (FAILED(hr)) {
    XELOGE("SubmitSourceBuffer failed with {:08X}", hr);
    assert_always();
    return;
  }

  current_frame_ = (current_frame_ + 1) % frame_count_;

  // Update playback ratio to our time scalar.
  // This will keep audio in sync with the game clock.
  float frequency_ratio = static_cast<float>(xe::Clock::guest_time_scalar());
  if (api_minor_version_ >= 8) {
    objects_.api_2_8.pcm_voice->SetFrequencyRatio(frequency_ratio);
  } else {
    objects_.api_2_7.pcm_voice->SetFrequencyRatio(frequency_ratio);
  }
}

void XAudio2AudioDriver::Shutdown() {
  if (api_minor_version_ >= 8) {
    ShutdownObjects(objects_.api_2_8);
  } else {
    ShutdownObjects(objects_.api_2_7);
  }

  if (xaudio2_module_) {
    FreeLibrary(reinterpret_cast<HMODULE>(xaudio2_module_));
    xaudio2_module_ = nullptr;
  }

  if (voice_callback_) {
    delete voice_callback_;
    voice_callback_ = nullptr;
  }
}

template <typename Objects>
void XAudio2AudioDriver::ShutdownObjects(Objects& objects) {
  if (objects.pcm_voice) {
    objects.pcm_voice->Stop();
    objects.pcm_voice->DestroyVoice();
    objects.pcm_voice = nullptr;
  }

  if (objects.mastering_voice) {
    objects.mastering_voice->DestroyVoice();
    objects.mastering_voice = nullptr;
  }

  if (objects.audio) {
    objects.audio->StopEngine();
    objects.audio->Release();
    objects.audio = nullptr;
  }
}

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe
