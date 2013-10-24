/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/audio_system.h>

#include <xenia/emulator.h>
#include <xenia/cpu/processor.h>
#include <xenia/apu/audio_driver.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::cpu::ppc;


AudioSystem::AudioSystem(Emulator* emulator) :
    emulator_(emulator),
    thread_(0), running_(false), driver_(0) {
  memory_ = xe_memory_retain(emulator->memory());

  // Create the run loop used for any windows/etc.
  // This must be done on the thread we create the driver.
  run_loop_ = xe_run_loop_create();
}

AudioSystem::~AudioSystem() {
  // TODO(benvanik): thread join.
  running_ = false;
  xe_thread_release(thread_);

  xe_run_loop_release(run_loop_);
  xe_memory_release(memory_);
}

X_STATUS AudioSystem::Setup() {
  processor_ = emulator_->processor();

  // Let the processor know we want register access callbacks.
  RegisterAccessCallbacks callbacks;
  callbacks.context = this;
  callbacks.handles = (RegisterHandlesCallback)HandlesRegisterThunk;
  callbacks.read    = (RegisterReadCallback)ReadRegisterThunk;
  callbacks.write   = (RegisterWriteCallback)WriteRegisterThunk;
  emulator_->processor()->AddRegisterAccessCallbacks(callbacks);

  // Create worker thread.
  // This will initialize the audio system.
  // Init needs to happen there so that any thread-local stuff
  // is created on the right thread.
  running_ = true;
  thread_ = xe_thread_create(
      "AudioSystem",
      (xe_thread_callback)ThreadStartThunk, this);
  xe_thread_start(thread_);

  return X_STATUS_SUCCESS;
}

void AudioSystem::ThreadStart() {
  xe_run_loop_ref run_loop = xe_run_loop_retain(run_loop_);

  // Initialize driver and ringbuffer.
  Initialize();
  XEASSERTNOTNULL(driver_);

  // Main run loop.
  while (running_) {
    // Peek main run loop.
    if (xe_run_loop_pump(run_loop)) {
      break;
    }
    if (!running_) {
      break;
    }

    // Pump worker.
    //worker_->Pump();

    // Pump audio system.
    Pump();
  }
  running_ = false;

  Shutdown();

  xe_run_loop_release(run_loop);

  // TODO(benvanik): call module API to kill?
}

void AudioSystem::Initialize() {
}

void AudioSystem::Shutdown() {
}

bool AudioSystem::HandlesRegister(uint32_t addr) {
  return (addr & 0xFFFF0000) == 0x7FEA0000;
}

uint64_t AudioSystem::ReadRegister(uint32_t addr) {
  uint32_t r = addr & 0xFFFF;
  XELOGAPU("ReadRegister(%.4X)", r);
  return 0;
}

void AudioSystem::WriteRegister(uint32_t addr, uint64_t value) {
  uint32_t r = addr & 0xFFFF;
  XELOGAPU("WriteRegister(%.4X, %.8X)", r, value);
}