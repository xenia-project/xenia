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
#include "xenia/apu/audio_decoder.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/profiling.h"

extern "C" {
#include "libavutil/log.h"
}

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

AudioSystem::AudioSystem(Emulator* emulator)
    : emulator_(emulator),
      memory_(emulator->memory()),
      worker_running_(false),
      decoder_running_(false) {
  std::memset(clients_, 0, sizeof(clients_));
  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    unused_clients_.push(i);
  }
  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    client_semaphores_[i] = CreateSemaphore(NULL, 0, kMaximumQueuedFrames, NULL);
    wait_handles_[i] = client_semaphores_[i];
  }
  shutdown_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
  wait_handles_[kMaximumClientCount] = shutdown_event_;
}

AudioSystem::~AudioSystem() {
  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    CloseHandle(client_semaphores_[i]);
  }
  CloseHandle(shutdown_event_);
}

void av_log_callback(void *avcl, int level, const char *fmt, va_list va) {
  StringBuffer buff;
  buff.AppendVarargs(fmt, va);

  xe::log_line('i', "libav: %s", buff.GetString());
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
      sizeof(XMAContextData) * kXmaContextCount, 256, kSystemHeapPhysical);
  // Add all contexts to the free list.
  for (int i = kXmaContextCount - 1; i >= 0; --i) {
    uint32_t ptr = registers_.xma_context_array_ptr + i * sizeof(XMAContextData);

    XMAContext& context = xma_context_array_[i];

    // Initialize it
    context.guest_ptr = ptr;
    context.in_use = false;
    context.kicked = false;

    // Create a new decoder per context
    // Needed because some data needs to be persisted across calls
    // TODO: Need to destroy this on class destruction
    context.decoder = new AudioDecoder();
    context.decoder->Initialize();
  }
  registers_.next_context = 1;

  // Setup libav logging callback
  av_log_set_callback(av_log_callback);

  worker_running_ = true;
  worker_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          emulator()->kernel_state(), 128 * 1024, 0, [this]() {
            WorkerThreadMain();
            return 0;
          }));
  worker_thread_->set_name("Audio Worker");
  worker_thread_->Create();

  decoder_running_ = true;
  decoder_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          emulator()->kernel_state(), 128 * 1024, 0, [this]() {
            DecoderThreadMain();
            return 0;
          }));
  decoder_thread_->set_name("Audio Decoder");
  decoder_thread_->Create();

  return X_STATUS_SUCCESS;
}

void AudioSystem::WorkerThreadMain() {
  // Initialize driver and ringbuffer.
  Initialize();

  auto processor = emulator_->processor();

  // Main run loop.
  while (worker_running_) {
    auto result =
        WaitForMultipleObjectsEx(DWORD(xe::countof(wait_handles_)),
                                 wait_handles_, FALSE, INFINITE, FALSE);
    if (result == WAIT_FAILED ||
        result == WAIT_OBJECT_0 + kMaximumClientCount) {
      continue;
    }

    size_t pumped = 0;
    if (result >= WAIT_OBJECT_0 &&
        result <= WAIT_OBJECT_0 + (kMaximumClientCount - 1)) {
      size_t index = result - WAIT_OBJECT_0;
      do {
        lock_.lock();
        uint32_t client_callback = clients_[index].callback;
        uint32_t client_callback_arg = clients_[index].wrapped_callback_arg;
        lock_.unlock();

        if (client_callback) {
          SCOPE_profile_cpu_i("apu", "xe::apu::AudioSystem->client_callback");
          uint64_t args[] = {client_callback_arg};
          processor->Execute(worker_thread_->thread_state(), client_callback,
                             args, xe::countof(args));
        }
        pumped++;
        index++;
      } while (index < kMaximumClientCount &&
               WaitForSingleObject(client_semaphores_[index], 0) ==
                   WAIT_OBJECT_0);
    }

    if (!worker_running_) {
      break;
    }

    if (!pumped) {
      SCOPE_profile_cpu_i("apu", "Sleep");
      Sleep(500);
    }
  }
  worker_running_ = false;

  // TODO(benvanik): call module API to kill?
}

