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
    "Audio system. Use: [any, nop, xaudio2]");


#include <xenia/apu/nop/nop_apu.h>
AudioSystem* xe::apu::CreateNop(Emulator* emulator) {
  return xe::apu::nop::Create(emulator);
}


#if XE_PLATFORM_WIN32
#include <xenia/apu/xaudio2/xaudio2_apu.h>
AudioSystem* xe::apu::CreateXAudio2(Emulator* emulator) {
  return xe::apu::xaudio2::Create(emulator);
}
#endif  // WIN32

AudioSystem* xe::apu::Create(Emulator* emulator) {
  if (FLAGS_apu.compare("nop") == 0) {
    return CreateNop(emulator);
#if XE_PLATFORM_WIN32
  }
  else if (FLAGS_apu.compare("xaudio2") == 0) {
    return CreateXAudio2(emulator);
#endif  // WIN32
  } else {
    // Create best available.
    AudioSystem* best = NULL;

#if XE_PLATFORM_WIN32
    best = CreateXAudio2(emulator);
    if (best) {
      return best;
    }
#endif  // WIN32

    // Fallback to nop.
    return CreateNop(emulator);
  }
}
