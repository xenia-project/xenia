/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/nop/nop_audio_system.h>

#include <xenia/apu/apu-private.h>
#include <xenia/apu/nop/nop_audio_driver.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::nop;


NopAudioSystem::NopAudioSystem(Emulator* emulator) :
    AudioSystem(emulator) {
}

NopAudioSystem::~NopAudioSystem() {
}

void NopAudioSystem::Initialize() {
  AudioSystem::Initialize();

  XEASSERTNULL(driver_);
  driver_ = new NopAudioDriver(memory_);
}

void NopAudioSystem::Pump() {
  //
}

void NopAudioSystem::SubmitFrame(uint32_t samples_ptr) {
  // Process samples! They are big-endian floats.
}

void NopAudioSystem::Shutdown() {
  AudioSystem::Shutdown();
}
