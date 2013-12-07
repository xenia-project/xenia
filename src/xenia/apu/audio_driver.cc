/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/audio_driver.h>


using namespace xe;
using namespace xe::apu;


AudioDriver::AudioDriver(Memory* memory) :
    memory_(memory) {
}

AudioDriver::~AudioDriver() {
}