void AudioSystem::DecoderThreadMain() {
  while (decoder_running_) {
    // Wait for a kick from WriteRegister.
    //decoder_fence_.Wait();

    // Check to see if we're supposed to exit
    if (!decoder_running_) {
      break;
    }

    // Okay, let's loop through XMA contexts to find ones we need to decode!
    for (uint32_t n = 0; n < kXmaContextCount; n++) {
      XMAContext& context = xma_context_array_[n];
      if (context.in_use && context.kicked) {
        context.lock.lock();
        context.kicked = false;

        auto context_ptr = memory()->TranslateVirtual(context.guest_ptr);
        XMAContextData data(context_ptr);
        ProcessXmaContext(context, data);
        data.Store(context_ptr);

        context.lock.unlock();
      }
    }
  }
}

void AudioSystem::Initialize() {}

void AudioSystem::Shutdown() {
  worker_running_ = false;
  SetEvent(shutdown_event_);
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();

  decoder_running_ = false;
  decoder_fence_.Signal();
  worker_thread_.reset();

  memory()->SystemHeapFree(registers_.xma_context_array_ptr);
}

uint32_t AudioSystem::AllocateXmaContext() {
  std::lock_guard<xe::mutex> lock(lock_);

  for (uint32_t n = 0; n < kXmaContextCount; n++) {
    XMAContext& context = xma_context_array_[n];
    if (!context.in_use) {
      context.in_use = true;
      return context.guest_ptr;
    }
  }

  return 0;
}

void AudioSystem::ReleaseXmaContext(uint32_t guest_ptr) {
  std::lock_guard<xe::mutex> lock(lock_);

  // Find it in the list.
  for (uint32_t n = 0; n < kXmaContextCount; n++) {
    XMAContext& context = xma_context_array_[n];
    if (context.guest_ptr == guest_ptr) {
      // Found it!
      // Lock it in case the decoder thread is working on it now
      context.lock.lock();

      context.in_use = false;
      auto context_ptr = memory()->TranslateVirtual(guest_ptr);
      std::memset(context_ptr, 0, sizeof(XMAContextData));  // Zero it.
      context.decoder->DiscardPacket();

      context.lock.unlock();
      break;
    }
  }
}

bool AudioSystem::BlockOnXmaContext(uint32_t guest_ptr, bool poll) {
  std::lock_guard<xe::mutex> lock(lock_);
  for (uint32_t n = 0; n < kXmaContextCount; n++) {
    XMAContext& context = xma_context_array_[n];
    if (context.guest_ptr == guest_ptr) {
      if (!context.lock.try_lock()) {
        if (poll) {
          return false;
        }
        context.lock.lock();
      }
      context.lock.unlock();
      return true;
    }
  }
  return true;
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg,
                                     size_t* out_index) {
  assert_true(unused_clients_.size());
  std::lock_guard<xe::mutex> lock(lock_);

  auto index = unused_clients_.front();

  auto client_semaphore = client_semaphores_[index];
  BOOL ret = ReleaseSemaphore(client_semaphore, kMaximumQueuedFrames, NULL);
  assert_true(ret == TRUE);

  AudioDriver* driver;
  auto result = CreateDriver(index, client_semaphore, &driver);
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

  std::lock_guard<xe::mutex> lock(lock_);
  assert_true(index < kMaximumClientCount);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  std::lock_guard<xe::mutex> lock(lock_);
  assert_true(index < kMaximumClientCount);
  DestroyDriver(clients_[index].driver);
  clients_[index] = {0};
  unused_clients_.push(index);

  // drain the semaphore of its count
  auto client_semaphore = client_semaphores_[index];
  DWORD wait_result;
  do {
    wait_result = WaitForSingleObject(client_semaphore, 0);
  } while (wait_result == WAIT_OBJECT_0);
  assert_true(wait_result == WAIT_TIMEOUT);
}

