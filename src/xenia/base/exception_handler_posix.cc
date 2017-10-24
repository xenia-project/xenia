/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/exception_handler.h"

#include <signal.h>
#include <ucontext.h>

#include "xenia/base/logging.h"

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

void ExceptionHandlerCallback(int signum, siginfo_t* siginfo, void* sigctx) {
  ucontext_t* ctx = static_cast<ucontext_t*>(sigctx);

  X64Context thread_context;

  thread_context.rip = ctx->uc_mcontext.gregs[REG_RIP];
  thread_context.eflags = ctx->uc_mcontext.gregs[REG_EFL];
  thread_context.rax = ctx->uc_mcontext.gregs[REG_RAX];
  thread_context.rcx = ctx->uc_mcontext.gregs[REG_RCX];
  thread_context.rdx = ctx->uc_mcontext.gregs[REG_RDX];
  thread_context.rbx = ctx->uc_mcontext.gregs[REG_RBX];
  thread_context.rsp = ctx->uc_mcontext.gregs[REG_RSP];
  thread_context.rbp = ctx->uc_mcontext.gregs[REG_RBP];
  thread_context.rsi = ctx->uc_mcontext.gregs[REG_RSI];
  thread_context.rdi = ctx->uc_mcontext.gregs[REG_RDI];
  thread_context.r8 = ctx->uc_mcontext.gregs[REG_R8];
  thread_context.r9 = ctx->uc_mcontext.gregs[REG_R9];
  thread_context.r10 = ctx->uc_mcontext.gregs[REG_R10];
  thread_context.r11 = ctx->uc_mcontext.gregs[REG_R11];
  thread_context.r12 = ctx->uc_mcontext.gregs[REG_R12];
  thread_context.r13 = ctx->uc_mcontext.gregs[REG_R13];
  thread_context.r14 = ctx->uc_mcontext.gregs[REG_R14];
  thread_context.r15 = ctx->uc_mcontext.gregs[REG_R15];
  std::memcpy(thread_context.xmm_registers, ctx->uc_mcontext.fpregs->_xmm,
              sizeof(thread_context.xmm_registers));

  Exception ex;
  switch (signum) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    case SIGSEGV: {
      Exception::AccessViolationOperation access_violation_operation =
          ((ucontext_t*)sigctx)->uc_mcontext.gregs[REG_ERR] & 0x2
              ? Exception::AccessViolationOperation::kRead
              : Exception::AccessViolationOperation::kWrite;
      ex.InitializeAccessViolation(&thread_context,
                                   reinterpret_cast<uint64_t>(siginfo->si_addr),
                                   access_violation_operation);
    } break;
    default:
      XELOGE("Unhandled signum: 0x{:08X}", signum);
      assert_always("Unhandled signum");
  }

  for (size_t i = 0; i < xe::countof(handlers_) && handlers_[i].first; ++i) {
    if (handlers_[i].first(&ex, handlers_[i].second)) {
      // Exception handled.
      return;
    }
  }

  assert_always("Unhandled exception");
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
  XELOGW("Too many exception handlers installed");
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
