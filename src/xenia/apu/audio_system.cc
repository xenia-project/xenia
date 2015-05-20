/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_system.h"

#include "xenia/apu/audio_driver.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/emulator.h"
#include "xenia/profiling.h"

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// For ease of implementation, most audio related processing is handled in
// AudioSystem, and the functions here call off to it.
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal AudioSystem handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc
//
// XMA details:
// https://devel.nuclex.org/external/svn/directx/trunk/include/xma2defs.h
// https://github.com/gdawg/fsbext/blob/master/src/xma_header.h
//
// XAudio2 uses XMA under the covers, and seems to map with the same
// restrictions of frame/subframe/etc:
// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.xaudio2.xaudio2_buffer(v=vs.85).aspx
//
// XMA contexts are 64b in size and tight bitfields. They are in physical
// memory not usually available to games. Games will use MmMapIoSpace to get
// the 64b pointer in user memory so they can party on it. If the game doesn't
// do this, it's likely they are either passing the context to XAudio or
// using the XMA* functions.

namespace xe {
namespace apu {

using namespace xe::cpu;
using namespace xe::kernel;

// Size of a hardware XMA context.
const uint32_t kXmaContextSize = 64;
// Total number of XMA contexts available.
const uint32_t kXmaContextCount = 320;

AudioSystem::AudioSystem(Emulator* emulator)
    : emulator_(emulator), memory_(emulator->memory()), running_(false) {
  memset(clients_, 0, sizeof(clients_));
  for (size_t i = 0; i < maximum_client_count_; ++i) {
    unused_clients_.push(i);
  }
  for (size_t i = 0; i < xe::countof(client_wait_handles_); ++i) {
    client_wait_handles_[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
  }
}

AudioSystem::~AudioSystem() {
  for (size_t i = 0; i < xe::countof(client_wait_handles_); ++i) {
    CloseHandle(client_wait_handles_[i]);
  }
}

X_STATUS AudioSystem::Setup() {
  processor_ = emulator_->processor();

  // Let the processor know we want register access callbacks.
  emulator_->memory()->AddVirtualMappedRange(
      0x7FEA0000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // Setup XMA contexts ptr.
  registers_.xma_context_array_ptr = memory()->SystemHeapAlloc(
      kXmaContextSize * kXmaContextCount, 256, kSystemHeapPhysical);
  // Add all contexts to the free list.
  for (int i = kXmaContextCount - 1; i >= 0; --i) {
    xma_context_free_list_.push_back(registers_.xma_context_array_ptr +
                                     i * kXmaContextSize);
  }
  registers_.next_context = 1;

  // Setup our worker thread
  std::function<int()> thread_fn = [this]() {
    this->ThreadStart();
    return 0;
  };

  running_ = true;

  thread_ = std::make_unique<XHostThread>(emulator()->kernel_state(), 
                                        128 * 1024, 0, thread_fn);
  thread_->Create();

  return X_STATUS_SUCCESS;
}

void AudioSystem::ThreadStart() {
  xe::threading::set_name("Audio Worker");

  // Initialize driver and ringbuffer.
  Initialize();

  auto processor = emulator_->processor();

  // Main run loop.
  while (running_) {
    auto result =
        WaitForMultipleObjectsEx(DWORD(xe::countof(client_wait_handles_)),
                                 client_wait_handles_, FALSE, INFINITE, FALSE);
    if (result == WAIT_FAILED ||
        result == WAIT_OBJECT_0 + maximum_client_count_) {
      continue;
    }

    size_t pumped = 0;
    if (result >= WAIT_OBJECT_0 &&
        result <= WAIT_OBJECT_0 + (maximum_client_count_ - 1)) {
      size_t index = result - WAIT_OBJECT_0;
      do {
        lock_.lock();
        uint32_t client_callback = clients_[index].callback;
        uint32_t client_callback_arg = clients_[index].wrapped_callback_arg;
        lock_.unlock();
        if (client_callback) {
          uint64_t args[] = {client_callback_arg};
          processor->Execute(thread_->thread_state(), client_callback, args,
                             xe::countof(args));
        }
        pumped++;
        index++;
      } while (index < maximum_client_count_ &&
               WaitForSingleObject(client_wait_handles_[index], 0) ==
                   WAIT_OBJECT_0);
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

void AudioSystem::Initialize() {}

void AudioSystem::Shutdown() {
  running_ = false;
  ResetEvent(client_wait_handles_[maximum_client_count_]);
  thread_->Wait(0, 0, 0, NULL);

  memory()->SystemHeapFree(registers_.xma_context_array_ptr);
}

uint32_t AudioSystem::AllocateXmaContext() {
  std::lock_guard<std::mutex> lock(lock_);
  if (xma_context_free_list_.empty()) {
    // No contexts available.
    return 0;
  }

  auto guest_ptr = xma_context_free_list_.back();
  xma_context_free_list_.pop_back();
  auto context_ptr = memory()->TranslateVirtual(guest_ptr);

  return guest_ptr;
}

void AudioSystem::ReleaseXmaContext(uint32_t guest_ptr) {
  std::lock_guard<std::mutex> lock(lock_);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr);
  std::memset(context_ptr, 0, kXmaContextSize);

  xma_context_free_list_.push_back(guest_ptr);
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg,
                                     size_t* out_index) {
  assert_true(unused_clients_.size());
  std::lock_guard<std::mutex> lock(lock_);

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

  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  xe::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);

  clients_[index] = {driver, callback, callback_arg, ptr};

  if (out_index) {
    *out_index = index;
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, uint32_t samples_ptr) {
  SCOPE_profile_cpu_f("apu");

  std::lock_guard<std::mutex> lock(lock_);
  assert_true(index < maximum_client_count_);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);
  ResetEvent(client_wait_handles_[index]);
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  std::lock_guard<std::mutex> lock(lock_);
  assert_true(index < maximum_client_count_);
  DestroyDriver(clients_[index].driver);
  clients_[index] = {0};
  unused_clients_.push(index);
  ResetEvent(client_wait_handles_[index]);
}

// free60 may be useful here, however it looks like it's using a different
// piece of hardware:
// https://github.com/Free60Project/libxenon/blob/master/libxenon/drivers/xenon_sound/sound.c

uint64_t AudioSystem::ReadRegister(uint32_t addr) {
  uint32_t r = addr & 0xFFFF;
  XELOGAPU("ReadRegister(%.4X)", r);
  // 1800h is read on startup and stored -- context? buffers?
  // 1818h is read during a lock?

  assert_true(r % 4 == 0);
  uint32_t value = register_file_[r / 4];

  // 1818 is rotating context processing # set to hardware ID of context being
  // processed.
  // If bit 200h is set, the locking code will possibly collide on hardware IDs
  // and error out, so we should never set it (I think?).
  if (r == 0x1818) {
    // To prevent games from seeing a stuck XMA context, return a rotating
    // number
    registers_.current_context = registers_.next_context;
    registers_.next_context = (registers_.next_context + 1) % kXmaContextCount;
    value = registers_.current_context;
  }

  value = xe::byte_swap(value);
  return value;
}

void AudioSystem::WriteRegister(uint32_t addr, uint64_t value) {
  uint32_t r = addr & 0xFFFF;
  value = xe::byte_swap(uint32_t(value));
  XELOGAPU("WriteRegister(%.4X, %.8X)", r, value);
  // 1804h is written to with 0x02000000 and 0x03000000 around a lock operation

  assert_true(r % 4 == 0);
  register_file_[r / 4] = uint32_t(value);

  if (r >= 0x1940 && r <= 0x1940 + 9 * 4) {
    // Context kick command.
    // This will kick off the given hardware contexts.
    for (int i = 0; value && i < 32; ++i) {
      if (value & 1) {
        uint32_t context_id = i + (r - 0x1940) / 4 * 32;
        XELOGAPU("AudioSystem: kicking context %d", context_id);
        // Games check bits 20/21 of context[0].
        // If both bits are set buffer full, otherwise room available.
        // Right after a kick we always set buffers to invalid so games keep
        // feeding data.
        uint32_t guest_ptr =
            registers_.xma_context_array_ptr + context_id * kXmaContextSize;
        auto context_ptr = memory()->TranslateVirtual(guest_ptr);
        XMAContextData data(context_ptr);

        bool has_valid_input = data.input_buffer_0_valid ||
                               data.input_buffer_1_valid;
        if (has_valid_input) {
          // Invalidate the buffers.
          data.input_buffer_0_valid = 0;
          data.input_buffer_1_valid = 0;

          // Set output buffer to invalid.
          data.output_buffer_valid = false;
        }

        data.Store(context_ptr);
      }
      value >>= 1;
    }
  } else if (r >= 0x1A40 && r <= 0x1A40 + 9 * 4) {
    // Context lock command.
    // This requests a lock by flagging the context.
    for (int i = 0; value && i < 32; ++i) {
      if (value & 1) {
        uint32_t context_id = i + (r - 0x1A40) / 4 * 32;
        XELOGAPU("AudioSystem: set context lock %d", context_id);
        // TODO(benvanik): set lock?
      }
      value >>= 1;
    }
  } else if (r >= 0x1A80 && r <= 0x1A80 + 9 * 4) {
    // Context clear command.
    // This will reset the given hardware contexts.
    for (int i = 0; value && i < 32; ++i) {
      if (value & 1) {
        uint32_t context_id = i + (r - 0x1A80) / 4 * 32;
        XELOGAPU("AudioSystem: reset context %d", context_id);
        // TODO(benvanik): something?
        uint32_t guest_ptr =
            registers_.xma_context_array_ptr + context_id * kXmaContextSize;
        auto context_ptr = memory()->TranslateVirtual(guest_ptr);
        XMAContextData data(context_ptr);



        data.Store(context_ptr);
      }
      value >>= 1;
    }
  } else {
    value = value;
  }
}

}  // namespace apu
}  // namespace xe