void AudioSystem::ProcessXmaContext(XMAContext& context, XMAContextData& data) {
  SCOPE_profile_cpu_f("apu");

  // Translate this for future use.
  uint8_t* output_buffer = memory()->TranslatePhysical(data.output_buffer_ptr);

  // What I see:
  // XMA outputs 2 bytes per sample
  // 512 samples per frame (128 per subframe)
  // Max output size is data.output_buffer_block_count * 256

  // This decoder is fed packets (max 4095 per buffer)
  // Packets contain "some" frames
  // 32bit header (big endian)

  // Frames are the smallest thing the SPUs can decode.
  // They usually can span packets (libav handles this)

  // Sample rates (data.sample_rate):
  // 0 - 24 kHz ?
  // 1 - 32 kHz
  // 2 - 44.1 kHz ?
  // 3 - 48 kHz ?

  // SPUs also support stereo decoding. (data.is_stereo)

  // Check the output buffer - we cannot decode anything else if it's
  // unavailable.
  if (!data.output_buffer_valid) {
    return;
  }

  // Output buffers are in raw PCM samples, 256 bytes per block.
  // Output buffer is a ring buffer. We need to write from the write offset
  // to the read offset.
  uint32_t output_capacity = data.output_buffer_block_count * 256;
  uint32_t output_read_offset = data.output_buffer_read_offset * 256;
  uint32_t output_write_offset = data.output_buffer_write_offset * 256;

  RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);

  size_t output_remaining_bytes = output_rb.write_count();

  // Decode until we can't write any more data.
  while (output_remaining_bytes > 0) {
    // This'll copy audio samples into the output buffer.
    // The samples need to be 2 bytes long!
    // Copies one frame at a time, so keep calling this until size == 0
    int read_bytes = 0;
    int decode_attempts_remaining = 3;

    uint8_t work_buffer[XMAContextData::kOutputMaxSizeBytes];
    while (decode_attempts_remaining) {
      read_bytes = context.decoder->DecodePacket(work_buffer, 0,
                                                 output_remaining_bytes);
      if (read_bytes >= 0) {
        //assert_true((read_bytes % 256) == 0);
        auto written_bytes = output_rb.Write(work_buffer, read_bytes);
        assert_true(read_bytes == written_bytes);

        // Ok.
        break;
      } else {
        // Sometimes the decoder will fail on a packet. I think it's
        // looking for cross-packet frames and failing. If you run it again
        // on the same packet it'll work though.
        --decode_attempts_remaining;
      }
    }

    if (!decode_attempts_remaining) {
      XELOGAPU("AudioSystem: libav failed to decode packet (returned %.8X)", -read_bytes);

      // Failed out.
      if (data.input_buffer_0_valid || data.input_buffer_1_valid) {
        // There's new data available - maybe we'll be ok if we decode it?
        read_bytes = 0;
        context.decoder->DiscardPacket();
      } else {
        // No data and hosed - bail.
        break;
      }
    }

    data.output_buffer_write_offset = output_rb.write_offset() / 256;
    output_remaining_bytes -= read_bytes;

    // If we need more data and the input buffers have it, grab it.
    if (read_bytes) {
      // Haven't finished with current packet.
      continue;
    } else if (data.input_buffer_0_valid || data.input_buffer_1_valid) {
      // Done with previous packet, so grab a new one.
      int ret = PrepareXMAPacket(context, data);
      if (ret <= 0) {
        // No more data (but may have prepared a packet)
        data.input_buffer_0_valid = 0;
        data.input_buffer_1_valid = 0;
      }
    } else {
      // Decoder is out of data and there's no more to give.
      break;
    }
  }

  // The game will kick us again with a new output buffer later.
  data.output_buffer_valid = 0;
}

