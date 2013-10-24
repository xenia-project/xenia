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


AudioDriver::AudioDriver(xe_memory_ref memory) {
  memory_ = xe_memory_retain(memory);
}

AudioDriver::~AudioDriver() {
  xe_memory_release(memory_);
}
