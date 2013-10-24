/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/apu.h>
#include <xenia/apu/apu-private.h>



using namespace xe;
using namespace xe::apu;


DEFINE_string(apu, "any",
    "Audio system. Use: [any, nop]");


#include <xenia/apu/nop/nop_apu.h>
AudioSystem* xe::apu::CreateNop(Emulator* emulator) {
  return xe::apu::nop::Create(emulator);
}


AudioSystem* xe::apu::Create(Emulator* emulator) {
  if (FLAGS_apu.compare("nop") == 0) {
    return CreateNop(emulator);
  } else {
    // Create best available.
    AudioSystem* best = NULL;

    // Fallback to nop.
    return CreateNop(emulator);
  }
}
