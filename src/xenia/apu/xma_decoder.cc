/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xma_decoder.h"

#include "xenia/apu/xma_context.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/xthread.h"

extern "C" {
#include "third_party/FFmpeg/libavutil/log.h"
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

DEFINE_bool(ffmpeg_verbose, false, "Verbose FFmpeg output (debug and above)",
            "APU");

namespace xe {
namespace apu {

XmaDecoder::XmaDecoder(cpu::Processor* processor)
    : memory_(processor->memory()), processor_(processor) {}

XmaDecoder::~XmaDecoder() = default;

void av_log_callback(void* avcl, int level, const char* fmt, va_list va) {
  if (!cvars::ffmpeg_verbose && level > AV_LOG_WARNING) {
    return;
  }

  char level_char = '?';
  LogLevel log_level;
  switch (level) {
    case AV_LOG_ERROR: {
      level_char = '!';
      log_level = xe::LogLevel::Error;
      break;
    }
    case AV_LOG_WARNING: {
      level_char = 'w';
      log_level = xe::LogLevel::Warning;
      break;
    }
    case AV_LOG_INFO: {
      level_char = 'i';
      log_level = xe::LogLevel::Info;
      break;
    }
    case AV_LOG_VERBOSE: {
      level_char = 'v';
      log_level = xe::LogLevel::Debug;
      break;
    }
    case AV_LOG_DEBUG: {
      level_char = 'd';
      log_level = xe::LogLevel::Debug;
      break;
    }
    default: {
      level_char = '?';
      log_level = xe::LogLevel::Debug;
      break;
    }
  }

  StringBuffer buff;
  buff.AppendVarargs(fmt, va);
  xe::logging::AppendLogLineFormat(LogSrc::Apu, log_level, level_char,
                                   "ffmpeg: {}",
                                   buff.to_string_view());
}

X_STATUS XmaDecoder::Setup(kernel::KernelState* kernel_state) {
  // Setup ffmpeg logging callback
  av_log_set_callback(av_log_callback);

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FEA0000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // Setup XMA context data.
  // The Xbox 360 kernel allocates the contexts with X_PAGE_NOCACHE |
  // X_PAGE_READWRITE and writes MmGetPhysicalAddress for the address to the
  // register.
  context_data_first_ptr_ = memory()->SystemHeapAlloc(
      sizeof(XMA_CONTEXT_DATA) * kContextCount, 256, kSystemHeapPhysical);
  context_data_last_ptr_ =
      context_data_first_ptr_ + (sizeof(XMA_CONTEXT_DATA) * kContextCount - 1);
  register_file_[XmaRegister::ContextArrayAddress] =
      memory()->GetPhysicalAddress(context_data_first_ptr_);

  // Setup XMA contexts.
  for (int i = 0; i < kContextCount; ++i) {
    uint32_t guest_ptr = context_data_first_ptr_ + i * sizeof(XMA_CONTEXT_DATA);
    XmaContext& context = contexts_[i];
    if (context.Setup(i, memory(), guest_ptr)) {
      assert_always();
    }
  }
  register_file_[XmaRegister::NextContextIndex] = 1;
  context_bitmap_.Resize(kContextCount);

  worker_running_ = true;
  work_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(work_event_);
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));
  worker_thread_->set_name("XMA Decoder");
  worker_thread_->set_can_debugger_suspend(true);
  worker_thread_->Create();

  return X_STATUS_SUCCESS;
}

void XmaDecoder::WorkerThreadMain() {
  uint32_t idle_loop_count = 0;
  while (worker_running_) {
    // Okay, let's loop through XMA contexts to find ones we need to decode!
    bool did_work = false;
    for (uint32_t n = 0; n < kContextCount; n++) {
      XmaContext& context = contexts_[n];
      did_work = context.Work() || did_work;

      // TODO: Need thread safety to do this.
      // Probably not too important though.
      // registers_.current_context = n;
      // registers_.next_context = (n + 1) % kContextCount;
    }

    if (paused_) {
      pause_fence_.Signal();
      resume_fence_.Wait();
    }

    if (!did_work) {
      idle_loop_count++;
    } else {
      idle_loop_count = 0;
    }
    xe::threading::Wait(work_event_.get(), false);
  }
}

void XmaDecoder::Shutdown() {
  worker_running_ = false;

  if (work_event_) {
    work_event_->Set();
  }

  if (paused_) {
    Resume();
  }

  if (worker_thread_) {
    // Wait for work thread.
    xe::threading::Wait(worker_thread_->thread(), false);
    worker_thread_.reset();
  }

  if (context_data_first_ptr_) {
    memory()->SystemHeapFree(context_data_first_ptr_);
  }

  context_data_first_ptr_ = 0;
  context_data_last_ptr_ = 0;
}

int XmaDecoder::GetContextId(uint32_t guest_ptr) {
  static_assert_size(XMA_CONTEXT_DATA, 64);
  if (guest_ptr < context_data_first_ptr_ ||
      guest_ptr > context_data_last_ptr_) {
    return -1;
  }
  assert_zero(guest_ptr & 0x3F);
  return (guest_ptr - context_data_first_ptr_) >> 6;
}

uint32_t XmaDecoder::AllocateContext() {
  size_t index = context_bitmap_.Acquire();
  if (index == -1) {
    // Out of contexts.
    return 0;
  }

  XmaContext& context = contexts_[index];
  assert_false(context.is_allocated());
  context.set_is_allocated(true);
  return context.guest_ptr();
}

void XmaDecoder::ReleaseContext(uint32_t guest_ptr) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  assert_true(context.is_allocated());
  context.Release();
  context_bitmap_.Release(context_id);
}

