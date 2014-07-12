/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/xaudio2/xaudio2_audio_system.h>
#include <xenia/apu/xaudio2/xaudio2_audio_driver.h>

#include <xenia/apu/apu-private.h>

#include <xenia/emulator.h>

using namespace xe;
using namespace xe::apu;
using namespace xe::apu::xaudio2;


XAudio2AudioSystem::XAudio2AudioSystem(Emulator* emulator) :
  AudioSystem(emulator) {
}

XAudio2AudioSystem::~XAudio2AudioSystem() {
}

void XAudio2AudioSystem::Initialize() {
  AudioSystem::Initialize();
}

X_STATUS XAudio2AudioSystem::CreateDriver(size_t index, HANDLE wait, AudioDriver** out_driver) {
  assert_not_null(out_driver);
  auto driver = new XAudio2AudioDriver(emulator_, wait);
  driver->Initialize();
  *out_driver = driver;
  return X_STATUS_SUCCESS;
}

void XAudio2AudioSystem::DestroyDriver(AudioDriver* driver) {
  assert_not_null(driver);
  auto xdriver = static_cast<XAudio2AudioDriver*>(driver);
  xdriver->Shutdown();
  assert_not_null(xdriver);
  delete xdriver;
}
