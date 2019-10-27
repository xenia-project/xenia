/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/sdl/sdl_audio_driver.h"

#include <array>

#include "xenia/apu/apu_flags.h"
#include "xenia/base/logging.h"
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

namespace xe {
namespace apu {
namespace sdl {

SDLAudioDriver::SDLAudioDriver(Memory* memory,
                               xe::threading::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {}

SDLAudioDriver::~SDLAudioDriver() {
  assert_true(frames_queued_.empty());
  assert_true(frames_unused_.empty());
};

bool SDLAudioDriver::Initialize() {
  // With msvc delayed loading, exceptions are used to determine dll presence.
#if XE_PLATFORM_WIN32
  __try {
#endif  // XE_PLATFORM_WIN32
    SDL_version ver = {};
    SDL_GetVersion(&ver);
    if ((ver.major < 2) ||
        (ver.major == 2 && ver.minor == 0 && ver.patch < 8)) {
      XELOGW(
          "SDL library version %d.%d.%d is outdated. "
          "You may experience choppy audio.",
          ver.major, ver.minor, ver.patch);
    }
#if XE_PLATFORM_WIN32
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#endif  // XE_PLATFORM_WIN32

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
    return false;
  }
  sdl_initialized_ = true;

  SDL_AudioCallback audio_callback = [](void* userdata, Uint8* stream,
                                        int len) -> void {
    assert_true(len == frame_size_);
    const auto driver = static_cast<SDLAudioDriver*>(userdata);

    std::unique_lock<std::mutex> guard(driver->frames_mutex_);
    if (driver->frames_queued_.empty()) {
      memset(stream, 0, len);
    } else {
      auto buffer = driver->frames_queued_.front();
      driver->frames_queued_.pop();
      if (cvars::mute) {
        memset(stream, 0, len);
      } else {
        memcpy(stream, buffer, len);
      }
      driver->frames_unused_.push(buffer);

      auto ret = driver->semaphore_->Release(1, nullptr);
      assert_true(ret);
    }
  };

  SDL_AudioSpec wanted_spec = {};
  wanted_spec.freq = frame_frequency_;
  wanted_spec.format = AUDIO_F32;
  wanted_spec.channels = frame_channels_;
  wanted_spec.samples = channel_samples_;
  wanted_spec.callback = audio_callback;
  wanted_spec.userdata = this;
  sdl_device_id_ = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, nullptr, 0);
  if (sdl_device_id_ <= 0) {
    XELOGE("SDL_OpenAudioDevice() failed.");
    return false;
  }
  SDL_PauseAudioDevice(sdl_device_id_, 0);

  return true;
}

void SDLAudioDriver::SubmitFrame(uint32_t frame_ptr) {
  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  float* output_frame;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[frame_samples_];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  // interleave the data
  for (size_t index = 0, o = 0; index < channel_samples_; ++index) {
    for (size_t channel = 0, table = 0; channel < frame_channels_;
         ++channel, table += channel_samples_) {
      output_frame[o++] = xe::byte_swap(input_frame[table + index]);
    }
  }

  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }
}

void SDLAudioDriver::Shutdown() {
  if (sdl_device_id_ > 0) {
    SDL_CloseAudioDevice(sdl_device_id_);
    sdl_device_id_ = -1;
  }
  if (sdl_initialized_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdl_initialized_ = false;
  }
  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (!frames_unused_.empty()) {
    delete[] frames_unused_.top();
    frames_unused_.pop();
  };
  while (!frames_queued_.empty()) {
    delete[] frames_queued_.front();
    frames_queued_.pop();
  };
}

}  // namespace sdl
}  // namespace apu
}  // namespace xe
