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
#include <xenia/cpu/xenon_thread_state.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::cpu;


AudioSystem::AudioSystem(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()),
    thread_(0), running_(false),
    client_({ 0 }), can_submit_(false) {
  // Create the run loop used for any windows/etc.
  // This must be done on the thread we create the driver.
  run_loop_ = xe_run_loop_create();

  lock_ = xe_mutex_alloc();
}

AudioSystem::~AudioSystem() {
  xe_mutex_free(lock_);
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

  // Setup worker thread state. This lets us make calls into guest code.
  thread_state_ = new XenonThreadState(
      emulator_->processor()->runtime(), 0, 16 * 1024, 0);
  thread_state_->set_name("Audio Worker");
  thread_block_ = (uint32_t)memory_->HeapAlloc(
      0, 2048, alloy::MEMORY_FLAG_ZERO);
  thread_state_->context()->r[13] = thread_block_;

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

  auto processor = emulator_->processor();

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
    Pump();

    xe_mutex_lock(lock_);
    uint32_t client_callback = client_.callback;
    uint32_t client_callback_arg = client_.wrapped_callback_arg;
    xe_mutex_unlock(lock_);
    if (client_callback && can_submit_) {
      processor->Execute(
        thread_state_, client_callback, client_callback_arg, 0);
    } else {
      Sleep(500);
    }

    if (!running_) {
      break;
    }

    // Pump audio system.
    Pump();
  }
  running_ = false;

  xe_run_loop_release(run_loop);

  // TODO(benvanik): call module API to kill?
}

void AudioSystem::Initialize() {
}

void AudioSystem::Shutdown() {
  running_ = false;
  xe_thread_join(thread_);
  xe_thread_release(thread_);

  delete thread_state_;
  memory()->HeapFree(thread_block_, 0);

  xe_run_loop_release(run_loop_);
}

void AudioSystem::RegisterClient(
    uint32_t callback, uint32_t callback_arg) {
  // Only support one client for now.
  XEASSERTZERO(client_.callback);

  uint32_t ptr = (uint32_t)memory()->HeapAlloc(0, 0x4, 0);
  auto mem = memory()->membase();
  XESETUINT32BE(mem + ptr, callback_arg);

  xe_mutex_lock(lock_);
  client_ = { callback, callback_arg, ptr };
  xe_mutex_unlock(lock_);
}

void AudioSystem::UnregisterClient() {
  xe_mutex_lock(lock_);
  client_ = { 0 };
  xe_mutex_unlock(lock_);
}

bool AudioSystem::HandlesRegister(uint64_t addr) {
  return (addr & 0xFFFF0000) == 0x7FEA0000;
}

// free60 may be useful here, however it looks like it's using a different
// piece of hardware:
// https://github.com/Free60Project/libxenon/blob/master/libxenon/drivers/xenon_sound/sound.c

uint64_t AudioSystem::ReadRegister(uint64_t addr) {
  uint32_t r = addr & 0xFFFF;
  XELOGAPU("ReadRegister(%.4X)", r);
  // 1800h is read on startup and stored -- context? buffers?
  // 1818h is read during a lock?
  return 0;
}

void AudioSystem::WriteRegister(uint64_t addr, uint64_t value) {
  uint32_t r = addr & 0xFFFF;
  XELOGAPU("WriteRegister(%.4X, %.8X)", r, value);
  // 1804h is written to with 0x02000000 and 0x03000000 around a lock operation
}