/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/nop/nop_audio_driver.h>

#include <xenia/apu/apu-private.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::nop;


NopAudioDriver::NopAudioDriver(Memory* memory) :
    AudioDriver(memory) {
}

NopAudioDriver::~NopAudioDriver() {
}

void NopAudioDriver::Initialize() {
}
