/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xma_decoder.h"

#include <gflags/gflags.h>

#include "xenia/apu/xma_context.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/profiling.h"

extern "C" {
#include "third_party/libav/libavutil/log.h"
}  // extern "C"

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal XmaDecoder handling take it, to prevent duplicate
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

DEFINE_bool(libav_verbose, false, "Verbose libav output (debug and above)");

namespace xe {
namespace apu {

XmaDecoder::XmaDecoder(cpu::Processor* processor)
    : memory_(processor->memory()), processor_(processor) {}

XmaDecoder::~XmaDecoder() = default;

void av_log_callback(void* avcl, int level, const char* fmt, va_list va) {
  if (!FLAGS_libav_verbose && level > AV_LOG_WARNING) {
    return;
  }

  char level_char = '?';
  switch (level) {
    case AV_LOG_ERROR:
      level_char = '!';
      break;
    case AV_LOG_WARNING:
      level_char = 'w';
      break;
    case AV_LOG_INFO:
      level_char = 'i';
      break;
    case AV_LOG_VERBOSE:
      level_char = 'v';
      break;
    case AV_LOG_DEBUG:
      level_char = 'd';
      break;
  }

  StringBuffer buff;
  buff.AppendVarargs(fmt, va);
  xe::LogLineFormat(level_char, "libav: %s", buff.GetString());
}

X_STATUS XmaDecoder::Setup(kernel::KernelState* kernel_state) {
  // Setup libav logging callback
  av_log_set_callback(av_log_callback);

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FEA0000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // Setup XMA context data.
  context_data_first_ptr_ = memory()->SystemHeapAlloc(
      sizeof(XMA_CONTEXT_DATA) * kContextCount, 256, kSystemHeapPhysical);
  context_data_last_ptr_ =
      context_data_first_ptr_ + (sizeof(XMA_CONTEXT_DATA) * kContextCount - 1);
  registers_.context_array_ptr = context_data_first_ptr_;

  // Setup XMA contexts.
  for (int i = 0; i < kContextCount; ++i) {
    uint32_t guest_ptr =
        registers_.context_array_ptr + i * sizeof(XMA_CONTEXT_DATA);
    XmaContext& context = contexts_[i];
    if (context.Setup(i, memory(), guest_ptr)) {
      assert_always();
    }
  }
  registers_.next_context = 1;

  worker_running_ = true;
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));
  worker_thread_->set_name("XMA Decoder Worker");
  worker_thread_->Create();

  return X_STATUS_SUCCESS;
}

void XmaDecoder::WorkerThreadMain() {
  while (worker_running_) {
    // Okay, let's loop through XMA contexts to find ones we need to decode!
    for (uint32_t n = 0; n < kContextCount; n++) {
      XmaContext& context = contexts_[n];
      context.Work();

      // TODO: Need thread safety to do this.
      // Probably not too important though.
      // registers_.current_context = n;
      // registers_.next_context = (n + 1) % kContextCount;
    }
    xe::threading::MaybeYield();
  }
}

void XmaDecoder::Shutdown() {
  worker_running_ = false;
  worker_fence_.Signal();
  worker_thread_.reset();

  memory()->SystemHeapFree(registers_.context_array_ptr);
}

int XmaDecoder::GetContextId(uint32_t guest_ptr) {
  static_assert(sizeof(XMA_CONTEXT_DATA) == 64, "FIXME");
  if (guest_ptr < context_data_first_ptr_ ||
      guest_ptr > context_data_last_ptr_) {
    return -1;
  }
  assert_zero(guest_ptr & 0x3F);
  return (guest_ptr - context_data_first_ptr_) >> 6;
}

uint32_t XmaDecoder::AllocateContext() {
  std::lock_guard<xe::mutex> lock(lock_);

  for (uint32_t n = 0; n < kContextCount; n++) {
    XmaContext& context = contexts_[n];
    if (!context.is_allocated()) {
      context.set_is_allocated(true);
      return context.guest_ptr();
    }
  }

  return 0;
}

void XmaDecoder::ReleaseContext(uint32_t guest_ptr) {
  std::lock_guard<xe::mutex> lock(lock_);

  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  context.Release();
}

bool XmaDecoder::BlockOnContext(uint32_t guest_ptr, bool poll) {
  std::lock_guard<xe::mutex> lock(lock_);

  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  return context.Block(poll);
}

// free60 may be useful here, however it looks like it's using a different
// piece of hardware:
// https://github.com/Free60Project/libxenon/blob/master/libxenon/drivers/xenon_sound/sound.c

uint32_t XmaDecoder::ReadRegister(uint32_t addr) {
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
    registers_.next_context = (registers_.next_context + 1) % kContextCount;
  }

  value = xe::byte_swap(value);
  return value;
}

void XmaDecoder::WriteRegister(uint32_t addr, uint32_t value) {
  SCOPE_profile_cpu_f("apu");

  uint32_t r = addr & 0xFFFF;
  value = xe::byte_swap(value);
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
    uint32_t base_context_id = (r - 0x1940) / 4 * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        XmaContext& context = contexts_[context_id];
        context.Enable();
      }
    }

    // Signal the decoder thread to start processing.
    worker_fence_.Signal();
  } else if (r >= 0x1A40 && r <= 0x1A40 + 9 * 4) {
    // Context lock command.
    // This requests a lock by flagging the context.
    // XMADisableContext
    uint32_t base_context_id = (r - 0x1A40) / 4 * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        XmaContext& context = contexts_[context_id];
        context.Disable();
      }
    }

    // Signal the decoder thread to start processing.
    worker_fence_.Signal();
  } else if (r >= 0x1A80 && r <= 0x1A80 + 9 * 4) {
    // Context clear command.
    // This will reset the given hardware contexts.
    uint32_t base_context_id = (r - 0x1A80) / 4 * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        XmaContext& context = contexts_[context_id];
        context.Clear();
      }
    }
  }
}

}  // namespace apu
}  // namespace xe
