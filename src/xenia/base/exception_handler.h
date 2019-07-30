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

#include "xenia/base/assert.h"
#include "xenia/base/x64_context.h"

namespace xe {

class Exception {
 public:
  enum class Code {
    kInvalidException = 0,
    kAccessViolation,
    kIllegalInstruction,
  };

  enum class AccessViolationOperation {
    kUnknown,
    kRead,
    kWrite,
  };

  void InitializeAccessViolation(X64Context* thread_context,
                                 uint64_t fault_address,
                                 AccessViolationOperation operation) {
    code_ = Code::kAccessViolation;
    thread_context_ = thread_context;
    fault_address_ = fault_address;
    access_violation_operation_ = operation;
  }
  void InitializeIllegalInstruction(X64Context* thread_context) {
    code_ = Code::kIllegalInstruction;
    thread_context_ = thread_context;
  }

  Code code() const { return code_; }

  // Returns the platform-specific thread context info.
  X64Context* thread_context() const { return thread_context_; }

#if XE_ARCH_AMD64
  // Returns the program counter where the exception occurred.
  // RIP on x64.
  uint64_t pc() const { return thread_context_->rip; }
  // Sets the program counter where execution will resume.
  void set_resume_pc(uint64_t pc) { thread_context_->rip = pc; }
#else
  // Returns the program counter where the exception occurred.
  // RIP on x64.
  uint64_t pc() const {
    assert_always();
    return 0;
  }
  // Sets the program counter where execution will resume.
  void set_resume_pc(uint64_t pc) { assert_always(); }
#endif

  // In case of AV, address that was read from/written to.
  uint64_t fault_address() const { return fault_address_; }

  // In case of AV, what kind of operation caused it.
  AccessViolationOperation access_violation_operation() const {
    return access_violation_operation_;
  }

 private:
  Code code_ = Code::kInvalidException;
  X64Context* thread_context_ = nullptr;
  uint64_t fault_address_ = 0;
  AccessViolationOperation access_violation_operation_ =
      AccessViolationOperation::kUnknown;
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
