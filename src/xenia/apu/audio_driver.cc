/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_driver.h"

#include "xenia/emulator.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"

using namespace xe;
using namespace xe::apu;
using namespace xe::cpu;

AudioDriver::AudioDriver(Emulator* emulator)
    : emulator_(emulator), memory_(emulator->memory()) {}

AudioDriver::~AudioDriver() {}
