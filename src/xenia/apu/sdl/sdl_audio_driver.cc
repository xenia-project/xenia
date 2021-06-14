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
#include <cstring>

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/conversion.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/helper/sdl/sdl_helper.h"

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
  SDL_version ver = {};
  SDL_GetVersion(&ver);
  if ((ver.major < 2) || (ver.major == 2 && ver.minor == 0 && ver.patch < 8)) {
    XELOGW(
        "SDL library version {}.{}.{} is outdated. "
        "You may experience choppy audio.",
        ver.major, ver.minor, ver.patch);
  }

  if (!xe::helper::sdl::SDLHelper::Prepare()) {
    return false;
  }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
    return false;
  }
  sdl_initialized_ = true;

  SDL_AudioSpec desired_spec = {};
  SDL_AudioSpec obtained_spec;
  desired_spec.freq = frame_frequency_;
  desired_spec.format = AUDIO_F32;
  desired_spec.channels = frame_channels_;
  desired_spec.samples = channel_samples_;
  desired_spec.callback = SDLCallback;
  desired_spec.userdata = this;
  // Allow the hardware to decide between 5.1 and stereo
  int allowed_change = SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
  for (int i = 0; i < 2; i++) {
    sdl_device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired_spec,
                                         &obtained_spec, allowed_change);
    if (sdl_device_id_ <= 0) {
      XELOGE("SDL_OpenAudioDevice() failed.");
      return false;
    }
    if (obtained_spec.channels == 2 || obtained_spec.channels == 6) {
      break;
    }
    // If the system is 4 or 7.1, let SDL convert
    allowed_change = 0;
    SDL_CloseAudioDevice(sdl_device_id_);
    sdl_device_id_ = -1;
  }
  if (sdl_device_id_ <= 0) {
    XELOGE("Failed to get a compatible SDL Audio Device.");
    return false;
  }
  sdl_device_channels_ = obtained_spec.channels;

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

  std::memcpy(output_frame, input_frame, frame_samples_ * sizeof(float));

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

void SDLAudioDriver::SDLCallback(void* userdata, Uint8* stream, int len) {
  SCOPE_profile_cpu_f("apu");
  if (!userdata || !stream) {
    XELOGE("SDLAudioDriver::sdl_callback called with nullptr.");
    return;
  }
  const auto driver = static_cast<SDLAudioDriver*>(userdata);
  assert_true(len ==
              sizeof(float) * channel_samples_ * driver->sdl_device_channels_);

  std::unique_lock<std::mutex> guard(driver->frames_mutex_);
  if (driver->frames_queued_.empty()) {
    std::memset(stream, 0, len);
  } else {
    auto buffer = driver->frames_queued_.front();
    driver->frames_queued_.pop();
    if (cvars::mute) {
      std::memset(stream, 0, len);
    } else {
      switch (driver->sdl_device_channels_) {
        case 2:
          conversion::sequential_6_BE_to_interleaved_2_LE(
              reinterpret_cast<float*>(stream), buffer, channel_samples_);
          break;
        case 6:
          conversion::sequential_6_BE_to_interleaved_6_LE(
              reinterpret_cast<float*>(stream), buffer, channel_samples_);
          break;
        default:
          assert_unhandled_case(driver->sdl_device_channels_);
          break;
      }
    }
    driver->frames_unused_.push(buffer);

    auto ret = driver->semaphore_->Release(1, nullptr);
    assert_true(ret);
  }
};
}  // namespace sdl
}  // namespace apu
}  // namespace xe
