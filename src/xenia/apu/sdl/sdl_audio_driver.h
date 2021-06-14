/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_SDL_SDL_AUDIO_DRIVER_H_
#define XENIA_APU_SDL_SDL_AUDIO_DRIVER_H_

#include <mutex>
#include <queue>
#include <stack>

#include "SDL.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/base/threading.h"

namespace xe {
namespace apu {
namespace sdl {

class SDLAudioDriver : public AudioDriver {
 public:
  SDLAudioDriver(Memory* memory, xe::threading::Semaphore* semaphore);
  ~SDLAudioDriver() override;

  bool Initialize();
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 protected:
  static void SDLCallback(void* userdata, Uint8* stream, int len);

  xe::threading::Semaphore* semaphore_ = nullptr;

  SDL_AudioDeviceID sdl_device_id_ = -1;
  bool sdl_initialized_ = false;
  uint8_t sdl_device_channels_ = 0;

  static const uint32_t frame_frequency_ = 48000;
  static const uint32_t frame_channels_ = 6;
  static const uint32_t channel_samples_ = 256;
  static const uint32_t frame_samples_ = frame_channels_ * channel_samples_;
  static const uint32_t frame_size_ = sizeof(float) * frame_samples_;
  std::queue<float*> frames_queued_ = {};
  std::stack<float*> frames_unused_ = {};
  std::mutex frames_mutex_ = {};
};

}  // namespace sdl
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_SDL_SDL_AUDIO_DRIVER_H_
