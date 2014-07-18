/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/apu/audio_system.h>
#include <xenia/apu/audio_driver.h>

#include <xenia/emulator.h>
#include <xenia/cpu/processor.h>
#include <xenia/cpu/xenon_thread_state.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::cpu;


AudioSystem::AudioSystem(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()),
    thread_(0), running_(false) {
  lock_ = xe_mutex_alloc();
  memset(clients_, 0, sizeof(clients_));
  for (size_t i = 0; i < maximum_client_count_; ++i) {
    client_wait_handles_[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
    unused_clients_.push(i);
  }
}

AudioSystem::~AudioSystem() {
  for (size_t i = 0; i < maximum_client_count_; ++i) {
    CloseHandle(client_wait_handles_[i]);
  }
  xe_mutex_free(lock_);
}

X_STATUS AudioSystem::Setup() {
  processor_ = emulator_->processor();

  // Let the processor know we want register access callbacks.
  emulator_->memory()->AddMappedRange(
      0x7FEA0000,
      0xFFFF0000,
      0x0000FFFF,
      this,
      reinterpret_cast<MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<MMIOWriteCallback>(MMIOWriteRegisterThunk));

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
  // Initialize driver and ringbuffer.
  Initialize();

  auto processor = emulator_->processor();

  // Main run loop.
  while (running_) {
    auto result = WaitForMultipleObjectsEx(maximum_client_count_, client_wait_handles_, FALSE, INFINITE, FALSE);
    if (result == WAIT_FAILED) {
      DWORD err = GetLastError();
      assert_always();
      break;
    }

    size_t pumped = 0;
    if (result >= WAIT_OBJECT_0 && result <= WAIT_OBJECT_0 + (maximum_client_count_ - 1)) {
      size_t index = result - WAIT_OBJECT_0;
      do {
        xe_mutex_lock(lock_);
        uint32_t client_callback = clients_[index].callback;
        uint32_t client_callback_arg = clients_[index].wrapped_callback_arg;
        xe_mutex_unlock(lock_);
        if (client_callback) {
          uint64_t args[] = { client_callback_arg };
          processor->Execute(thread_state_, client_callback, args, XECOUNT(args));
        }
        pumped++;
        index++;
      } while (index < maximum_client_count_ && WaitForSingleObject(client_wait_handles_[index], 0) == WAIT_OBJECT_0);
    }

    if (!running_) {
      break;
    }

    if (!pumped) {
      SCOPE_profile_cpu_i("apu", "Sleep");
      Sleep(500);
    }
  }
  running_ = false;

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
}

X_STATUS AudioSystem::RegisterClient(
    uint32_t callback, uint32_t callback_arg, size_t* out_index) {
  assert_true(unused_clients_.size());
  xe_mutex_lock(lock_);

  auto index = unused_clients_.front();

  auto wait_handle = client_wait_handles_[index];
  ResetEvent(wait_handle);
  AudioDriver* driver;
  auto result = CreateDriver(index, wait_handle, &driver);
  if (XFAILED(result)) {
    return result;
  }
  assert_not_null(driver);

  unused_clients_.pop();

  uint32_t ptr = (uint32_t)memory()->HeapAlloc(0, 0x4, 0);
  auto mem = memory()->membase();
  poly::store_and_swap<uint32_t>(mem + ptr, callback_arg);

  clients_[index] = { driver, callback, callback_arg, ptr };

  if (out_index) {
    *out_index = index;
  }

  xe_mutex_unlock(lock_);
  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, uint32_t samples_ptr) {
  SCOPE_profile_cpu_f("apu");

  xe_mutex_lock(lock_);
  assert_true(index < maximum_client_count_);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);
  ResetEvent(client_wait_handles_[index]);
  xe_mutex_unlock(lock_);
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  xe_mutex_lock(lock_);
  assert_true(index < maximum_client_count_);
  DestroyDriver(clients_[index].driver);
  clients_[index] = { 0 };
  unused_clients_.push(index);
  xe_mutex_unlock(lock_);
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
