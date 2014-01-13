/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/xaudio2/xaudio2_audio_driver.h>

#include <xenia/apu/apu-private.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::xaudio2;


XAudio2AudioDriver::XAudio2AudioDriver(Memory* memory) :
    AudioDriver(memory) {
}

XAudio2AudioDriver::~XAudio2AudioDriver() {
}

void XAudio2AudioDriver::Initialize() {
}
