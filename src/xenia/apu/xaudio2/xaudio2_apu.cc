/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/xaudio2/xaudio2_apu.h>

#include <xenia/apu/xaudio2/xaudio2_audio_system.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::xaudio2;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    //

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
  }
}


AudioSystem* xe::apu::xaudio2::Create(Emulator* emulator) {
  InitializeIfNeeded();
  return new XAudio2AudioSystem(emulator);
}
