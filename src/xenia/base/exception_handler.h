/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_EXCEPTION_HANDLER_H_
#define XENIA_BASE_EXCEPTION_HANDLER_H_

#include <functional>
#include <vector>

#include "xenia/base/x64_context.h"

namespace xe {

class Exception {
 public:
  enum class Code {
    kInvalidException = 0,
    kAccessViolation,
    kIllegalInstruction,
  };

  void InitializeAccessViolation(X64Context* thread_context,
                                 uint64_t fault_address) {
    code_ = Code::kAccessViolation;
    thread_context_ = thread_context;
    fault_address_ = fault_address;
  }
  void InitializeIllegalInstruction(X64Context* thread_context) {
    code_ = Code::kIllegalInstruction;
    thread_context_ = thread_context;
  }

  Code code() const { return code_; }

  // Returns the platform-specific thread context info.
  X64Context* thread_context() const { return thread_context_; }

  // Returns the program counter where the exception occurred.
  // RIP on x64.
  uint64_t pc() const { return thread_context_->rip; }
  // Sets the program counter where execution will resume.
  void set_resume_pc(uint64_t pc) { thread_context_->rip = pc; }

  // In case of AV, address that was read from/written to.
  uint64_t fault_address() const { return fault_address_; }

 private:
  Code code_ = Code::kInvalidException;
  X64Context* thread_context_ = nullptr;
  uint64_t fault_address_ = 0;
};

class ExceptionHandler {
 public:
  typedef bool (*Handler)(Exception* ex, void* data);

  // Installs an exception handler.
  // Handlers are called in the order they are installed.
  static void Install(Handler fn, void* data);

  // Uninstalls a previously-installed exception handler.
  static void Uninstall(Handler fn, void* data);
};

}  // namespace xe

#endif  // XENIA_BASE_EXCEPTION_HANDLER_H_
