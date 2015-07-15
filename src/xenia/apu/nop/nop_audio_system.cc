/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/nop/nop_audio_system.h"

#include "xenia/apu/apu_flags.h"

namespace xe {
namespace apu {
namespace nop {

std::unique_ptr<AudioSystem> NopAudioSystem::Create(Emulator* emulator) {
  return std::make_unique<NopAudioSystem>(emulator);
}

NopAudioSystem::NopAudioSystem(Emulator* emulator) : AudioSystem(emulator) {}

NopAudioSystem::~NopAudioSystem() = default;

X_STATUS NopAudioSystem::CreateDriver(size_t index,
                                      xe::threading::Semaphore* semaphore,
                                      AudioDriver** out_driver) {
  return X_STATUS_NOT_IMPLEMENTED;
}

void NopAudioSystem::DestroyDriver(AudioDriver* driver) { assert_always(); }

}  // namespace nop
}  // namespace apu
}  // namespace xe