bool XmaDecoder::BlockOnContext(uint32_t guest_ptr, bool poll) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  return context.Block(poll);
}

uint32_t XmaDecoder::ReadRegister(uint32_t addr) {
  auto r = (addr & 0xFFFF) / 4;

  assert_true(r < XmaRegisterFile::kRegisterCount);

  switch (r) {
    case XmaRegister::ContextArrayAddress:
      break;
    case XmaRegister::CurrentContextIndex: {
      // 0606h (1818h) is rotating context processing # set to hardware ID of
      // context being processed.
      // If bit 200h is set, the locking code will possibly collide on hardware
      // IDs and error out, so we should never set it (I think?).
      uint32_t& current_context_index =
          register_file_[XmaRegister::CurrentContextIndex];
      uint32_t& next_context_index =
          register_file_[XmaRegister::NextContextIndex];
      // To prevent games from seeing a stuck XMA context, return a rotating
      // number.
      current_context_index = next_context_index;
      next_context_index = (next_context_index + 1) % kContextCount;
      break;
    }
    default:
      const auto register_info = register_file_.GetRegisterInfo(r);
      if (register_info) {
        XELOGW("XMA: Read from unhandled register ({:04X}, {})", r,
               register_info->name);
      } else {
        XELOGW("XMA: Read from unknown register ({:04X})", r);
      }
      break;
  }

  return xe::byte_swap(register_file_[r]);
}

void XmaDecoder::WriteRegister(uint32_t addr, uint32_t value) {
  SCOPE_profile_cpu_f("apu");

  uint32_t r = (addr & 0xFFFF) / 4;
  value = xe::byte_swap(value);

  assert_true(r < XmaRegisterFile::kRegisterCount);
  register_file_[r] = value;

  if (r >= XmaRegister::Context0Kick && r <= XmaRegister::Context9Kick) {
    // Context kick command.
    // This will kick off the given hardware contexts.
    // Basically, this kicks the SPU and says "hey, decode that audio!"
    // XMAEnableContext

    // The context ID is a bit in the range of the entire context array.
    uint32_t base_context_id = (r - XmaRegister::Context0Kick) * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        auto& context = contexts_[context_id];
        context.Enable();
      }
    }
    // Signal the decoder thread to start processing.
    work_event_->SetBoostPriority();
  } else if (r >= XmaRegister::Context0Lock && r <= XmaRegister::Context9Lock) {
    // Context lock command.
    // This requests a lock by flagging the context.
    // XMADisableContext
    uint32_t base_context_id = (r - XmaRegister::Context0Lock) * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        auto& context = contexts_[context_id];
        context.Disable();
      }
    }
    // Signal the decoder thread to start processing.
    // work_event_->Set();
  } else if (r >= XmaRegister::Context0Clear &&
             r <= XmaRegister::Context9Clear) {
    // Context clear command.
    // This will reset the given hardware contexts.
    uint32_t base_context_id = (r - XmaRegister::Context0Clear) * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        XmaContext& context = contexts_[context_id];
        context.Clear();
      }
    }
  } else {
    // 0601h (1804h) is written to with 0x02000000 and 0x03000000 around a lock
    // operation
    switch (r) {
      case 0x601:
        break;
      default: {
        const auto register_info = register_file_.GetRegisterInfo(r);
        if (register_info) {
          XELOGW("XMA: Write to unhandled register ({:04X}, {}): {:08X}", r,
                 register_info->name, value);
        } else {
          XELOGW("XMA: Write to unknown register ({:04X}): {:08X}", r, value);
        }
        break;
      }
#pragma warning(suppress : 4065)
    }
  }
}

void XmaDecoder::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  pause_fence_.Wait();
}

void XmaDecoder::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  resume_fence_.Signal();
}

}  // namespace apu
}  // namespace xe
