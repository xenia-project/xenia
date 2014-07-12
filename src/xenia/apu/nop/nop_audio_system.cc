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


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::nop;


NopAudioSystem::NopAudioSystem(Emulator* emulator) :
    AudioSystem(emulator) {
}

NopAudioSystem::~NopAudioSystem() {
}

X_STATUS NopAudioSystem::CreateDriver(size_t index, HANDLE wait_handle, AudioDriver** out_driver) {
  return X_STATUS_NOT_IMPLEMENTED;
}

void NopAudioSystem::DestroyDriver(AudioDriver* driver) {
  assert_always();
}
