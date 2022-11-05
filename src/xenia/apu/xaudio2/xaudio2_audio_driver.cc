/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xaudio2/xaudio2_audio_driver.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/conversion.h"
#include "xenia/apu/xaudio2/xaudio2_api.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/threading.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioDriver::VoiceCallback : public api::IXAudio2VoiceCallback {
 public:
  explicit VoiceCallback(xe::threading::Semaphore* semaphore)
      : semaphore_(semaphore) {}
  ~VoiceCallback() {}

  void OnStreamEnd() noexcept {}
  void OnVoiceProcessingPassEnd() noexcept {}
  void OnVoiceProcessingPassStart(uint32_t samples_required) noexcept {}
  void OnBufferEnd(void* context) noexcept {
    auto ret = semaphore_->Release(1, nullptr);
    assert_true(ret);
  }
  void OnBufferStart(void* context) noexcept {}
  void OnLoopEnd(void* context) noexcept {}
  void OnVoiceError(void* context, HRESULT result) noexcept {}

 private:
  xe::threading::Semaphore* semaphore_ = nullptr;
};

XAudio2AudioDriver::XAudio2AudioDriver(Memory* memory,
                                       xe::threading::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {}

XAudio2AudioDriver::~XAudio2AudioDriver() = default;

bool XAudio2AudioDriver::Initialize() {
  voice_callback_ = new VoiceCallback(semaphore_);

  // Load the XAudio2 DLL dynamically. Needed both for 2.7 and for
  // differentiating between 2.8 and later versions. Windows 8.1 SDK references
  // XAudio2_8.dll in xaudio2.lib, which is available on Windows 8.1, however,
  // Windows 10 SDK references XAudio2_9.dll in it, which is only available in
  // Windows 10, and XAudio2_8.dll is linked through a different .lib -
  // xaudio2_8.lib, so easier not to link the .lib at all.
  xaudio2_module_ = static_cast<void*>(LoadLibraryW(L"XAudio2_8.dll"));
  if (xaudio2_module_) {
    xaudio2_create_ = reinterpret_cast<decltype(xaudio2_create_)>(
        GetProcAddress(static_cast<HMODULE>(xaudio2_module_), "XAudio2Create"));
    if (xaudio2_create_) {
      api_minor_version_ = 8;
    } else {
      XELOGE("XAudio2Create not found in XAudio2_8.dll");
      FreeLibrary(static_cast<HMODULE>(xaudio2_module_));
      xaudio2_module_ = nullptr;
    }
  }
  if (!xaudio2_module_) {
    XELOGE("Failed to load XAudio 2.8 library DLL");
    return false;
  }

  // We need to be able to accept frames from any non-STA thread - primarily
  // from any guest thread, so MTA needs to be used. The AudioDriver, however,
  // may be initialized from the UI thread, which has the STA concurrency model,
  // or from another thread regardless of its concurrency model. So, all
  // management of the objects needs to be performed in MTA. Launch the
  // lifecycle management thread, which will handle initialization and shutdown,
  // and also provide a scope for implicit MTA in threads that have never
  // initialized COM explicitly, which lasts until all threads that have
  // initialized MTA explicitly have uninitialized it - the thread that holds
  // the MTA scope needs to be running while other threads are able to submit
  // frames as they might have not initialized MTA explicitly.
  // https://devblogs.microsoft.com/oldnewthing/?p=4613
  // This is needed for both XAudio 2.7 (created via explicit CoCreateInstance)
  // and 2.8 (using XAudio2Create, but still requiring CoInitializeEx).
  // https://docs.microsoft.com/en-us/windows/win32/xaudio2/how-to--initialize-xaudio2
  assert_false(mta_thread_.joinable());
  mta_thread_initialization_attempt_completed_ = false;
  mta_thread_shutdown_requested_ = false;
  mta_thread_ = std::thread(&XAudio2AudioDriver::MTAThread, this);
  {
    std::unique_lock<std::mutex> mta_thread_initialization_completion_lock(
        mta_thread_initialization_completion_mutex_);
    while (true) {
      if (mta_thread_initialization_attempt_completed_) {
        break;
      }
      mta_thread_initialization_completion_cond_.wait(
          mta_thread_initialization_completion_lock);
    }
  }
  if (!mta_thread_initialization_completion_result_) {
    mta_thread_.join();
    return false;
  }
  return true;
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
    XELOGE("IXAudio2::CreateMasteringVoice failed with 0x{:08X}", hr);
    ShutdownObjects(objects);
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
    XELOGE("IXAudio2::CreateSourceVoice failed with 0x{:08X}", hr);
    ShutdownObjects(objects);
    return false;
  }

  hr = objects.pcm_voice->Start();
  if (FAILED(hr)) {
    XELOGE("IXAudio2SourceVoice::Start failed with 0x{:08X}", hr);
    ShutdownObjects(objects);
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
    XELOGE("SubmitSourceBuffer failed with 0x{:08X}", hr);
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
  // XAudio2 lifecycle is managed by the MTA thread.
  if (mta_thread_.joinable()) {
    {
      std::unique_lock<std::mutex> mta_thread_shutdown_request_lock(
          mta_thread_shutdown_request_mutex_);
      mta_thread_shutdown_requested_ = true;
    }
    mta_thread_shutdown_request_cond_.notify_all();
    mta_thread_.join();
  }

  xaudio2_create_ = nullptr;
  if (xaudio2_module_) {
    FreeLibrary(static_cast<HMODULE>(xaudio2_module_));
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

void XAudio2AudioDriver::MTAThread() {
  xe::threading::set_name("XAudio2 MTA");

  assert_false(mta_thread_initialization_attempt_completed_);

  bool initialized = false;

  // Initializing MTA COM in this thread, as well making other (guest) threads
  // that don't explicitly call CoInitializeEx implicitly MTA for the period of
  // time when they can interact with XAudio through the XAudio2AudioDriver,
  // until the CoUninitialize (to be more precise, the CoUninitialize for the
  // last remaining MTA thread, but we need implicit MTA for the audio here).
  // https://devblogs.microsoft.com/oldnewthing/?p=4613
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool com_initialized = SUCCEEDED(hr);
  if (!com_initialized) {
    XELOGE("XAudio2 MTA thread CoInitializeEx failed with 0x{:08X}", hr);
  } else {
    if (api_minor_version_ >= 8) {
      hr = xaudio2_create_(&objects_.api_2_8.audio, 0, kProcessor);
      if (FAILED(hr)) {
        XELOGE("XAudio2Create failed with 0x{:08X}", hr);
      } else {
        initialized = InitializeObjects(objects_.api_2_8);
      }
    } else {
      hr = CoCreateInstance(__uuidof(api::XAudio2_7), nullptr,
                            CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&objects_.api_2_7.audio));
      if (FAILED(hr)) {
        XELOGE("CoCreateInstance for XAudio2 failed with 0x{:08X}", hr);
      } else {
        hr = objects_.api_2_7.audio->Initialize(0, kProcessor);
        if (FAILED(hr)) {
          XELOGE("IXAudio2::Initialize failed with 0x{:08X}", hr);
        } else {
          initialized = InitializeObjects(objects_.api_2_7);
        }
      }
    }
  }

  // Notify the threads waiting for the initialization of the result.
  mta_thread_initialization_completion_result_ = initialized;
  {
    std::unique_lock<std::mutex> mta_thread_initialization_completion_lock(
        mta_thread_initialization_completion_mutex_);
    mta_thread_initialization_attempt_completed_ = true;
  }
  mta_thread_initialization_completion_cond_.notify_all();

  if (initialized) {
    // Initialized successfully, await a shutdown request while keeping an
    // implicit COM MTA scope.
    {
      std::unique_lock<std::mutex> mta_thread_shutdown_request_lock(
          mta_thread_shutdown_request_mutex_);
      while (true) {
        if (mta_thread_shutdown_requested_) {
          break;
        }
        mta_thread_shutdown_request_cond_.wait(
            mta_thread_shutdown_request_lock);
      }
    }

    if (api_minor_version_ >= 8) {
      ShutdownObjects(objects_.api_2_8);
    } else {
      ShutdownObjects(objects_.api_2_7);
    }
  }

  if (com_initialized) {
    CoUninitialize();
  }
}

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe
