/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 ******************************************************************************
 */

#define _XOPEN_SOURCE 500

#include "xenia/base/exception_handler.h"

#include <signal.h>
#include <ucontext.h>
#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/host_thread_context.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"

namespace xe {

bool signal_handlers_installed_ = false;
struct sigaction original_sigill_handler_;
struct sigaction original_sigsegv_handler_;

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(signal_context);
  mcontext_t& mcontext = ucontext->uc_mcontext;

  HostThreadContext thread_context;

#if XE_ARCH_ARM64
  // Extract the general-purpose registers from mcontext
  thread_context.pc = mcontext->__ss.__pc;
  thread_context.sp = mcontext->__ss.__sp;
  thread_context.pstate = mcontext->__ss.__cpsr;

  // x0 - x28
  for (int i = 0; i < 29; ++i) {
    thread_context.x[i] = mcontext->__ss.__x[i];
  }
  // x29 is the frame pointer
  thread_context.x[29] = mcontext->__ss.__fp;
  // x30 is the link register
  thread_context.x[30] = mcontext->__ss.__lr;

  // Copy the vector registers
  for (int i = 0; i < 32; ++i) {
      thread_context.v[i].low = static_cast<uint64_t>(mcontext->__ns.__v[i] & 0xFFFFFFFFFFFFFFFF);
      thread_context.v[i].high = static_cast<uint64_t>((mcontext->__ns.__v[i] >> 64) & 0xFFFFFFFFFFFFFFFF);
  }

  thread_context.fpsr = mcontext->__ns.__fpsr;
  thread_context.fpcr = mcontext->__ns.__fpcr;
#endif  // XE_ARCH_ARM64

  Exception ex;
  switch (signal_number) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    case SIGSEGV: {
      Exception::AccessViolationOperation access_violation_operation =
          Exception::AccessViolationOperation::kUnknown;

      ex.InitializeAccessViolation(
          &thread_context, reinterpret_cast<uint64_t>(signal_info->si_addr),
          access_violation_operation);
    } break;
    default:
      assert_unhandled_case(signal_number);
  }

  for (size_t i = 0; i < xe::countof(handlers_) && handlers_[i].first; ++i) {
    if (handlers_[i].first(&ex, handlers_[i].second)) {
      // Exception handled.
#if XE_ARCH_ARM64
      // Write back the modified registers to mcontext
      mcontext->__ss.__pc = thread_context.pc;
      mcontext->__ss.__sp = thread_context.sp;
      mcontext->__ss.__cpsr = (__uint32_t)thread_context.pstate;

      uint32_t modified_register_index;
      uint32_t modified_x_registers_remaining = ex.modified_x_registers();
      while (xe::bit_scan_forward(modified_x_registers_remaining,
                                  &modified_register_index)) {
        modified_x_registers_remaining &=
            ~(UINT32_C(1) << modified_register_index);
        if (modified_register_index < 29) {
          mcontext->__ss.__x[modified_register_index] =
              thread_context.x[modified_register_index];
        } else if (modified_register_index == 29) {
          mcontext->__ss.__fp = thread_context.x[29];
        } else if (modified_register_index == 30) {
          mcontext->__ss.__lr = thread_context.x[30];
        }
      }

      if (ex.modified_v_registers()) {
        uint32_t modified_v_register_index;
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (xe::bit_scan_forward(modified_v_registers_remaining,
                                    &modified_v_register_index)) {
          modified_v_registers_remaining &=
              ~(UINT32_C(1) << modified_v_register_index);
            mcontext->__ns.__v[modified_v_register_index] =
                (static_cast<__uint128_t>(thread_context.v[modified_v_register_index].high) << 64) |
                static_cast<__uint128_t>(thread_context.v[modified_v_register_index].low);
        }
        mcontext->__ns.__fpsr = thread_context.fpsr;
        mcontext->__ns.__fpcr = thread_context.fpcr;
      }
#endif  // XE_ARCH_ARM64
      return;
    }
  }
}

void ExceptionHandler::Install(Handler fn, void* data) {
  if (!signal_handlers_installed_) {
    struct sigaction signal_handler;

    std::memset(&signal_handler, 0, sizeof(signal_handler));
    signal_handler.sa_sigaction = ExceptionHandlerCallback;
    signal_handler.sa_flags = SA_SIGINFO;

    if (sigaction(SIGILL, &signal_handler, &original_sigill_handler_) != 0) {
      assert_always("Failed to install new SIGILL handler");
    }
    if (sigaction(SIGSEGV, &signal_handler, &original_sigsegv_handler_) != 0) {
      assert_always("Failed to install new SIGSEGV handler");
    }
    signal_handlers_installed_ = true;
  }

  for (size_t i = 0; i < xe::countof(handlers_); ++i) {
    if (!handlers_[i].first) {
      handlers_[i].first = fn;
      handlers_[i].second = data;
      return;
    }
  }
  assert_always("Too many exception handlers installed");
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  for (size_t i = 0; i < xe::countof(handlers_); ++i) {
    if (handlers_[i].first == fn && handlers_[i].second == data) {
      for (; i < xe::countof(handlers_) - 1; ++i) {
        handlers_[i] = handlers_[i + 1];
      }
      handlers_[i].first = nullptr;
      handlers_[i].second = nullptr;
      break;
    }
  }

  bool has_any = false;
  for (size_t i = 0; i < xe::countof(handlers_); ++i) {
    if (handlers_[i].first) {
      has_any = true;
      break;
    }
  }
  if (!has_any) {
    if (signal_handlers_installed_) {
      if (sigaction(SIGILL, &original_sigill_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGILL handler");
      }
      if (sigaction(SIGSEGV, &original_sigsegv_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGSEGV handler");
      }
      signal_handlers_installed_ = false;
    }
  }
}

}  // namespace xe
