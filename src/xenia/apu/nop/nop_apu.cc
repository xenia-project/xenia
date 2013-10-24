/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/nop/nop_apu.h>

#include <xenia/apu/nop/nop_audio_system.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::apu::nop;


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


AudioSystem* xe::apu::nop::Create(Emulator* emulator) {
  InitializeIfNeeded();
  return new NopAudioSystem(emulator);
}