int AudioSystem::PrepareXMAPacket(XMAContext &context, XMAContextData &data) {
  // Translate pointers for future use.
  uint8_t* in0 = data.input_buffer_0_valid
                     ? memory()->TranslatePhysical(data.input_buffer_0_ptr)
                     : nullptr;
  uint8_t* in1 = data.input_buffer_1_valid
                     ? memory()->TranslatePhysical(data.input_buffer_1_ptr)
                     : nullptr;

  int sample_rate = 0;
  if (data.sample_rate == 0) {
    sample_rate = 24000;
  } else if (data.sample_rate == 1) {
    sample_rate = 32000;
  } else if (data.sample_rate == 2) {
    sample_rate = 44100;
  } else if (data.sample_rate == 3) {
    sample_rate = 48000;
  }
  int channels = data.is_stereo ? 2 : 1;

  // See if we've finished with the input.
  // Block count is in packets, so expand by packet size.
  uint32_t input_size_0_bytes = (data.input_buffer_0_packet_count) * 2048;
  uint32_t input_size_1_bytes = (data.input_buffer_1_packet_count) * 2048;

  // Total input size
  uint32_t input_size_bytes = input_size_0_bytes + input_size_1_bytes;

  // Input read offset is in bits. Typically starts at 32 (4 bytes).
  // "Sequence" offset - used internally for WMA Pro decoder.
  // Just the read offset.
  uint32_t seq_offset_bytes = (data.input_buffer_read_offset & ~0x7FF) / 8;
  uint32_t input_remaining_bytes = input_size_bytes - seq_offset_bytes;

  if (seq_offset_bytes < input_size_bytes) {
    // Setup input offset and input buffer.
    uint32_t input_offset_bytes = seq_offset_bytes;
    auto input_buffer = in0;

    if (seq_offset_bytes >= input_size_0_bytes) {
      // Size overlap, select input buffer 1.
      // TODO: This needs testing.
      input_offset_bytes -= input_size_0_bytes;
      input_buffer = in1;
    }

    // Still have data to read.
    auto packet = input_buffer + input_offset_bytes;
    assert_true(input_offset_bytes % 2048 == 0);
    context.decoder->PreparePacket(packet, seq_offset_bytes,
                                   XMAContextData::kBytesPerPacket,
                                   sample_rate, channels);
    data.input_buffer_read_offset += XMAContextData::kBytesPerPacket * 8;

    input_remaining_bytes -= XMAContextData::kBytesPerPacket;
    if (input_remaining_bytes <= 0) {
      // Used the last of the data but prepared a packet
      return 0;
    }
  } else {
    // No more data available and no packet prepared.
    return -1;
  }


  return input_remaining_bytes;
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
  SCOPE_profile_cpu_f("apu");

  uint32_t r = addr & 0xFFFF;
  value = xe::byte_swap(uint32_t(value));
  XELOGAPU("WriteRegister(%.4X, %.8X)", r, value);
  // 1804h is written to with 0x02000000 and 0x03000000 around a lock operation

  assert_true(r % 4 == 0);
  register_file_[r / 4] = uint32_t(value);

  if (r >= 0x1940 && r <= 0x1940 + 9 * 4) {
    // Context kick command.
    // This will kick off the given hardware contexts.
    // Basically, this kicks the SPU and says "hey, decode that audio!"
    // XMAEnableContext

    // The context ID is a bit in the range of the entire context array.
    for (int i = 0; value && i < 32; ++i) {
      if (value & 1) {
        uint32_t context_id = i + (r - 0x1940) / 4 * 32;
        XMAContext& context = xma_context_array_[context_id];

        context.lock.lock();
        auto context_ptr = memory()->TranslateVirtual(context.guest_ptr);
        XMAContextData data(context_ptr);

        XELOGAPU("AudioSystem: kicking context %d (%d/%d bytes)", context_id,
            (data.input_buffer_read_offset & ~0x7FF) / 8,
            (data.input_buffer_0_packet_count + data.input_buffer_1_packet_count)
            * XMAContextData::kBytesPerPacket);

        // Reset valid flags so our audio decoder knows to process this one.
        data.input_buffer_0_valid = data.input_buffer_0_ptr != 0;
        data.input_buffer_1_valid = data.input_buffer_1_ptr != 0;

        data.Store(context_ptr);

        context.kicked = true;
        context.lock.unlock();
      }
      value >>= 1;
    }

    // Signal the decoder thread to start processing.
    decoder_fence_.Signal();
  } else if (r >= 0x1A40 && r <= 0x1A40 + 9 * 4) {
    // Context lock command.
    // This requests a lock by flagging the context.
    // XMADisableContext
    for (int i = 0; value && i < 32; ++i) {
      if (value & 1) {
        uint32_t context_id = i + (r - 0x1A40) / 4 * 32;
        XELOGAPU("AudioSystem: set context lock %d", context_id);
      }
      value >>= 1;
    }

    // Signal the decoder thread to start processing.
    decoder_fence_.Signal();
  } else if (r >= 0x1A80 && r <= 0x1A80 + 9 * 4) {
    // Context clear command.
    // This will reset the given hardware contexts.
    for (int i = 0; value && i < 32; ++i) {
      if (value & 1) {
        uint32_t context_id = i + (r - 0x1A80) / 4 * 32;
        XMAContext& context = xma_context_array_[context_id];
        XELOGAPU("AudioSystem: reset context %d", context_id);

        uint32_t guest_ptr = registers_.xma_context_array_ptr +
            context_id * sizeof(XMAContextData);
        context.lock.lock();
        auto context_ptr = memory()->TranslateVirtual(context.guest_ptr);
        XMAContextData data(context_ptr);

        context.decoder->DiscardPacket();
        data.input_buffer_0_valid = 0;
        data.input_buffer_1_valid = 0;
        data.output_buffer_valid = 0;

        data.output_buffer_read_offset = 0;
        data.output_buffer_write_offset = 0;

        data.Store(context_ptr);
        context.lock.unlock();
      }
      value >>= 1;
    }
  } else {
    value = value;
  }
}

}  // namespace apu
}  // namespace xe
