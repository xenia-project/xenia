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

#include <condition_variable>
#include <mutex>
#include <thread>

#include "xenia/apu/audio_driver.h"
#include "xenia/apu/xaudio2/xaudio2_api.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioDriver : public AudioDriver {
 public:
  XAudio2AudioDriver(Memory* memory, xe::threading::Semaphore* semaphore);
  ~XAudio2AudioDriver() override;

  bool Initialize();
  // Must not be called from COM STA threads as MTA XAudio2 will be used. It's
  // fine to call this from threads that have never initialized COM as
  // initializing MTA for any thread implicitly initializes it for all threads
  // not explicitly requesting STA (until COM is uninitialized all threads that
  // have initialized MTA).
  // https://devblogs.microsoft.com/oldnewthing/?p=4613
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 private:
  // First CPU (2.8 default). XAUDIO2_ANY_PROCESSOR (2.7 default) steals too
  // much time from other things. Ideally should process audio on what roughly
  // represents thread 4 (5th) on the Xbox 360 (2.7 default on the console), or
  // even beyond the 6 guest cores.
  api::XAUDIO2_PROCESSOR kProcessor = 0x00000001;

  // InitializeObjects and ShutdownObjects must be called only in the lifecycle
  // management thread with COM MTA initialized.
  template <typename Objects>
  bool InitializeObjects(Objects& objects);
  template <typename Objects>
  void ShutdownObjects(Objects& objects);

  void MTAThread();

  void* xaudio2_module_ = nullptr;
  // clang-format off
  HRESULT (__stdcall* xaudio2_create_)(
      api::IXAudio2_8** xaudio2_out, UINT32 flags,
      api::XAUDIO2_PROCESSOR xaudio2_processor) = nullptr;
  // clang-format on
  uint32_t api_minor_version_ = 7;

  bool mta_thread_initialization_completion_result_;
  std::mutex mta_thread_initialization_completion_mutex_;
  std::condition_variable mta_thread_initialization_completion_cond_;
  bool mta_thread_initialization_attempt_completed_;

  std::mutex mta_thread_shutdown_request_mutex_;
  std::condition_variable mta_thread_shutdown_request_cond_;
  bool mta_thread_shutdown_requested_;

  std::thread mta_thread_;

  union {
    struct {
      api::IXAudio2_7* audio;
      api::IXAudio2_7MasteringVoice* mastering_voice;
      api::IXAudio2_7SourceVoice* pcm_voice;
    } api_2_7;
    struct {
      api::IXAudio2_8* audio;
      api::IXAudio2_8MasteringVoice* mastering_voice;
      api::IXAudio2_8SourceVoice* pcm_voice;
    } api_2_8;
  } objects_ = {};
  xe::threading::Semaphore* semaphore_ = nullptr;

  class VoiceCallback;
  VoiceCallback* voice_callback_ = nullptr;

  static const uint32_t frame_count_ = api::XE_XAUDIO2_MAX_QUEUED_BUFFERS;
  static const uint32_t frame_channels_ = 6;
  static const uint32_t channel_samples_ = 256;
  static const uint32_t frame_samples_ = frame_channels_ * channel_samples_;
  static const uint32_t frame_size_ = sizeof(float) * frame_samples_;
  float frames_[frame_count_][frame_samples_];
  uint32_t current_frame_ = 0;
};

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XAUDIO2_XAUDIO2_AUDIO_DRIVER_H_
